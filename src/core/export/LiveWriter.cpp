#include "LiveWriter.h"

#include <algorithm>

#include "ffmpeg/FFmpeg.h"

extern "C" {
#include <libavutil/audio_fifo.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
}

namespace lumora {

struct LiveWriter::Impl {
    struct Item {
        bool picture = false;
        QImage image;
        std::int64_t ms = 0;
        std::vector<std::int16_t> pcm;
    };

    Options options;
    av::FormatOutputPtr out;
    av::CodecContextPtr venc, aenc;
    AVStream* vstream = nullptr;
    AVStream* astream = nullptr;
    av::SwsPtr sws;
    av::SwrPtr swr;
    AVAudioFifo* fifo = nullptr;
    av::FramePtr vframe, aframe;
    av::PacketPtr packet;
    std::int64_t lastPts = -1;
    std::int64_t samplesOut = 0;

    mutable std::mutex mutex;
    std::condition_variable wake;
    std::deque<Item> queue;
    int pictures = 0;
    bool stopping = false;
    std::thread thread;
    QString error;

    ~Impl() {
        if (fifo)
            av_audio_fifo_free(fifo);
    }

    bool setError(const QString& text) {
        std::lock_guard lock(mutex);
        if (error.isEmpty())
            error = text;
        return false;
    }

    bool write(AVCodecContext* enc, AVStream* stream, AVFrame* frame) {
        int ret = avcodec_send_frame(enc, frame);
        if (ret < 0 && ret != AVERROR_EOF)
            return setError(LiveWriter::tr("Encoding failed: %1").arg(av::errorString(ret)));
        while ((ret = avcodec_receive_packet(enc, packet.get())) >= 0) {
            av_packet_rescale_ts(packet.get(), enc->time_base, stream->time_base);
            packet->stream_index = stream->index;
            ret = av_interleaved_write_frame(out.get(), packet.get());
            if (ret < 0)
                return setError(LiveWriter::tr("Cannot write the file: %1").arg(av::errorString(ret)));
        }
        if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            return setError(LiveWriter::tr("Encoding failed: %1").arg(av::errorString(ret)));
        return true;
    }

    bool encodePicture(const QImage& image, std::int64_t ms) {
        if (ms <= lastPts)
            return true; // two pictures in the same millisecond: keep the first
        QImage img = image.convertToFormat(QImage::Format_RGB32);
        const int w = venc->width, h = venc->height;
        sws.reset(sws_getCachedContext(sws.release(), img.width(), img.height(), AV_PIX_FMT_BGRA, w, h, venc->pix_fmt,
                                       SWS_BICUBIC, nullptr, nullptr, nullptr));
        if (!sws)
            return setError(LiveWriter::tr("Cannot convert the picture."));
        const int* coeffs = sws_getCoefficients(SWS_CS_ITU709);
        sws_setColorspaceDetails(sws.get(), coeffs, 1, coeffs, 0, 0, 1 << 16, 1 << 16);
        if (av_frame_make_writable(vframe.get()) < 0)
            return setError(LiveWriter::tr("Out of memory."));
        const uint8_t* src[1] = {img.constBits()};
        const int stride[1] = {int(img.bytesPerLine())};
        sws_scale(sws.get(), src, stride, 0, img.height(), vframe->data, vframe->linesize);
        vframe->pts = ms;
        lastPts = ms;
        return write(venc.get(), vstream, vframe.get());
    }

    bool drainSound(bool flush) {
        const int frameSize = aenc->frame_size > 0 ? aenc->frame_size : 1024;
        while (av_audio_fifo_size(fifo) >= frameSize || (flush && av_audio_fifo_size(fifo) > 0)) {
            const int n = std::min(frameSize, av_audio_fifo_size(fifo));
            if (av_frame_make_writable(aframe.get()) < 0)
                return setError(LiveWriter::tr("Out of memory."));
            aframe->nb_samples = n;
            av_audio_fifo_read(fifo, reinterpret_cast<void**>(aframe->data), n);
            aframe->pts = samplesOut;
            samplesOut += n;
            if (!write(aenc.get(), astream, aframe.get()))
                return false;
        }
        return true;
    }

    bool encodeSound(const std::vector<std::int16_t>& pcm) {
        const int frames = int(pcm.size()) / options.channels;
        if (frames <= 0)
            return true;
        const int outMax = swr_get_out_samples(swr.get(), frames);
        uint8_t** planes = nullptr;
        if (av_samples_alloc_array_and_samples(&planes, nullptr, options.channels, outMax, aenc->sample_fmt, 0) < 0)
            return setError(LiveWriter::tr("Out of memory."));
        const uint8_t* in[1] = {reinterpret_cast<const uint8_t*>(pcm.data())};
        const int got = swr_convert(swr.get(), planes, outMax, in, frames);
        if (got > 0)
            av_audio_fifo_write(fifo, reinterpret_cast<void**>(planes), got);
        av_freep(&planes[0]);
        av_freep(&planes);
        return drainSound(false);
    }

    void run() {
        for (;;) {
            Item item;
            {
                std::unique_lock lock(mutex);
                wake.wait(lock, [this] { return stopping || !queue.empty(); });
                if (queue.empty())
                    return;
                item = std::move(queue.front());
                queue.pop_front();
                if (item.picture)
                    --pictures;
            }
            if (!error.isEmpty())
                continue;
            if (item.picture)
                encodePicture(item.image, item.ms);
            else if (aenc)
                encodeSound(item.pcm);
        }
    }
};

LiveWriter::LiveWriter() : d(std::make_unique<Impl>()) {}

LiveWriter::~LiveWriter() {
    if (d->thread.joinable()) {
        {
            std::lock_guard lock(d->mutex);
            d->stopping = true;
            d->queue.clear();
        }
        d->wake.notify_all();
        d->thread.join();
    }
}

bool LiveWriter::open(const QString& path, const Options& options, QString* error) {
    auto fail = [&](const QString& text) {
        if (error)
            *error = text;
        return false;
    };
    d->options = options;
    const int w = std::max(2, options.size.width() & ~1), h = std::max(2, options.size.height() & ~1);

    const AVCodec* vcodec = avcodec_find_encoder_by_name("libx264");
    if (!vcodec)
        vcodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!vcodec)
        return fail(tr("This FFmpeg has no H.264 encoder."));
    d->venc.reset(avcodec_alloc_context3(vcodec));
    AVCodecContext* v = d->venc.get();
    v->width = w;
    v->height = h;
    v->pix_fmt = AV_PIX_FMT_YUV420P;
    v->time_base = {1, 1000};
    v->framerate = {std::max(1, options.frameRate), 1};
    v->gop_size = std::max(1, options.frameRate) * 2;
    v->color_range = AVCOL_RANGE_MPEG;
    v->colorspace = AVCOL_SPC_BT709;
    v->color_primaries = AVCOL_PRI_BT709;
    v->color_trc = AVCOL_TRC_BT709;
    AVDictionary* vopts = nullptr;
    if (QLatin1String(vcodec->name) == QLatin1String("libx264")) {
        // Fast enough to keep up with the screen, at a quality that looks
        // like it; screens are mostly still, so the file stays small.
        av_dict_set(&vopts, "preset", "veryfast", 0);
        av_dict_set(&vopts, "crf", QByteArray::number(options.quality).constData(), 0);
    } else {
        v->bit_rate = std::int64_t(w) * h * options.frameRate / 6;
    }

    AVFormatContext* raw = nullptr;
    const std::string outPath = av::toFFmpegPath(path);
    int ret = avformat_alloc_output_context2(&raw, nullptr, "mp4", outPath.c_str());
    if (ret < 0 || !raw)
        return fail(tr("Cannot create the output file: %1").arg(av::errorString(ret)));
    d->out.reset(raw);
    if (d->out->oformat->flags & AVFMT_GLOBALHEADER)
        v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    ret = avcodec_open2(v, vcodec, &vopts);
    av_dict_free(&vopts);
    if (ret < 0)
        return fail(tr("Cannot start the video encoder: %1").arg(av::errorString(ret)));
    d->vstream = avformat_new_stream(d->out.get(), nullptr);
    avcodec_parameters_from_context(d->vstream->codecpar, v);
    d->vstream->time_base = v->time_base;
    d->vframe = av::makeFrame();
    d->vframe->format = v->pix_fmt;
    d->vframe->width = w;
    d->vframe->height = h;
    if (av_frame_get_buffer(d->vframe.get(), 0) < 0)
        return fail(tr("Out of memory."));

    if (options.sampleRate > 0 && options.channels > 0) {
        const AVCodec* acodec = avcodec_find_encoder(AV_CODEC_ID_AAC);
        if (!acodec)
            return fail(tr("This FFmpeg has no AAC encoder."));
        d->aenc.reset(avcodec_alloc_context3(acodec));
        AVCodecContext* a = d->aenc.get();
        a->sample_rate = options.sampleRate;
        av_channel_layout_default(&a->ch_layout, options.channels);
        a->sample_fmt = AV_SAMPLE_FMT_FLTP;
        a->bit_rate = 192000;
        a->time_base = {1, options.sampleRate};
        if (d->out->oformat->flags & AVFMT_GLOBALHEADER)
            a->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(a, acodec, nullptr);
        if (ret < 0)
            return fail(tr("Cannot start the sound encoder: %1").arg(av::errorString(ret)));
        d->astream = avformat_new_stream(d->out.get(), nullptr);
        avcodec_parameters_from_context(d->astream->codecpar, a);
        d->astream->time_base = a->time_base;
        SwrContext* s = nullptr;
        AVChannelLayout layout;
        av_channel_layout_default(&layout, options.channels);
        swr_alloc_set_opts2(&s, &layout, AV_SAMPLE_FMT_FLTP, options.sampleRate, &layout, AV_SAMPLE_FMT_S16,
                            options.sampleRate, 0, nullptr);
        d->swr.reset(s);
        if (!s || swr_init(s) < 0)
            return fail(tr("Cannot convert the sound."));
        d->fifo = av_audio_fifo_alloc(AV_SAMPLE_FMT_FLTP, options.channels, 4096);
        d->aframe = av::makeFrame();
        d->aframe->format = AV_SAMPLE_FMT_FLTP;
        av_channel_layout_copy(&d->aframe->ch_layout, &a->ch_layout);
        d->aframe->sample_rate = options.sampleRate;
        d->aframe->nb_samples = a->frame_size > 0 ? a->frame_size : 1024;
        if (av_frame_get_buffer(d->aframe.get(), 0) < 0)
            return fail(tr("Out of memory."));
    }

    ret = avio_open(&d->out->pb, outPath.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0)
        return fail(tr("Cannot write %1: %2").arg(path, av::errorString(ret)));
    ret = avformat_write_header(d->out.get(), nullptr);
    if (ret < 0)
        return fail(tr("Cannot write the file header: %1").arg(av::errorString(ret)));
    d->packet = av::makePacket();
    d->thread = std::thread([this] { d->run(); });
    return true;
}

void LiveWriter::addPicture(const QImage& image, std::int64_t ms) {
    {
        std::lock_guard lock(d->mutex);
        Impl::Item item;
        item.picture = true;
        item.image = image;
        item.ms = ms;
        d->queue.push_back(std::move(item));
        ++d->pictures;
    }
    d->wake.notify_one();
}

void LiveWriter::addSound(const std::int16_t* samples, int frames) {
    if (frames <= 0)
        return;
    {
        std::lock_guard lock(d->mutex);
        Impl::Item item;
        item.pcm.assign(samples, samples + size_t(frames) * d->options.channels);
        d->queue.push_back(std::move(item));
    }
    d->wake.notify_one();
}

int LiveWriter::backlog() const {
    std::lock_guard lock(d->mutex);
    return d->pictures;
}

bool LiveWriter::finish(QString* error) {
    if (d->thread.joinable()) {
        {
            std::lock_guard lock(d->mutex);
            d->stopping = true;
        }
        d->wake.notify_all();
        d->thread.join();
    }
    if (d->error.isEmpty() && d->venc)
        d->write(d->venc.get(), d->vstream, nullptr);
    if (d->error.isEmpty() && d->aenc && d->drainSound(true))
        d->write(d->aenc.get(), d->astream, nullptr);
    if (d->error.isEmpty() && d->out) {
        const int ret = av_write_trailer(d->out.get());
        if (ret < 0)
            d->setError(tr("Cannot finish the file: %1").arg(av::errorString(ret)));
    }
    d->out.reset();
    if (error)
        *error = d->error;
    return d->error.isEmpty();
}

} // namespace lumora

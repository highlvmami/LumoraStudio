#include "SpeechToText.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <thread>

#include "render/AudioMixer.h"

#ifdef LUMORA_HAVE_WHISPER
#include <whisper.h>
#endif

namespace lumora::speech {

namespace {
constexpr int kRate = 16000; // what Whisper listens at

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::speech)
};
} // namespace

bool available() {
#ifdef LUMORA_HAVE_WHISPER
    return true;
#else
    return false;
#endif
}

std::vector<Model> models() {
    return {
        {QStringLiteral("base"), Tr::tr("Fast (base)"), 148},
        {QStringLiteral("small"), Tr::tr("Balanced (small)"), 488},
        {QStringLiteral("large-v3-turbo-q5_0"), Tr::tr("Most accurate (large v3 turbo)"), 574},
    };
}

QString defaultModel() { return QStringLiteral("small"); }

QUrl modelUrl(const QString& id) {
    return QUrl(QStringLiteral("https://huggingface.co/ggerganov/whisper.cpp/resolve/main/ggml-%1.bin").arg(id));
}

QString modelPath(const QString& id) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/models");
    QDir().mkpath(dir);
    return dir + QStringLiteral("/ggml-%1.bin").arg(id);
}

Result transcribe(const TimelineState& timeline, const MediaPool& pool, Ticks from, Ticks to, const Options& options,
                  const Progress& progress) {
    Result r;
#ifndef LUMORA_HAVE_WHISPER
    (void)timeline, (void)pool, (void)from, (void)to, (void)options, (void)progress;
    r.error = Tr::tr("This build of LumoraStudio has no speech recognition.");
    return r;
#else
    if (to <= from) {
        r.error = Tr::tr("There is nothing to listen to.");
        return r;
    }
    if (!QFileInfo::exists(options.modelPath)) {
        r.error = Tr::tr("The speech recognition model is missing.");
        return r;
    }
    // 1. The mix as Whisper wants it: 16 kHz mono float, without the master
    //    limiter (its loudness does not matter here).
    AudioMixer mixer(&pool, kRate, 1);
    mixer.setTimeline(timeline);
    mixer.setMasterEnabled(false);
    if (mixer.silent()) {
        r.error = Tr::tr("The timeline has no sound.");
        return r;
    }
    const std::int64_t start = av_rescale(from, kRate, kTicksPerSecond);
    const std::int64_t total = av_rescale(to - from, kRate, kTicksPerSecond);
    std::vector<float> pcm(static_cast<size_t>(total));
    constexpr int chunk = kRate * 5;
    for (std::int64_t done = 0; done < total; done += chunk) {
        const int n = static_cast<int>(std::min<std::int64_t>(chunk, total - done));
        mixer.mix(start + done, n, pcm.data() + done);
        if (progress && !progress(0.1 * done / total)) {
            r.cancelled = true;
            return r;
        }
    }

    // 2. Recognition.
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;
    const QByteArray model = QFile::encodeName(options.modelPath);
    whisper_context* ctx = whisper_init_from_file_with_params(model.constData(), cparams);
    if (!ctx) {
        r.error = Tr::tr("The speech recognition model cannot be read. Download it again.");
        return r;
    }
    struct Callbacks {
        const Progress* progress;
        bool cancelled = false;
    } cb{&progress};
    whisper_full_params p = whisper_full_default_params(WHISPER_SAMPLING_BEAM_SEARCH);
    p.beam_search.beam_size = 5;
    const QByteArray lang = options.language.toLatin1();
    p.language = lang.constData();
    p.detect_language = false;
    p.translate = false;
    p.n_threads = options.threads > 0 ? options.threads : std::max(1, static_cast<int>(std::thread::hardware_concurrency()) - 1);
    p.print_progress = false;
    p.print_realtime = false;
    p.print_special = false;
    p.print_timestamps = false;
    p.suppress_nst = true;
    p.token_timestamps = true;
    p.max_len = std::max(16, options.maxLineLength);
    p.split_on_word = true;
    p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* user) {
        auto* c = static_cast<Callbacks*>(user);
        if (*c->progress && !(*c->progress)(0.1 + 0.9 * percent / 100.0))
            c->cancelled = true;
    };
    p.progress_callback_user_data = &cb;
    p.abort_callback = [](void* user) { return static_cast<Callbacks*>(user)->cancelled; };
    p.abort_callback_user_data = &cb;
    const int rc = whisper_full(ctx, p, pcm.data(), static_cast<int>(pcm.size()));
    if (cb.cancelled) {
        whisper_free(ctx);
        r.cancelled = true;
        return r;
    }
    if (rc != 0) {
        whisper_free(ctx);
        r.error = Tr::tr("Speech recognition failed.");
        return r;
    }
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        const QString text = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).simplified();
        if (text.isEmpty())
            continue;
        // Whisper times are in 10 ms steps.
        const Ticks s = from + whisper_full_get_segment_t0(ctx, i) * (kTicksPerSecond / 100);
        const Ticks e = from + whisper_full_get_segment_t1(ctx, i) * (kTicksPerSecond / 100);
        if (e <= s)
            continue;
        // Short pieces that belong together read better as one cue.
        if (!r.cues.empty()) {
            SubtitleCue& last = r.cues.back();
            const bool sentenceEnded = last.text.endsWith(QLatin1Char('.')) || last.text.endsWith(QLatin1Char('?')) ||
                                       last.text.endsWith(QLatin1Char('!'));
            if (!sentenceEnded && !last.text.contains(QLatin1Char('\n')) && s - last.end < kTicksPerSecond / 4 &&
                last.text.size() + text.size() < 2 * options.maxLineLength && e - last.start < 6 * kTicksPerSecond) {
                last.text += QLatin1Char('\n') + text;
                last.end = e;
                continue;
            }
        }
        r.cues.push_back({s, std::min(e, to), text});
    }
    whisper_free(ctx);
    if (progress)
        progress(1.0);
    return r;
#endif
}

} // namespace lumora::speech

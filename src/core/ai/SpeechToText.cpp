#include "SpeechToText.h"

#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <thread>

#include "render/AudioMixer.h"

#ifdef LUMORA_HAVE_WHISPER
#include <whisper.h>
#endif

namespace lumora::speech {

namespace {
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

std::vector<float> listen(const TimelineState& timeline, const MediaPool& pool, Ticks from, Ticks to,
                          const Progress& progress, bool* cancelled) {
    if (cancelled)
        *cancelled = false;
    if (to <= from)
        return {};
    AudioMixer mixer(&pool, kRate, 1);
    mixer.setTimeline(timeline);
    mixer.setMasterEnabled(false);
    if (mixer.silent())
        return {};
    const std::int64_t start = av_rescale(from, kRate, kTicksPerSecond);
    const std::int64_t total = av_rescale(to - from, kRate, kTicksPerSecond);
    std::vector<float> pcm(static_cast<size_t>(total));
    constexpr int chunk = kRate * 5;
    for (std::int64_t done = 0; done < total; done += chunk) {
        const int n = static_cast<int>(std::min<std::int64_t>(chunk, total - done));
        mixer.mix(start + done, n, pcm.data() + done);
        if (progress && !progress(static_cast<double>(done) / total)) {
            if (cancelled)
                *cancelled = true;
            return {};
        }
    }
    return pcm;
}

#ifdef LUMORA_HAVE_WHISPER

namespace {

// Loaded model and the outcome of one recognition run.
struct Recognition {
    whisper_context* ctx = nullptr;
    QString error;
    bool cancelled = false;
    ~Recognition() {
        if (ctx)
            whisper_free(ctx);
    }
};

// Moves a Whisper start time (10 ms steps) forward to where the sound
// actually begins: Whisper often starts the first segment or word of a
// window at the window's start even when the speech begins later.
class SoundStart {
public:
    explicit SoundStart(const std::vector<float>& pcm) : m_level(pcm.size() / 160) {
        float peak = 0.0f;
        for (size_t f = 0; f < m_level.size(); ++f) {
            double sum = 0.0;
            for (size_t k = f * 160; k < (f + 1) * 160; ++k)
                sum += double(pcm[k]) * pcm[k];
            m_level[f] = static_cast<float>(std::sqrt(sum / 160.0));
            peak = std::max(peak, m_level[f]);
        }
        m_quiet = std::max(0.003f, 0.1f * peak);
    }
    int64_t operator()(int64_t t0, int64_t t1) const {
        while (t0 < t1 && t0 >= 0 && t0 < static_cast<int64_t>(m_level.size()) && m_level[t0] < m_quiet)
            ++t0;
        return t0 < t1 ? t0 : t1;
    }

private:
    std::vector<float> m_level;
    float m_quiet = 0.0f;
};

// Runs Whisper on `pcm`. `wordByWord` makes every segment a single word;
// `prompt` is text the model takes as what came before (its style is
// copied).
void recognise(Recognition& r, const std::vector<float>& pcm, const Options& options, bool wordByWord, const char* prompt,
               const Progress& progress, double progressFrom) {
    if (!QFileInfo::exists(options.modelPath)) {
        r.error = Tr::tr("The speech recognition model is missing.");
        return;
    }
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = false;
    const QByteArray model = QFile::encodeName(options.modelPath);
    r.ctx = whisper_init_from_file_with_params(model.constData(), cparams);
    if (!r.ctx) {
        r.error = Tr::tr("The speech recognition model cannot be read. Download it again.");
        return;
    }
    struct Callbacks {
        const Progress* progress;
        double from;
        bool cancelled = false;
    } cb{&progress, progressFrom};
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
    p.suppress_nst = !wordByWord; // hesitations are "non-speech" to Whisper
    p.token_timestamps = true;
    p.max_len = wordByWord ? 1 : std::max(16, options.maxLineLength);
    p.split_on_word = true;
    p.initial_prompt = prompt;
    p.progress_callback = [](whisper_context*, whisper_state*, int percent, void* user) {
        auto* c = static_cast<Callbacks*>(user);
        if (*c->progress && !(*c->progress)(c->from + (1.0 - c->from) * percent / 100.0))
            c->cancelled = true;
    };
    p.progress_callback_user_data = &cb;
    p.abort_callback = [](void* user) { return static_cast<Callbacks*>(user)->cancelled; };
    p.abort_callback_user_data = &cb;
    const int rc = whisper_full(r.ctx, p, pcm.data(), static_cast<int>(pcm.size()));
    if (cb.cancelled)
        r.cancelled = true;
    else if (rc != 0)
        r.error = Tr::tr("Speech recognition failed.");
}

} // namespace

#endif

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
    // 1. The mix as Whisper wants it.
    const std::vector<float> pcm = listen(timeline, pool, from, to, [&](double p) { return !progress || progress(0.1 * p); },
                                          &r.cancelled);
    if (r.cancelled)
        return r;
    if (pcm.empty()) {
        r.error = Tr::tr("The timeline has no sound.");
        return r;
    }

    // 2. Recognition.
    Recognition rec;
    recognise(rec, pcm, options, false, nullptr, progress, 0.1);
    if (rec.cancelled || !rec.error.isEmpty()) {
        r.cancelled = rec.cancelled;
        r.error = rec.error;
        return r;
    }
    whisper_context* ctx = rec.ctx;
    // Whisper often starts the first segment of a window at the window's
    // start even when the speech begins later, so each cue start is moved to
    // where the sound actually begins.
    const SoundStart soundStart(pcm);
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        const QString text = QString::fromUtf8(whisper_full_get_segment_text(ctx, i)).simplified();
        if (text.isEmpty())
            continue;
        // Whisper times are in 10 ms steps.
        const int64_t t1 = whisper_full_get_segment_t1(ctx, i);
        const Ticks s = from + soundStart(whisper_full_get_segment_t0(ctx, i), t1) * (kTicksPerSecond / 100);
        const Ticks e = from + t1 * (kTicksPerSecond / 100);
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
    if (progress)
        progress(1.0);
    return r;
#endif
}

WordsResult words(const std::vector<float>& pcm, Ticks from, const Options& options, const Progress& progress) {
    WordsResult r;
#ifndef LUMORA_HAVE_WHISPER
    (void)pcm, (void)from, (void)options, (void)progress;
    r.error = Tr::tr("This build of LumoraStudio has no speech recognition.");
    return r;
#else
    if (pcm.empty())
        return r;
    // A prompt full of hesitations makes Whisper write them down instead of
    // tidying them away.
    const char* prompt = options.language == QLatin1String("tr") ? "Eee, ııı, şey... Hmm, yani, ıı, ee, böyle."
                                                                 : "Umm, let me think, like, hmm... Uh, so, er, I mean.";
    Recognition rec;
    recognise(rec, pcm, options, true, prompt, progress, 0.0);
    r.cancelled = rec.cancelled;
    r.error = rec.error;
    if (!r.ok())
        return r;
    const SoundStart soundStart(pcm);
    const int n = whisper_full_n_segments(rec.ctx);
    for (int i = 0; i < n; ++i) {
        const QString text = QString::fromUtf8(whisper_full_get_segment_text(rec.ctx, i)).trimmed();
        if (text.isEmpty())
            continue;
        const int64_t t1 = whisper_full_get_segment_t1(rec.ctx, i);
        const Ticks s = from + soundStart(whisper_full_get_segment_t0(rec.ctx, i), t1) * (kTicksPerSecond / 100);
        const Ticks e = from + t1 * (kTicksPerSecond / 100);
        if (e > s)
            r.words.push_back({s, e, text});
    }
    return r;
#endif
}

} // namespace lumora::speech

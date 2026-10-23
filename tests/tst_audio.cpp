#include <QTemporaryDir>
#include <QtTest>
#include <cmath>

#include "TestMedia.h"
#include "audio/AudioDsp.h"
#include "audio/Loudness.h"
#include "media/MediaPool.h"
#include "render/AudioMixer.h"
#include "timeline/Timeline.h"

using namespace lumora;
using namespace lumora::test;

namespace {
Ticks sec(double s) { return ticksFromSeconds(s); }

std::vector<float> sine(int frames, int channels, double freq, double amp, int rate = 48000) {
    std::vector<float> v(static_cast<size_t>(frames) * channels);
    for (int i = 0; i < frames; ++i)
        for (int ch = 0; ch < channels; ++ch)
            v[static_cast<size_t>(i) * channels + ch] = static_cast<float>(amp * std::sin(2 * M_PI * freq * i / rate));
    return v;
}

double rms(const std::vector<float>& s, size_t from, size_t to) {
    double sum = 0;
    for (size_t i = from; i < to; ++i)
        sum += static_cast<double>(s[i]) * s[i];
    return to > from ? std::sqrt(sum / (to - from)) : 0.0;
}
} // namespace

class AudioTest : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    MediaPool m_pool;
    MediaItemPtr m_tone; // 440 Hz at 0.2 amplitude, 4 s

private slots:
    void initTestCase() {
        QVERIFY(m_dir.isValid());
        TestVideoSpec spec;
        QString err;
        QVERIFY2(writeTestVideo(m_dir.filePath("tone.mp4"), spec, &err), qPrintable(err));
        m_tone = m_pool.import(m_dir.filePath("tone.mp4"));
        QVERIFY(m_tone);
    }

    void loudnessOfReferenceTones() {
        // EBU Tech 3341: a 1 kHz sine with -20 dBFS peaks in both channels
        // reads -20 LUFS; in one channel only it reads 3 dB less.
        const double amp = std::pow(10.0, -20.0 / 20.0); // peak level
        LoudnessMeter stereo(48000, 2);
        auto s = sine(48000 * 5, 2, 997.0, amp);
        stereo.add(s.data(), 48000 * 5);
        QVERIFY2(std::abs(stereo.integrated() - (-20.0)) < 0.1, qPrintable(QString::number(stereo.integrated())));
        LoudnessMeter mono(48000, 1);
        auto m = sine(48000 * 5, 1, 997.0, amp);
        mono.add(m.data(), 48000 * 5);
        QVERIFY(std::abs(mono.integrated() - (-23.01)) < 0.1);
        LoudnessMeter silent(48000, 2);
        std::vector<float> zeros(48000 * 2 * 2, 0.0f);
        silent.add(zeros.data(), 48000 * 2);
        QCOMPARE(silent.integrated(), loudness::kSilent);
    }

    void limiterNeverExceedsTheCeiling() {
        dsp::Limiter lim(48000, 2, -1.0);
        auto s = sine(48000, 2, 220.0, 1.6); // +4 dBFS peaks
        lim.process(s.data(), 48000);
        const float ceiling = static_cast<float>(std::pow(10.0, -1.0 / 20.0));
        float peak = 0;
        for (float v : s)
            peak = std::max(peak, std::abs(v));
        QVERIFY2(peak <= ceiling + 1e-6f, qPrintable(QString::number(peak)));
        QVERIFY(peak > ceiling * 0.9f); // limited, not muted
        // Quiet audio passes unchanged, only delayed.
        dsp::Limiter lim2(48000, 1, -1.0);
        auto q = sine(4800, 1, 220.0, 0.3);
        auto orig = q;
        lim2.process(q.data(), 4800);
        for (int i = lim2.latency(); i < 4800; ++i)
            QVERIFY(std::abs(q[i] - orig[i - lim2.latency()]) < 1e-6f);
    }

    void denoiserIsTransparentForSignalAndDelaysExactly() {
        // A tone in bursts (like speech, it pauses) passes almost unchanged
        // and comes out exactly latency() samples later.
        dsp::SpectralDenoiser d(48000, 1, 0.5);
        auto s = sine(48000, 1, 1000.0, 0.5);
        quint32 seed = 7;
        for (int i = 0; i < 48000; ++i) {
            if ((i / 9600) % 2 == 0)
                s[i] = 0.0f;
            seed = seed * 1664525u + 1013904223u;
            s[i] += (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.002f; // a quiet noise floor
        }
        auto orig = s;
        d.process(s.data(), 48000);
        const int L = d.latency();
        double err = 0, ref = 0;
        for (int i = 24000; i < 48000; ++i) {
            err += std::pow(s[i] - orig[i - L], 2);
            ref += std::pow(orig[i - L], 2);
        }
        QVERIFY2(err / ref < 0.01, qPrintable(QString::number(err / ref)));
    }

    void denoiserLowersSteadyNoise() {
        dsp::SpectralDenoiser d(48000, 1, 1.0);
        std::vector<float> noise(48000 * 3);
        quint32 seed = 1;
        for (float& v : noise) {
            seed = seed * 1664525u + 1013904223u;
            v = (static_cast<float>(seed >> 8) / 16777216.0f - 0.5f) * 0.05f;
        }
        const double before = rms(noise, 48000 * 2, noise.size());
        d.process(noise.data(), static_cast<int>(noise.size()));
        const double after = rms(noise, 48000 * 2, noise.size());
        QVERIFY2(after < before * 0.3, qPrintable(QString("%1 -> %2").arg(before).arg(after)));
    }

    void humRemovalTakesOutMainsBuzzOnly() {
        // 50 Hz hum with harmonics under a 1 kHz tone: the hum goes, the tone stays.
        AudioEffects fx;
        fx.humHz = 50;
        dsp::ClipChain chain(fx, 48000, 1);
        auto hum = sine(48000 * 2, 1, 50.0, 0.1);
        auto h3 = sine(48000 * 2, 1, 150.0, 0.05);
        auto tone = sine(48000 * 2, 1, 1110.0, 0.1);
        for (size_t i = 0; i < hum.size(); ++i)
            hum[i] += h3[i];
        auto mixed = hum;
        for (size_t i = 0; i < mixed.size(); ++i)
            mixed[i] += tone[i];
        chain.process(hum.data(), 48000 * 2);
        QVERIFY2(rms(hum, 48000, hum.size()) < 0.005, qPrintable(QString::number(rms(hum, 48000, hum.size()))));
        dsp::ClipChain chain2(fx, 48000, 1);
        chain2.process(mixed.data(), 48000 * 2);
        const double toneRms = 0.1 / std::sqrt(2.0);
        QVERIFY(std::abs(rms(mixed, 48000, mixed.size()) / toneRms - 1.0) < 0.05);
        // 60 Hz notches leave 50 Hz alone.
        fx.humHz = 60;
        dsp::ClipChain chain3(fx, 48000, 1);
        auto fifty = sine(48000 * 2, 1, 50.0, 0.1);
        chain3.process(fifty.data(), 48000 * 2);
        QVERIFY(rms(fifty, 48000, fifty.size()) > 0.06);
        QVERIFY(AudioEffects::fromJson(AudioEffects{.humHz = 60}.toJson()).humHz == 60);
    }

    void windReducerPullsDownGustsKeepsVoice() {
        // Wind: loud noise below 300 Hz. Voice: a 1 kHz tone with a 200 Hz
        // part at a natural level.
        const int n = 48000 * 2;
        std::vector<float> wind(n);
        quint32 seed = 3;
        double lp1 = 0.0, lp2 = 0.0;
        for (float& v : wind) {
            seed = seed * 1664525u + 1013904223u;
            const double w = (static_cast<double>(seed >> 8) / 16777216.0 - 0.5);
            lp1 += 0.02 * (w - lp1); // low-pass: rumble under ~150 Hz
            lp2 += 0.02 * (lp1 - lp2);
            v = static_cast<float>(lp2 * 20.0);
        }
        auto voice = sine(n, 1, 1000.0, 0.1);
        auto low = sine(n, 1, 200.0, 0.1);
        for (int i = 0; i < n; ++i)
            voice[i] += low[i];
        auto mixed = voice;
        for (int i = 0; i < n; ++i)
            mixed[i] += wind[i];
        const double windBefore = rms(wind, n / 2, n);
        dsp::WindReducer r(48000, 1, 1.0);
        r.process(wind.data(), n);
        QVERIFY2(rms(wind, n / 2, n) < windBefore * 0.3,
                 qPrintable(QString("%1 -> %2").arg(windBefore).arg(rms(wind, n / 2, n))));
        // The voice alone passes (only delayed).
        dsp::WindReducer r2(48000, 1, 1.0);
        auto v = voice;
        r2.process(v.data(), n);
        const int L = r2.latency();
        double err = 0, ref = 0;
        for (int i = n / 2; i < n; ++i) {
            err += std::pow(v[i] - voice[i - L], 2);
            ref += std::pow(voice[i - L], 2);
        }
        QVERIFY2(err / ref < 0.01, qPrintable(QString::number(err / ref)));
    }

    void equaliserBoostsItsBand() {
        dsp::Biquad b;
        b.design(dsp::Biquad::Type::Peak, 48000, 1000, 0.9, 6.0);
        auto at = sine(48000, 1, 1000.0, 0.1);
        b.process(at.data(), 48000, 1);
        QVERIFY(std::abs(rms(at, 24000, 48000) / (0.1 / std::sqrt(2.0)) - 2.0) < 0.05);
        dsp::Biquad b2;
        b2.design(dsp::Biquad::Type::Peak, 48000, 1000, 0.9, 6.0);
        auto far = sine(48000, 1, 60.0, 0.1);
        b2.process(far.data(), 48000, 1);
        QVERIFY(std::abs(rms(far, 24000, 48000) / (0.1 / std::sqrt(2.0)) - 1.0) < 0.05);
    }

    void clipEffectsStayInSync() {
        // A noise reduced clip must line up with the untouched one sample for
        // sample (the effect's delay is compensated), also after a seek.
        Timeline plain, cleaned;
        plain.insertMedia(m_tone->id, 0, sec(4), true, true);
        cleaned.insertMedia(m_tone->id, 0, sec(4), true, true);
        // (A very light setting: the test tone is steady, which noise
        // reduction rightly treats as noise.)
        AudioEffects fx;
        fx.denoise = 1;
        QVERIFY(cleaned.setAudioEffects(cleaned.tracks()[1].clips[0].id, fx));
        AudioMixer a(&m_pool, 48000, 2), b(&m_pool, 48000, 2);
        a.setTimeline(plain.state());
        b.setTimeline(cleaned.state());
        std::vector<float> x(9600 * 2), y(9600 * 2);
        for (std::int64_t start : {std::int64_t(48000), std::int64_t(57600), std::int64_t(120000)}) {
            a.mix(start, 9600, x.data());
            b.mix(start, 9600, y.data());
            double err = 0, ref = 0;
            for (size_t i = 0; i < x.size(); ++i) {
                err += std::pow(x[i] - y[i], 2);
                ref += std::pow(x[i], 2);
            }
            QVERIFY2(err / ref < 0.02, qPrintable(QString("%1 at %2").arg(err / ref).arg(start)));
        }
    }

    void duckingLowersMusicUnderOtherClips() {
        Timeline tl;
        const int a2 = tl.addTrack(TrackType::Audio);
        // "Music" on A2 for 4 s, "voice" on A1 from 1 s to 3 s.
        const QUuid music = tl.placeMedia(m_tone->id, 0, sec(4), false, true, a2, 0);
        tl.placeMedia(m_tone->id, 0, sec(2), false, true, 1, sec(1));
        AudioEffects fx;
        fx.duckDb = -20;
        QVERIFY(tl.setAudioEffects(music, fx));
        // Mute the voice track to hear only the music; the key still comes
        // from the voice, so measure with the voice on but compare against
        // a mix of the voice alone.
        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        std::vector<float> buf(48000 * 4 * 2);
        for (int s = 0; s < 4; ++s)
            mixer.mix(s * 48000, 48000, buf.data() + static_cast<size_t>(s) * 48000 * 2);
        const double before = rms(buf, 24000 * 2, 46000 * 2);   // music alone (0.5 .. ~1 s)
        const double during = rms(buf, 72000 * 2, 140000 * 2);  // voice + ducked music
        // Same tone in phase: voice 0.2 + music 0.02 -> 1.1x, versus 2x without ducking.
        QVERIFY2(during / before < 1.3, qPrintable(QString::number(during / before)));
        QVERIFY(during / before > 0.9);
    }

    void mutedClipsAreSilentOthersPlay() {
        // Two videos with sound on V2; only the first one is muted.
        Timeline tl;
        const int v2 = tl.addTrack(TrackType::Video);
        const QUuid first = tl.placeMedia(m_tone->id, 0, sec(2), true, true, v2, 0);
        const QUuid second = tl.placeMedia(m_tone->id, 0, sec(2), true, true, v2, sec(2));
        QVERIFY(!first.isNull() && !second.isNull());
        QVERIFY(!tl.clipsMuted({first}));
        QVERIFY(tl.setClipsMuted({first}, true));
        QVERIFY(tl.clipsMuted({first}));
        QVERIFY(!tl.clipsMuted({first, second}));
        QVERIFY(!tl.setClipsMuted({first}, true)); // nothing changes
        // The linked sound clip is muted too, the picture stays.
        int mutedAudio = 0;
        for (const auto& id : tl.linkedClips(first))
            mutedAudio += tl.clip(id)->muted ? 1 : 0;
        QCOMPARE(mutedAudio, 2);
        QVERIFY(tl.state().hasAudioChanges());

        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        std::vector<float> buf(48000 * 4 * 2);
        for (int s = 0; s < 4; ++s)
            mixer.mix(s * 48000, 48000, buf.data() + static_cast<size_t>(s) * 48000 * 2);
        QVERIFY2(rms(buf, 0, 90000 * 2) < 1e-4, qPrintable(QString::number(rms(buf, 0, 90000 * 2))));
        QVERIFY(rms(buf, 100000 * 2, 190000 * 2) > 0.05);

        QVERIFY(tl.setClipsMuted({first}, false));
        mixer.setTimeline(tl.state());
        mixer.mix(48000, 9600, buf.data());
        QVERIFY(rms(buf, 0, 9600 * 2) > 0.05);
    }

    void normalisationReachesTheTarget() {
        Timeline tl;
        tl.insertMedia(m_tone->id, 0, sec(4), true, true);
        AudioMaster m;
        m.normalize = true;
        m.targetLufs = -16.0;
        m.ceilingDb = -0.5;
        QVERIFY(tl.setAudioMaster(m));
        const auto r = loudness::measureTimeline(tl.state(), &m_pool, 0, sec(4));
        QVERIFY(r.ok);
        const double gain = loudness::normalizationGain(r.integrated, m.targetLufs);
        AudioMixer mixer(&m_pool, 48000, 2);
        mixer.setTimeline(tl.state());
        mixer.setNormalizationGainDb(gain);
        LoudnessMeter meter(48000, 2);
        std::vector<float> buf(48000 * 2);
        for (int s = 0; s < 4; ++s) {
            mixer.mix(s * 48000, 48000, buf.data());
            meter.add(buf.data(), 48000);
        }
        QVERIFY2(std::abs(meter.integrated() - (-16.0)) < 0.5, qPrintable(QString::number(meter.integrated())));
        QVERIFY(meter.samplePeakDb() <= -0.5 + 0.01);
    }

    void effectsJsonRoundTrip() {
        AudioEffects fx;
        fx.lowCut = 80;
        fx.denoise = 40;
        fx.eqMid = -3;
        fx.compressor = true;
        fx.duckDb = -12;
        QCOMPARE(AudioEffects::fromJson(fx.toJson()), fx);
        QVERIFY(AudioEffects::fromJson({}).isIdentity());
        AudioMaster m;
        m.limiter = true;
        m.ceilingDb = -2;
        QCOMPARE(AudioMaster::fromJson(m.toJson()), m);
    }
};

QTEST_MAIN(AudioTest)
#include "tst_audio.moc"

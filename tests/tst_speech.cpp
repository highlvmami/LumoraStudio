#include <QtTest>

#include "TestMedia.h"
#include "ai/SpeechToText.h"
#include "media/MediaPool.h"
#include "timeline/Timeline.h"

using namespace lumora;

class SpeechTest : public QObject {
    Q_OBJECT

private slots:
    void modelsAreOffered() {
        const auto models = speech::models();
        QVERIFY(models.size() >= 2);
        bool hasDefault = false;
        for (const auto& m : models) {
            hasDefault |= m.id == speech::defaultModel();
            QVERIFY(m.sizeMB > 0);
            QVERIFY(speech::modelUrl(m.id).toString().endsWith(QStringLiteral("ggml-%1.bin").arg(m.id)));
        }
        QVERIFY(hasDefault);
    }

    void missingModelOrSoundIsReported() {
        if (!speech::available())
            QSKIP("built without whisper.cpp");
        Timeline tl;
        speech::Options o;
        o.modelPath = QStringLiteral("/nonexistent/ggml-small.bin");
        auto r = speech::transcribe(tl.state(), MediaPool(), 0, ticksFromSeconds(1), o);
        QVERIFY(!r.ok());
        QVERIFY(!r.error.isEmpty());
    }

    void speechBecomesSubtitles() {
        if (!speech::available())
            QSKIP("built without whisper.cpp");
        const QString model = qEnvironmentVariable("LUMORA_WHISPER_MODEL");
        const QString wav = qEnvironmentVariable("LUMORA_SPEECH_WAV");
        if (model.isEmpty() || wav.isEmpty())
            QSKIP("set LUMORA_WHISPER_MODEL and LUMORA_SPEECH_WAV to run");
        MediaPool pool;
        QString err;
        auto item = pool.import(wav, &err);
        QVERIFY2(item, qPrintable(err));
        Timeline tl;
        // Two seconds of silence first: cue times must be timeline times.
        tl.insertMedia(item->id, 0, item->info.duration, false, true, ticksFromSeconds(2));
        speech::Options o;
        o.modelPath = model;
        o.language = QStringLiteral("en");
        double last = -1;
        auto r = speech::transcribe(tl.state(), pool, 0, tl.duration(), o, [&](double p) {
            last = p;
            return true;
        });
        QVERIFY2(r.ok(), qPrintable(r.error));
        QVERIFY(!r.cues.empty());
        QCOMPARE(last, 1.0);
        QString all;
        for (const auto& c : r.cues) {
            QVERIFY(c.end > c.start);
            QVERIFY(c.end <= tl.duration());
            all += c.text + QLatin1Char(' ');
        }
        qInfo() << all;
        // The smallest model mishears the synthetic voice now and then, so
        // most of the words, not every one, must come through.
        int heard = 0;
        for (const char* word : {"hello", "world", "test", "automatic", "subtitle"})
            heard += all.contains(QLatin1String(word), Qt::CaseInsensitive);
        QVERIFY2(heard >= 3, qPrintable(all));
        QVERIFY(r.cues.front().start >= ticksFromSeconds(1.5));

        // Word by word, with timeline times in order.
        const auto pcm = speech::listen(tl.state(), pool, 0, tl.duration());
        QVERIFY(!pcm.empty());
        const auto w = speech::words(pcm, 0, o);
        QVERIFY2(w.ok(), qPrintable(w.error));
        QVERIFY(w.words.size() >= 4);
        for (size_t i = 0; i < w.words.size(); ++i) {
            QVERIFY(w.words[i].end > w.words[i].start);
            QVERIFY(!w.words[i].text.contains(QLatin1Char(' ')));
            if (i > 0)
                QVERIFY(w.words[i].start >= w.words[i - 1].start);
        }
        QVERIFY(w.words.front().start >= ticksFromSeconds(1.5));

        // Cancelling stops early.
        auto c = speech::transcribe(tl.state(), pool, 0, tl.duration(), o, [](double p) { return p < 0.05; });
        QVERIFY(c.cancelled);
    }
};

QTEST_GUILESS_MAIN(SpeechTest)
#include "tst_speech.moc"

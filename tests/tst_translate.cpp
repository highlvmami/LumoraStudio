#include <QtTest>

#include "ai/SentencePiece.h"
#include "ai/Translate.h"

using namespace lumora;

namespace {

// A SentencePiece model file in protocol buffer form: the pieces with their
// scores and types (1 normal, 2 unknown, 3 control), no character map.
QByteArray spmModel(const std::vector<std::tuple<QByteArray, float, int>>& pieces) {
    const auto varint = [](QByteArray& out, quint64 v) {
        do {
            char b = static_cast<char>(v & 0x7f);
            v >>= 7;
            if (v)
                b |= static_cast<char>(0x80);
            out.append(b);
        } while (v);
    };
    QByteArray model;
    for (const auto& [text, score, type] : pieces) {
        QByteArray p;
        p.append(char(0x0a));
        varint(p, text.size());
        p.append(text);
        p.append(char(0x15));
        p.append(reinterpret_cast<const char*>(&score), 4);
        p.append(char(0x18));
        varint(p, type);
        model.append(char(0x0a));
        varint(model, p.size());
        model.append(p);
    }
    return model;
}

QStringList encode(const ai::SentencePiece& sp, const QString& text) {
    QStringList out;
    for (const auto& p : sp.encode(text))
        out << QString::fromStdString(p);
    return out;
}

} // namespace

class TranslateTest : public QObject {
    Q_OBJECT

private slots:
    void sentencePieceFindsTheLikeliestSplit() {
        ai::SentencePiece sp;
        QString err;
        QVERIFY2(sp.loadData(spmModel({{"<unk>", 0, 2},
                                       {"<s>", 0, 3},
                                       {"</s>", 0, 3},
                                       {"\xe2\x96\x81", -2, 1},
                                       {"\xe2\x96\x81merhaba", -3, 1},
                                       {"\xe2\x96\x81mer", -2, 1},
                                       {"haba", -2, 1},
                                       {"\xe2\x96\x81" "d\xc3\xbc", -3, 1},
                                       {"nya", -3, 1},
                                       {"m", -5, 1},
                                       {"e", -5, 1},
                                       {"r", -5, 1},
                                       {"a", -5, 1}}),
                             &err),
                 qPrintable(err));
        // One piece (-3) beats two (-4); spaces collapse and become "▁".
        QCOMPARE(encode(sp, QStringLiteral("  merhaba   dünya ")),
                 (QStringList{QStringLiteral("▁merhaba"), QStringLiteral("▁dü"), QStringLiteral("nya")}));
        // Unknown characters in a row come back as one piece.
        QCOMPARE(encode(sp, QStringLiteral("mer xyz")),
                 (QStringList{QStringLiteral("▁mer"), QStringLiteral("▁"), QStringLiteral("xyz")}));
        QVERIFY(encode(sp, QStringLiteral("   ")).isEmpty());
        QCOMPARE(ai::SentencePiece::decode({"\xe2\x96\x81Hello", ",", "\xe2\x96\x81wor", "ld"}), QStringLiteral("Hello, world"));
    }

    void brokenModelIsRefused() {
        ai::SentencePiece sp;
        QString err;
        QVERIFY(!sp.loadData(QByteArray("\x0a\xff\xff\xff", 4), &err));
        QVERIFY(!err.isEmpty());
        QVERIFY(sp.isNull());
    }

    void languagesAreRouted() {
        QCOMPARE(translate::route(QStringLiteral("tr"), QStringLiteral("en")), QStringList{QStringLiteral("tr-en")});
        QCOMPARE(translate::route(QStringLiteral("en"), QStringLiteral("tr")), QStringList{QStringLiteral("en-tr")});
        QCOMPARE(translate::route(QStringLiteral("tr"), QStringLiteral("de")),
                 (QStringList{QStringLiteral("tr-en"), QStringLiteral("en-de")}));
        QCOMPARE(translate::route(QStringLiteral("de"), QStringLiteral("fr")), QStringList{QStringLiteral("de-fr")});
        QVERIFY(translate::route(QStringLiteral("tr"), QStringLiteral("tr")).isEmpty());
        QVERIFY(translate::route(QStringLiteral("tr"), QStringLiteral("xx")).isEmpty());
        // Every language reaches every other one.
        for (const auto& a : translate::languages())
            for (const auto& b : translate::languages())
                if (a.code != b.code)
                    QVERIFY2(!translate::route(a.code, b.code).isEmpty(), qPrintable(a.code + QLatin1Char('-') + b.code));
        for (const auto& m : translate::models()) {
            QVERIFY(m.sizeMB > 0);
            for (const auto& f : translate::modelFiles())
                QVERIFY(translate::fileUrl(m.id, f).toString().startsWith(QStringLiteral("https://huggingface.co/") + m.repo));
        }
    }

    void linesAreBalanced() {
        QCOMPARE(translate::fitLines(QStringLiteral("  Short  line "), 2), QStringLiteral("Short line"));
        const QString two = translate::fitLines(QStringLiteral("This sentence is a good deal longer than one subtitle line"), 2);
        const QStringList lines = two.split(QLatin1Char('\n'));
        QCOMPARE(lines.size(), 2);
        QVERIFY(qAbs(lines[0].size() - lines[1].size()) <= 8);
        QCOMPARE(lines.join(QLatin1Char(' ')), QStringLiteral("This sentence is a good deal longer than one subtitle line"));
        QCOMPARE(translate::fitLines(QStringLiteral("Onewordthatisfartoolongforasinglesubtitleline"), 2),
                 QStringLiteral("Onewordthatisfartoolongforasinglesubtitleline"));
    }

    void missingModelIsReported() {
        if (!translate::available())
            QSKIP("built without ONNX Runtime");
        qputenv("LUMORA_TRANSLATE_MODELS", QDir::tempPath().toUtf8() + "/lumora-no-models");
        const auto r = translate::translateCues({{0, 1000, QStringLiteral("Merhaba")}}, QStringLiteral("tr"), QStringLiteral("en"));
        QVERIFY(!r.ok());
        QVERIFY(!r.error.isEmpty());
        qputenv("LUMORA_TRANSLATE_MODELS", m_models.toUtf8());
    }

    // End to end with the real models (CI downloads them).
    void subtitlesAreTranslated() {
        if (!translate::available())
            QSKIP("built without ONNX Runtime");
        if (m_models.isEmpty())
            QSKIP("set LUMORA_TRANSLATE_MODELS to a folder with the tr-en, en-tr and en-de models to run");
        for (const char* id : {"tr-en", "en-tr", "en-de"})
            if (!translate::modelReady(QString::fromLatin1(id)))
                QSKIP("translation models missing");
        QString err;
        auto toEnglish = translate::Translator::load(QStringLiteral("tr-en"), &err);
        QVERIFY2(toEnglish, qPrintable(err));
        const QString hello = toEnglish->translate(QStringLiteral("Merhaba, nasılsın?"), &err);
        qInfo() << "tr-en:" << hello;
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(hello.contains(QStringLiteral("how are you"), Qt::CaseInsensitive));

        auto toTurkish = translate::Translator::load(QStringLiteral("en-tr"), &err);
        QVERIFY2(toTurkish, qPrintable(err));
        const QString tom = toTurkish->translate(QStringLiteral("I know Tom didn't want to eat that."), &err);
        qInfo() << "en-tr:" << tom;
        QVERIFY2(err.isEmpty(), qPrintable(err));
        QVERIFY(tom.contains(QStringLiteral("Tom")));
        QVERIFY(tom.contains(QStringLiteral("biliyorum")));
        // Greedy search gives a translation too.
        toTurkish->setBeams(1);
        QVERIFY(!toTurkish->translate(QStringLiteral("Good morning."), &err).isEmpty());
        toTurkish->setBeams(4);

        // Turkish to German goes through English; times and empty cues stay.
        const std::vector<SubtitleCue> cues{{0, 2000, QStringLiteral("Günaydın!")},
                                            {2000, 3000, QString()},
                                            {3000, 6000, QStringLiteral("Bugün hava çok güzel.\nDışarı çıkalım.")}};
        double last = -1;
        const auto r = translate::translateCues(cues, QStringLiteral("tr"), QStringLiteral("de"), [&](double p) {
            last = p;
            return true;
        });
        QVERIFY2(r.ok(), qPrintable(r.error));
        QCOMPARE(r.cues.size(), cues.size());
        for (const auto& c : r.cues)
            qInfo() << "tr-de:" << c.text;
        QCOMPARE(r.cues[1].text, QString());
        QCOMPARE(r.cues[2].start, cues[2].start);
        QCOMPARE(r.cues[2].end, cues[2].end);
        QVERIFY(r.cues[0].text.contains(QStringLiteral("Morgen"), Qt::CaseInsensitive));
        QVERIFY(r.cues[2].text.contains(QStringLiteral("Wetter"), Qt::CaseInsensitive));
        QCOMPARE(last, 1.0);

        const auto cancelled = translate::translateCues(cues, QStringLiteral("tr"), QStringLiteral("en"), [](double) { return false; });
        QVERIFY(cancelled.cancelled);
    }

private:
    QString m_models = qEnvironmentVariable("LUMORA_TRANSLATE_MODELS");
};

QTEST_GUILESS_MAIN(TranslateTest)
#include "tst_translate.moc"

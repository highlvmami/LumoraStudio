#include "Translate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>
#include <algorithm>
#include <cmath>
#include <limits>
#include <mutex>
#include <unordered_map>

#include "ai/Onnx.h"
#include "ai/SentencePiece.h"

namespace lumora::translate {

namespace {

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(lumora::translate)
};

constexpr int kMaxSourceTokens = 256;

} // namespace

bool available() { return ai::onnxAvailable(); }

std::vector<Language> languages() {
    return {
        {QStringLiteral("tr"), Tr::tr("Turkish")}, {QStringLiteral("en"), Tr::tr("English")},
        {QStringLiteral("de"), Tr::tr("German")},  {QStringLiteral("fr"), Tr::tr("French")},
        {QStringLiteral("es"), Tr::tr("Spanish")}, {QStringLiteral("it"), Tr::tr("Italian")},
        {QStringLiteral("nl"), Tr::tr("Dutch")},   {QStringLiteral("ru"), Tr::tr("Russian")},
        {QStringLiteral("ar"), Tr::tr("Arabic")},
    };
}

QString languageName(const QString& code) {
    for (const auto& l : languages())
        if (l.code == code)
            return l.name;
    return code;
}

std::vector<Model> models() {
    // Turkish uses the larger "tc-big" models: the small ones translate it
    // poorly.
    const auto small = [](const char* from, const char* to, const char* prefix = "") {
        const QString id = QStringLiteral("%1-%2").arg(QLatin1String(from), QLatin1String(to));
        return Model{id, QStringLiteral("Xenova/opus-mt-") + id, QString::fromLatin1(prefix), 110};
    };
    return {
        {QStringLiteral("tr-en"), QStringLiteral("Xenova/opus-mt-tc-big-tr-en"), {}, 300},
        {QStringLiteral("en-tr"), QStringLiteral("onnx-community/opus-mt-tc-big-en-tr"), {}, 535},
        small("de", "en"), small("en", "de"), small("fr", "en"), small("en", "fr"),
        small("es", "en"), small("en", "es"), small("it", "en"), small("en", "it"),
        small("nl", "en"), small("en", "nl"), small("ru", "en"), small("en", "ru"),
        small("ar", "en"), small("en", "ar", ">>ara<<"),
        // Direct pairs between other languages skip the detour through English.
        small("de", "fr"), small("fr", "de"), small("de", "es"), small("es", "de"),
        small("fr", "es"), small("es", "fr"), small("es", "it"), small("it", "es"),
        small("fr", "ru"), small("ru", "fr"), small("es", "ru"), small("ru", "es"),
    };
}

Model model(const QString& id) {
    for (auto& m : models())
        if (m.id == id)
            return m;
    return {};
}

QStringList route(const QString& from, const QString& to) {
    if (from == to)
        return {};
    const auto has = [](const QString& id) { return !model(id).id.isEmpty(); };
    const QString direct = from + QLatin1Char('-') + to;
    if (has(direct))
        return {direct};
    const QString a = from + QStringLiteral("-en"), b = QStringLiteral("en-") + to;
    if (has(a) && has(b))
        return {a, b};
    return {};
}

std::vector<ModelFile> modelFiles() {
    return {
        {QStringLiteral("config.json"), QStringLiteral("config.json")},
        {QStringLiteral("vocab.json"), QStringLiteral("vocab.json")},
        {QStringLiteral("source.spm"), QStringLiteral("source.spm")},
        {QStringLiteral("onnx/encoder_model_quantized.onnx"), QStringLiteral("encoder.onnx")},
        {QStringLiteral("onnx/decoder_model_merged_quantized.onnx"), QStringLiteral("decoder.onnx")},
    };
}

QUrl fileUrl(const QString& id, const ModelFile& file) {
    return QUrl(QStringLiteral("https://huggingface.co/%1/resolve/main/%2").arg(model(id).repo, file.remote));
}

QString modelDir(const QString& id) {
    // Tests and CI can point at a folder of already downloaded models.
    const QString override = qEnvironmentVariable("LUMORA_TRANSLATE_MODELS");
    const QString base = !override.isEmpty()
                             ? override
                             : QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) +
                                   QStringLiteral("/models/translate");
    const QString dir = base + QLatin1Char('/') + id;
    QDir().mkpath(dir);
    return dir;
}

bool modelReady(const QString& id) {
    if (!available() || model(id).id.isEmpty())
        return false;
    const QString dir = modelDir(id);
    for (const auto& f : modelFiles())
        if (!QFileInfo::exists(dir + QLatin1Char('/') + f.local))
            return false;
    return true;
}

// --- Translator ---------------------------------------------------------

struct Translator::Impl {
    std::shared_ptr<ai::Network> encoder, decoder;
    ai::SentencePiece spm;
    std::unordered_map<std::string, int64_t> vocab;
    std::vector<std::string> pieces; // id -> piece
    int64_t unk = 0, eos = 0, pad = 0, start = 0;
    int64_t prefixId = -1;
    int layers = 0, heads = 8, headDim = 64;
    int beams = 4;
    bool merged = false; // decoder with a cache (decoder_model_merged)
};

Translator::Translator() : d(std::make_unique<Impl>()) {}
Translator::~Translator() = default;

void Translator::setBeams(int beams) { d->beams = std::clamp(beams, 1, 8); }

std::shared_ptr<Translator> Translator::load(const QString& id, QString* error) {
    static std::mutex mutex;
    static QHash<QString, std::shared_ptr<Translator>> cache;
    std::lock_guard lock(mutex);
    if (auto it = cache.find(id); it != cache.end())
        return *it;
    const Model m = model(id);
    if (m.id.isEmpty()) {
        if (error)
            *error = Tr::tr("No translation model for %1.").arg(id);
        return nullptr;
    }
    if (!modelReady(id)) {
        if (error)
            *error = Tr::tr("The translation model has not been downloaded yet.");
        return nullptr;
    }
    auto t = loadFrom(modelDir(id), m.prefix, error);
    if (t)
        cache.insert(id, t);
    return t;
}

std::shared_ptr<Translator> Translator::loadFrom(const QString& dir, const QString& prefix, QString* error) {
    if (!available()) {
        if (error)
            *error = Tr::tr("This build of LumoraStudio has no AI tools.");
        return nullptr;
    }
    std::shared_ptr<Translator> t(new Translator);
    Impl& d = *t->d;
    const auto fail = [&](const QString& why) -> std::shared_ptr<Translator> {
        if (error)
            *error = Tr::tr("The translation model could not be loaded: %1").arg(why);
        return nullptr;
    };
    QString why;
    if (!d.spm.load(dir + QStringLiteral("/source.spm"), &why))
        return fail(why);

    QFile vocabFile(dir + QStringLiteral("/vocab.json"));
    if (!vocabFile.open(QIODevice::ReadOnly))
        return fail(vocabFile.errorString());
    const QJsonObject vocab = QJsonDocument::fromJson(vocabFile.readAll()).object();
    if (vocab.isEmpty())
        return fail(QStringLiteral("empty vocabulary"));
    for (auto it = vocab.begin(); it != vocab.end(); ++it) {
        const int64_t id = it.value().toInteger(-1);
        if (id < 0)
            continue;
        d.vocab[it.key().toStdString()] = id;
        if (id >= static_cast<int64_t>(d.pieces.size()))
            d.pieces.resize(id + 1);
        d.pieces[id] = it.key().toStdString();
    }
    const auto token = [&](const char* piece, int64_t fallback) {
        const auto it = d.vocab.find(piece);
        return it == d.vocab.end() ? fallback : it->second;
    };
    d.unk = token("<unk>", 1);
    d.eos = token("</s>", 0);
    d.pad = token("<pad>", static_cast<int64_t>(d.pieces.size()) - 1);

    QFile configFile(dir + QStringLiteral("/config.json"));
    QJsonObject config;
    if (configFile.open(QIODevice::ReadOnly))
        config = QJsonDocument::fromJson(configFile.readAll()).object();
    d.eos = config.value(QStringLiteral("eos_token_id")).toInteger(d.eos);
    d.pad = config.value(QStringLiteral("pad_token_id")).toInteger(d.pad);
    d.start = config.value(QStringLiteral("decoder_start_token_id")).toInteger(d.pad);
    d.heads = config.value(QStringLiteral("decoder_attention_heads")).toInt(8);
    const int model = config.value(QStringLiteral("d_model")).toInt(512);
    d.headDim = model / std::max(1, d.heads);
    if (!prefix.isEmpty()) {
        const auto it = d.vocab.find(prefix.toStdString());
        if (it == d.vocab.end())
            return fail(QStringLiteral("unknown language token %1").arg(prefix));
        d.prefixId = it->second;
    }

    d.encoder = ai::Network::load(dir + QStringLiteral("/encoder.onnx"), &why);
    if (!d.encoder)
        return fail(why);
    d.decoder = ai::Network::load(dir + QStringLiteral("/decoder.onnx"), &why);
    if (!d.decoder)
        return fail(why);
    for (const auto& name : d.decoder->inputNames()) {
        if (name == "use_cache_branch")
            d.merged = true;
        if (name.ends_with(".decoder.key"))
            ++d.layers;
    }
    if (d.merged && d.layers == 0)
        return fail(QStringLiteral("unexpected decoder inputs"));
    return t;
}

std::vector<int64_t> Translator::tokenize(const QString& text) const {
    std::vector<int64_t> ids;
    if (d->prefixId >= 0)
        ids.push_back(d->prefixId);
    for (const auto& piece : d->spm.encode(text)) {
        const auto it = d->vocab.find(piece);
        ids.push_back(it == d->vocab.end() ? d->unk : it->second);
        if (static_cast<int>(ids.size()) >= kMaxSourceTokens - 1)
            break;
    }
    ids.push_back(d->eos);
    return ids;
}

namespace {

ai::Tensor ints(std::vector<int64_t> shape, std::vector<int64_t> values, ai::Tensor::Type type = ai::Tensor::Type::Int64) {
    ai::Tensor t;
    t.shape = std::move(shape);
    t.ints = std::move(values);
    t.type = type;
    return t;
}

// Row `row` of a [rows, ...] tensor repeated / picked into a new batch.
ai::Tensor gather(const ai::Tensor& t, const std::vector<int>& rows) {
    ai::Tensor out;
    out.shape = t.shape;
    out.shape[0] = static_cast<int64_t>(rows.size());
    const size_t stride = t.shape[0] ? t.data.size() / t.shape[0] : 0;
    out.data.resize(stride * rows.size());
    for (size_t i = 0; i < rows.size(); ++i)
        std::copy_n(t.data.begin() + rows[i] * stride, stride, out.data.begin() + i * stride);
    return out;
}

struct Hypothesis {
    std::vector<int64_t> tokens;
    double score = 0; // sum of log probabilities
};

} // namespace

QString Translator::translate(const QString& text, QString* error) const {
    const QString source = text.simplified();
    if (source.isEmpty())
        return {};
    const std::vector<int64_t> ids = tokenize(source);
    const auto S = static_cast<int64_t>(ids.size());

    auto encoded = d->encoder->run({{"input_ids", ints({1, S}, ids)}, {"attention_mask", ints({1, S}, std::vector<int64_t>(S, 1))}},
                                   {d->encoder->outputNames().front()}, error);
    if (encoded.empty())
        return {};
    const ai::Tensor hidden = std::move(encoded.front());

    const int B = d->beams;
    const int maxLength = static_cast<int>(std::min<int64_t>(512, 3 * S + 10));
    std::vector<Hypothesis> alive{{{d->start}, 0.0}};
    std::vector<std::pair<double, std::vector<int64_t>>> finished; // length-normalised score
    // The decoder's cache: self attention per layer (key, value), grown by
    // one step each time, and the cross attention, fixed after the first.
    std::vector<ai::Tensor> selfCache, crossCache;
    ai::Tensor batchHidden = hidden;
    int batch = 1;

    for (int step = 0; step < maxLength && !alive.empty(); ++step) {
        std::vector<std::pair<std::string, ai::Tensor>> in;
        std::vector<std::string> out{"logits"};
        in.emplace_back("encoder_attention_mask", ints({batch, S}, std::vector<int64_t>(batch * S, 1)));
        in.emplace_back("encoder_hidden_states", batchHidden);
        if (d->merged) {
            std::vector<int64_t> last;
            for (const auto& h : alive)
                last.push_back(h.tokens.back());
            in.emplace_back("input_ids", ints({batch, 1}, last));
            const bool cached = step > 0;
            for (int l = 0; l < d->layers; ++l) {
                const std::string p = "past_key_values." + std::to_string(l);
                for (int k = 0; k < 2; ++k) {
                    const char* kv = k ? ".value" : ".key";
                    ai::Tensor self, cross;
                    if (cached) {
                        self = selfCache[l * 2 + k];
                        cross = crossCache[l * 2 + k];
                    } else {
                        self.shape = cross.shape = {batch, d->heads, 0, d->headDim};
                    }
                    in.emplace_back(p + ".decoder" + kv, std::move(self));
                    in.emplace_back(p + ".encoder" + kv, std::move(cross));
                    out.push_back("present." + std::to_string(l) + ".decoder" + kv);
                    if (!cached)
                        out.push_back("present." + std::to_string(l) + ".encoder" + kv);
                }
            }
            in.emplace_back("use_cache_branch", ints({1}, {cached ? 1 : 0}, ai::Tensor::Type::Bool));
        } else {
            const auto t = static_cast<int64_t>(alive.front().tokens.size());
            std::vector<int64_t> all;
            for (const auto& h : alive)
                all.insert(all.end(), h.tokens.begin(), h.tokens.end());
            in.emplace_back("input_ids", ints({batch, t}, all));
        }
        auto result = d->decoder->run(in, out, error);
        if (result.empty())
            return {};
        const ai::Tensor& logits = result[0];
        const int64_t V = logits.shape.back();
        const int64_t positions = logits.shape.size() >= 3 ? logits.shape[1] : 1;

        // Every continuation of every hypothesis, best first.
        struct Candidate {
            double score;
            int beam;
            int64_t token;
        };
        std::vector<Candidate> candidates;
        // The last step can only end the sentence (forced, so it costs
        // nothing).
        const bool last = step + 1 == maxLength;
        for (int b = 0; b < batch; ++b) {
            if (last) {
                candidates.push_back({alive[b].score, b, d->eos});
                continue;
            }
            const float* row = logits.data.data() + ((b * positions) + positions - 1) * V;
            double peak = -std::numeric_limits<double>::infinity();
            for (int64_t v = 0; v < V; ++v)
                peak = std::max<double>(peak, row[v]);
            double sum = 0;
            for (int64_t v = 0; v < V; ++v)
                sum += std::exp(row[v] - peak);
            const double norm = peak + std::log(sum);
            // Only the best 2B of each row can be among the best 2B overall.
            // The padding token is never written.
            std::vector<std::pair<float, int64_t>> top;
            top.reserve(V);
            for (int64_t v = 0; v < V; ++v)
                if (v != d->pad)
                    top.emplace_back(row[v], v);
            const size_t keep = std::min<size_t>(top.size(), 2 * B);
            std::partial_sort(top.begin(), top.begin() + keep, top.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < keep; ++i)
                candidates.push_back({alive[b].score + top[i].first - norm, b, top[i].second});
        }
        std::sort(candidates.begin(), candidates.end(), [](auto& a, auto& b) { return a.score > b.score; });

        const int length = step + 1; // tokens written after the start token
        std::vector<Hypothesis> next;
        std::vector<int> origin;
        for (size_t i = 0; i < candidates.size() && static_cast<int>(next.size()) < B; ++i) {
            const Candidate& c = candidates[i];
            if (c.token == d->eos) {
                if (static_cast<int>(i) < B) {
                    std::vector<int64_t> tokens(alive[c.beam].tokens.begin() + 1, alive[c.beam].tokens.end());
                    finished.emplace_back(c.score / length, std::move(tokens));
                }
                continue;
            }
            Hypothesis h = alive[c.beam];
            h.tokens.push_back(c.token);
            h.score = c.score;
            next.push_back(std::move(h));
            origin.push_back(c.beam);
        }
        std::sort(finished.begin(), finished.end(), [](auto& a, auto& b) { return a.first > b.first; });
        if (static_cast<int>(finished.size()) > B)
            finished.resize(B);
        // Done when B translations are finished and no open one can still
        // beat the worst of them.
        if (static_cast<int>(finished.size()) >= B && (next.empty() || finished.back().first >= next.front().score / length))
            break;
        alive = std::move(next);
        if (alive.empty())
            break;
        if (d->merged) {
            if (step == 0) {
                crossCache.clear();
                selfCache.clear();
                for (int l = 0; l < d->layers; ++l)
                    for (int k = 0; k < 2; ++k) {
                        selfCache.push_back(gather(result[1 + l * 4 + k * 2], origin));
                        crossCache.push_back(gather(result[1 + l * 4 + k * 2 + 1], origin));
                    }
            } else {
                for (int l = 0; l < d->layers; ++l)
                    for (int k = 0; k < 2; ++k)
                        selfCache[l * 2 + k] = gather(result[1 + l * 2 + k], origin);
                if (static_cast<int>(origin.size()) != batch)
                    for (auto& c : crossCache)
                        c = gather(c, std::vector<int>(origin.size(), 0));
            }
        }
        if (static_cast<int>(alive.size()) != batch) {
            batch = static_cast<int>(alive.size());
            batchHidden = gather(hidden, std::vector<int>(batch, 0));
        }
    }
    if (finished.empty())
        return {};
    std::sort(finished.begin(), finished.end(), [](auto& a, auto& b) { return a.first > b.first; });
    std::vector<std::string> pieces;
    for (int64_t id : finished.front().second) {
        if (id == d->eos || id == d->pad || id == d->unk || id < 0 || id >= static_cast<int64_t>(d->pieces.size()))
            continue;
        pieces.push_back(d->pieces[id]);
    }
    return ai::SentencePiece::decode(pieces);
}

// --- subtitles ------------------------------------------------------------

QString fitLines(const QString& text, int lines, int maxLine) {
    const QStringList words = text.simplified().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    const QString joined = words.join(QLatin1Char(' '));
    const int needed = static_cast<int>(std::ceil(joined.size() / static_cast<double>(std::max(1, maxLine))));
    const int count = std::clamp(needed, 1, std::max(1, lines));
    if (count <= 1 || words.size() < 2)
        return joined;
    // Break where each line comes closest to an equal share.
    QStringList out;
    QString line;
    int done = 0;
    for (int i = 0; i < words.size(); ++i) {
        const int left = count - static_cast<int>(out.size());
        const double target = (joined.size() - done) / static_cast<double>(left);
        if (!line.isEmpty() && left > 1) {
            const double with = line.size() + 1 + words[i].size();
            if (std::abs(with - target) > std::abs(line.size() - target) && words.size() - i >= left - 1) {
                done += line.size() + 1;
                out << line;
                line.clear();
            }
        }
        line += (line.isEmpty() ? QString() : QStringLiteral(" ")) + words[i];
    }
    out << line;
    return out.join(QLatin1Char('\n'));
}

Result translateCues(const std::vector<SubtitleCue>& cues, const QString& from, const QString& to, const Progress& progress) {
    Result r;
    r.cues = cues;
    const QStringList steps = route(from, to);
    if (steps.isEmpty()) {
        r.error = Tr::tr("Subtitles cannot be translated from %1 into %2.").arg(languageName(from), languageName(to));
        return r;
    }
    std::vector<QString> texts;
    for (const auto& c : cues)
        texts.push_back(c.text);
    const size_t total = cues.size() * steps.size();
    size_t done = 0;
    for (const QString& id : steps) {
        auto translator = Translator::load(id, &r.error);
        if (!translator)
            return r;
        QHash<QString, QString> seen; // repeated lines are translated once
        for (auto& text : texts) {
            if (progress && !progress(static_cast<double>(done) / std::max<size_t>(1, total))) {
                r.cancelled = true;
                return r;
            }
            ++done;
            const QString source = text.simplified();
            if (source.isEmpty())
                continue;
            auto it = seen.find(source);
            if (it == seen.end()) {
                QString error;
                const QString out = translator->translate(source, &error);
                if (!error.isEmpty()) {
                    r.error = Tr::tr("Translation failed: %1").arg(error);
                    return r;
                }
                it = seen.insert(source, out);
            }
            text = *it;
        }
    }
    for (size_t i = 0; i < cues.size(); ++i) {
        const int lines = std::max<int>(2, static_cast<int>(cues[i].text.count(QLatin1Char('\n'))) + 1);
        r.cues[i].text = texts[i].isEmpty() ? cues[i].text : fitLines(texts[i], lines);
    }
    if (progress)
        progress(1.0);
    return r;
}

} // namespace lumora::translate


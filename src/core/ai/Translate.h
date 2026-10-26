#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>
#include <functional>
#include <memory>
#include <vector>

#include "timeline/Subtitles.h"

namespace lumora::translate {

// Reports progress (0..1); returning false cancels the work.
using Progress = std::function<bool(double)>;

// Text translation on this computer with the Opus-MT models of the
// University of Helsinki (MarianMT, run with ONNX Runtime). Each language
// pair is its own model, downloaded once; nothing is sent anywhere.
bool available();

struct Language {
    QString code; // ISO 639-1, e.g. "tr"
    QString name; // shown to the user
};
std::vector<Language> languages();
QString languageName(const QString& code);

struct Model {
    QString id;     // "<from>-<to>", e.g. "tr-en"
    QString repo;   // Hugging Face repository
    QString prefix; // target language token some models need, e.g. ">>ara<<"
    int sizeMB = 0; // download size
};
std::vector<Model> models();
Model model(const QString& id);

// The models that translate `from` into `to`, in order: one when a model
// for the pair exists, two through English otherwise. Empty when the
// languages are the same or not offered.
QStringList route(const QString& from, const QString& to);

// The files of a model: where each comes from in the repository and its
// name in the model's folder.
struct ModelFile {
    QString remote;
    QString local;
};
std::vector<ModelFile> modelFiles();
QUrl fileUrl(const QString& id, const ModelFile& file);
// Where a model's files are kept (the folder is created when missing).
QString modelDir(const QString& id);
bool modelReady(const QString& id);

// One loaded model. Loading takes a few seconds, so models are kept once
// loaded; translating is thread safe.
class Translator {
public:
    ~Translator();
    Translator(const Translator&) = delete;
    Translator& operator=(const Translator&) = delete;

    // nullptr (with `error` set) when the model is missing or broken.
    static std::shared_ptr<Translator> load(const QString& id, QString* error = nullptr);
    // Loads a model from any folder holding its files.
    static std::shared_ptr<Translator> loadFrom(const QString& dir, const QString& prefix, QString* error = nullptr);

    // Translates one piece of text (a sentence or a few). Empty with
    // `error` set on failure.
    QString translate(const QString& text, QString* error = nullptr) const;

    // Hypotheses kept while searching for the best translation (1 = greedy).
    void setBeams(int beams);

    // The token ids the model reads for `text`, for the tests.
    std::vector<int64_t> tokenize(const QString& text) const;

private:
    Translator();
    struct Impl;
    std::unique_ptr<Impl> d;
};

struct Result {
    std::vector<SubtitleCue> cues;
    QString error;
    bool cancelled = false;
    bool ok() const { return error.isEmpty() && !cancelled; }
};

// Translates the text of every cue from `from` into `to`; the times stay as
// they are. The models of route(from, to) must be downloaded.
Result translateCues(const std::vector<SubtitleCue>& cues, const QString& from, const QString& to,
                     const Progress& progress = {});

// Breaks `text` into at most `lines` lines of about equal length, each no
// longer than `maxLine` characters where the words allow.
QString fitLines(const QString& text, int lines, int maxLine = 42);

} // namespace lumora::translate

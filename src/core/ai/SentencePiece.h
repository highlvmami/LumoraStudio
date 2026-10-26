#pragma once

#include <QByteArray>
#include <QString>
#include <string>
#include <unordered_map>
#include <vector>

namespace lumora::ai {

// Splits text into the pieces a translation model was trained on, the way
// Google's SentencePiece library does with a unigram model (.spm file):
// the text is normalised with the model's own character map, spaces become
// "▁" and the most likely split into known pieces is chosen.
class SentencePiece {
public:
    // Reads a SentencePiece model file. False (with `error` set) when it
    // cannot be read or is not a unigram model.
    bool load(const QString& path, QString* error = nullptr);
    bool loadData(const QByteArray& data, QString* error = nullptr);
    bool isNull() const { return m_pieces.empty(); }

    // The normalised text (UTF-8, spaces as "▁").
    std::string normalize(const std::string& text) const;
    // The pieces of `text`, in order. A run of characters the model does not
    // know comes back as one piece holding those characters.
    std::vector<std::string> encode(const QString& text) const;

    // Text from pieces: joined, "▁" back to spaces.
    static QString decode(const std::vector<std::string>& pieces);

private:
    struct Piece {
        float score = 0;
        bool userDefined = false;
    };
    // Longest prefix of `input` the character map rewrites: the replacement
    // and the number of bytes it stands for (0 when nothing matches).
    std::pair<const char*, size_t> mapPrefix(const char* input, size_t length) const;

    std::unordered_map<std::string, Piece> m_pieces;
    size_t m_maxPieceBytes = 0;
    float m_minScore = 0, m_maxScore = 0;
    std::vector<uint32_t> m_trie;   // Darts double array of the character map
    QByteArray m_replacements;      // NUL-terminated replacement strings
    bool m_addDummyPrefix = true;
    bool m_removeExtraWhitespaces = true;
    bool m_escapeWhitespaces = true;
};

} // namespace lumora::ai

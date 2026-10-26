#include "SentencePiece.h"

#include <QFile>
#include <cstring>
#include <limits>

namespace lumora::ai {

namespace {

const char kSpace[] = "\xe2\x96\x81"; // ▁
constexpr float kUnknownPenalty = 10.0f;

// Just enough of the protocol buffer wire format for a SentencePiece model.
struct Reader {
    const char* p;
    const char* end;

    bool atEnd() const { return p >= end; }
    bool varint(uint64_t& v) {
        v = 0;
        for (int shift = 0; shift < 64 && p < end; shift += 7) {
            const auto b = static_cast<uint8_t>(*p++);
            v |= static_cast<uint64_t>(b & 0x7f) << shift;
            if (!(b & 0x80))
                return true;
        }
        return false;
    }
    // Reads one field: its number, wire type and (for length-delimited
    // fields) the bytes, or (for numbers) the value.
    bool field(uint32_t& number, uint32_t& type, Reader& bytes, uint64_t& value) {
        uint64_t key;
        if (!varint(key))
            return false;
        number = static_cast<uint32_t>(key >> 3);
        type = static_cast<uint32_t>(key & 7);
        switch (type) {
        case 0:
            return varint(value);
        case 1:
            if (end - p < 8)
                return false;
            std::memcpy(&value, p, 8);
            p += 8;
            return true;
        case 2: {
            uint64_t n;
            if (!varint(n) || static_cast<uint64_t>(end - p) < n)
                return false;
            bytes = {p, p + n};
            p += n;
            return true;
        }
        case 5: {
            if (end - p < 4)
                return false;
            uint32_t v;
            std::memcpy(&v, p, 4);
            value = v;
            p += 4;
            return true;
        }
        default:
            return false;
        }
    }
};

size_t utf8Length(const char* s, size_t left) {
    const auto c = static_cast<uint8_t>(*s);
    size_t n = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
    if (n > left)
        return 1;
    for (size_t i = 1; i < n; ++i)
        if ((static_cast<uint8_t>(s[i]) & 0xc0) != 0x80)
            return 1;
    return n;
}

// Darts-clone double array unit fields.
bool hasLeaf(uint32_t u) { return (u >> 8) & 1; }
uint32_t value(uint32_t u) { return u & ((1u << 31) - 1); }
uint32_t label(uint32_t u) { return u & ((1u << 31) | 0xff); }
uint32_t offset(uint32_t u) { return (u >> 10) << ((u & (1u << 9)) >> 6); }

} // namespace

bool SentencePiece::load(const QString& path, QString* error) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return loadData(f.readAll(), error);
}

bool SentencePiece::loadData(const QByteArray& data, QString* error) {
    m_pieces.clear();
    m_trie.clear();
    m_replacements.clear();
    m_maxPieceBytes = 0;
    m_minScore = std::numeric_limits<float>::max();
    m_maxScore = std::numeric_limits<float>::lowest();
    const auto fail = [&](const QString& why) {
        m_pieces.clear();
        if (error)
            *error = why;
        return false;
    };
    Reader model{data.constData(), data.constData() + data.size()};
    QByteArray charsmap;
    while (!model.atEnd()) {
        uint32_t number, type;
        Reader bytes{nullptr, nullptr};
        uint64_t v = 0;
        if (!model.field(number, type, bytes, v))
            return fail(QStringLiteral("damaged SentencePiece model"));
        if (number == 1 && type == 2) {
            std::string text;
            float score = 0;
            int kind = 1; // NORMAL
            while (!bytes.atEnd()) {
                uint32_t n, t;
                Reader b{nullptr, nullptr};
                uint64_t x = 0;
                if (!bytes.field(n, t, b, x))
                    return fail(QStringLiteral("damaged SentencePiece model"));
                if (n == 1 && t == 2)
                    text.assign(b.p, b.end);
                else if (n == 2 && t == 5) {
                    const auto bits = static_cast<uint32_t>(x);
                    std::memcpy(&score, &bits, 4);
                } else if (n == 3 && t == 0)
                    kind = static_cast<int>(x);
            }
            // Normal and user defined pieces are matched; control, unknown,
            // unused and byte pieces never come out of the text.
            if (kind == 1 || kind == 4) {
                m_pieces[text] = {score, kind == 4};
                m_maxPieceBytes = std::max(m_maxPieceBytes, text.size());
            }
            if (kind == 1) {
                m_minScore = std::min(m_minScore, score);
                m_maxScore = std::max(m_maxScore, score);
            }
        } else if (number == 2 && type == 2) {
            while (!bytes.atEnd()) {
                uint32_t n, t;
                Reader b{nullptr, nullptr};
                uint64_t x = 0;
                if (!bytes.field(n, t, b, x))
                    return fail(QStringLiteral("damaged SentencePiece model"));
                if (n == 3 && t == 0 && x != 1) // model_type: 1 = unigram
                    return fail(QStringLiteral("only unigram SentencePiece models are supported"));
            }
        } else if (number == 3 && type == 2) {
            while (!bytes.atEnd()) {
                uint32_t n, t;
                Reader b{nullptr, nullptr};
                uint64_t x = 0;
                if (!bytes.field(n, t, b, x))
                    return fail(QStringLiteral("damaged SentencePiece model"));
                if (n == 2 && t == 2)
                    charsmap = QByteArray(b.p, b.end - b.p);
                else if (n == 3 && t == 0)
                    m_addDummyPrefix = x != 0;
                else if (n == 4 && t == 0)
                    m_removeExtraWhitespaces = x != 0;
                else if (n == 5 && t == 0)
                    m_escapeWhitespaces = x != 0;
            }
        }
    }
    if (m_pieces.empty())
        return fail(QStringLiteral("the SentencePiece model has no pieces"));
    if (!charsmap.isEmpty()) {
        // A 32-bit size, the double array of that size, then the
        // replacement strings.
        uint32_t trieBytes = 0;
        if (charsmap.size() < 4)
            return fail(QStringLiteral("damaged SentencePiece character map"));
        std::memcpy(&trieBytes, charsmap.constData(), 4);
        if (trieBytes % 4 || trieBytes > static_cast<uint32_t>(charsmap.size() - 4))
            return fail(QStringLiteral("damaged SentencePiece character map"));
        m_trie.resize(trieBytes / 4);
        std::memcpy(m_trie.data(), charsmap.constData() + 4, trieBytes);
        m_replacements = charsmap.mid(4 + trieBytes);
        m_replacements.append('\0');
    }
    return true;
}

std::pair<const char*, size_t> SentencePiece::mapPrefix(const char* input, size_t length) const {
    if (m_trie.empty())
        return {nullptr, 0};
    size_t best = 0;
    uint32_t bestValue = 0;
    size_t node = 0;
    uint32_t unit = m_trie[0];
    node ^= offset(unit);
    for (size_t i = 0; i < length; ++i) {
        const auto c = static_cast<uint8_t>(input[i]);
        node ^= c;
        if (node >= m_trie.size())
            break;
        unit = m_trie[node];
        if (label(unit) != c)
            break;
        node ^= offset(unit);
        if (node >= m_trie.size())
            break;
        if (hasLeaf(unit)) {
            best = i + 1;
            bestValue = value(m_trie[node]);
        }
    }
    if (best == 0 || bestValue >= static_cast<uint32_t>(m_replacements.size()))
        return {nullptr, 0};
    return {m_replacements.constData() + bestValue, best};
}

std::string SentencePiece::normalize(const std::string& text) const {
    std::string out;
    const char* p = text.data();
    size_t left = text.size();
    // The next piece of normalised text and how many input bytes it uses.
    const auto next = [&](std::string_view& piece) {
        auto [replacement, used] = mapPrefix(p, left);
        if (used) {
            piece = replacement;
            return used;
        }
        const size_t n = utf8Length(p, left);
        const auto c = static_cast<uint8_t>(*p);
        if (n == 1 && c >= 0x80)
            piece = "\xef\xbf\xbd"; // broken UTF-8
        else
            piece = std::string_view(p, n);
        return n;
    };
    if (m_removeExtraWhitespaces) {
        while (left) {
            std::string_view piece;
            const size_t used = next(piece);
            if (piece != " ")
                break;
            p += used, left -= used;
        }
    }
    if (!left)
        return out;
    if (m_addDummyPrefix)
        out += m_escapeWhitespaces ? kSpace : " ";
    bool prevSpace = m_removeExtraWhitespaces;
    while (left) {
        std::string_view piece;
        const size_t used = next(piece);
        while (prevSpace && !piece.empty() && piece.front() == ' ')
            piece.remove_prefix(1);
        if (!piece.empty()) {
            for (char c : piece) {
                if (m_escapeWhitespaces && c == ' ')
                    out += kSpace;
                else
                    out += c;
            }
            prevSpace = piece.back() == ' ';
        }
        p += used, left -= used;
    }
    if (m_removeExtraWhitespaces) {
        const std::string_view space = m_escapeWhitespaces ? std::string_view(kSpace) : std::string_view(" ");
        while (out.size() >= space.size() && std::string_view(out).substr(out.size() - space.size()) == space)
            out.resize(out.size() - space.size());
    }
    return out;
}

std::vector<std::string> SentencePiece::encode(const QString& text) const {
    std::vector<std::string> pieces;
    if (m_pieces.empty())
        return pieces;
    const std::string s = normalize(text.toStdString());
    const size_t n = s.size();
    if (!n)
        return pieces;
    const float unknownScore = m_minScore - kUnknownPenalty;
    // Viterbi over byte positions: the best score of a split up to each
    // position and where its last piece starts.
    struct Node {
        double score = -std::numeric_limits<double>::infinity();
        size_t from = 0;
        bool unknown = false;
        bool reached = false;
    };
    std::vector<Node> best(n + 1);
    best[0].score = 0;
    best[0].reached = true;
    for (size_t i = 0; i < n;) {
        const size_t charBytes = utf8Length(s.data() + i, n - i);
        if (best[i].reached) {
            bool single = false;
            const size_t limit = std::min(m_maxPieceBytes, n - i);
            for (size_t len = 1; len <= limit; ++len) {
                const auto it = m_pieces.find(s.substr(i, len));
                if (it == m_pieces.end())
                    continue;
                // Pieces are whole characters, so `len` ends on one.
                double score = it->second.score;
                if (it->second.userDefined) {
                    size_t chars = 0;
                    for (size_t k = i; k < i + len; k += utf8Length(s.data() + k, i + len - k))
                        ++chars;
                    score = chars * m_maxScore - 0.1;
                }
                const double total = best[i].score + score;
                Node& to = best[i + len];
                if (!to.reached || total > to.score)
                    to = {total, i, false, true};
                if (len == charBytes)
                    single = true;
            }
            if (!single) {
                const double total = best[i].score + unknownScore;
                Node& to = best[i + charBytes];
                if (!to.reached || total > to.score)
                    to = {total, i, true, true};
            }
        }
        i += charBytes;
    }
    std::vector<std::pair<std::string, bool>> path;
    for (size_t at = n; at > 0; at = best[at].from)
        path.emplace_back(s.substr(best[at].from, at - best[at].from), best[at].unknown);
    // Runs of unknown characters become one piece.
    for (auto it = path.rbegin(); it != path.rend(); ++it) {
        if (it->second && !pieces.empty() && it != path.rbegin() && std::prev(it)->second)
            pieces.back() += it->first;
        else
            pieces.push_back(it->first);
    }
    return pieces;
}

QString SentencePiece::decode(const std::vector<std::string>& pieces) {
    std::string text;
    for (const auto& piece : pieces)
        text += piece;
    QString out = QString::fromStdString(text);
    out.replace(QChar(0x2581), QLatin1Char(' '));
    return out.trimmed();
}

} // namespace lumora::ai

#include "dupli/text_dedup.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

extern "C" {
#include "blake3.h"
}

namespace dupli {

namespace {

bool is_ascii_space(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

bool is_ascii_digit(char c) { return c >= '0' && c <= '9'; }

bool is_ascii_upper(char c) { return c >= 'A' && c <= 'Z'; }

char ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Replace CRLF and lone CR with LF so sentence splitting sees one newline
/// convention. This does not change the comparison key beyond line endings,
/// which are layout, not content.
std::string normalize_newlines(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    for (size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n') ++i;
            out.push_back('\n');
        } else {
            out.push_back(text[i]);
        }
    }
    return out;
}

/// Strict UTF-8 validation: rejects overlong forms, surrogates and truncated
/// sequences. Only valid text is decoded implicitly; anything else must be
/// disambiguated by the user instead of being mangled silently.
bool is_valid_utf8(const std::string& text, bool& has_nul) {
    has_nul = false;
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        if (lead == 0) has_nul = true;
        if (lead < 0x80) {
            ++i;
            continue;
        }
        size_t extra = 0;
        uint32_t code_point = 0;
        if ((lead & 0xE0) == 0xC0) {
            extra = 1;
            code_point = lead & 0x1F;
        } else if ((lead & 0xF0) == 0xE0) {
            extra = 2;
            code_point = lead & 0x0F;
        } else if ((lead & 0xF8) == 0xF0) {
            extra = 3;
            code_point = lead & 0x07;
        } else {
            return false;
        }
        if (i + extra >= text.size()) return false;
        for (size_t k = 1; k <= extra; ++k) {
            const unsigned char continuation = static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) return false;
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        // Reject overlong encodings, UTF-16 surrogate halves and out-of-range
        // code points: all of them indicate the bytes are not UTF-8 text.
        if ((extra == 1 && code_point < 0x80) || (extra == 2 && code_point < 0x800) ||
            (extra == 3 && code_point < 0x10000) || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            return false;
        }
        i += extra + 1;
    }
    return true;
}

void append_utf8(uint32_t code_point, std::string& out) {
    if (code_point <= 0x7F) {
        out.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

/// UTF-16 to UTF-8 with explicit surrogate handling. An unpaired surrogate is an
/// error rather than U+FFFD, because silently replacing text could turn two
/// different sources into the same comparison key.
Result<std::string> decode_utf16(const std::vector<std::byte>& bytes, bool little_endian) {
    if (bytes.size() % 2 != 0) {
        return Error{ErrorCode::Unsupported, "UTF-16 source has an odd number of bytes", "",
                     "decode_text"};
    }
    std::string out;
    out.reserve(bytes.size());
    for (size_t i = 0; i + 1 < bytes.size(); i += 2) {
        const auto low = static_cast<unsigned char>(bytes[i]);
        const auto high = static_cast<unsigned char>(bytes[i + 1]);
        uint32_t unit = little_endian ? (static_cast<uint32_t>(high) << 8) | low
                                      : (static_cast<uint32_t>(low) << 8) | high;
        if (unit >= 0xD800 && unit <= 0xDBFF) {
            if (i + 3 >= bytes.size()) {
                return Error{ErrorCode::Unsupported, "Truncated UTF-16 surrogate pair", "",
                             "decode_text"};
            }
            const auto low2 = static_cast<unsigned char>(bytes[i + 2]);
            const auto high2 = static_cast<unsigned char>(bytes[i + 3]);
            const uint32_t second = little_endian ? (static_cast<uint32_t>(high2) << 8) | low2
                                                  : (static_cast<uint32_t>(low2) << 8) | high2;
            if (second < 0xDC00 || second > 0xDFFF) {
                return Error{ErrorCode::Unsupported, "Unpaired UTF-16 surrogate", "",
                             "decode_text"};
            }
            unit = 0x10000 + ((unit - 0xD800) << 10) + (second - 0xDC00);
            i += 2;
        } else if (unit >= 0xDC00 && unit <= 0xDFFF) {
            return Error{ErrorCode::Unsupported, "Unpaired UTF-16 surrogate", "", "decode_text"};
        }
        append_utf8(unit, out);
    }
    return out;
}

std::string decode_latin1(const std::vector<std::byte>& bytes) {
    std::string out;
    out.reserve(bytes.size());
    for (const std::byte value : bytes) {
        append_utf8(static_cast<unsigned char>(value), out);
    }
    return out;
}

/// A NUL byte or a high share of control characters means "this is not prose".
/// Such sources are surfaced with a warning and skipped by the caller instead of
/// being turned into gibberish sentences.
bool looks_binary(const std::vector<std::byte>& bytes) {
    if (bytes.empty()) return false;
    size_t control = 0;
    for (const std::byte value : bytes) {
        const auto byte = static_cast<unsigned char>(value);
        if (byte == 0) return true;
        if (byte < 0x20 && byte != '\t' && byte != '\n' && byte != '\r' && byte != '\f' &&
            byte != '\v') {
            ++control;
        }
    }
    return control * 10 > bytes.size();
}

const char* const kAbbreviations[] = {
    "mr",  "mrs", "ms",  "dr",  "prof", "sr",   "jr",  "st",  "mt",  "vs",  "etc",
    "eg",  "e.g", "ie",  "i.e", "fig",  "inc",  "ltd", "co",  "approx", "dept",
    "est", "jan", "feb", "mar", "apr",  "jun",  "jul", "aug", "sep", "sept", "oct",
    "nov", "dec", "p.m", "a.m", "u.s",  "u.k",  "ph.d"};

bool is_abbreviation_token(std::string token) {
    for (char& c : token) c = ascii_lower(c);
    for (const char* candidate : kAbbreviations) {
        if (token == candidate) return true;
    }
    return false;
}

/// Index of the start of the whitespace-delimited token that ends at `end`.
size_t token_start(const std::string& text, size_t end) {
    size_t position = end;
    while (position > 0 && !is_ascii_space(text[position - 1])) --position;
    return position;
}

/// Closing quotes/brackets belong to the sentence they close. The byte length is
/// returned so multi-byte UTF-8 punctuation ("”", "’", "»") is consumed whole.
size_t closing_length(const std::string& text, size_t index) {
    if (index >= text.size()) return 0;
    const unsigned char lead = static_cast<unsigned char>(text[index]);
    if (lead == '"' || lead == '\'' || lead == ')' || lead == ']' || lead == '}') return 1;
    if (lead == 0xC2 && index + 1 < text.size()) {
        const auto second = static_cast<unsigned char>(text[index + 1]);
        if (second == 0xA9 || second == 0xBB) return 2; // © »  (» is 0xC2 0xBB)
    }
    if (lead == 0xE2 && index + 2 < text.size()) {
        const auto second = static_cast<unsigned char>(text[index + 1]);
        const auto third = static_cast<unsigned char>(text[index + 2]);
        if (second == 0x80 && (third == 0x99 || third == 0x9D || third == 0x9C)) {
            return 3; // ’ ” “
        }
    }
    return 0;
}

std::string collapse_whitespace(const std::string& text) {
    std::string out;
    out.reserve(text.size());
    bool previous_space = false;
    for (char c : text) {
        if (is_ascii_space(c)) {
            if (!previous_space) out.push_back(' ');
            previous_space = true;
        } else {
            out.push_back(c);
            previous_space = false;
        }
    }
    return out;
}

std::string trim_ascii_whitespace(const std::string& text) {
    size_t begin = 0;
    size_t end = text.size();
    while (begin < end && is_ascii_space(text[begin])) ++begin;
    while (end > begin && is_ascii_space(text[end - 1])) --end;
    return text.substr(begin, end - begin);
}

/// Turn the raw slice into the stored sentence: line breaks become spaces (a
/// sentence must stay on one output line), then the explicitly chosen trimming
/// policy is applied. Nothing else is normalised by default.
std::string prepare_sentence_text(const std::string& raw, const SentencePolicy& policy) {
    std::string text;
    text.reserve(raw.size());
    for (char c : raw) text.push_back(c == '\n' ? ' ' : c);
    if (policy.normalize_internal_whitespace) text = collapse_whitespace(text);
    if (policy.trim_boundary_whitespace) text = trim_ascii_whitespace(text);
    return text;
}

std::string comparison_key(const std::string& text, const SentencePolicy& policy) {
    if (policy.case_sensitive) return text;
    // ASCII-only case folding is deliberate: Unicode case folding is
    // locale-sensitive and can map characters a writer meant to keep distinct.
    std::string key = text;
    for (char& c : key) c = ascii_lower(c);
    return key;
}

} // namespace

std::string to_string(TextEncoding encoding) {
    switch (encoding) {
    case TextEncoding::Auto: return "auto";
    case TextEncoding::Utf8: return "utf-8";
    case TextEncoding::Utf16LE: return "utf-16-le";
    case TextEncoding::Utf16BE: return "utf-16-be";
    case TextEncoding::Latin1: return "latin-1";
    }
    return "auto";
}

bool parse_text_encoding(std::string_view text, TextEncoding& out) {
    if (text == "auto") { out = TextEncoding::Auto; return true; }
    if (text == "utf-8" || text == "utf8") { out = TextEncoding::Utf8; return true; }
    if (text == "utf-16-le" || text == "utf-16le") { out = TextEncoding::Utf16LE; return true; }
    if (text == "utf-16-be" || text == "utf-16be") { out = TextEncoding::Utf16BE; return true; }
    if (text == "latin-1" || text == "latin1" || text == "iso-8859-1") {
        out = TextEncoding::Latin1;
        return true;
    }
    return false;
}

Result<DecodedText> decode_text(const std::vector<std::byte>& bytes, TextEncoding forced) {
    DecodedText decoded;
    decoded.binary_looking = looks_binary(bytes);

    size_t skip = 0;
    TextEncoding detected = forced;
    if (bytes.size() >= 3 && bytes[0] == std::byte{0xEF} && bytes[1] == std::byte{0xBB} &&
        bytes[2] == std::byte{0xBF}) {
        skip = 3;
        detected = TextEncoding::Utf8;
    } else if (bytes.size() >= 2 && bytes[0] == std::byte{0xFF} && bytes[1] == std::byte{0xFE}) {
        skip = 2;
        detected = TextEncoding::Utf16LE;
    } else if (bytes.size() >= 2 && bytes[0] == std::byte{0xFE} && bytes[1] == std::byte{0xFF}) {
        skip = 2;
        detected = TextEncoding::Utf16BE;
    }

    const std::vector<std::byte> payload(bytes.begin() + static_cast<std::ptrdiff_t>(skip),
                                         bytes.end());

    if (detected == TextEncoding::Auto) {
        const std::string probe(reinterpret_cast<const char*>(payload.data()), payload.size());
        bool has_nul = false;
        if (is_valid_utf8(probe, has_nul) && !has_nul) {
            detected = TextEncoding::Utf8;
        } else if (has_nul) {
            // NUL bytes without a BOM almost always mean UTF-16 written by a
            // program that omitted the BOM, but guessing the byte order would
            // corrupt the text, so the caller must choose.
            return Error{ErrorCode::Unsupported,
                         "The file looks binary or is UTF-16 without a byte-order mark",
                         "", "decode_text"};
        } else {
            return Error{ErrorCode::Unsupported,
                         "The file is not valid UTF-8; choose an encoding explicitly",
                         "", "decode_text"};
        }
    }

    decoded.encoding = detected;
    if (detected == TextEncoding::Utf16LE || detected == TextEncoding::Utf16BE) {
        auto text = decode_utf16(payload, detected == TextEncoding::Utf16LE);
        if (text.has_error()) return text.error();
        decoded.utf8 = std::move(text.value());
    } else if (detected == TextEncoding::Latin1) {
        decoded.utf8 = decode_latin1(payload);
    } else {
        decoded.utf8.assign(reinterpret_cast<const char*>(payload.data()), payload.size());
    }

    if (decoded.binary_looking) {
        decoded.warning =
            "The source contains binary data; it was skipped rather than decoded as text.";
    }
    return decoded;
}

Result<DecodedText> read_text_source(IFileSystem& fs, const std::filesystem::path& path,
                                    TextEncoding forced, size_t max_bytes) {
    auto size = fs.file_size(path);
    if (size.has_error()) return size.error();
    if (size.value() > max_bytes) {
        return Error{ErrorCode::Unsupported,
                     "The text source is larger than the configured limit", path.string(),
                     "read_text_source"};
    }

    auto opened = fs.open_read(path);
    if (opened.has_error()) return opened.error();
    auto bytes = opened.value()->read_all();
    if (bytes.has_error()) return bytes.error();
    auto closed = opened.value()->close();
    if (closed.has_error()) return closed.error();

    auto decoded = decode_text(bytes.value(), forced);
    if (decoded.has_error()) {
        Error error = decoded.error();
        if (error.path.empty()) error.path = path_to_utf8(path);
        error.operation = "read_text_source";
        return error;
    }
    return decoded;
}

std::vector<std::string> split_sentences(const std::string& utf8_text,
                                        const SentencePolicy& policy) {
    const std::string text = normalize_newlines(utf8_text);
    std::vector<std::string> sentences;

    auto emit = [&](size_t begin, size_t end) {
        const std::string raw = text.substr(begin, end - begin);
        const std::string prepared = prepare_sentence_text(raw, policy);
        if (prepared.empty()) return; // Blank lines never become sentences.
        sentences.push_back(prepared);
    };

    size_t i = 0;
    size_t start = 0;
    while (i < text.size()) {
        const unsigned char lead = static_cast<unsigned char>(text[i]);
        bool terminator = text[i] == '.' || text[i] == '!' || text[i] == '?';
        bool ellipsis = lead == 0xE2 && i + 2 < text.size() &&
                        static_cast<unsigned char>(text[i + 1]) == 0x80 &&
                        static_cast<unsigned char>(text[i + 2]) == 0xA6; // "…"

        if (text[i] == '.') {
            // Skip a period that is part of a run ("..." / "?!."): the run is
            // processed once at its final character so it yields one boundary.
            if ((i > 0 && text[i - 1] == '.') ||
                (i + 1 < text.size() && text[i + 1] == '.')) {
                ++i;
                continue;
            }
            if (i > 0 && i + 1 < text.size() && is_ascii_digit(text[i - 1]) &&
                is_ascii_digit(text[i + 1])) {
                ++i; // decimal number like 3.14
                continue;
            }
            const size_t begin = token_start(text, i);
            const std::string token = text.substr(begin, i - begin);
            if (token.size() == 1 && is_ascii_upper(token[0])) {
                ++i; // initial like "J. R. R. Tolkien"
                continue;
            }
            if (!token.empty() && is_abbreviation_token(token)) {
                ++i; // abbreviation like "Dr." or "e.g."
                continue;
            }
        }

        if (!terminator && !ellipsis) {
            ++i;
            continue;
        }

        size_t run_end = i;
        while (run_end < text.size() &&
               (text[run_end] == '.' || text[run_end] == '!' || text[run_end] == '?')) {
            ++run_end;
        }
        if (run_end == i && ellipsis) run_end = i + 3;

        // Closing quotes and brackets stay attached to the sentence they close.
        size_t close = run_end;
        for (size_t length = closing_length(text, close); length > 0;
             length = closing_length(text, close)) {
            close += length;
        }

        const bool at_end = close >= text.size();
        const bool followed_by_space = !at_end && is_ascii_space(text[close]);
        if (!at_end && !followed_by_space) {
            // "Hi!there" or "example.com": no boundary, keep the text intact
            // rather than splitting on a guess.
            i = run_end;
            continue;
        }

        emit(start, close);
        size_t next = close;
        while (next < text.size() && is_ascii_space(text[next])) ++next;
        start = next;
        i = next;
    }

    // Trailing text without a terminator is still a sentence: dropping it would
    // silently lose the user's last line.
    if (start < text.size()) emit(start, text.size());
    return sentences;
}

Result<SentenceDedupResult> deduplicate_sentences(const std::vector<SentenceSource>& sources,
                                                 const SentencePolicy& policy) {
    SentenceDedupResult result;
    std::map<std::string, size_t> first_occurrence; // key -> index in result.sentences

    for (size_t source_index = 0; source_index < sources.size(); ++source_index) {
        const SentenceSource& source = sources[source_index];
        const std::vector<std::string> sentences = split_sentences(source.utf8_text, policy);
        for (size_t sentence_index = 0; sentence_index < sentences.size(); ++sentence_index) {
            ExtractedSentence extracted;
            extracted.text = sentences[sentence_index];
            extracted.key = comparison_key(extracted.text, policy);
            extracted.source_label = source.label;
            extracted.source_index = source_index;
            extracted.sentence_index = sentence_index;

            ++result.total_sentences;
            auto found = first_occurrence.find(extracted.key);
            if (found == first_occurrence.end()) {
                first_occurrence.emplace(extracted.key, result.sentences.size());
                extracted.kept_source_label = source.label;
                extracted.kept_source_index = source_index;
                extracted.kept_sentence_index = sentence_index;
                ++result.unique_sentences;
            } else {
                const ExtractedSentence& keeper = result.sentences[found->second];
                extracted.duplicate = true;
                extracted.kept_source_label = keeper.source_label;
                extracted.kept_source_index = keeper.source_index;
                extracted.kept_sentence_index = keeper.sentence_index;
                ++result.duplicate_sentences;
            }
            result.sentences.push_back(std::move(extracted));
        }
    }

    if (result.total_sentences == 0) {
        result.warnings.push_back("No sentences were extracted from the selected sources.");
    }
    return result;
}

std::string SentenceDedupResult::output() const {
    std::string text;
    for (const auto& sentence : sentences) {
        if (sentence.duplicate) continue;
        text += sentence.text;
        text += '\n';
    }
    return text;
}

Result<void> write_text_output(IFileSystem& fs, const std::filesystem::path& destination,
                               const std::string& utf8_text, bool overwrite) {
    if (destination.empty()) {
        return Error{ErrorCode::InvalidArgument, "No output file was given", "", "write_text"};
    }

    auto exists = fs.exists(destination);
    if (exists.has_error()) return exists.error();
    if (exists.value() && !overwrite) {
        return Error{ErrorCode::Conflict, "The output file already exists", path_to_utf8(destination),
                     "write_text"};
    }

    auto parent = destination.parent_path();
    if (!parent.empty()) {
        auto parent_exists = fs.exists(parent);
        if (parent_exists.has_error()) return parent_exists.error();
        if (!parent_exists.value()) {
            auto created = fs.create_directory(parent);
            if (created.has_error()) return created.error();
        }
    }

    const std::filesystem::path temporary = destination.string() + ".dupli-tmp";
    auto temporary_exists = fs.exists(temporary);
    if (temporary_exists.has_error()) return temporary_exists.error();
    if (temporary_exists.value()) {
        return Error{ErrorCode::Conflict,
                     "A temporary output file is already present; remove it and try again",
                     path_to_utf8(temporary), "write_text"};
    }

    auto opened = fs.open_write(temporary);
    if (opened.has_error()) return opened.error();
    IWritableFile& file = *opened.value();

    size_t written = 0;
    while (written < utf8_text.size()) {
        auto chunk = file.write(reinterpret_cast<const std::byte*>(utf8_text.data()) + written,
                                utf8_text.size() - written);
        if (chunk.has_error()) {
            file.close();
            (void)fs.remove(temporary);
            return chunk.error();
        }
        if (chunk.value() == 0) {
            file.close();
            (void)fs.remove(temporary);
            return Error{ErrorCode::IoError, "The output write made no progress",
                         path_to_utf8(temporary), "write_text"};
        }
        written += chunk.value();
    }

    auto flushed = file.flush();
    if (flushed.has_error()) {
        file.close();
        (void)fs.remove(temporary);
        return flushed.error();
    }
    auto closed = file.close();
    if (closed.has_error()) {
        (void)fs.remove(temporary);
        return closed.error();
    }

    if (exists.value()) {
        // Only reached when overwrite was approved: remove the old file after
        // the replacement is fully on disk, then publish.
        auto removed = fs.remove(destination);
        if (removed.has_error()) {
            (void)fs.remove(temporary);
            return removed.error();
        }
    }

    auto published = fs.rename(temporary, destination);
    if (published.has_error()) {
        (void)fs.remove(temporary);
        return published.error();
    }
    return Result<void>();
}

std::string text_digest_hex(const std::string& utf8_text) {
    blake3_hasher hasher;
    blake3_hasher_init(&hasher);
    blake3_hasher_update(&hasher, utf8_text.data(), utf8_text.size());
    unsigned char bytes[32];
    blake3_hasher_finalize(&hasher, bytes, sizeof(bytes));
    static const char* digits = "0123456789abcdef";
    std::string hex;
    hex.reserve(64);
    for (unsigned char byte : bytes) {
        hex.push_back(digits[byte >> 4]);
        hex.push_back(digits[byte & 0x0F]);
    }
    return hex;
}

} // namespace dupli

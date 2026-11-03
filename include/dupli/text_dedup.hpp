#pragma once

#include "dupli/filesystem.hpp"
#include "dupli/result.hpp"

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace dupli {

// Exact sentence de-duplication.
//
// This module answers one narrow question: "which sentences occur more than
// once, byte for byte, under an explicitly chosen comparison policy?" It is
// deliberately NOT semantic similarity and NOT a language model: paraphrases,
// reordered words, different punctuation and different capitalisation are all
// treated as different sentences unless the user explicitly opts into
// case-insensitive comparison. Anything fuzzy would risk deleting text that
// only looks redundant.
//
// The splitting rules are a documented heuristic (see split_sentences()):
// regex-only sentence splitting is never presented as linguistically perfect,
// and ambiguous text is kept rather than dropped.

enum class TextEncoding { Auto, Utf8, Utf16LE, Utf16BE, Latin1 };

std::string to_string(TextEncoding encoding);
bool parse_text_encoding(std::string_view text, TextEncoding& out);

struct DecodedText {
    std::string utf8;
    TextEncoding encoding = TextEncoding::Utf8;
    /// True when the bytes look like a binary file (embedded NULs, control
    /// characters) rather than text. Such a source must be skipped with a
    /// warning instead of being decoded into garbage sentences.
    bool binary_looking = false;
    std::string warning;
};

/// Decode raw file bytes into UTF-8. With TextEncoding::Auto the encoding is
/// detected from a BOM or from strict UTF-8 validity. Bytes that are neither
/// valid UTF-8 nor BOM-marked are reported as uncertain (an error) so the caller
/// can ask the user rather than guessing and silently corrupting the output.
Result<DecodedText> decode_text(const std::vector<std::byte>& bytes,
                               TextEncoding forced = TextEncoding::Auto);

/// Text sources larger than this are refused instead of being read into memory.
/// The limit exists so a mistakenly selected disk image cannot exhaust RAM.
constexpr size_t kMaxTextSourceBytes = 64u * 1024u * 1024u;

/// Read and decode one text source through the injected filesystem.
Result<DecodedText> read_text_source(IFileSystem& fs, const std::filesystem::path& path,
                                    TextEncoding forced = TextEncoding::Auto,
                                    size_t max_bytes = kMaxTextSourceBytes);

struct SentencePolicy {
    /// Trim only whitespace at the two ends of an extracted sentence. Internal
    /// whitespace stays significant by default, so "a  b" and "a b" differ.
    bool trim_boundary_whitespace = true;
    /// Optional, explicit collapse of internal whitespace runs to one space.
    /// Off by default because it changes the comparison key.
    bool normalize_internal_whitespace = false;
    /// Comparison only; the output always keeps the first occurrence's exact
    /// text. Case-insensitive matching is opt-in, never silent.
    bool case_sensitive = true;
};

struct SentenceSource {
    /// Shown in previews and reports, e.g. a file path or "Pasted text 2".
    std::string label;
    std::string utf8_text;
};

struct ExtractedSentence {
    std::string text;            // exact text of this occurrence
    std::string key;             // comparison key derived from text + policy
    std::string source_label;
    size_t source_index = 0;     // order in which the source was supplied
    size_t sentence_index = 0;   // position inside that source
    bool duplicate = false;
    /// Provenance of the kept (first) occurrence; equal to this occurrence when
    /// it is the keeper.
    std::string kept_source_label;
    size_t kept_source_index = 0;
    size_t kept_sentence_index = 0;
};

struct SentenceDedupResult {
    std::vector<ExtractedSentence> sentences;
    size_t total_sentences = 0;
    size_t unique_sentences = 0;
    size_t duplicate_sentences = 0;
    std::vector<std::string> warnings;

    /// Output format: one unique sentence per line, in first-seen source order.
    /// Paragraph layout is intentionally not preserved, because reconstructing
    /// it after removing sentences would invent structure the sources never had.
    std::string output() const;
};

/// Extract every sentence and mark repeats after the first occurrence.
Result<SentenceDedupResult> deduplicate_sentences(const std::vector<SentenceSource>& sources,
                                                 const SentencePolicy& policy);

/// Split text into sentences using the documented conservative heuristic.
/// Exposed separately so the splitting behaviour is directly testable and can be
/// previewed on its own.
std::vector<std::string> split_sentences(const std::string& utf8_text,
                                        const SentencePolicy& policy = {});

/// Write the de-duplicated output. Content is written to a sibling temporary
/// file, flushed, and only then renamed into place, so a failed write can never
/// leave a half-written output that looks complete.
///
/// The destination is never replaced unless `overwrite` is true, which the GUI
/// sets only after the user confirmed the Save dialog's overwrite prompt.
Result<void> write_text_output(IFileSystem& fs, const std::filesystem::path& destination,
                               const std::string& utf8_text, bool overwrite);

/// BLAKE3 hex digest of UTF-8 text, recorded so a later "remove the generated
/// output" action can verify the file still matches what was written.
std::string text_digest_hex(const std::string& utf8_text);

} // namespace dupli

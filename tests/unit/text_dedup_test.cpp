#include "dupli/fake_filesystem.hpp"
#include "dupli/text_dedup.hpp"
#include "test_utils.hpp"

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

using namespace dupli;

namespace {

std::vector<std::byte> bytes_of(const std::string& text) {
    std::vector<std::byte> bytes;
    bytes.reserve(text.size());
    for (char c : text) bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
    return bytes;
}

std::vector<std::byte> utf16le_of(const std::string& ascii) {
    std::vector<std::byte> bytes;
    for (char c : ascii) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
        bytes.push_back(std::byte{0});
    }
    return bytes;
}

std::vector<std::string> split(const std::string& text) { return split_sentences(text); }

void test_basic_splitting() {
    CHECK_EQ(split("Hello world. This is next! And last?").size(), size_t(3));
    const auto quoted = split("He said \"Stop.\" Then left.");
    CHECK_EQ(quoted.size(), size_t(2));
    CHECK_EQ(quoted[0], std::string("He said \"Stop.\""));
    CHECK_EQ(quoted[1], std::string("Then left."));

    // Text without a terminator must not be dropped.
    const auto trailing = split("No terminator here");
    CHECK_EQ(trailing.size(), size_t(1));
    CHECK_EQ(trailing[0], std::string("No terminator here"));

    // Blank lines never become empty sentences.
    CHECK_EQ(split("One.\n\n\nTwo.").size(), size_t(2));
    std::cout << "text: basic splitting\n";
}

void test_conservative_rules() {
    // Abbreviations, initials and decimals do not end a sentence.
    CHECK_EQ(split("Dr. Smith arrived. He left.").size(), size_t(2));
    CHECK_EQ(split("Pi is 3.14 exactly. Done.").size(), size_t(2));
    CHECK_EQ(split("J. R. R. Tolkien wrote it. Yes.").size(), size_t(2));
    CHECK_EQ(split("Use e.g. this one. Thanks.").size(), size_t(2));

    // An ellipsis is a single boundary, not three (one boundary per character).
    const auto ellipsis = split("Wait… what? Fine.");
    CHECK_EQ(ellipsis.size(), size_t(3));
    CHECK_EQ(ellipsis[0], std::string("Wait…"));
    CHECK_EQ(ellipsis[2], std::string("Fine."));

    // No whitespace after the terminator means "not a sentence boundary".
    CHECK_EQ(split("example.com is a domain. Really.").size(), size_t(2));
    CHECK_EQ(split("Hi!there").size(), size_t(1));
    std::cout << "text: conservative rules\n";
}

void test_policies() {
    SentencePolicy trim;
    trim.trim_boundary_whitespace = true;
    const auto trimmed = split_sentences("  One.  Two.  ", trim);
    CHECK_EQ(trimmed.size(), size_t(2));
    CHECK_EQ(trimmed[0], std::string("One."));

    SentencePolicy keep;
    keep.trim_boundary_whitespace = false;
    const auto untrimmed = split_sentences("  One.  Two.  ", keep);
    CHECK_EQ(untrimmed[0], std::string("  One."));

    // Internal whitespace stays significant by default.
    const auto distinct = split_sentences("a  b. a b.", trim);
    CHECK_EQ(distinct.size(), size_t(2));
    CHECK(distinct[0] != distinct[1]);

    SentencePolicy collapse;
    collapse.normalize_internal_whitespace = true;
    const auto collapsed = split_sentences("a  b. a b.", collapse);
    CHECK_EQ(collapsed[0], collapsed[1]);
    std::cout << "text: whitespace policy\n";
}

void test_deduplication_and_provenance() {
    std::vector<SentenceSource> sources = {
        {"first.txt", "One. Two. Two. "},
        {"second.txt", "Two. Three."},
    };
    SentencePolicy policy;
    auto result = deduplicate_sentences(sources, policy);
    CHECK(result.ok());
    const SentenceDedupResult& dedup = result.value();
    CHECK_EQ(dedup.total_sentences, size_t(5));
    CHECK_EQ(dedup.unique_sentences, size_t(3));
    CHECK_EQ(dedup.duplicate_sentences, size_t(2));
    CHECK_EQ(dedup.output(), std::string("One.\nTwo.\nThree.\n"));

    // The second "Two." knows which occurrence was kept.
    const auto& third = dedup.sentences[2];
    CHECK(third.duplicate);
    CHECK_EQ(third.source_label, std::string("first.txt"));
    CHECK_EQ(third.kept_sentence_index, size_t(1));
    CHECK_EQ(third.kept_source_label, std::string("first.txt"));

    const auto& cross_source = dedup.sentences[3];
    CHECK(cross_source.duplicate);
    CHECK_EQ(cross_source.kept_source_index, size_t(0));

    // Case and punctuation differences are not silently folded.
    std::vector<SentenceSource> cases = {{"a", "Hello. hello. Hello!"}};
    auto sensitive = deduplicate_sentences(cases, policy);
    CHECK_EQ(sensitive.value().unique_sentences, size_t(3));

    SentencePolicy insensitive;
    insensitive.case_sensitive = false;
    auto folded = deduplicate_sentences(cases, insensitive);
    CHECK_EQ(folded.value().unique_sentences, size_t(2));

    auto empty = deduplicate_sentences({{"blank", "   "}}, policy);
    CHECK(empty.ok());
    CHECK_EQ(empty.value().total_sentences, size_t(0));
    CHECK(!empty.value().warnings.empty());
    std::cout << "text: deduplication and provenance\n";
}

void test_encoding_detection() {
    // UTF-8 BOM is stripped, not kept as text.
    auto bom = bytes_of("\xEF\xBB\xBFHello.");
    auto decoded_bom = decode_text(bom);
    CHECK(decoded_bom.ok());
    CHECK_EQ(decoded_bom.value().encoding, TextEncoding::Utf8);
    CHECK_EQ(decoded_bom.value().utf8, std::string("Hello."));

    // UTF-16LE with BOM.
    auto utf16 = utf16le_of("Hi there.");
    utf16.insert(utf16.begin(), std::byte{0xFF});
    utf16.insert(utf16.begin() + 1, std::byte{0xFE});
    auto decoded_utf16 = decode_text(utf16);
    CHECK(decoded_utf16.ok());
    CHECK_EQ(decoded_utf16.value().encoding, TextEncoding::Utf16LE);
    CHECK_EQ(decoded_utf16.value().utf8, std::string("Hi there."));

    // Invalid UTF-8 without a BOM is uncertain, never guessed.
    auto latin1 = std::vector<std::byte>{std::byte{0x63}, std::byte{0x61}, std::byte{0x66},
                                         std::byte{0xE9}};
    auto uncertain = decode_text(latin1);
    CHECK(uncertain.has_error());
    CHECK_EQ(uncertain.error().code, ErrorCode::Unsupported);

    auto forced = decode_text(latin1, TextEncoding::Latin1);
    CHECK(forced.ok());
    CHECK_EQ(forced.value().utf8, std::string("caf\xC3\xA9"));

    // Binary-looking input is flagged and refused in auto mode.
    auto binary = std::vector<std::byte>{std::byte{'a'}, std::byte{0}, std::byte{'b'}};
    auto binary_result = decode_text(binary);
    CHECK(binary_result.has_error());
    std::cout << "text: encoding detection\n";
}

void test_read_and_write() {
    FakeFileSystem fs;
    fs.add_directory("/root");
    fs.add_file_bytes("/root/utf16.txt", [] {
        std::vector<std::byte> value = utf16le_of("One. Two.");
        value.insert(value.begin(), std::byte{0xFF});
        value.insert(value.begin() + 1, std::byte{0xFE});
        return value;
    }());

    auto decoded = read_text_source(fs, "/root/utf16.txt");
    CHECK(decoded.ok());
    CHECK_EQ(decoded.value().utf8, std::string("One. Two."));

    auto result = deduplicate_sentences({{"utf16", decoded.value().utf8}}, {});
    CHECK(result.ok());

    auto written = write_text_output(fs, "/root/out.txt", result.value().output(), false);
    CHECK(written.ok());
    auto exists = fs.exists("/root/out.txt");
    CHECK(exists.ok() && exists.value());
    auto temp = fs.exists("/root/out.txt.dupli-tmp");
    CHECK(temp.ok() && !temp.value());

    auto again = write_text_output(fs, "/root/out.txt", "different", false);
    CHECK(again.has_error());
    CHECK_EQ(again.error().code, ErrorCode::Conflict);

    auto overwritten = write_text_output(fs, "/root/out.txt", "different", true);
    CHECK(overwritten.ok());

    // The source file was only read; its content is unchanged.
    auto source = read_text_source(fs, "/root/utf16.txt");
    CHECK(source.ok());
    CHECK_EQ(source.value().utf8, std::string("One. Two."));

    CHECK_EQ(text_digest_hex("abc").size(), size_t(64));
    std::cout << "text: read and write\n";
}

} // namespace

int main() {
    test_basic_splitting();
    test_conservative_rules();
    test_policies();
    test_deduplication_and_provenance();
    test_encoding_detection();
    test_read_and_write();
    std::cout << "All text dedup tests passed\n";
    return 0;
}

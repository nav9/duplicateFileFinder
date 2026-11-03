#include "dupli/filesystem.hpp"

namespace dupli {

std::string path_to_utf8(const std::filesystem::path& path) {
    // u8string() gives UTF-8 on every platform (it performs the wide/ANSI to
    // UTF-8 conversion on Windows), which keeps reports and journal entries
    // readable and round-trippable regardless of the local code page.
    const std::u8string text = path.u8string();
    return std::string(reinterpret_cast<const char*>(text.data()), text.size());
}

std::filesystem::path path_from_utf8(const std::string& utf8) {
    const std::u8string text(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size());
    return std::filesystem::path(text);
}

} // namespace dupli

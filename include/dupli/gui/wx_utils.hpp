#pragma once

#include <wx/string.h>

#include <cstdint>
#include <filesystem>
#include <string>

#include "dupli/filesystem.hpp"

namespace dupli {
namespace gui {

// wxString::ToStdString() uses the current locale, which is wrong for non-ASCII
// paths on Windows and in non-UTF-8 locales. All conversions go through UTF-8.
inline std::string toUtf8(const wxString& value) {
    const wxScopedCharBuffer buffer = value.ToUTF8();
    return buffer ? std::string(buffer.data()) : std::string();
}

inline wxString fromUtf8(const std::string& value) {
    return wxString::FromUTF8(value.c_str());
}

inline std::filesystem::path pathFromWx(const wxString& value) {
    return path_from_utf8(toUtf8(value));
}

inline wxString wxFromPath(const std::filesystem::path& value) {
    return fromUtf8(path_to_utf8(value));
}

inline wxString wxFromUtf8Path(const std::string& value) {
    return fromUtf8(value);
}

// Human-readable byte counts. Binary units are used because file managers on
// every desktop platform show them this way; no rounding is hidden from the
// user (the exact byte count is always available in the details pane).
inline wxString format_bytes(uintmax_t bytes) {
    static const char* const units[] = {"B", "KiB", "MiB", "GiB", "TiB"};
    double value = static_cast<double>(bytes);
    size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < sizeof(units) / sizeof(units[0])) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return wxString::Format("%llu B", static_cast<unsigned long long>(bytes));
    }
    return wxString::Format("%.1f %s", value, units[unit]);
}

// Thousands separators make large counts readable without changing the value.
inline wxString format_count(size_t count) {
    wxString text = wxString::Format("%zu", count);
    for (int position = static_cast<int>(text.length()) - 3; position > 0; position -= 3) {
        text.insert(static_cast<size_t>(position), ",");
    }
    return text;
}

} // namespace gui
} // namespace dupli

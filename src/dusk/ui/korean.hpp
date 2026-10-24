#pragma once

#include <array>
#include <cstddef>
#include <string_view>

// This helper is opt-in at host UI display call sites. Do not apply it to
// configuration keys, navigation identifiers, filenames, or text from mods.
namespace dusk::ui::korean {
namespace detail {

struct Entry {
    std::string_view english;
    const char* translated;
};

#include "korean_strings.inc"

}  // namespace detail

// Literal input keeps fmt's compile-time format checking and permits use in
// constexpr label arrays. Missing translations preserve the original pointer.
template <std::size_t N>
constexpr const char* text(const char (&literal)[N]) noexcept {
    const std::string_view key(literal, N - 1);
    std::size_t begin = 0;
    std::size_t end = detail::kStrings.size();
    while (begin < end) {
        const std::size_t middle = begin + (end - begin) / 2;
        const auto& entry = detail::kStrings[middle];
        const int order = key.compare(entry.english);
        if (order == 0) {
            return entry.translated;
        }
        if (order < 0) {
            end = middle;
        } else {
            begin = middle + 1;
        }
    }
    return literal;
}

}  // namespace dusk::ui::korean

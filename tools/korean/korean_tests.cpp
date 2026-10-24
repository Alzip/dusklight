// Compiled by test_generate_strings.py against its isolated fixture dictionary.
#include "korean.hpp"

#include <fmt/format.h>

#include <array>
#include <iostream>
#include <string_view>

namespace ko = dusk::ui::korean;
using namespace std::literals;

static_assert(ko::text("Save") == "저장"sv);
static_assert(ko::text("A") == "처음"sv);
static_assert(ko::text("Zulu") == "마지막 영문"sv);
static_assert(ko::text("한글 원문") == "한글 번역"sv);
static_assert(ko::text("") == ""sv);
static_assert(ko::text("!") == "!"sv);
static_assert(ko::text("Count: missing") == "Count: missing"sv);
static_assert(ko::text("힣") == "힣"sv);

constexpr char unknown[] = "No translation for this literal";
static_assert(ko::text(unknown) == unknown);
constexpr std::array labels{ko::text("Save"), ko::text("A"), ko::text("Missing")};
static_assert(labels[0] == "저장"sv && labels[2] == "Missing"sv);

// A constexpr function returning const char* must retain fmt's consteval
// argument checking. These calls deliberately do not use fmt::runtime.
constexpr fmt::format_string<int> count_format = ko::text("Count: {}");

int main() {
    if (fmt::format(count_format, 42) != "개수: 42") {
        return 1;
    }
    if (fmt::format(ko::text("Count: {}"), 7) != "개수: 7") {
        return 2;
    }
    if (fmt::format(ko::text("Value: {0:{1}.{2}f}"), 1.234, 6, 2) != "값:   1.23") {
        return 3;
    }
    if (fmt::format(ko::text("{{value}}: {}"), 9) != "{값}: 9") {
        return 4;
    }
    if (fmt::format(ko::text("Unknown format: {}"), "Zelda") != "Unknown format: Zelda") {
        return 5;
    }
    std::cout << "Korean constexpr lookup and fmt compile-time formatting passed\n";
    return 0;
}

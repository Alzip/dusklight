#!/usr/bin/env python3
"""Read-only verification of the display-only Korean patch against Dusklight 2.0.3.

Uses only the Python standard library and git. In particular, fontTools is not
required in CI. Source validation reverses literal lookup wrappers, then compares
C++ tokens with the exact upstream source plus the explicitly reviewed display
insertions below. It never removes arbitrary functions, lambdas or code blocks.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
import sys

# The verifier does not create a local __pycache__ while importing the shared
# dictionary validator. CI and local checks leave project files untouched.
sys.dont_write_bytecode = True
from generate_strings import DictionaryError, generate, load_dictionary, normalize_line_endings


BASELINE = "40457c6adb381928e4b5fef6ed459ed291edd5e2"
UI = "src/dusk/ui/"
DISPLAY_FILES = ("settings.cpp", "controller_config.cpp", "graphics_tuner.cpp", "prelaunch.cpp", "menu_bar.cpp")
SOURCE_FILES = DISPLAY_FILES + ("tab_bar.cpp", "tab_bar.hpp", "ui.cpp")
SETTINGS_TABS = ("Prelaunch", "Video", "Input", "Audio", "Gameplay", "Cheats", "Interface", "Tools")

# These seal the reviewed lookup call sites, not translated wording. Moving a
# lookup onto a config key, file path, callback condition or canonical tab ID
# must fail even though removing that lookup would restore the English source.
# Updating a seal requires reviewing the new call sites; there is no accept flag.
REVIEWED_WRAPPERS = {
    "settings.cpp": (345, "2d10429ff160bc74a98ddf86a0a14f832d7e9adef1adfc996ca1e11c8f28c0a7"),
    "controller_config.cpp": (85, "5499f87b8df96dd438a0f169154a59b7fa7328aa8a72ead4b5c5a94eb6cedbbc"),
    "graphics_tuner.cpp": (9, "414ee5b04a0adf199e95aa7cbe48eae21251c83428dcfa8155e68cf3d214e3bc"),
    "prelaunch.cpp": (47, "fcddab306bb2849304542680a64c39d892f4eefce82ae48cc15a8c4c4f03d4e1"),
    "menu_bar.cpp": (16, "50e255c3629127b0b82f041ef3ce47bda0ceae3f3f9c1503bc76638f41001cdc"),
}


class VerificationError(ValueError):
    pass


def require(value: bool, message: str) -> None:
    if not value:
        raise VerificationError(message)


class FontCmap:
    """Bounded TrueType/OpenType Unicode cmap reader (formats 4, 12 and 13)."""

    def __init__(self, data: bytes):
        self.data = data
        require(len(data) >= 12 and data[:4] in (b"\x00\x01\x00\x00", b"OTTO", b"true"), "Unsupported or truncated font")
        count = self.u16(4)
        require(0 < count <= 256 and 12 + count * 16 <= len(data), "Invalid font table directory")
        tables: dict[bytes, tuple[int, int]] = {}
        for i in range(count):
            pos = 12 + 16 * i
            tag = data[pos:pos + 4]
            offset, size = self.u32(pos + 8), self.u32(pos + 12)
            require(tag not in tables and offset <= len(data) and size <= len(data) - offset, "Invalid or duplicate font table")
            tables[tag] = (offset, size)
        require(b"cmap" in tables and b"maxp" in tables, "Font must contain cmap and maxp tables")
        maxp, maxp_size = tables[b"maxp"]
        require(maxp_size >= 6, "Truncated maxp table")
        self.glyph_count = self.u16(maxp + 4)
        require(self.glyph_count > 0, "Font has no glyphs")
        cmap, size = tables[b"cmap"]
        require(size >= 4 and self.u16(cmap) == 0, "Invalid cmap header")
        record_count = self.u16(cmap + 2)
        require(0 < record_count <= 256 and 4 + record_count * 8 <= size, "Invalid cmap record count")
        self.subtables: list[tuple[int, int, int]] = []
        seen = set()
        for i in range(record_count):
            pos = cmap + 4 + i * 8
            platform, encoding, relative = self.u16(pos), self.u16(pos + 2), self.u32(pos + 4)
            if platform != 0 and (platform != 3 or encoding not in (1, 10)):
                continue
            require(relative + 2 <= size, "cmap subtable outside table")
            start = cmap + relative
            kind = self.u16(start)
            if kind not in (4, 12, 13) or start in seen:
                continue
            seen.add(start)
            require(relative + (4 if kind == 4 else 16) <= size, "Truncated Unicode cmap")
            length = self.u16(start + 2) if kind == 4 else self.u32(start + 4)
            require(length > 0 and relative + length <= size, "Unicode cmap exceeds table bounds")
            if kind == 4:
                require(length >= 16 and self.u16(start + 6) % 2 == 0, "Invalid format-4 segments")
                segments = self.u16(start + 6) // 2
                require(segments > 0 and 16 + 8 * segments <= length, "Truncated format-4 segments")
                previous = -1
                for segment in range(segments):
                    end = self.u16(start + 14 + 2 * segment)
                    begin = self.u16(start + 16 + 2 * segments + 2 * segment)
                    require(previous < begin <= end, "Unsorted or overlapping format-4 segments")
                    previous = end
            else:
                groups = self.u32(start + 12)
                require(groups <= 0x110000 and 16 + groups * 12 <= length, "Truncated format-12/13 groups")
                previous = -1
                for group in range(groups):
                    pos = start + 16 + group * 12
                    begin, end = self.u32(pos), self.u32(pos + 4)
                    require(previous < begin <= end <= 0x10FFFF, "Invalid Unicode cmap group")
                    previous = end
            self.subtables.append((kind, start, length))
        require(bool(self.subtables), "Font has no supported Unicode cmap")

    def u16(self, offset: int) -> int:
        require(0 <= offset <= len(self.data) - 2, "Font u16 read outside buffer")
        return struct.unpack_from(">H", self.data, offset)[0]

    def u32(self, offset: int) -> int:
        require(0 <= offset <= len(self.data) - 4, "Font u32 read outside buffer")
        return struct.unpack_from(">I", self.data, offset)[0]

    def contains(self, codepoint: int) -> bool:
        for kind, start, length in self.subtables:
            glyph = 0
            if kind == 4:
                if codepoint > 0xFFFF:
                    continue
                segments = self.u16(start + 6) // 2
                for i in range(segments):
                    end = self.u16(start + 14 + 2 * i)
                    if codepoint > end:
                        continue
                    begin = self.u16(start + 16 + 2 * segments + 2 * i)
                    if codepoint < begin:
                        break
                    delta = self.u16(start + 16 + 4 * segments + 2 * i)
                    range_pos = start + 16 + 6 * segments + 2 * i
                    relative = self.u16(range_pos)
                    if relative == 0:
                        glyph = (codepoint + delta) & 0xFFFF
                    else:
                        pos = range_pos + relative + 2 * (codepoint - begin)
                        require(pos >= start + 16 + 8 * segments and pos + 2 <= start + length, "Glyph lookup exceeds cmap bounds")
                        glyph = self.u16(pos)
                        if glyph:
                            glyph = (glyph + delta) & 0xFFFF
                    break
            else:
                for i in range(self.u32(start + 12)):
                    pos = start + 16 + i * 12
                    begin, end, first = self.u32(pos), self.u32(pos + 4), self.u32(pos + 8)
                    if codepoint < begin:
                        break
                    if codepoint <= end:
                        glyph = first + (codepoint - begin if kind == 12 else 0)
                        break
            require(glyph < self.glyph_count, "cmap references a nonexistent glyph")
            if glyph:
                return True
        return False


TOKEN = re.compile(
    r"(?P<space>\s+)|(?P<comment>//[^\n]*|/\*[\s\S]*?\*/)"
    r'|(?P<raw>(?:u8|u|U|L)?R"(?P<delimiter>[^ ()\\\t\r\n]{0,16})\([\s\S]*?\)(?P=delimiter)")'
    r'|(?P<string>(?:u8|u|U|L)?"(?:\\[\s\S]|[^"\\])*")'
    r"|(?P<char>(?:u8|u|U|L)?'(?:\\[\s\S]|[^'\\])*')"
    r"|(?P<identifier>[A-Za-z_][A-Za-z_0-9]*)"
    r"|(?P<number>(?:0[xX][0-9A-Fa-f]+|[0-9]+(?:\.[0-9]*)?)(?:[eEpP][+-]?[0-9]+)?[A-Za-z_0-9]*)"
    r"|(?P<operator><=>|>>=|<<=|->\*|\.\.\.|::|->|\+\+|--|&&|\|\||==|!=|<=|>=|<<|>>|\+=|-=|\*=|/=|%=|&=|\|=|\^=|\.\*|##|.)",
    re.DOTALL,
)


@dataclass(frozen=True)
class Token:
    kind: str
    text: str
    line: int


def tokens(source: str) -> list[Token]:
    result = []
    for match in TOKEN.finditer(source):
        if match.lastgroup in ("space", "comment"):
            continue
        result.append(Token(match.lastgroup or "", match.group(), source.count("\n", 0, match.start()) + 1))
    return result


def literal_value(token: Token) -> str:
    text = token.text
    if token.kind == "raw":
        match = re.fullmatch(r'(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(([\s\S]*)\)\1"', text)
        require(match is not None, "Malformed raw string")
        return match.group(2)
    require(token.kind == "string" and text.startswith('"'), "Lookup must use a plain UTF-8 string literal")
    inside = text[1:-1]
    out = bytearray()
    position = 0
    for match in re.finditer(r"\\(?:x[0-9a-fA-F]+|u[0-9a-fA-F]{4}|U[0-9a-fA-F]{8}|[0-7]{1,3}|[\s\S])", inside):
        out.extend(inside[position:match.start()].encode("utf-8"))
        escaped = match.group()[1:]
        if escaped.startswith("x"):
            value = int(escaped[1:], 16)
            require(value <= 255, "Out-of-range byte escape")
            out.append(value)
        elif escaped.startswith(("u", "U")):
            out.extend(chr(int(escaped[1:], 16)).encode("utf-8"))
        elif escaped[0] in "01234567":
            value = int(escaped, 8)
            require(value <= 255, "Out-of-range octal escape")
            out.append(value)
        elif escaped == "\n":
            pass
        else:
            escapes = {"n": "\n", "r": "\r", "t": "\t", '"': '"', "'": "'", "\\": "\\", "a": "\a", "b": "\b", "f": "\f", "v": "\v", "?": "?"}
            require(escaped in escapes, f"Unsupported C++ escape: {escaped!r}")
            out.extend(escapes[escaped].encode("utf-8"))
        position = match.end()
    out.extend(inside[position:].encode("utf-8"))
    return out.decode("utf-8", errors="strict")


def unwrap(source: str, dictionary: dict[str, str]) -> tuple[list[Token], list[tuple[int, str]]]:
    original = tokens(source)
    out: list[Token] = []
    calls: list[tuple[int, str]] = []
    i = 0
    while i < len(original):
        if [x.text for x in original[i:i + 4]] != ["korean", "::", "text", "("]:
            out.append(original[i]); i += 1; continue
        end = i + 4
        while end < len(original) and original[end].kind in ("string", "raw"):
            end += 1
        require(end > i + 4 and end < len(original) and original[end].text == ")", "Non-literal or malformed korean::text argument")
        key = "".join(literal_value(t) for t in original[i + 4:end])
        require(key in dictionary, f"Missing translation for wrapped literal: {key!r}")
        calls.append((len(out), key))
        out.extend(original[i + 4:end])
        i = end + 1
    return out, calls


def spellings(items: list[Token]) -> list[str]:
    return [token.text for token in items]


def replace_exact(source: str, before: str, after: str, count: int = 1) -> str:
    require(source.count(before) == count, f"Reviewed source anchor changed: {before[:100]!r}")
    return source.replace(before, after)


TAB_METHOD = '''bool TabBar::set_tab_label(const Rml::String& canonical_title, const Rml::String& display_text) {
    for (auto& tab : mTabs) {
        if (tab.title == canonical_title) {
            tab.button->set_text(display_text);
            return true;
        }
    }
    return false;
}

'''
BOOL_ADAPTER = '''#pragma once
#include "bool_button.hpp"
#include "korean.hpp"
namespace dusk::ui {
class KoreanBoolButton final : public BoolButton {
public:
    using BoolButton::BoolButton;
protected:
    Rml::String format_value() override {
        auto value = BoolButton::format_value();
        if (value == "On") { return korean::text("On"); }
        if (value == "Off") { return korean::text("Off"); }
        return value;
    }
};
}
'''


def expected_source(name: str, upstream: str) -> str:
    """Apply only fixed, reviewed presentation edits to the exact upstream."""
    source = upstream
    if name in DISPLAY_FILES:
        include = f'#include "{name.removesuffix(".cpp")}.hpp"\n'
        added = include + '#include "korean.hpp"\n'
        if name in ("settings.cpp", "controller_config.cpp"):
            added += '#include "korean_controls.hpp"\n'
        source = replace_exact(source, include, added)
    if name in ("settings.cpp", "controller_config.cpp"):
        source = replace_exact(source, "add_child<BoolButton>", "add_child<KoreanBoolButton>", 2 if name == "settings.cpp" else 4)
    if name == "settings.cpp":
        insert = "".join(f'    mTabBar->set_tab_label("{label}", korean::text("{label}"));\n' for label in SETTINGS_TABS)
        end = "\n}\n\nvoid SettingsWindow::update()"
        source = replace_exact(source, end, "\n" + insert + end)
    if name == "menu_bar.cpp":
        for label, window in (("Settings", "SettingsWindow"), ("Warp", "WarpWindow"),
                              ("Editor", "EditorWindow"), ("Achievements", "AchievementsWindow")):
            before = f'mTabBar->add_tab("{label}", [this] {{ push(std::make_unique<{window}>()); }});'
            source = replace_exact(source, before, before[:-1] + f'.set_text(korean::text("{label}"));')
        before = '    mModsButton = &mTabBar->add_tab("Mods", [this] { push(std::make_unique<ModsWindow>()); });\n'
        source = replace_exact(source, before, before + '    mModsButton->set_text(korean::text("Mods"));\n')
        source = replace_exact(source, '    });\n    mTabBar->add_tab("Quit", [this] {',
                               '    }).set_text(korean::text("Reset"));\n    mTabBar->add_tab("Quit", [this] {')
        source = replace_exact(source, '    });\n\n    if (speedrun::isActive()) {',
                               '    }).set_text(korean::text("Quit"));\n\n    if (speedrun::isActive()) {')
        source = replace_exact(source, '        });\n    }\n}\n\nvoid MenuBar::show()',
                               '        }).set_text(korean::text("Reset Run"));\n    }\n}\n\nvoid MenuBar::show()')
    if name == "controller_config.cpp":
        before = '''            build_port_tab(content, port);
        });
    }
}'''
        after = '''            build_port_tab(content, port);
        });
        mTabBar->set_tab_label(fmt::format("Port {}", port + 1),
            fmt::format(korean::text("Port {}"), port + 1));
    }
}'''
        source = replace_exact(source, before, after)
    if name == "prelaunch.cpp":
        source = replace_exact(source, '<eyebrow><studio-name>Twilit Realm</studio-name> presents</eyebrow>',
                               '<eyebrow><studio-name>Twilit Realm</studio-name> 제작</eyebrow>')
        source = replace_exact(source, '<version-label>Version <version-number id="version-text">',
                               '<version-label>버전 <version-number id="version-text">')
    if name == "tab_bar.cpp":
        source = replace_exact(source, "void TabBar::clear_tabs() {", TAB_METHOD + "void TabBar::clear_tabs() {")
    if name == "tab_bar.hpp":
        before = "    Button& add_tab(const Rml::String& title, TabCallback callback);\n"
        source = replace_exact(source, before, before + "    bool set_tab_label(const Rml::String& canonical_title, const Rml::String& display_text);\n")
    if name == "ui.cpp":
        before = '    load_font("NotoMono-Regular.ttf");\n'
        source = replace_exact(source, before, before + '    load_font("NanumGothic-Regular.ttf", true);\n')
    return source


def git(root: Path, *args: str) -> str:
    result = subprocess.run(["git", "-C", str(root), *args], capture_output=True, check=False)
    require(result.returncode == 0, "git " + " ".join(args) + ": " + result.stderr.decode("utf-8", errors="replace"))
    return result.stdout.decode("utf-8", errors="strict")


def signature(calls: list[tuple[int, str]]) -> str:
    return hashlib.sha256(json.dumps(calls, ensure_ascii=False, separators=(",", ":")).encode("utf-8")).hexdigest()


def compare_source(name: str, source: str, expected: str, dictionary: dict[str, str]) -> tuple[int, str]:
    actual_tokens, calls = unwrap(source, dictionary)
    expected_tokens, _ = unwrap(expected, dictionary)
    actual, wanted = spellings(actual_tokens), spellings(expected_tokens)
    if actual != wanted:
        first = next((i for i, pair in enumerate(zip(actual, wanted)) if pair[0] != pair[1]), min(len(actual), len(wanted)))
        line = actual_tokens[min(first, len(actual_tokens) - 1)].line if actual_tokens else 1
        raise VerificationError(f"{name}:{line}: non-display source change near {actual[max(0, first-5):first+8]!r}; expected {wanted[max(0, first-5):first+8]!r}")
    return len(calls), signature(calls)


def verify(root: Path) -> dict[str, object]:
    source = (root / "res/localization/ko.json").read_bytes()
    dictionary = load_dictionary(source)
    # Git may check out JSON and this generated ASCII file with Windows CRLF.
    # Normalize physical line endings only; escaped literal contents stay exact.
    generated_bytes = (root / (UI + "korean_strings.inc")).read_bytes()
    require(normalize_line_endings(generated_bytes) == generate(source).encode("ascii"), "Generated Korean table is stale")
    font = FontCmap((root / "res/NanumGothic-Regular.ttf").read_bytes())
    wanted = sorted({ord(ch) for text in dictionary.values() for ch in text if ord(ch) >= 128})
    missing = [f"U+{cp:04X} {chr(cp)!r}" for cp in wanted if not font.contains(cp)]
    require(not missing, "Bundled Korean font is missing: " + ", ".join(missing))

    resolved = git(root, "rev-parse", BASELINE).strip()
    require(resolved == BASELINE, "Pinned upstream revision is unavailable")
    allowed = {UI + name for name in SOURCE_FILES} | {UI + name for name in ("korean.hpp", "korean_controls.hpp", "korean_strings.inc")}
    changed = set(git(root, "diff", "--name-only", BASELINE, "--", "src").splitlines())
    require(changed <= allowed, "Changes outside the reviewed host UI files: " + ", ".join(sorted(changed - allowed)))
    counts: dict[str, int] = {}
    for name in SOURCE_FILES:
        upstream = git(root, "show", f"{BASELINE}:{UI}{name}")
        expected = expected_source(name, upstream)
        actual = (root / (UI + name)).read_text(encoding="utf-8")
        count, digest = compare_source(name, actual, expected, dictionary)
        if name in DISPLAY_FILES:
            require(REVIEWED_WRAPPERS.get(name) == (count, digest), f"Unreviewed lookup call sites in {name}: {count}, {digest}")
        else:
            require(count == 0, f"Unexpected text lookup in {name}")
        counts[name] = count
    require(spellings(tokens((root / (UI + "korean_controls.hpp")).read_text(encoding="utf-8"))) == spellings(tokens(BOOL_ADAPTER)),
            "KoreanBoolButton must only format the original value once; getters, setters and input handling cannot change")
    return {"baseline": BASELINE, "dictionary_entries": len(dictionary), "non_ascii_codepoints": len(wanted),
            "unicode_cmap_formats": sorted({kind for kind, _, _ in font.subtables}),
            "verified_source_files": len(SOURCE_FILES), "lookup_calls": counts}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--json", action="store_true", help="print the verification summary as JSON")
    args = parser.parse_args(argv)
    try:
        result = verify(args.root.resolve())
    except (OSError, UnicodeError, DictionaryError, VerificationError, struct.error) as exc:
        print(f"Korean UI verification failed: {exc}", file=sys.stderr)
        return 1
    if args.json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        print(f"Verified {result['dictionary_entries']} translations and {result['non_ascii_codepoints']} non-ASCII font glyphs without external Python dependencies.")
        print(f"All {result['verified_source_files']} host UI files match pinned 2.0.3 after only reviewed display changes; {sum(result['lookup_calls'].values())} lookup call sites sealed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Validate the Korean host UI dictionary and emit a constexpr lookup table.

The generated table is used only at explicit host UI call sites. It does not
intercept text from mods, mutate settings, or perform runtime file access.
"""

from __future__ import annotations

import argparse
import hashlib
from html.parser import HTMLParser
import json
from pathlib import Path
import sys
import unicodedata


class DictionaryError(ValueError):
    """A source dictionary cannot safely replace the corresponding literals."""


def _unique_object(pairs: list[tuple[str, object]]) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise DictionaryError(f"duplicate JSON key: {key!r}")
        result[key] = value
    return result


def _validate_text(text: str, description: str) -> None:
    if not text:
        raise DictionaryError(f"{description}: empty strings are not allowed")
    try:
        text.encode("utf-8", errors="strict")
    except UnicodeEncodeError as exc:
        raise DictionaryError(f"{description}: invalid Unicode scalar value") from exc
    if unicodedata.normalize("NFC", text) != text:
        raise DictionaryError(f"{description}: text must use NFC Unicode normalization")
    for char in text:
        if (ord(char) < 0x20 and char not in "\t\n\r") or 0x7F <= ord(char) <= 0x9F:
            raise DictionaryError(f"{description}: forbidden control character U+{ord(char):04X}")


def format_tokens(text: str) -> tuple[str, ...]:
    """Keep replacement fields, specs, nesting, and escaped braces unchanged.

    This is deliberately conservative: translators cannot change argument
    positions or formatting types. The C++ compiler still validates each fmt
    call against its actual argument types.
    """
    tokens: list[str] = []
    index = 0
    while index < len(text):
        char = text[index]
        if char not in "{}":
            index += 1
            continue
        if text[index:index + 2] in ("{{", "}}"):
            tokens.append(text[index:index + 2])
            index += 2
            continue
        if char == "}":
            raise DictionaryError("unmatched closing format brace")
        start = index
        depth = 1
        index += 1
        while index < len(text) and depth:
            if text[index] == "{":
                depth += 1
            elif text[index] == "}":
                depth -= 1
            index += 1
        if depth:
            raise DictionaryError("unmatched opening format brace")
        tokens.append(text[start:index])
    return tuple(tokens)


class _MarkupSignature(HTMLParser):
    """Compare markup while allowing the human-readable text to change.

    Start tags are kept byte-for-byte, including attributes. Partial markup
    fragments are allowed because some host help strings begin with a <br/>.
    Plain comparison signs and ampersands remain ordinary text.
    """

    def __init__(self) -> None:
        super().__init__(convert_charrefs=False)
        self.tokens: list[tuple[str, str]] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.tokens.append(("start", self.get_starttag_text()))

    def handle_startendtag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        self.tokens.append(("empty", self.get_starttag_text()))

    def handle_endtag(self, tag: str) -> None:
        self.tokens.append(("end", tag))

    def handle_entityref(self, name: str) -> None:
        self.tokens.append(("entity", name))

    def handle_charref(self, name: str) -> None:
        self.tokens.append(("character", name))

    def handle_comment(self, data: str) -> None:
        self.tokens.append(("comment", data))

    def handle_decl(self, decl: str) -> None:
        self.tokens.append(("declaration", decl))

    def handle_pi(self, data: str) -> None:
        self.tokens.append(("instruction", data))

    def unknown_decl(self, data: str) -> None:
        self.tokens.append(("unknown", data))


def markup_tokens(text: str) -> tuple[tuple[str, str], ...]:
    parser = _MarkupSignature()
    try:
        parser.feed(text)
        parser.close()
    except (ValueError, AssertionError) as exc:
        raise DictionaryError(f"invalid markup: {exc}") from exc
    return tuple(parser.tokens)


def load_dictionary(source: bytes) -> dict[str, str]:
    try:
        document = json.loads(source.decode("utf-8", errors="strict"),
                              object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise DictionaryError(f"invalid UTF-8 JSON: {exc}") from exc
    if not isinstance(document, dict):
        raise DictionaryError("dictionary root must be an object")
    if type(document.get("version")) is not int or document["version"] != 1:
        raise DictionaryError("dictionary version must be 1")
    if document.get("language") != "ko":
        raise DictionaryError("dictionary language must be 'ko'")
    strings = document.get("strings")
    if not isinstance(strings, dict) or not strings:
        raise DictionaryError("'strings' must be a nonempty object")
    for english, translated in strings.items():
        if not isinstance(english, str) or not isinstance(translated, str):
            raise DictionaryError("all source literals and translations must be strings")
        _validate_text(english, f"source {english!r}")
        _validate_text(translated, f"translation of {english!r}")
        try:
            if format_tokens(english) != format_tokens(translated):
                raise DictionaryError("format fields or escaped braces changed")
            if markup_tokens(english) != markup_tokens(translated):
                raise DictionaryError("RML tags, attributes, or character references changed")
        except DictionaryError as exc:
            raise DictionaryError(f"{english!r}: {exc}") from exc
    return strings


def cpp_string(text: str) -> str:
    """Use ASCII source with unambiguous UTF-8 octal escapes on every compiler."""
    escaped: list[str] = ['"']
    for byte in text.encode("utf-8"):
        if byte == 0x22:
            escaped.append('\\"')
        elif byte == 0x5C:
            escaped.append("\\\\")
        elif byte == 0x0A:
            escaped.append("\\n")
        elif byte == 0x0D:
            escaped.append("\\r")
        elif byte == 0x09:
            escaped.append("\\t")
        elif 0x20 <= byte < 0x7F:
            escaped.append(chr(byte))
        else:
            escaped.append(f"\\{byte:03o}")
    escaped.append('"')
    return "".join(escaped)


def generate(source: bytes) -> str:
    strings = load_dictionary(source)
    ordered = sorted(strings.items(), key=lambda pair: pair[0].encode("utf-8"))
    lines = [
        "// Generated by tools/korean/generate_strings.py. Do not edit by hand.",
        f"// Source SHA-256: {hashlib.sha256(source).hexdigest()}",
        f"inline constexpr std::array<Entry, {len(ordered)}> kStrings = {{{{",
    ]
    for english, translated in ordered:
        lines.append(f"    {{{cpp_string(english)}, {cpp_string(translated)}}},")
    lines.append("}};")
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    root = Path(__file__).resolve().parents[2]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, default=root / "res/localization/ko.json")
    parser.add_argument("--output", type=Path, default=root / "src/dusk/ui/korean_strings.inc")
    parser.add_argument("--check", action="store_true", help="fail if generated output is stale")
    args = parser.parse_args(argv)
    try:
        generated = generate(args.input.read_bytes())
        if args.check:
            if not args.output.is_file() or args.output.read_bytes() != generated.encode("ascii"):
                raise DictionaryError(f"{args.output}: missing or stale; run generate_strings.py")
        else:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(generated.encode("ascii"))
    except (OSError, DictionaryError) as exc:
        print(f"Korean dictionary: {exc}", file=sys.stderr)
        return 1
    action = "Verified" if args.check else "Generated"
    print(f"{action} {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

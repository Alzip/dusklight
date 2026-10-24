#!/usr/bin/env python3
"""Standalone dictionary safety and C++/fmt integration tests.

No game files or full host build are needed. Pass --fmt-include to also compile
the C++ integration test with the same fmt headers as the host.
"""

from __future__ import annotations

import argparse
import contextlib
import io
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest

import generate_strings as generator


ROOT = Path(__file__).resolve().parents[2]
FMT_INCLUDE: Path | None = None
COMPILERS = ("cl", "clang-cl", "clang++", "g++") if os.name == "nt" else ("c++", "clang++", "g++")
CXX: str | None = next((compiler for name in COMPILERS if (compiler := shutil.which(name))), None)


def dictionary(strings: dict[str, str]) -> bytes:
    return json.dumps({"version": 1, "language": "ko", "strings": strings},
                      ensure_ascii=False).encode("utf-8")


class DictionaryTests(unittest.TestCase):
    def test_valid_help_and_format_fields(self) -> None:
        strings = {
            "Save": "저장",
            "<b>{}</b><br/><icon class=\"warning\"/>Restart":
                "<b>{}</b><br/><icon class=\"warning\"/>다시 시작",
            "{{value}}: {0:{1}.{2}f}": "{{값}}: {0:{1}.{2}f}",
            "CPU & GPU": "CPU 및 GPU",
            "Size < 3": "크기 < 3",
            "Line one\nLine two": "첫째 줄\n둘째 줄",
        }
        self.assertEqual(generator.load_dictionary(dictionary(strings)), strings)

    def test_rejects_duplicate_translation_keys(self) -> None:
        source = b'{"version":1,"language":"ko","strings":{"Save":"a","Save":"b"}}'
        with self.assertRaisesRegex(generator.DictionaryError, "duplicate JSON key"):
            generator.load_dictionary(source)

    def test_rejects_duplicate_root_keys(self) -> None:
        source = b'{"version":1,"version":1,"language":"ko","strings":{"a":"b"}}'
        with self.assertRaisesRegex(generator.DictionaryError, "duplicate JSON key"):
            generator.load_dictionary(source)

    def test_rejects_invalid_utf8(self) -> None:
        with self.assertRaisesRegex(generator.DictionaryError, "invalid UTF-8 JSON"):
            generator.load_dictionary(b'{"strings":{"a":"\xff"}}')

    def test_rejects_unpaired_surrogate(self) -> None:
        source = b'{"version":1,"language":"ko","strings":{"Save":"\\ud800"}}'
        with self.assertRaisesRegex(generator.DictionaryError, "Unicode scalar"):
            generator.load_dictionary(source)

    def test_rejects_control_characters(self) -> None:
        for character in ("\0", "\x01", "\x7f", "\x85"):
            with self.subTest(character=repr(character)):
                with self.assertRaisesRegex(generator.DictionaryError, "control character"):
                    generator.load_dictionary(dictionary({"Save": "저장" + character}))

    def test_requires_nfc_hangul(self) -> None:
        with self.assertRaisesRegex(generator.DictionaryError, "NFC"):
            generator.load_dictionary(dictionary({"Save": "\u1100\u1161"}))

    def test_rejects_empty_or_nonstring_entries(self) -> None:
        for strings in ({"Save": ""}, {"": "저장"}, {"Save": None}, {}):
            with self.subTest(strings=strings):
                with self.assertRaises(generator.DictionaryError):
                    generator.load_dictionary(dictionary(strings))

    def test_rejects_wrong_schema(self) -> None:
        for document in ([], {}, {"version": True, "language": "ko", "strings": {"a": "b"}},
                         {"version": 1, "language": "en", "strings": {"a": "b"}},
                         {"version": 1, "language": "ko", "strings": []}):
            with self.subTest(document=document):
                with self.assertRaises(generator.DictionaryError):
                    generator.load_dictionary(json.dumps(document).encode())

    def test_rejects_changed_format_arguments(self) -> None:
        cases = (
            ("Count: {}", "개수"),
            ("Name", "이름 {}"),
            ("{0}: {1}", "{1}: {0}"),
            ("{:.2f}", "{:.0f}"),
            ("{name}", "{이름}"),
            ("{0:{1}.{2}f}", "{0:{2}.{1}f}"),
            ("{{value}}: {}", "값: {}"),
        )
        for english, translated in cases:
            with self.subTest(english=english, translated=translated):
                with self.assertRaisesRegex(generator.DictionaryError, "format fields"):
                    generator.load_dictionary(dictionary({english: translated}))

    def test_rejects_unmatched_format_braces(self) -> None:
        for text in ("}", "{", "Count: {0", "Count: {} }", "Value: {0:{1}"):
            with self.subTest(text=text):
                with self.assertRaisesRegex(generator.DictionaryError, "unmatched"):
                    generator.load_dictionary(dictionary({text: text}))

    def test_rejects_changed_markup(self) -> None:
        cases = (
            ("Save", "<b>저장</b>"),
            ("<b>Save</b>", "<i>저장</i>"),
            ("<br/>Restart", "다시 시작"),
            ('<a href="https://example.test/">Open</a>', '<a href="file://bad">열기</a>'),
            ('<icon class="warning"/>Warning', '<icon class="other"/>경고'),
            ("A &amp; B", "가 &lt; 나"),
            ("Save", "저장<!--hidden-->"),
        )
        for english, translated in cases:
            with self.subTest(english=english, translated=translated):
                with self.assertRaisesRegex(generator.DictionaryError, "RML"):
                    generator.load_dictionary(dictionary({english: translated}))

    def test_preserves_nested_format_specs_and_escaped_braces(self) -> None:
        self.assertEqual(generator.format_tokens("{{x}}: {0:{1}.{2}f} {}"),
                         ("{{", "}}", "{0:{1}.{2}f}", "{}"))

    def test_generated_source_is_ascii_sorted_and_escaped(self) -> None:
        source = dictionary({"z": "끝", "A": '가"\\\n9', "한글": "한글"})
        generated = generator.generate(source)
        generated.encode("ascii")
        self.assertLess(generated.index('{"A",'), generated.index('{"z",'))
        self.assertLess(generated.index('{"z",'), generated.index('{"\\355'))
        self.assertIn('\\352\\260\\200\\"\\\\\\n9', generated)
        self.assertEqual(generated, generator.generate(source))

    def test_cli_check_detects_stale_or_missing_output(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            source = Path(directory) / "ko.json"
            output = Path(directory) / "table.inc"
            source.write_bytes(dictionary({"Save": "저장"}))
            args = ["--input", str(source), "--output", str(output)]
            with contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                self.assertEqual(generator.main(args + ["--check"]), 1)
                self.assertEqual(generator.main(args), 0)
                self.assertEqual(generator.main(args + ["--check"]), 0)
                source.write_bytes(dictionary({"Save": "저장하기"}))
                self.assertEqual(generator.main(args + ["--check"]), 1)


class CppIntegrationTests(unittest.TestCase):
    def test_constexpr_fallback_and_fmt(self) -> None:
        if not FMT_INCLUDE or not CXX:
            self.skipTest("pass --fmt-include and provide a C++20 compiler for the fmt probe")
        self.assertTrue((FMT_INCLUDE / "fmt/format.h").is_file(), "fmt headers not found")
        fixture = dictionary({
            "A": "처음",
            "Count: {}": "개수: {}",
            "Save": "저장",
            "Value: {0:{1}.{2}f}": "값: {0:{1}.{2}f}",
            "Zulu": "마지막 영문",
            "{{value}}: {}": "{{값}}: {}",
            "한글 원문": "한글 번역",
        })
        with tempfile.TemporaryDirectory() as directory:
            temporary = Path(directory)
            shutil.copyfile(ROOT / "src/dusk/ui/korean.hpp", temporary / "korean.hpp")
            (temporary / "korean_strings.inc").write_text(generator.generate(fixture), encoding="ascii")
            executable = temporary / ("korean-tests.exe" if os.name == "nt" else "korean-tests")
            test_source = str(Path(__file__).with_name("korean_tests.cpp").resolve())
            compiler_name = Path(CXX).name.lower()
            if compiler_name in ("cl", "cl.exe", "clang-cl", "clang-cl.exe"):
                command = [CXX, "/nologo", "/std:c++20", "/EHsc", "/utf-8", "/W4",
                           "/DFMT_HEADER_ONLY", "/I" + str(temporary), "/I" + str(FMT_INCLUDE.resolve()),
                           test_source, "/Fe" + str(executable),
                           "/Fo" + str(temporary / "korean_tests.obj")]
            else:
                command = [CXX, "-std=c++20", "-Wall", "-Wextra", "-pedantic", "-DFMT_HEADER_ONLY",
                           "-I", str(temporary), "-I", str(FMT_INCLUDE.resolve()),
                           test_source, "-o", str(executable)]
            compiled = subprocess.run(command, cwd=temporary, text=True, capture_output=True, timeout=120)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            executed = subprocess.run([str(executable)], text=True, capture_output=True, timeout=10)
            self.assertEqual(executed.returncode, 0, executed.stdout + executed.stderr)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--fmt-include", type=Path)
    parser.add_argument("--cxx", default=CXX)
    args, unittest_args = parser.parse_known_args()
    FMT_INCLUDE = args.fmt_include
    CXX = args.cxx
    unittest.main(argv=[sys.argv[0], *unittest_args])

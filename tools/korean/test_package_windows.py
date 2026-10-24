#!/usr/bin/env python3
"""Regression checks for preserving the original Windows runtime package."""

from __future__ import annotations

import argparse
import io
from pathlib import Path
import stat
import sys
import unittest
import zipfile

from package_windows import (PACKAGE_ADDITIONS, allowed_source, original_files,
                             safe_zip_files, verify_executable, verify_file_inventory)
from pe_checks import PEImage, VerificationError, compare_host, verify_imports


ORIGINAL_ZIP: Path | None = None


def zip_bytes(entries: list[tuple[str | zipfile.ZipInfo, bytes]]) -> bytes:
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, "w") as archive:
        for name, data in entries:
            archive.writestr(name, data)
    return buffer.getvalue()


class PackageBoundaryTests(unittest.TestCase):
    def test_allows_only_ui_source_scope(self) -> None:
        for path in ("src/dusk/ui/settings.cpp", "src/dusk/ui/korean_strings.inc",
                     "res/localization/ko.json", "tools/korean/build_windows.ps1"):
            self.assertTrue(allowed_source(path), path)
        for path in ("src/dusk/app_info.hpp", "src/dusk/config.cpp", "src/d/a.cpp",
                     "sdk/include/dusklight/services/save.h", "cmake/GameABIConfig.cmake",
                     "extern/borealis", "mods/randomizer", "CMakeLists.txt",
                     "res/rml/global.rcss"):
            self.assertFalse(allowed_source(path), path)

    def test_zip_rejects_traversal_and_windows_path_aliases(self) -> None:
        for name in ("../config.json", "/save.gci", "C:/save.gci", "res\\ko.json",
                     "res//ko.json", "res/./ko.json"):
            with self.subTest(name=name), self.assertRaises(VerificationError):
                safe_zip_files(zip_bytes([(name, b"bad")]), "test")

    def test_zip_rejects_case_collisions(self) -> None:
        with self.assertRaisesRegex(VerificationError, "Duplicate"):
            safe_zip_files(zip_bytes([("res/font.ttf", b"a"), ("res/FONT.ttf", b"b")]), "test")

    def test_zip_rejects_symlinks(self) -> None:
        link = zipfile.ZipInfo("res/font.ttf")
        link.create_system = 3
        link.external_attr = (stat.S_IFLNK | 0o777) << 16
        with self.assertRaisesRegex(VerificationError, "Unsafe"):
            safe_zip_files(zip_bytes([(link, b"../../outside")]), "test")

    def test_inventory_rejects_runtime_replacement_and_user_data(self) -> None:
        original = {"dusklight.exe": b"old executable", "SDL3.dll": b"original runtime",
                    "res/rml/theme.rcss": b"original CSS", "mods/randomizer.dusk": b"original mod"}
        package = {**original, **{name: b"new UI material" for name in PACKAGE_ADDITIONS}}
        package["dusklight.exe"] = b"translated executable"
        result = verify_file_inventory(original, package)
        self.assertEqual(result["changed_original_files"], ["dusklight.exe"])
        for name in ("SDL3.dll", "res/rml/theme.rcss", "mods/randomizer.dusk"):
            with self.subTest(name=name), self.assertRaisesRegex(VerificationError, "files changed"):
                verify_file_inventory(original, {**package, name: b"unexpected replacement"})
        with self.assertRaisesRegex(VerificationError, "Unexpected package additions"):
            verify_file_inventory(original, {**package, "config.json": b"{}"})
        missing = dict(package)
        del missing["SDL3.dll"]
        with self.assertRaisesRegex(VerificationError, "missing"):
            verify_file_inventory(original, missing)

    def test_truncated_pe_fails_closed(self) -> None:
        for data in (b"", b"MZ", b"MZ" + b"\xff" * 100):
            with self.subTest(length=len(data)), self.assertRaises(VerificationError):
                PEImage(data)


class OriginalBinaryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        if not ORIGINAL_ZIP:
            raise unittest.SkipTest("pass --original to run checks against the pinned official release")
        cls.files = original_files(ORIGINAL_ZIP)
        cls.original = PEImage(cls.files["dusklight.exe"], "official dusklight.exe")

    def test_original_runtime_and_all_bundled_mods_resolve(self) -> None:
        report = verify_executable(self.files, self.files["dusklight.exe"])
        self.assertEqual(report["host"]["preserved_original_named_exports"], 30042)
        self.assertEqual(len(report["imports"]), 4)
        self.assertTrue(report["host"]["version_resource_identical"])

    def test_removed_host_export_is_rejected(self) -> None:
        rebuilt = PEImage(self.files["dusklight.exe"], "rebuilt")
        del rebuilt.exports[next(iter(rebuilt.exports))]
        with self.assertRaisesRegex(VerificationError, "exports were removed"):
            compare_host(self.original, rebuilt)

    def test_forwarder_change_is_rejected(self) -> None:
        rebuilt = PEImage(self.files["dusklight.exe"], "rebuilt")
        name = next(name for name, value in rebuilt.exports.items() if value.forwarder)
        entry = rebuilt.exports[name]
        rebuilt.exports[name] = type(entry)(entry.ordinal, entry.rva, "other_dll.other_function")
        with self.assertRaisesRegex(VerificationError, "forwarders changed"):
            compare_host(self.original, rebuilt)

    def test_version_identity_change_is_rejected(self) -> None:
        data = self.files["dusklight.exe"]
        version = self.original.version_resource()
        changed = version.replace("dusklight.exe".encode("utf-16le"),
                                  "different.exe".encode("utf-16le"))
        # Keep resource length identical, while altering the original filename.
        self.assertEqual(len(changed), len(version))
        self.assertNotEqual(changed, version)
        position = data.index(version)
        rebuilt = PEImage(data[:position] + changed + data[position + len(version):], "changed metadata")
        with self.assertRaisesRegex(VerificationError, "metadata differs"):
            compare_host(self.original, rebuilt)

    def test_missing_symbol_manifest_is_rejected(self) -> None:
        rebuilt = PEImage(self.files["dusklight.exe"], "rebuilt")
        del rebuilt.sections[".symdb"]
        with self.assertRaisesRegex(VerificationError, "symdb"):
            compare_host(self.original, rebuilt)

    def test_new_runtime_requirement_is_rejected(self) -> None:
        rebuilt = PEImage(self.files["dusklight.exe"], "rebuilt")
        rebuilt.imports = {"new-required-runtime.dll": {"NewFunction"}}
        with self.assertRaisesRegex(VerificationError, "new system imports"):
            verify_imports(rebuilt, {}, {})

    def test_import_missing_from_original_dll_is_rejected(self) -> None:
        rebuilt = PEImage(self.files["dusklight.exe"], "rebuilt")
        rebuilt.imports = {"sdl3.dll": {"FunctionIntroducedByAnUnrequestedUpgrade"}}
        provider = PEImage(self.files["SDL3.dll"], "original SDL3.dll")
        with self.assertRaisesRegex(VerificationError, "unavailable import"):
            verify_imports(rebuilt, {"sdl3.dll": provider}, {})


if __name__ == "__main__":
    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument("--original", type=Path)
    args, unittest_args = parser.parse_known_args()
    ORIGINAL_ZIP = args.original
    unittest.main(argv=[sys.argv[0], *unittest_args])

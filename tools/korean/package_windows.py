#!/usr/bin/env python3
"""Audit and package the display-only Korean fork of Dusklight 2.0.3.

The release ZIP is the package base. No rebuilt DLL, bundled mod, game asset,
cache, or unrelated stylesheet is allowed to replace an original file.
"""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import zipfile

from pe_checks import PEImage, VerificationError, compare_host, verify_imports


BASE_REVISION = "40457c6adb381928e4b5fef6ed459ed291edd5e2"
BASE_VERSION = "2.0.3"
RELEASE_URL = ("https://github.com/TwilitRealm/dusklight/releases/download/v2.0.3/"
               "Dusklight-v2.0.3-win32-x86_64.zip")
RELEASE_SHA256 = "ac63514eeb13cd4e9bd22be02dabee4952d1331a78f0496326bba2b577b12caf"
VCPKG_REVISION = "5f96cd15fd745122cf27e0524606d6c1efc5fd07"
SUBMODULES = {
    "extern/aurora": "3227d76c60e1e782ca576610bce61c9e7744d8be",
    "extern/borealis": "4ac5e7052a8c49a122f8d57f626b5c75c5ca6968",
    "mods/cosmetics": "fd93491089fa95441789244f2b2fd22a451ecbcc",
    "mods/randomizer": "1f24e81303536db4b77f49d057a8ec1f11162000",
}
PORTS = {
    "brotli": "1.2.0", "bzip2": "1.0.8#6", "freetype": "2.14.3",
    "libpng": "1.6.58", "vcpkg-cmake": "2025-08-07",
    "vcpkg-cmake-config": "2026-07-21", "zlib": "1.3.2#2", "zstd": "1.5.7",
}
ADDED_RESOURCES = {
    "res/NanumGothic-Regular.ttf",
    "res/localization/NanumGothic-OFL.txt",
    "res/localization/FONT_PROVENANCE.txt",
    "res/localization/ko.json",
}
UI_SOURCES = {
    "src/dusk/ui/localization.hpp", "src/dusk/ui/korean.hpp",
    "src/dusk/ui/korean_controls.hpp",
    "src/dusk/ui/korean_strings.inc",
    "src/dusk/ui/settings.cpp", "src/dusk/ui/prelaunch.cpp",
    "src/dusk/ui/menu_bar.cpp", "src/dusk/ui/controller_config.cpp",
    "src/dusk/ui/graphics_tuner.cpp", "src/dusk/ui/graphics_tuner.hpp",
    "src/dusk/ui/preset.cpp", "src/dusk/ui/tab_bar.cpp",
    "src/dusk/ui/tab_bar.hpp", "src/dusk/ui/ui.cpp",
}
PACKAGE_ADDITIONS = ADDED_RESOURCES | {"KOREAN_UI.md", "BUILD_INFO.json", "VERIFICATION.json"}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def git(repo: Path, *arguments: str) -> str:
    result = subprocess.run(["git", "-C", str(repo), *arguments], check=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    return result.stdout.decode("utf-8").strip()


def allowed_source(path: str) -> bool:
    return (path in UI_SOURCES or path in ADDED_RESOURCES
            or path in {"KOREAN_UI.md", "README.md", ".github/workflows/build.yml",
                        ".github/workflows/korean-windows.yml"}
            or path.startswith("tools/korean/")
            or path.startswith("tests/korean_"))


def audit_source(repo: Path, *, require_clean: bool = True,
                 require_submodules: bool = True) -> dict:
    git(repo, "merge-base", "--is-ancestor", BASE_REVISION, "HEAD")
    source_revision = git(repo, "rev-parse", "HEAD")
    changes = git(repo, "diff", "--name-only", BASE_REVISION, "--").splitlines()
    forbidden = [path for path in changes if not allowed_source(path)]
    if forbidden:
        raise VerificationError(f"Changes outside the Korean UI/build scope: {forbidden}")
    # New localization/helper files are allowed during a local review, but CI
    # packaging must use committed source, never an unrecorded worktree edit.
    untracked = git(repo, "ls-files", "--others", "--exclude-standard").splitlines()
    relevant_untracked = [path for path in untracked if allowed_source(path)]
    tracked_dirty = git(repo, "status", "--porcelain", "--untracked-files=no")
    if require_clean and (tracked_dirty or relevant_untracked):
        raise VerificationError("Commit the Korean UI sources before producing a release package")
    all_changes = sorted(set(changes + relevant_untracked))
    module_report = {}
    for path, revision in SUBMODULES.items():
        tree = git(repo, "ls-tree", "HEAD", "--", path).split()
        if len(tree) < 3 or tree[2] != revision:
            raise VerificationError(f"Pinned 2.0.3 submodule changed: {path}")
        if require_submodules:
            actual = git(repo / path, "rev-parse", "HEAD")
            if actual != revision:
                raise VerificationError(f"Wrong checkout for {path}: {actual}")
            if git(repo / path, "status", "--porcelain", "--untracked-files=no"):
                raise VerificationError(f"Modified submodule: {path}")
        module_report[path] = revision
    return {"baseline_revision": BASE_REVISION, "source_revision": source_revision,
            "source_clean": not bool(tracked_dirty or relevant_untracked),
            "changed_source_files": all_changes, "submodules": module_report,
            "game_sdk_and_engine_sources_unchanged": True}


def safe_zip_files(data: bytes, label: str) -> dict[str, bytes]:
    files = {}
    case_names = set()
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        total = 0
        for entry in archive.infolist():
            # ZipInfo normalizes Windows separators and truncates NUL bytes
            # while reading too. Check the raw name before trusting filename.
            if entry.orig_filename != entry.filename:
                raise VerificationError(f"Unsafe ZIP entry in {label}: {entry.orig_filename!r}")
            if entry.is_dir():
                continue
            path = PurePosixPath(entry.filename)
            if (path.is_absolute() or ".." in path.parts or ":" in entry.filename
                    or "\\" in entry.filename or str(path) != entry.filename
                    or stat.S_ISLNK(entry.external_attr >> 16)):
                raise VerificationError(f"Unsafe ZIP entry in {label}: {entry.filename}")
            folded = entry.filename.casefold()
            if folded in case_names:
                raise VerificationError(f"Duplicate ZIP entry in {label}: {entry.filename}")
            case_names.add(folded)
            total += entry.file_size
            if total > 1024 * 1024 * 1024 or entry.file_size > 512 * 1024 * 1024:
                raise VerificationError(f"Unexpectedly large ZIP contents in {label}")
            files[entry.filename] = archive.read(entry)
    return files


def original_files(path: Path) -> dict[str, bytes]:
    data = path.read_bytes()
    actual = sha256(data)
    if actual != RELEASE_SHA256:
        raise VerificationError(f"Official 2.0.3 ZIP hash mismatch: {actual}")
    return safe_zip_files(data, path.name)


def mod_images(files: dict[str, bytes]) -> list[PEImage]:
    images = []
    for name, data in sorted(files.items()):
        if name.startswith("mods/") and name.endswith(".dusk"):
            for module, content in safe_zip_files(data, name).items():
                if module.startswith("lib/windows-amd64/") and module.endswith(".dll"):
                    images.append(PEImage(content, f"{name}:{module}"))
    return images


def original_images(files: dict[str, bytes]) -> tuple[dict[str, PEImage], list[PEImage]]:
    providers = {name.lower(): PEImage(data, name) for name, data in files.items()
                 if "/" not in name and name.lower().endswith((".exe", ".dll"))}
    return providers, mod_images(files)


def collect_system_imports(images: list[PEImage], providers: dict[str, PEImage]) -> dict:
    result: dict[str, set[str | int]] = {}
    for image in images:
        for library, symbols in image.imports.items():
            if library not in providers:
                result.setdefault(library, set()).update(symbols)
    return result


def verify_executable(files: dict[str, bytes], executable: bytes,
                      tracker: Path | None = None) -> dict:
    providers, bundled = original_images(files)
    original = providers["dusklight.exe"]
    rebuilt = PEImage(executable, "Korean dusklight.exe")
    host_report = compare_host(original, rebuilt)
    system = collect_system_imports(list(providers.values()) + bundled, providers)
    providers["dusklight.exe"] = rebuilt
    imports = [verify_imports(rebuilt, providers, system)]
    for module in bundled:
        imports.append(verify_imports(module, providers, system, original_host=original))
    tracker_checked = []
    if tracker:
        tracker_files = safe_zip_files(tracker.read_bytes(), tracker.name)
        modules = [(name, data) for name, data in tracker_files.items()
                   if name.startswith("lib/windows-amd64/") and name.endswith(".dll")]
        if not modules:
            raise VerificationError("The supplied tracker package has no Windows amd64 module")
        for name, data in modules:
            image = PEImage(data, f"{tracker.name}:{name}")
            # Its OS imports are already part of a working independently built
            # mod. The compatibility question here is the replaced host/DLLs.
            tracker_system = {library: symbols for library, symbols in image.imports.items()
                              if library not in providers}
            tracker_checked.append(verify_imports(image, providers, tracker_system,
                                                  original_host=original))
    return {"host": host_report, "imports": imports,
            "tracker_import_checks": tracker_checked,
            "live_gameplay_tested": False}


def verify_file_inventory(original: dict[str, bytes], package: dict[str, bytes]) -> dict:
    missing = original.keys() - package.keys()
    if missing:
        raise VerificationError(f"Original package files are missing: {sorted(missing)}")
    unexpected = package.keys() - original.keys() - PACKAGE_ADDITIONS
    if unexpected:
        raise VerificationError(f"Unexpected package additions: {sorted(unexpected)}")
    required = PACKAGE_ADDITIONS - package.keys()
    if required:
        raise VerificationError(f"Required Korean package files are missing: {sorted(required)}")
    changed = [name for name, data in original.items() if package[name] != data]
    if any(name != "dusklight.exe" for name in changed):
        raise VerificationError(f"Original runtime/resource files changed: {changed}")
    return {"original_file_count": len(original),
            "preserved_files": {name: sha256(data) for name, data in sorted(original.items())
                                if name != "dusklight.exe"},
            "changed_original_files": changed,
            "added_files": sorted(package.keys() - original.keys())}


def package(args: argparse.Namespace) -> None:
    repo = args.repo.resolve()
    source = audit_source(repo)
    original = original_files(args.original)
    executable = args.executable.read_bytes()
    verification = verify_executable(original, executable, args.tracker)
    files = dict(original)
    files["dusklight.exe"] = executable
    for name in sorted(ADDED_RESOURCES | {"KOREAN_UI.md"}):
        path = repo / name
        if path.is_symlink() or not path.is_file():
            raise VerificationError(f"Missing regular resource file: {name}")
        if name in original:
            raise VerificationError(f"Localization unexpectedly overwrites an original resource: {name}")
        files[name] = path.read_bytes()
    # Parse the displayed-language resource before packaging; the source's
    # localization-specific checks provide the more detailed contract.
    json.loads(files["res/localization/ko.json"].decode("utf-8"))
    toolchain = json.loads(args.toolchain.read_text(encoding="utf-8-sig"))
    info = {
        "package_format": 1, "target_dusklight": BASE_VERSION,
        "variant": "unofficial Korean UI", "source": source,
        "original_release_url": RELEASE_URL, "original_release_sha256": RELEASE_SHA256,
        "executable_sha256": sha256(executable), "toolchain": toolchain,
        "dependency_registry_revision": VCPKG_REVISION,
        "dependency_ports": PORTS,
        "runtime_dlls_bundled_mods_and_original_resources": "original 2.0.3 bytes",
        "settings_and_save_directory_identity": "TwilitRealm/Dusklight",
        "sentry_dsn": "empty; upstream private credential is not copied",
        "live_gameplay_tested": False,
    }
    files["BUILD_INFO.json"] = json_bytes(info)
    files["VERIFICATION.json"] = b"{}\n"
    verification["inventory"] = verify_file_inventory(original, files)
    files["VERIFICATION.json"] = json_bytes(verification)
    args.output.mkdir(parents=True, exist_ok=True)
    archive_path = args.output / "Dusklight-2.0.3-Korean-windows-x64.zip"
    with zipfile.ZipFile(archive_path, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        for name, data in sorted(files.items()):
            entry = zipfile.ZipInfo(name, date_time=(2026, 9, 30, 0, 0, 0))
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o100644 << 16
            archive.writestr(entry, data)
    # Verify the serialized ZIP, not just the intended input dictionary.
    serialized = safe_zip_files(archive_path.read_bytes(), archive_path.name)
    verify_file_inventory(original, serialized)
    if serialized != files:
        raise VerificationError("Serialized package differs from verified input files")
    digest = sha256(archive_path.read_bytes())
    (args.output / (archive_path.name + ".sha256")).write_text(
        f"{digest}  {archive_path.name}\n", encoding="ascii")
    info["package"] = archive_path.name
    info["sha256"] = digest
    (args.output / "BUILD_INFO.json").write_bytes(json_bytes(info))
    (args.output / "VERIFICATION.json").write_bytes(json_bytes(verification))
    print(f"PASS: {archive_path.name}")
    print(f"SHA256: {digest}")
    print(f"Preserved original files: {len(original) - 1}; only dusklight.exe replaced")
    print(f"Preserved host exports: {verification['host']['preserved_original_named_exports']}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    audit = subparsers.add_parser("audit")
    audit.add_argument("--repo", type=Path, required=True)
    audit.add_argument("--allow-dirty", action="store_true")
    audit.add_argument("--skip-submodule-checkouts", action="store_true")
    audit.add_argument("--output", type=Path)
    ports = subparsers.add_parser("ports")
    ports.add_argument("--installed", type=Path, required=True,
                       help="Output of vcpkg list, in UTF-8")
    verify = subparsers.add_parser("verify-executable")
    verify.add_argument("--original", type=Path, required=True)
    verify.add_argument("--executable", type=Path, required=True)
    verify.add_argument("--tracker", type=Path)
    verify.add_argument("--output", type=Path)
    create = subparsers.add_parser("package")
    create.add_argument("--repo", type=Path, required=True)
    create.add_argument("--original", type=Path, required=True)
    create.add_argument("--executable", type=Path, required=True)
    create.add_argument("--toolchain", type=Path, required=True)
    create.add_argument("--output", type=Path, required=True)
    create.add_argument("--tracker", type=Path)
    args = parser.parse_args()
    if args.command == "audit":
        report = audit_source(args.repo, require_clean=not args.allow_dirty,
                              require_submodules=not args.skip_submodule_checkouts)
        if args.output:
            args.output.parent.mkdir(parents=True, exist_ok=True)
            args.output.write_bytes(json_bytes(report))
        print(json.dumps(report, ensure_ascii=False, indent=2))
    elif args.command == "ports":
        actual = {}
        for line in args.installed.read_text(encoding="utf-8-sig").splitlines():
            columns = line.split()
            if len(columns) > 1 and columns[0].endswith(":x64-windows") and "[" not in columns[0]:
                actual[columns[0].split(":")[0]] = columns[1]
        if actual != PORTS:
            raise VerificationError(f"Pinned dependency ports differ: expected {PORTS}; got {actual}")
        print("PASS: all 8 dependency ports match the official 2.0.3 build")
    elif args.command == "verify-executable":
        report = verify_executable(original_files(args.original), args.executable.read_bytes(), args.tracker)
        if args.output:
            args.output.write_bytes(json_bytes(report))
        print(json.dumps(report, ensure_ascii=False, indent=2))
    else:
        package(args)


if __name__ == "__main__":
    try:
        main()
    except (VerificationError, OSError, subprocess.CalledProcessError,
            json.JSONDecodeError, zipfile.BadZipFile) as error:
        print(f"ERROR: {error}", file=sys.stderr)
        sys.exit(1)

"""Small, read-only PE checks for the Korean Windows package (stdlib only).

This reads PE metadata; it never loads an executable or DLL. Export addresses
may change when the executable is rebuilt, but names and import availability
must remain compatible with the original 2.0.3 package.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass


class VerificationError(ValueError):
    pass


@dataclass(frozen=True)
class Export:
    ordinal: int
    rva: int
    forwarder: str | None


class PEImage:
    def __init__(self, data: bytes, label: str = "PE image") -> None:
        self.data = data
        self.label = label
        if self.read(0, 2) != b"MZ":
            self.fail("missing DOS signature")
        pe = self.unpack("<I", 0x3C)[0]
        if self.read(pe, 4) != b"PE\0\0":
            self.fail("missing PE signature")
        self.machine, count, _, _, _, optional_size, _ = self.unpack("<HHIIIHH", pe + 4)
        optional = pe + 24
        if self.unpack("<H", optional)[0] != 0x20B:
            self.fail("expected a PE32+ image")
        if count > 96 or optional_size < 112:
            self.fail("invalid PE headers")
        self.linker_version = list(self.unpack("<BB", optional + 2))
        self.image_base = self.unpack("<Q", optional + 24)[0]
        self.header_size = self.unpack("<I", optional + 60)[0]
        self.sections: dict[str, tuple[int, int, int, int]] = {}
        for index in range(count):
            at = optional + optional_size + 40 * index
            name = self.read(at, 8).split(b"\0", 1)[0].decode("ascii", errors="strict")
            virtual_size, rva, raw_size, raw_offset = self.unpack("<IIII", at + 8)
            self.read(raw_offset, raw_size)
            if name in self.sections:
                self.fail(f"duplicate section {name}")
            self.sections[name] = (rva, virtual_size, raw_offset, raw_size)
        directory_count = self.unpack("<I", optional + 108)[0]
        if directory_count > 16 or 112 + directory_count * 8 > optional_size:
            self.fail("invalid data directories")
        self.directories = [self.unpack("<II", optional + 112 + i * 8)
                            for i in range(directory_count)]
        self.export_name = ""
        self.exports: dict[str, Export] = {}
        self.ordinals: dict[int, Export] = {}
        self.imports: dict[str, set[str | int]] = {}
        self._read_exports()
        self._read_imports()
        self._read_delay_imports()

    def fail(self, message: str) -> None:
        raise VerificationError(f"{self.label}: {message}")

    def read(self, offset: int, length: int) -> bytes:
        if offset < 0 or length < 0 or offset + length > len(self.data):
            self.fail("PE offset is outside the file")
        return self.data[offset:offset + length]

    def unpack(self, fmt: str, offset: int) -> tuple:
        return struct.unpack(fmt, self.read(offset, struct.calcsize(fmt)))

    def offset(self, rva: int, size: int = 1) -> int:
        if 0 <= rva < self.header_size and rva + size <= self.header_size:
            self.read(rva, size)
            return rva
        for start, virtual_size, raw_offset, raw_size in self.sections.values():
            if start <= rva < start + max(virtual_size, raw_size):
                relative = rva - start
                if relative + size > raw_size:
                    self.fail("requested data has no on-disk PE bytes")
                self.read(raw_offset + relative, size)
                return raw_offset + relative
        self.fail(f"unmapped PE RVA {rva:#x}")
        raise AssertionError("unreachable")

    def string(self, rva: int, limit: int = 65536) -> str:
        start = self.offset(rva)
        end = self.data.find(b"\0", start, min(len(self.data), start + limit))
        if end < 0:
            self.fail("unterminated PE string")
        try:
            return self.data[start:end].decode("ascii")
        except UnicodeDecodeError as error:
            raise VerificationError(f"{self.label}: invalid PE metadata string") from error

    def directory(self, index: int) -> tuple[int, int]:
        return self.directories[index] if index < len(self.directories) else (0, 0)

    def _read_exports(self) -> None:
        start, size = self.directory(0)
        if not start:
            return
        values = self.unpack("<IIHHIIIIIII", self.offset(start, 40))
        _, _, _, _, name, base, function_count, name_count, functions, names, ordinals = values
        self.export_name = self.string(name)
        if function_count > 1000000 or name_count > function_count:
            self.fail("invalid export count")
        function_table = self.offset(functions, function_count * 4)
        for index in range(function_count):
            rva = self.unpack("<I", function_table + index * 4)[0]
            if rva:
                forwarder = self.string(rva) if start <= rva < start + size else None
                self.ordinals[base + index] = Export(base + index, rva, forwarder)
        names_at = self.offset(names, name_count * 4)
        ordinals_at = self.offset(ordinals, name_count * 2)
        for index in range(name_count):
            name_rva = self.unpack("<I", names_at + index * 4)[0]
            ordinal = base + self.unpack("<H", ordinals_at + index * 2)[0]
            name_text = self.string(name_rva)
            if ordinal not in self.ordinals or name_text in self.exports:
                self.fail("invalid or duplicate named export")
            self.exports[name_text] = self.ordinals[ordinal]

    def _thunks(self, library: str, rva: int) -> None:
        imported = self.imports.setdefault(library.lower(), set())
        for index in range(1000000):
            value = self.unpack("<Q", self.offset(rva + index * 8, 8))[0]
            if not value:
                return
            if value & (1 << 63):
                imported.add(value & 0xFFFF)
            else:
                imported.add(self.string(value + 2))  # skip the import hint
        self.fail("unterminated import thunk table")

    def _read_imports(self) -> None:
        start, size = self.directory(1)
        if not start:
            return
        for index in range(size // 20):
            values = self.unpack("<IIIII", self.offset(start + index * 20, 20))
            if not any(values):
                return
            lookup, _, _, name, first = values
            self._thunks(self.string(name), lookup or first)
        self.fail("unterminated import descriptor table")

    def _read_delay_imports(self) -> None:
        start, size = self.directory(13)
        if not start:
            return
        for index in range(size // 32):
            values = self.unpack("<IIIIIIII", self.offset(start + index * 32, 32))
            if not any(values):
                return
            attributes, name, _, first, lookup, _, _, _ = values
            if attributes != 1:
                self.fail("unsupported non-RVA delay import descriptor")
            self._thunks(self.string(name), lookup or first)
        self.fail("unterminated delay import descriptor table")

    def version_resource(self) -> bytes:
        """Return the unchanged RT_VERSION resource, independent of section RVAs."""
        base, _ = self.directory(2)
        if not base:
            self.fail("version resource is missing")

        def entries(relative: int) -> list[tuple[int, int]]:
            at = self.offset(base + relative, 16)
            named, numbered = self.unpack("<HH", at + 12)
            count = named + numbered
            if count > 65536:
                self.fail("invalid resource count")
            return [self.unpack("<II", self.offset(base + relative + 16 + i * 8, 8))
                    for i in range(count)]

        versions = [value for name, value in entries(0) if name == 16]
        if len(versions) != 1 or not versions[0] & 0x80000000:
            self.fail("expected one version resource tree")
        pending = [(versions[0] & 0x7FFFFFFF, 0)]
        resources = []
        while pending:
            relative, depth = pending.pop()
            if depth > 4:
                self.fail("version resource tree is too deep")
            for _, value in entries(relative):
                if value & 0x80000000:
                    pending.append((value & 0x7FFFFFFF, depth + 1))
                else:
                    rva, size, _, _ = self.unpack("<IIII", self.offset(base + value, 16))
                    resources.append(self.read(self.offset(rva, size), size))
        if len(resources) != 1:
            self.fail("expected one version resource payload")
        return resources[0]


def compare_host(original: PEImage, rebuilt: PEImage) -> dict:
    if rebuilt.machine != original.machine or rebuilt.machine != 0x8664:
        raise VerificationError("The rebuilt host must remain Windows x64")
    if rebuilt.export_name.lower() != "dusklight.exe":
        raise VerificationError("The exported executable identity must remain dusklight.exe")
    missing = original.exports.keys() - rebuilt.exports.keys()
    if missing:
        raise VerificationError(f"Original exports were removed ({len(missing)}): {sorted(missing)[:12]}")
    changed_forwarders = [name for name, item in original.exports.items()
                          if item.forwarder != rebuilt.exports[name].forwarder]
    if changed_forwarders:
        raise VerificationError(f"Original export forwarders changed: {changed_forwarders[:12]}")
    if original.version_resource() != rebuilt.version_resource():
        raise VerificationError("Windows executable version/identity metadata differs from official 2.0.3")
    for name in (".symdb", ".symdbh"):
        if name not in rebuilt.sections or not rebuilt.sections[name][3]:
            raise VerificationError(f"The rebuilt host lacks its generated {name} section")
    return {
        "machine": "x86_64",
        "original_named_exports": len(original.exports),
        "rebuilt_named_exports": len(rebuilt.exports),
        "preserved_original_named_exports": len(original.exports),
        "added_named_exports": sorted(rebuilt.exports.keys() - original.exports.keys()),
        "ordinal_changes": sum(item.ordinal != rebuilt.exports[name].ordinal
                               for name, item in original.exports.items()),
        "version_resource_identical": True,
        "symbol_manifest_present": True,
        "original_linker_version": original.linker_version,
        "rebuilt_linker_version": rebuilt.linker_version,
    }


def verify_imports(image: PEImage, providers: dict[str, PEImage],
                   allowed_system_imports: dict[str, set[str | int]],
                   *, original_host: PEImage | None = None) -> dict:
    """Resolve imports against original DLLs or the system imports of the baseline.

    New imports from system DLLs are rejected too: a newer compiler must not
    silently increase the Windows runtime requirements of this package.
    """
    checked = 0
    for library, symbols in image.imports.items():
        provider = providers.get(library)
        if provider:
            for symbol in symbols:
                entry = (provider.ordinals.get(symbol) if isinstance(symbol, int)
                         else provider.exports.get(symbol))
                if entry is None:
                    raise VerificationError(f"{image.label}: unavailable import {library}!{symbol}")
                if isinstance(symbol, int) and library == "dusklight.exe" and original_host:
                    old_names = {name for name, value in original_host.exports.items()
                                 if value.ordinal == symbol}
                    new_names = {name for name, value in provider.exports.items()
                                 if value.ordinal == symbol}
                    if old_names != new_names:
                        raise VerificationError(f"{image.label}: host ordinal {symbol} changed meaning")
                checked += 1
        else:
            missing = symbols - allowed_system_imports.get(library, set())
            if missing:
                sample = sorted((str(value) for value in missing))[:12]
                raise VerificationError(f"{image.label}: new system imports from {library}: {sample}")
            checked += len(symbols)
    return {"image": image.label, "imported_libraries": sorted(image.imports),
            "resolved_imports": checked}

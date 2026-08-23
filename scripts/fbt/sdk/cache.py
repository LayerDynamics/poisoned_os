import csv
import operator
import os
import struct
import zlib
from dataclasses import dataclass
from enum import Enum, auto
from typing import Any, ClassVar, Iterable, Set

from ansi.color import fg

from . import ApiEntries, ApiEntryFunction, ApiEntryVariable, ApiHeader


EXTERNAL_API_TABLE_MAGIC = b"FAPI"
EXTERNAL_API_TABLE_FORMAT = 1
EXTERNAL_API_TABLE_HEADER = struct.Struct("<4sHHIII")
EXTERNAL_API_TABLE_ENTRY = struct.Struct("<II")


class ExternalApiTableError(ValueError):
    """Raised when an external firmware API lookup table is malformed."""


def elf_gnu_hash(name: str) -> int:
    value = 0x1505
    for byte in name.encode("utf-8"):
        value = ((value << 5) + value + byte) & 0xFFFFFFFF
    return value


def encode_external_api_table(
    api_version: int,
    symbols: Iterable[tuple[str, int]],
) -> bytes:
    if not 0 <= api_version <= 0xFFFFFFFF:
        raise ExternalApiTableError("API version is outside uint32 range")

    entries = sorted((elf_gnu_hash(name), address) for name, address in symbols)
    for index, (symbol_hash, address) in enumerate(entries):
        if not 0 <= address <= 0xFFFFFFFF:
            raise ExternalApiTableError("symbol address is outside uint32 range")
        if index and entries[index - 1][0] == symbol_hash:
            raise ExternalApiTableError(
                f"firmware API hash collision: 0x{symbol_hash:08x}"
            )

    encoded_entries = b"".join(
        EXTERNAL_API_TABLE_ENTRY.pack(symbol_hash, address)
        for symbol_hash, address in entries
    )
    header = EXTERNAL_API_TABLE_HEADER.pack(
        EXTERNAL_API_TABLE_MAGIC,
        EXTERNAL_API_TABLE_FORMAT,
        EXTERNAL_API_TABLE_HEADER.size,
        api_version,
        len(entries),
        zlib.crc32(encoded_entries) & 0xFFFFFFFF,
    )
    return header + encoded_entries


def decode_external_api_table(data: bytes) -> tuple[int, list[tuple[int, int]]]:
    if len(data) < EXTERNAL_API_TABLE_HEADER.size:
        raise ExternalApiTableError("firmware API table header is truncated")

    magic, format_version, header_size, api_version, entry_count, expected_crc = (
        EXTERNAL_API_TABLE_HEADER.unpack_from(data)
    )
    if magic != EXTERNAL_API_TABLE_MAGIC:
        raise ExternalApiTableError("firmware API table magic is invalid")
    if format_version != EXTERNAL_API_TABLE_FORMAT:
        raise ExternalApiTableError("firmware API table format is unsupported")
    if header_size != EXTERNAL_API_TABLE_HEADER.size:
        raise ExternalApiTableError("firmware API table header size is invalid")

    expected_size = header_size + entry_count * EXTERNAL_API_TABLE_ENTRY.size
    if len(data) != expected_size:
        raise ExternalApiTableError("firmware API table size does not match its header")

    encoded_entries = data[header_size:]
    if zlib.crc32(encoded_entries) & 0xFFFFFFFF != expected_crc:
        raise ExternalApiTableError("firmware API table checksum is invalid")

    entries = list(EXTERNAL_API_TABLE_ENTRY.iter_unpack(encoded_entries))
    if any(
        entries[index - 1][0] >= entries[index][0] for index in range(1, len(entries))
    ):
        raise ExternalApiTableError("firmware API table hashes are not strictly sorted")
    return api_version, entries


@dataclass(frozen=True)
class SdkVersion:
    major: int = 0
    minor: int = 0

    csv_type: ClassVar[str] = "Version"

    def __str__(self) -> str:
        return f"{self.major}.{self.minor}"

    def as_int(self) -> int:
        return ((self.major & 0xFFFF) << 16) | (self.minor & 0xFFFF)

    @staticmethod
    def from_str(s: str) -> "SdkVersion":
        major, minor = s.split(".")
        return SdkVersion(int(major), int(minor))

    def dictify(self) -> dict:
        return dict(name=str(self), type=None, params=None)


class VersionBump(Enum):
    NONE = auto()
    MAJOR = auto()
    MINOR = auto()


class ApiEntryState(Enum):
    PENDING = "?"
    APPROVED = "+"
    DISABLED = "-"
    # Special value for API version entry so users have less incentive to edit it
    VERSION_PENDING = "v"


# Class that stores all known API entries, both enabled and disabled.
# Also keeps track of API versioning
# Allows comparison and update from newly-generated API
class SdkCache:
    CSV_FIELD_NAMES = ("entry", "status", "name", "type", "params")

    def __init__(self, cache_file: str, load_version_only=False):
        self.cache_file_name = cache_file
        self.version = SdkVersion(0, 0)
        self.sdk = ApiEntries()
        self.disabled_entries = set()
        self.new_entries = set()
        self.loaded_dirty_version = False

        self.version_action = VersionBump.NONE
        self._load_version_only = load_version_only
        self.load_cache()

    def is_buildable(self) -> bool:
        return (
            self.version != SdkVersion(0, 0)
            and self.version_action == VersionBump.NONE
            and not self._have_pending_entries()
        )

    def _filter_enabled(self, sdk_entries):
        return sorted(
            filter(lambda e: e not in self.disabled_entries, sdk_entries),
            key=operator.attrgetter("name"),
        )

    def get_valid_names(self):
        syms = set(map(lambda e: e.name, self.get_functions()))
        syms.update(map(lambda e: e.name, self.get_variables()))
        return syms

    def get_disabled_names(self):
        return set(map(lambda e: e.name, self.disabled_entries))

    def get_functions(self):
        return self._filter_enabled(self.sdk.functions)

    def get_variables(self):
        return self._filter_enabled(self.sdk.variables)

    def get_headers(self):
        return self._filter_enabled(self.sdk.headers)

    def _get_entry_status(self, entry) -> str:
        if entry in self.disabled_entries:
            return ApiEntryState.DISABLED
        elif entry in self.new_entries:
            if isinstance(entry, SdkVersion):
                return ApiEntryState.VERSION_PENDING
            return ApiEntryState.PENDING
        else:
            return ApiEntryState.APPROVED

    def _format_entry(self, obj):
        obj_dict = obj.dictify()
        obj_dict.update(
            dict(
                entry=obj.csv_type,
                status=self._get_entry_status(obj).value,
            )
        )
        return obj_dict

    def save(self) -> None:
        if self._load_version_only:
            raise Exception("Only SDK version was loaded, cannot save")

        if self.version_action == VersionBump.MINOR:
            self.version = SdkVersion(self.version.major, self.version.minor + 1)
        elif self.version_action == VersionBump.MAJOR:
            self.version = SdkVersion(self.version.major + 1, 0)

        if self._have_pending_entries():
            self.new_entries.add(self.version)
            print(
                fg.red(
                    f"API version is still WIP: {self.version}. Review the changes and re-run command."
                )
            )
            print("CSV file entries to mark up:")
            print(
                fg.yellow(
                    "\n".join(
                        map(
                            str,
                            filter(
                                lambda e: not isinstance(e, SdkVersion),
                                self.new_entries,
                            ),
                        )
                    )
                )
            )
        else:
            print(fg.green(f"API version {self.version} is up to date"))

        regenerate_csv = (
            self.loaded_dirty_version
            or self._have_pending_entries()
            or self.version_action != VersionBump.NONE
        )

        if regenerate_csv:
            str_cache_entries = [self.version]
            name_getter = operator.attrgetter("name")
            str_cache_entries.extend(sorted(self.sdk.headers, key=name_getter))
            str_cache_entries.extend(sorted(self.sdk.functions, key=name_getter))
            str_cache_entries.extend(sorted(self.sdk.variables, key=name_getter))

            with open(self.cache_file_name, "wt", newline="") as f:
                writer = csv.DictWriter(f, fieldnames=SdkCache.CSV_FIELD_NAMES)
                writer.writeheader()

                for entry in str_cache_entries:
                    writer.writerow(self._format_entry(entry))

    def _process_entry(self, entry_dict: dict) -> None:
        entry_class = entry_dict["entry"]
        entry_status = entry_dict["status"]
        entry_name = entry_dict["name"]

        entry = None
        if entry_class == SdkVersion.csv_type:
            self.version = SdkVersion.from_str(entry_name)
            if entry_status == ApiEntryState.VERSION_PENDING.value:
                self.loaded_dirty_version = True
        elif entry_class == ApiHeader.csv_type:
            self.sdk.headers.add(entry := ApiHeader(entry_name))
        elif entry_class == ApiEntryFunction.csv_type:
            self.sdk.functions.add(
                entry := ApiEntryFunction(
                    entry_name,
                    entry_dict["type"],
                    entry_dict["params"],
                )
            )
        elif entry_class == ApiEntryVariable.csv_type:
            self.sdk.variables.add(
                entry := ApiEntryVariable(entry_name, entry_dict["type"])
            )
        else:
            print(entry_dict)
            raise Exception("Unknown entry type: %s" % entry_class)

        if entry is None:
            return

        if entry_status == ApiEntryState.DISABLED.value:
            self.disabled_entries.add(entry)
        elif entry_status == ApiEntryState.PENDING.value:
            self.new_entries.add(entry)

    def load_cache(self) -> None:
        if not os.path.exists(self.cache_file_name):
            raise Exception(
                f"Cannot load symbol cache '{self.cache_file_name}'! File does not exist"
            )

        with open(self.cache_file_name, "rt") as f:
            reader = csv.DictReader(f)
            for row in reader:
                self._process_entry(row)
                if self._load_version_only and row.get("entry") == SdkVersion.csv_type:
                    break

    def _have_pending_entries(self) -> bool:
        return any(
            filter(
                lambda e: not isinstance(e, SdkVersion),
                self.new_entries,
            )
        )

    def sync_sets(
        self, known_set: Set[Any], new_set: Set[Any], update_version: bool = True
    ):
        new_entries = new_set - known_set
        if new_entries:
            print(f"New: {new_entries}")
            known_set |= new_entries
            self.new_entries |= new_entries
            if update_version and self.version_action == VersionBump.NONE:
                self.version_action = VersionBump.MINOR
        removed_entries = known_set - new_set
        if removed_entries:
            print(f"Removed: {removed_entries}")
            self.loaded_dirty_version = True
            known_set -= removed_entries
            # If any of removed entries was a part of active API, that's a major bump
            if update_version and any(
                filter(
                    lambda e: e not in self.disabled_entries
                    and e not in self.new_entries,
                    removed_entries,
                )
            ):
                self.version_action = VersionBump.MAJOR
            self.disabled_entries -= removed_entries
            self.new_entries -= removed_entries

    def validate_api(self, api: ApiEntries) -> None:
        self.sync_sets(self.sdk.headers, api.headers, False)
        self.sync_sets(self.sdk.functions, api.functions)
        self.sync_sets(self.sdk.variables, api.variables)


class LazySdkVersionLoader:
    def __init__(self, sdk_path: str):
        self.sdk_path = sdk_path
        self._version = None

    @property
    def version(self) -> SdkVersion:
        if self._version is None:
            self._version = SdkCache(self.sdk_path, load_version_only=True).version
        return self._version

    def __str__(self) -> str:
        return str(self.version)

"""Safe Steam non-Steam shortcut helper for Steam Input gamepad emulation.

Default mode is a read-only dry run: it reports the exact Steam Target,
Start In folder and Launch Options for a direct SimpsonsNative.exe entry
and checks whether that exact owned entry already exists. It never writes
to Steam files, never stops Steam, and never launches the game.

Optional ``--install`` performs an explicit, audited edit of shortcuts.vdf
only when Steam is fully closed. It preserves every pre-existing entry
byte-for-byte (new bytes are appended before the final terminator),
refuses ambiguous layouts, backs up the original, replaces atomically,
and re-verifies the result. Do not run ``--install`` while Steam is open:
Steam rewrites shortcuts.vdf on exit and would discard or corrupt the edit.

No Steamworks SDK or app ID is used. The new entry keeps Steam's own
non-Steam shortcut appid scheme (CRC32 of Exe+AppName with the top bit
set). No Spacewar or other borrowed app ID is involved.
"""

from __future__ import annotations

import argparse
import datetime
import json
import os
import struct
import subprocess
import sys
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
APP_NAME = "The Simpsons Game (Native)"
DEFAULT_STEAM_ROOT = Path("C:/Program Files (x86)/Steam")
DEFAULT_RECORD = ROOT / "build" / "steam-input-shortcut.json"


class SetupError(RuntimeError):
    pass


# ---------------------------------------------------------------- config

def load_recorded_selection(root: Path = ROOT) -> dict:
    """Return only the small selection fields from config/startup_replay.json.

    The file also contains a large ``screens`` cue table with pixel samples;
    callers must never dump or forward that table. Only the four selection
    fields below are needed to reproduce the recorded profile/content choice.
    """
    path = root / "config" / "startup_replay.json"
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as error:
        raise SetupError(f"Missing recorded selection file: {path}") from error
    except json.JSONDecodeError as error:
        raise SetupError(f"Recorded selection file is not valid JSON: {path}") from error
    selection = {
        "profile_store": config.get("profile_store"),
        "content_store": config.get("content_store"),
        "profile_id": config.get("profile_id"),
        "save_index": config.get("save_index"),
    }
    for key, value in selection.items():
        if not isinstance(value, str) or not value or "\x00" in value:
            raise SetupError(f"Recorded selection field {key!r} is missing or invalid.")
    return selection


def quote_launch_value(value: str) -> str:
    """Quote one launch-option value the way the MSVC CRT parses it.

    Uses :func:`subprocess.list2cmdline` (the standard CRT quoting rule,
    including doubling backslashes before the closing quote) and explicitly
    rejects embedded double quotes, NUL, and line breaks instead of guessing
    an escape that Steam or the game might parse differently.
    """
    if "\x00" in value or "\n" in value or "\r" in value:
        raise SetupError("Launch value contains a line break; refusing to quote it.")
    if '"' in value:
        raise SetupError("Launch value contains a double quote; refusing to quote it.")
    return subprocess.list2cmdline([value])


def build_launch_spec(root: Path = ROOT, selection: dict | None = None) -> dict:
    """Build the exact Steam Target / StartDir / LaunchOptions triple."""
    selection = selection if selection is not None else load_recorded_selection(root)
    exe = (root / "build" / "native" / "SimpsonsNative.exe").resolve()
    image = (root / "analysis" / "simpsons.pe").resolve()
    profile_dir = (root / selection["profile_store"]).resolve()
    content_dir = (root / selection["content_store"]).resolve()
    start_dir = root.resolve()

    missing = [p for p in (exe, image) if not p.is_file()]
    if missing:
        raise SetupError("Missing required game file(s): " + ", ".join(str(p) for p in missing))
    profile_file = profile_dir / (selection["profile_id"] + ".profile")
    if not profile_file.is_file():
        raise SetupError(f"The recorded Player profile is missing: {profile_file}")
    save_file = content_dir / selection["save_index"]
    if not save_file.is_file():
        raise SetupError(
            "The recorded existing save is missing; refusing to create a new game: "
            + str(save_file)
        )

    launch_options = " ".join(
        [
            "--image",
            quote_launch_value(str(image)),
            "--profile-store",
            quote_launch_value(str(profile_dir)),
            "--content-store",
            quote_launch_value(str(content_dir)),
            "--local-profile",
            "0:" + selection["profile_id"],
        ]
    )
    # Match the quoting style Steam already uses for non-Steam Exe fields:
    # the absolute path wrapped in one pair of double quotes.
    exe_field = '"' + str(exe) + '"'
    if '"' in str(exe) and not (str(exe).startswith('"') and str(exe).endswith('"')):
        raise SetupError("Game executable path contains a double quote; refusing to proceed.")
    return {
        "app_name": APP_NAME,
        "exe": exe_field,
        "start_dir": str(start_dir),
        "launch_options": launch_options,
        "appid_signed": shortcut_appid_signed(exe_field, APP_NAME),
        "image": str(image),
        "profile_dir": str(profile_dir),
        "content_dir": str(content_dir),
        "local_profile": "0:" + selection["profile_id"],
        "selection": selection,
    }


def shortcut_appid_signed(exe_field: str, app_name: str) -> int:
    """Steam non-Steam shortcut appid: signed32(CRC32(Exe+AppName) | 0x80000000)."""
    raw = zlib.crc32((exe_field + app_name).encode("utf-8")) & 0xFFFFFFFF
    raw |= 0x80000000
    return struct.unpack("<i", struct.pack("<I", raw))[0]


# ------------------------------------------------------- binary shortcuts.vdf

_TYPE_OBJECT = 0x00
_TYPE_STRING = 0x01
_TYPE_INT = 0x02
_TYPE_END = 0x08

# Ordered field list per entry: (type, key). Unknown layouts are refused
# instead of rewritten, so old Steam versions never lose data silently.
KNOWN_ENTRY_ORDER = (
    (_TYPE_INT, b"appid"),
    (_TYPE_STRING, b"AppName"),
    (_TYPE_STRING, b"Exe"),
    (_TYPE_STRING, b"StartDir"),
    (_TYPE_STRING, b"icon"),
    (_TYPE_STRING, b"ShortcutPath"),
    (_TYPE_STRING, b"LaunchOptions"),
    (_TYPE_INT, b"IsHidden"),
    (_TYPE_INT, b"AllowDesktopConfig"),
    (_TYPE_INT, b"AllowOverlay"),
    (_TYPE_INT, b"OpenVR"),
    (_TYPE_INT, b"Devkit"),
    (_TYPE_STRING, b"DevkitGameID"),
    (_TYPE_INT, b"DevkitOverrideAppID"),
    (_TYPE_INT, b"LastPlayTime"),
    (_TYPE_STRING, b"FlatpakAppID"),
    (_TYPE_STRING, b"sortas"),
)


def parse_shortcuts(data: bytes) -> tuple[list, bytes]:
    """Parse shortcuts.vdf into ordered entries; return (entries, raw).

    Each entry is (index_key: bytes, fields: list). A field is
    (type, key, value) where value is int for 0x02, bytes for 0x01, and a
    nested field list for 0x00 (only ``tags`` is expected). Anything else
    raises SetupError so the caller refuses the edit instead of corrupting it.
    """
    pos = 0

    def read_cstring(buf: bytes, at: int) -> tuple[bytes, int]:
        end = buf.find(b"\x00", at)
        if end < 0:
            raise SetupError("shortcuts.vdf ends inside a string.")
        return buf[at:end], end + 1

    def parse_object(buf: bytes, at: int) -> tuple[list, int]:
        fields: list = []
        while True:
            if at >= len(buf):
                raise SetupError("shortcuts.vdf ends inside an object.")
            kind = buf[at]
            at += 1
            if kind == _TYPE_END:
                return fields, at
            if kind not in (_TYPE_OBJECT, _TYPE_STRING, _TYPE_INT):
                raise SetupError(f"Unsupported value type 0x{kind:02x}; refusing to edit.")
            key, at = read_cstring(buf, at)
            if kind == _TYPE_OBJECT:
                children, at = parse_object(buf, at)
                fields.append((kind, key, children))
            elif kind == _TYPE_STRING:
                value, at = read_cstring(buf, at)
                fields.append((kind, key, value))
            else:
                if at + 4 > len(buf):
                    raise SetupError("shortcuts.vdf ends inside an integer.")
                fields.append((kind, key, int.from_bytes(buf[at:at + 4], "little", signed=True)))
                at += 4

    if not data:
        raise SetupError("shortcuts.vdf is empty; refusing to edit.")
    root_kind = data[0]
    if root_kind != _TYPE_OBJECT:
        raise SetupError("shortcuts.vdf does not start with an object; refusing to edit.")
    root_key, pos = read_cstring(data, 1)
    if root_key != b"shortcuts":
        raise SetupError("shortcuts.vdf root is not 'shortcuts'; refusing to edit.")
    entries_raw, pos = parse_object(data, pos)
    entries: list = []
    for kind, key, value in entries_raw:
        if kind != _TYPE_OBJECT:
            raise SetupError("Top-level shortcut is not an object; refusing to edit.")
        entries.append((key, value))
    # Real Steam files terminate the 'shortcuts' object (consumed above) and
    # then carry one final file-level 0x08.
    if pos + 1 != len(data) or data[pos] != _TYPE_END:
        raise SetupError("shortcuts.vdf has an unexpected terminator layout; refusing to edit.")
    pos += 1
    if pos != len(data):
        raise SetupError("shortcuts.vdf has trailing bytes; refusing to edit.")
    for key, _ in entries:
        try:
            int(key.decode("ascii"))
        except (UnicodeDecodeError, ValueError) as error:
            raise SetupError(f"Non-numeric shortcut index {key!r}; refusing to edit.") from error
    return entries, data


def serialize_shortcuts(entries: list) -> bytes:
    out = bytearray()
    out.append(_TYPE_OBJECT)
    out += b"shortcuts\x00"
    for index_key, fields in entries:
        if isinstance(index_key, str):
            index_key = index_key.encode("ascii")
        out.append(_TYPE_OBJECT)
        out += index_key + b"\x00"
        for kind, key, value in fields:
            out.append(kind)
            out += key + b"\x00"
            if kind == _TYPE_STRING:
                out += value + b"\x00"
            elif kind == _TYPE_INT:
                out += int(value).to_bytes(4, "little", signed=True)
            elif kind == _TYPE_OBJECT:
                for ck, cv in value:
                    out.append(_TYPE_STRING)
                    out += ck + b"\x00"
                    out += cv + b"\x00"
                out.append(_TYPE_END)
            else:
                raise SetupError("Cannot serialize unknown field type.")
        out.append(_TYPE_END)
    out.append(_TYPE_END)
    out.append(_TYPE_END)  # object end + file-level terminator, as Steam writes it.
    return bytes(out)


def new_entry_fields(spec: dict) -> list:
    fields: list = [
        (_TYPE_INT, b"appid", int(spec["appid_signed"])),
        (_TYPE_STRING, b"AppName", spec["app_name"].encode("utf-8")),
        (_TYPE_STRING, b"Exe", spec["exe"].encode("utf-8")),
        (_TYPE_STRING, b"StartDir", spec["start_dir"].encode("utf-8")),
        (_TYPE_STRING, b"icon", b""),
        (_TYPE_STRING, b"ShortcutPath", b""),
        (_TYPE_STRING, b"LaunchOptions", spec["launch_options"].encode("utf-8")),
        (_TYPE_INT, b"IsHidden", 0),
        (_TYPE_INT, b"AllowDesktopConfig", 1),
        (_TYPE_INT, b"AllowOverlay", 1),
        (_TYPE_INT, b"OpenVR", 0),
        (_TYPE_INT, b"Devkit", 0),
        (_TYPE_STRING, b"DevkitGameID", b""),
        (_TYPE_INT, b"DevkitOverrideAppID", 0),
        (_TYPE_INT, b"LastPlayTime", 0),
        (_TYPE_STRING, b"FlatpakAppID", b""),
        (_TYPE_STRING, b"sortas", b""),
        (_TYPE_OBJECT, b"tags", []),
    ]
    return fields


def find_exact_entry(entries: list, spec: dict) -> str | None:
    """Return the index key of the entry that exactly matches our spec."""
    want = {
        b"AppName": spec["app_name"].encode("utf-8"),
        b"Exe": spec["exe"].encode("utf-8"),
        b"StartDir": spec["start_dir"].encode("utf-8"),
        b"LaunchOptions": spec["launch_options"].encode("utf-8"),
    }
    for index_key, fields in entries:
        got = {k: v for t, k, v in fields if t == _TYPE_STRING}
        if all(got.get(k) == v for k, v in want.items()):
            return index_key.decode("ascii")
    return None


def check_no_appid_collision(entries: list, spec: dict) -> None:
    for index_key, fields in entries:
        appid = next((v for t, k, v in fields if t == _TYPE_INT and k == b"appid"), None)
        if appid == spec["appid_signed"]:
            if find_exact_entry([(index_key, fields)], spec) is None:
                raise SetupError(
                    f"appid {appid} already belongs to a different shortcut "
                    f"(index {index_key.decode('ascii', 'replace')}); refusing to edit."
                )


def write_backup_exclusive(shortcuts_file: Path, data: bytes) -> Path:
    """Write an exclusive timestamped backup; never overwrite an earlier one."""
    stamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-%f")
    base = f"shortcuts.vdf.bak.{stamp}-pid{os.getpid()}"
    for attempt in range(100):
        name = base if attempt == 0 else f"{base}-{attempt}"
        backup = shortcuts_file.with_name(name)
        try:
            descriptor = os.open(str(backup), os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0))
        except FileExistsError:
            continue
        try:
            with os.fdopen(descriptor, "wb") as stream:
                stream.write(data)
        except Exception:
            try:
                os.unlink(backup)
            except OSError:
                pass
            raise
        return backup
    raise SetupError("Could not allocate a new backup path; refusing to edit.")


def plan_new_bytes(existing: bytes, spec: dict) -> tuple[bytes, str]:
    entries, _ = parse_shortcuts(existing)
    existing_match = find_exact_entry(entries, spec)
    if existing_match is not None:
        raise SetupError(f"Exact owned entry already exists at index {existing_match}; nothing to do.")
    check_no_appid_collision(entries, spec)
    indices = sorted(int(k.decode("ascii")) for k, _ in entries)
    next_index = (indices[-1] + 1) if indices else 0
    # Serialize the new single entry and splice it before the final two
    # terminators (object end + file end) so every pre-existing byte is
    # preserved verbatim.
    if not existing.endswith(b"\x08\x08"):
        raise SetupError("shortcuts.vdf does not end with the expected terminators; refusing to edit.")
    single = serialize_shortcuts([(str(next_index).encode("ascii"), new_entry_fields(spec))])
    # single = 00 'shortcuts' 00 <entry> 08 08 ; strip the wrapper, keep entry.
    prefix = b"\x00shortcuts\x00"
    if not single.startswith(prefix) or not single.endswith(b"\x08\x08"):
        raise SetupError("Internal serialization error; refusing to edit.")
    trailing = single[len(prefix):]  # entry bytes + object end + file end
    new_data = existing[:-2] + trailing
    # Verify: the result must parse, contain the owned entry, and keep the
    # old bytes as an exact prefix (minus the moved terminators).
    reparsed, _ = parse_shortcuts(new_data)
    if find_exact_entry(reparsed, spec) is None:
        raise SetupError("Verification failed: new entry not found after planning.")
    if not new_data.startswith(existing[:-2]):
        raise SetupError("Verification failed: existing bytes were not preserved.")
    round_trip = serialize_shortcuts(reparsed)
    if round_trip != new_data:
        raise SetupError("Verification failed: re-serialization mismatch.")
    return new_data, str(next_index)


# ------------------------------------------------------------------ steam

def shortcuts_candidates(steam_root: Path) -> list[Path]:
    return sorted((steam_root / "userdata").glob("*/config/shortcuts.vdf"))


def resolve_shortcuts_file(steam_root: Path, user_id: str | None) -> Path:
    if user_id is not None:
        candidate = steam_root / "userdata" / user_id / "config" / "shortcuts.vdf"
        if not candidate.is_file():
            raise SetupError(
                f"No shortcuts.vdf for user id {user_id!r} under {steam_root}; "
                "create one non-Steam shortcut in Steam first, then retry."
            )
        return candidate
    candidates = shortcuts_candidates(steam_root)
    if len(candidates) == 0:
        raise SetupError(
            f"No userdata shortcuts.vdf found under {steam_root}; create one "
            "non-Steam shortcut in Steam first, then retry."
        )
    if len(candidates) > 1:
        ids = ", ".join(p.parents[1].name for p in candidates)
        raise SetupError(
            f"Multiple userdata shortcuts files found ({ids}); "
            "re-run with an explicit --user-id to refuse ambiguity."
        )
    return candidates[0]


def is_steam_running() -> bool:
    """Fail closed: return True unless tasklist positively shows no steam.exe.

    Uses ``tasklist /FO CSV /NH`` and matches the exact ``steam.exe`` image
    basename (case-insensitive). A nonzero exit, an unreadable/unparseable
    response, or any exception means "assume running" so ``--install``
    refuses rather than racing Steam.
    """
    import csv
    import io

    try:
        completed = subprocess.run(
            ["tasklist", "/FO", "CSV", "/NH", "/FI", "IMAGENAME eq steam.exe"],
            capture_output=True,
            text=True,
            timeout=15,
        )
    except (OSError, subprocess.SubprocessError):
        # If the process list cannot be read, fail closed: refuse the install.
        return True
    if completed.returncode != 0:
        return True
    output = completed.stdout or ""
    if not output.strip():
        return True
    try:
        rows = list(csv.reader(io.StringIO(output)))
    except (csv.Error, ValueError):
        return True
    if not rows:
        return True
    matched_steam = False
    for row in rows:
        if not row or all(not (cell or "").strip() for cell in row):
            continue
        first = (row[0] or "").strip().strip('"')
        lowered = first.lower()
        if lowered == "steam.exe":
            matched_steam = True
        elif lowered.startswith("info:"):
            continue  # tasklist's well-formed "no match" notice.
        else:
            # An unexpected row (garbled output or a filter that did not
            # apply) must not be read as "Steam is closed".
            return True
    # A well-formed empty result (INFO line only, no steam.exe row) means
    # Steam is not running. Anything else above already returned True.
    return matched_steam


# -------------------------------------------------------------------- cli

def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Dry-run (default) or explicitly install the native Steam shortcut. "
        "Never stops Steam; --install refuses while Steam is running."
    )
    parser.add_argument("--steam-root", type=Path, default=DEFAULT_STEAM_ROOT)
    parser.add_argument("--user-id", default=None, help="Numeric userdata directory name.")
    parser.add_argument(
        "--install",
        action="store_true",
        help="Explicitly edit shortcuts.vdf. Requires Steam to be fully closed.",
    )
    parser.add_argument(
        "--write-record",
        type=Path,
        nargs="?",
        const=DEFAULT_RECORD,
        default=None,
        help="Write the reproducible shortcut record JSON (default path when flag is bare).",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        selection = load_recorded_selection(ROOT)
        spec = build_launch_spec(ROOT, selection)
    except SetupError as error:
        print(f"setup_steam_input: {error}", file=sys.stderr)
        return 2

    print("Steam Input shortcut dry run (no Steam files touched).")
    print(f"App name:    {spec['app_name']}")
    print(f"Target:      {spec['exe']}")
    print(f"Start in:    {spec['start_dir']}")
    print(f"Launch opts: {spec['launch_options']}")
    print(f"Non-Steam appid: {spec['appid_signed']} "
          f"(unsigned {spec['appid_signed'] & 0xFFFFFFFF:#010x})")
    print(f"Recorded profile_store={selection['profile_store']} "
          f"content_store={selection['content_store']}")
    print(f"Recorded profile_id={selection['profile_id']}")
    print(f"Recorded save_index={selection['save_index']}")

    try:
        shortcuts_file = resolve_shortcuts_file(args.steam_root, args.user_id)
    except SetupError as error:
        print(f"setup_steam_input: {error}", file=sys.stderr)
        return 2
    print(f"Shortcuts file: {shortcuts_file}")
    running = is_steam_running()
    print(f"Steam running: {'yes' if running else 'no'}")

    try:
        existing = shortcuts_file.read_bytes()
        entries, _ = parse_shortcuts(existing)
    except (OSError, SetupError) as error:
        print(f"setup_steam_input: cannot inspect shortcuts.vdf: {error}", file=sys.stderr)
        return 2
    print(f"Existing entries: {len(entries)}")
    match = find_exact_entry(entries, spec)
    if match is not None:
        print(f"Exact owned entry already present at index {match}; nothing to install.")
    else:
        indices = sorted(int(k.decode('ascii')) for k, _ in entries) if entries else []
        print(f"Next free index would be: {(indices[-1] + 1) if indices else 0}")
        print("Owned entry not present; re-run reviewed --install with Steam closed to add it.")

    record_path = args.write_record
    if record_path is not None:
        record = {
            "app_name": spec["app_name"],
            "exe": spec["exe"],
            "start_dir": spec["start_dir"],
            "launch_options": spec["launch_options"],
            "appid_signed": spec["appid_signed"],
            "appid_unsigned": spec["appid_signed"] & 0xFFFFFFFF,
            "profile_store": selection["profile_store"],
            "content_store": selection["content_store"],
            "profile_id": selection["profile_id"],
            "save_index": selection["save_index"],
            "steam_root": str(args.steam_root),
            "generator": "tools/setup_steam_input.py dry run (Steam Input, XInput emulation)",
        }
        try:
            record_path.parent.mkdir(parents=True, exist_ok=True)
            tmp = record_path.with_suffix(record_path.suffix + ".tmp")
            tmp.write_text(json.dumps(record, indent=2) + "\n", encoding="utf-8")
            os.replace(tmp, record_path)
        except OSError as error:
            print(f"setup_steam_input: cannot write record: {error}", file=sys.stderr)
            return 2
        print(f"Shortcut record written: {record_path}")

    if not args.install:
        return 0

    # Explicit install path: fail closed on every ambiguity.
    if match is not None:
        print("Already installed; nothing to do.")
        return 0
    if running:
        print(
            "setup_steam_input: Steam is running; close Steam normally "
            "(do not kill it) and re-run --install. No files were changed.",
            file=sys.stderr,
        )
        return 3
    # Re-check immediately before writing in case Steam started.
    if is_steam_running():
        print("setup_steam_input: Steam started during setup; aborting.", file=sys.stderr)
        return 3
    try:
        # Refuse a concurrent change: the file must still equal the bytes the
        # plan was built from; Steam rewrites shortcuts.vdf on exit/startup.
        current = shortcuts_file.read_bytes()
        if current != existing:
            raise SetupError(
                "shortcuts.vdf changed since inspection; refusing to replace "
                "a concurrently modified file."
            )
        new_data, next_index = plan_new_bytes(existing, spec)
    except SetupError as error:
        print(f"setup_steam_input: {error}", file=sys.stderr)
        return 2
    try:
        backup = write_backup_exclusive(shortcuts_file, existing)
        tmp = shortcuts_file.with_name(
            "shortcuts.vdf.tmp.pid%d.%s"
            % (
                os.getpid(),
                datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%d-%H%M%S-%f"),
            )
        )
        tmp.write_bytes(new_data)
        # Verify the temp file before the atomic swap.
        reparsed, _ = parse_shortcuts(tmp.read_bytes())
        if find_exact_entry(reparsed, spec) is None:
            raise SetupError("Verification failed before replace.")
        os.replace(tmp, shortcuts_file)
        final = shortcuts_file.read_bytes()
        if final != new_data:
            raise SetupError("Post-install verification mismatch.")
    except (OSError, SetupError) as error:
        try:
            if "tmp" in locals() and tmp.exists():
                tmp.unlink()
        except OSError as cleanup_error:
            print(f"setup_steam_input: cleanup failed: {cleanup_error}", file=sys.stderr)
        print(f"setup_steam_input: install failed: {error}", file=sys.stderr)
        return 2
    print(f"Installed owned entry at index {next_index}. Backup: {backup}")
    print("Reopen Steam and enable Steam Input for the entry (see docs/steam-input.md).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
"""Extract one exact-name resource payload; never alter the original asset tree."""

import sys

# Importing the inspector must not create __pycache__ beside the tools.
sys.dont_write_bytecode = True

import argparse
import hashlib
import json
import mmap
import os
from pathlib import Path
import stat


class ExtractionError(ValueError):
    pass


def require(condition, message):
    if not condition:
        raise ExtractionError(message)


def identity(info):
    return info.st_dev, info.st_ino


def fingerprint(info):
    # On Windows, path stat and descriptor stat can expose different ctime values.
    return identity(info), info.st_size, info.st_mtime_ns, info.st_nlink


def reject_link(info, path):
    require(not stat.S_ISLNK(info.st_mode) and not (
        getattr(info, "st_file_attributes", 0)
        & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400)
    ), f"links and reparse points are not allowed: {path}")


def reserved_windows_name(part):
    # Python 3.10-compatible Win32 device-name check (also applies before extensions).
    stem = part.split(".", 1)[0].rstrip(" ").upper()
    return stem in {"CON", "PRN", "AUX", "NUL", "CONIN$", "CONOUT$"} or (
        len(stem) == 4 and stem[:3] in {"COM", "LPT"}
        and stem[3] in "123456789\u00b9\u00b2\u00b3"
    )


def checked_path(value, *, missing_leaf=False):
    """Check each component before resolving, including components preceding '..'."""
    path = Path(value).expanduser()
    if not path.is_absolute():
        path = Path.cwd() / path
    current = Path(path.anchor)
    parts = path.parts[1:]
    reject_link(current.lstat(), current)
    for index, part in enumerate(parts):
        if part == "..":
            current = current.parent
            continue
        if os.name == "nt":
            require(not any(ord(c) < 32 or c in ':<>"|?*' for c in part)
                    and not part.endswith((".", " "))
                    and not reserved_windows_name(part),
                    f"ambiguous or reserved Windows path component: {part!r}")
        current = current / part
        try:
            info = current.lstat()
        except FileNotFoundError:
            require(missing_leaf and index == len(parts) - 1,
                    f"path does not exist (output parent must already exist): {current}")
            return current.resolve(strict=False)
        reject_link(info, current)
        if index < len(parts) - 1:
            require(stat.S_ISDIR(info.st_mode), f"not a directory: {current}")
    return current.resolve(strict=True)


def within(path, root):
    return path == root or root in path.parents


def check_destination(output, root, protected):
    output = checked_path(output, missing_leaf=True)
    require(not within(output, root), "output must resolve outside the asset root")
    require(output not in protected, "output would overwrite the source or extraction tools")
    if output.exists():
        info = output.lstat()
        require(stat.S_ISREG(info.st_mode) and info.st_nlink == 1,
                "existing output must be a regular file with exactly one hard link")
        require(all(identity(info) != identity(p.stat()) for p in protected),
                "output aliases the source or extraction tools")
    return output


def read_flags():
    return os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)


def select_payload(source, entry_index, name, root):
    # Kept lazy so --help does not depend on inspector implementation availability.
    from inspect_assets import decode_entry, resource_chunks, stoc_entries

    before_open = source.lstat()
    require(stat.S_ISREG(before_open.st_mode), "input must be a regular file")
    reject_link(before_open, source)
    with os.fdopen(os.open(source, read_flags()), "rb") as stream:
        before = os.fstat(stream.fileno())
        require(fingerprint(before) == fingerprint(before_open), "input changed while opening")
        require(before.st_size > 0, "input is empty")
        with mmap.mmap(stream.fileno(), 0, access=mmap.ACCESS_READ) as data:
            source_hash = hashlib.sha256(data).hexdigest()
            entries = stoc_entries(data)["entries"]
            require(0 <= entry_index < len(entries), "entry index is out of range")
            entry = entries[entry_index]
            decoded, consumed = decode_entry(data, entry)
            require(len(decoded) == entry["decoded_size"] and
                    isinstance(consumed, int) and 0 < consumed <= entry["stored_size"],
                    "decoded entry length/consumed span disagrees with the entry")
            matches = [item for item in resource_chunks(decoded) if item.get("name") == name]
            require(len(matches) == 1,
                    f"expected exactly one resource named {name!r}; found {len(matches)}")
            resource = matches[0]
            offset, size = resource["payload_decoded_offset"], resource["payload_size"]
            require(isinstance(offset, int) and isinstance(size, int) and
                    offset >= 0 and size >= 0 and offset + size <= len(decoded),
                    "resource payload is outside the decoded entry")
            payload = bytes(decoded[offset:offset + size])
            payload_hash = hashlib.sha256(payload).hexdigest()
            require(payload_hash == resource["payload_sha256"], "resource payload hash mismatch")
        require(fingerprint(before) == fingerprint(os.fstat(stream.fileno())) and
                fingerprint(before) == fingerprint(source.lstat()),
                "input changed during extraction")
    provenance = {
        "format": "named_resource_payload_v1",
        "source": {"path": source.relative_to(root).as_posix(),
                   "size": before.st_size, "sha256": source_hash},
        "entry": {"index": entry_index, "table_offset": entry["table_offset"],
                  "file_offset": entry["file_offset"], "encoding": entry["encoding"],
                  "stored_size": entry["stored_size"], "decoded_size": len(decoded),
                  "consumed_bytes": consumed},
        "resource": {"name": resource["name"], "type_name": resource["type_name"],
                     "chunk_decoded_offset": resource["decoded_offset"],
                     "payload_decoded_offset": offset, "payload_size": size,
                     "payload_sha256": payload_hash},
    }
    return payload, provenance


def verify_existing(output, payload, root, protected):
    check_destination(output, root, protected)
    before_open = output.lstat()
    with os.fdopen(os.open(output, read_flags()), "rb") as stream:
        before = os.fstat(stream.fileno())
        require(fingerprint(before) == fingerprint(before_open) and
                stat.S_ISREG(before.st_mode) and before.st_nlink == 1,
                "output changed while opening")
        require(before.st_size == len(payload), "existing output differs; refusing to overwrite")
        for pos in range(0, len(payload), 1024 * 1024):
            require(stream.read(min(1024 * 1024, len(payload) - pos)) == payload[pos:pos + 1024 * 1024],
                    "existing output differs; refusing to overwrite")
        require(not stream.read(1) and fingerprint(before) == fingerprint(os.fstat(stream.fileno())),
                "existing output changed during comparison")
        check_destination(output, root, protected)
        require(fingerprint(before) == fingerprint(output.lstat()),
                "existing output changed during comparison")


def write_payload(output, payload, root, protected):
    check_destination(output, root, protected)
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    try:
        descriptor = os.open(output, flags, 0o666)
    except FileExistsError:
        verify_existing(output, payload, root, protected)
        return
    created = os.fstat(descriptor)
    try:
        with os.fdopen(descriptor, "wb") as stream:
            require(stat.S_ISREG(created.st_mode) and created.st_nlink == 1,
                    "new output must be a regular file with exactly one hard link")
            check_destination(output, root, protected)
            require(identity(output.lstat()) == identity(created), "output changed while creating")
            require(stream.write(payload) == len(payload), "short output write")
            stream.flush()
            os.fsync(stream.fileno())
            after = os.fstat(stream.fileno())
            require(after.st_size == len(payload) and after.st_nlink == 1,
                    "output changed during writing")
            check_destination(output, root, protected)
            require(identity(output.lstat()) == identity(after), "output changed during writing")
    except Exception:
        # Only remove the incomplete file this call created, never a replacement.
        try:
            if identity(output.lstat()) == identity(created):
                output.unlink()
        except OSError as cleanup_error:
            print(f"extract_resource: cleanup failed: {cleanup_error}", file=sys.stderr)
        raise


def nonnegative(value):
    try:
        result = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("entry must be a nonnegative integer") from exc
    if result < 0:
        raise argparse.ArgumentTypeError("entry must be a nonnegative integer")
    return result


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", required=True, type=Path, help="original .str file within --asset-root")
    parser.add_argument("--entry", required=True, type=nonnegative, help="zero-based SToc entry index")
    parser.add_argument("--name", required=True, help="exact, case-sensitive resource name")
    parser.add_argument("--output", required=True, type=Path,
                        help="explicit payload file outside asset root; parent directory must exist")
    parser.add_argument("--asset-root", type=Path,
                        default=Path(__file__).resolve().parent.parent / "Simpsons Game, The (USA)")
    args = parser.parse_args(argv)
    try:
        root = checked_path(args.asset_root)
        require(root.is_dir(), "asset root must be a directory")
        source = checked_path(args.input)
        require(within(source, root) and source.suffix.lower() == ".str",
                "input must be a .str file resolving inside the asset root")
        extractor = Path(__file__).resolve(strict=True)
        inspector = extractor.with_name("inspect_assets.py").resolve(strict=True)
        protected = (source, inspector, extractor)
        output = check_destination(args.output, root, protected)
        payload, provenance = select_payload(source, args.entry, args.name, root)
        provenance["output"] = str(output)
        serialized = json.dumps(provenance, ensure_ascii=True, sort_keys=True)
        write_payload(output, payload, root, protected)
        print(serialized)
        return 0
    except (OSError, ValueError, ImportError, RuntimeError) as exc:
        print(f"extraction failed: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())

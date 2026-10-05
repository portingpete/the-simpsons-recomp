"""Exercise native profile persistence across actual executable processes."""
import pathlib
import os
import subprocess
import sys
import tempfile
import uuid

exe = pathlib.Path(sys.argv[1]).resolve(strict=True)


def run(*args, success=True):
    result = subprocess.run([str(exe), *map(str, args)], capture_output=True,
                            text=True, timeout=15)
    if (result.returncode == 0) != success:
        raise AssertionError((args, result.returncode, result.stdout, result.stderr))
    return result


with tempfile.TemporaryDirectory(prefix="SimpsonsProfileCli-") as temporary:
    root = pathlib.Path(temporary) / "profiles"
    prefix = ("--profile-store", root)
    assert not run(*prefix, "--list-local-profiles").stdout.strip()
    created = run(*prefix, "--create-local-profile", "Native CLI")
    profile_id, name = created.stdout.strip().split("\t")
    assert str(uuid.UUID(profile_id)) == profile_id and name == "Native CLI"
    first_list = run(*prefix, "--list-local-profiles")
    assert first_list.stdout == created.stdout, (created.stdout, first_list.stdout, first_list.stderr,
        str(root), [(p.name, p.read_bytes()) for p in root.iterdir() if p.is_file()], run(*prefix, "--list-local-profiles").stdout)
    before = {path.name: path.read_bytes() for path in root.iterdir() if path.is_file()}
    for name in ("", "1234567890123456", " bad", "bad ", "bad\nname", "bad\tname"):
        run(*prefix, "--create-local-profile", name, success=False)
    assert before == {path.name: path.read_bytes() for path in root.iterdir() if path.is_file()}
    later_list = run(*prefix, "--list-local-profiles")
    assert later_list.stdout == created.stdout, (created.stdout, later_list.stdout, later_list.stderr,
        str(root), [(p.name, p.read_bytes()) for p in root.iterdir() if p.is_file()], run(*prefix, "--list-local-profiles").stdout)
    # Invalid combinations must be rejected by argument parsing before any
    # profile store is opened or created.
    unused = pathlib.Path(temporary) / "must-not-exist"
    for args in (("--create-local-profile", "X", "--list-local-profiles"),
                 ("--list-local-profiles", "--local-profile", f"0:{profile_id}"),
                 ("--list-local-profiles", "--image", "missing.pe"),
                 ("--list-local-profiles", "--capture-frames", "unused-captures"),
                 ("--list-local-profiles", "--controller-input", "unused-input"),
                 ("--list-local-profiles", "--frame-rate", "60"),
                 ("--list-local-profiles", "--first-mission-completion"),
                 ("--list-local-profiles", "--stage", "loc"),
                 ("--list-local-profiles", "--resource-audit", "unused.jsonl"),
                 ("--image", "missing.pe", "--stage", "../loc"),
                 ("--image", "missing.pe", "--stage", "loc", "--bartman-begins"),
                 ("--image", "missing.pe", "--play-stage-intro"),
                 ("--image", "missing.pe", "--stage", "loc", "--play-stage-intro", "--play-stage-intro"),
                 ("--create-local-profile", "X", "--first-mission-completion"),
                 ("--image", "missing.pe", "--first-mission-completion", "--first-mission-completion"),
                 ("--image", "missing.pe", "--render-test-first-mission", "--first-mission-completion"),
                 ("--image", "missing.pe", "--local-profile", f"7:{profile_id}"),
                 ("--image", "missing.pe", "--local-profile", f"0:{profile_id}",
                  "--local-profile", f"0:{profile_id}")):
        rejected = run("--profile-store", unused, *args, success=False)
        assert rejected.returncode == 2, (args, rejected.returncode, rejected.stderr)
        assert not unused.exists()
    second = run(*prefix, "--create-local-profile", "Second CLI")
    second_id = second.stdout.split("\t")[0]
    assert second_id != profile_id
    final_list = run(*prefix, "--list-local-profiles")
    listing = final_list.stdout.splitlines()
    assert listing == sorted([created.stdout.strip(), second.stdout.strip()]), (listing, created.stdout, second.stdout,
        final_list.stderr, str(root), [(p.name, p.read_bytes()) for p in root.iterdir() if p.is_file()], run(*prefix, "--list-local-profiles").stdout)
    # A failed output pipe must not be reported as a successful empty listing.
    reader, writer = os.pipe()
    os.close(reader)
    try:
        failed_output = subprocess.run([str(exe), *map(str, prefix), "--list-local-profiles"],
            stdout=writer, stderr=subprocess.PIPE, text=True, timeout=15)
    finally:
        os.close(writer)
    assert failed_output.returncode == 1 and "Unable to write local profile command output" in failed_output.stderr, failed_output
print("PASS native profile CLI: real create/list/reopen processes, stable distinct IDs, invalid names and parse-before-write; no game or save claim")

"""Tests for the Steam Input shortcut helper.

Read-only against the live Steam shortcuts file (never writes, never stops
Steam). All other cases use synthetic in-memory shortcuts.vdf bytes.
Run with:  python -B -m unittest tests.test_steam_input_shortcut -v
"""

import struct
import unittest
import zlib
from pathlib import Path

import tools.setup_steam_input as helper

ROOT = Path(__file__).resolve().parents[1]
LIVE = Path("C:/Program Files (x86)/Steam/userdata/76505701/config/shortcuts.vdf")


def synthetic_spec(index_hint="3"):
    return {
        "app_name": "The Simpsons Game (Native)",
        "exe": '"K:\\SimpsonsNativeCopy\\build\\native\\SimpsonsNative.exe"',
        "start_dir": "K:\\SimpsonsNativeCopy",
        "launch_options": "--image K:\\SimpsonsNativeCopy\\analysis\\simpsons.pe --frame-rate 60",
        "appid_signed": helper.shortcut_appid_signed(
            '"K:\\SimpsonsNativeCopy\\build\\native\\SimpsonsNative.exe"',
            "The Simpsons Game (Native)",
        ),
    }


def other_spec():
    return {
        "app_name": "Other Entry",
        "exe": '"K:\\Other\\game.exe"',
        "start_dir": "K:\\Other",
        "launch_options": "--other",
        "appid_signed": helper.shortcut_appid_signed('"K:\\Other\\game.exe"', "Other Entry"),
    }


class SteamInputShortcutTests(unittest.TestCase):
    def test_recorded_selection_small_fields_only(self):
        selection = helper.load_recorded_selection(ROOT)
        self.assertEqual(
            set(selection),
            {"profile_store", "content_store", "profile_id", "save_index"},
        )
        self.assertEqual(selection["profile_store"], "build/mainmenu-profile-204")
        self.assertEqual(selection["content_store"], "build/mainmenu-content-204")
        self.assertEqual(selection["profile_id"], "575cf79a-3815-45f7-a6f7-e8d709d16298")
        self.assertTrue(selection["save_index"].endswith("SIMPSONS_SLOT1.save"))
        # The large screens cue table must not leak into the selection record.
        self.assertNotIn("screens", selection)

    def test_launch_spec_exact_args(self):
        spec = helper.build_launch_spec(ROOT)
        opts = spec["launch_options"]
        for token in (
            "--image",
            "--profile-store",
            "--content-store",
            "--local-profile",
            "0:575cf79a-3815-45f7-a6f7-e8d709d16298",
        ):
            self.assertIn(token, opts)
        self.assertNotIn("--frame-rate",opts) # Respect in-game saved video preferences.
        self.assertIn("analysis\\simpsons.pe", opts)
        self.assertIn("mainmenu-profile-204", opts)
        self.assertIn("mainmenu-content-204", opts)
        # No per-run replay flags in the interactive Steam entry.
        self.assertNotIn("--controller-input", opts)
        self.assertNotIn("--capture-frames", opts)
        self.assertEqual(spec["app_name"], "The Simpsons Game (Native)")
        self.assertTrue(spec["exe"].endswith('SimpsonsNative.exe"'))
        self.assertTrue(spec["start_dir"].endswith("SimpsonsNativeCopy"))

    def test_quote_paths_with_spaces(self):
        self.assertEqual(helper.quote_launch_value("C:\\plain\\x.pe"), "C:\\plain\\x.pe")
        self.assertEqual(
            helper.quote_launch_value("C:\\with space\\x.pe"), '"C:\\with space\\x.pe"'
        )
        # CRT rule: backslashes before the closing quote are doubled.
        self.assertEqual(
            helper.quote_launch_value("C:\\with space\\"),
            '"C:\\with space\\\\"',
        )
        self.assertEqual(helper.quote_launch_value(""), '""')
        with self.assertRaises(helper.SetupError):
            helper.quote_launch_value('C:\\bad"quote\\x.pe')
        with self.assertRaises(helper.SetupError):
            helper.quote_launch_value("line1\nline2")
        with self.assertRaises(helper.SetupError):
            helper.quote_launch_value("line1\rline2")

    def test_is_steam_running_fail_closed(self):
        import subprocess as sp

        orig_run = sp.run
        try:
            # Nonzero exit -> assume running.
            sp.run = lambda *a, **k: sp.CompletedProcess(a, 1, "", "")  # type: ignore
            self.assertTrue(helper.is_steam_running())
            # Unparseable/garbled output -> assume running.
            sp.run = lambda *a, **k: sp.CompletedProcess(a, 0, "\x00\x01-binary-\xff", "")  # type: ignore
            self.assertTrue(helper.is_steam_running())
            # Empty output -> assume running.
            sp.run = lambda *a, **k: sp.CompletedProcess(a, 0, "", "")  # type: ignore
            self.assertTrue(helper.is_steam_running())
            # Well-formed CSV with no steam.exe row -> not running.
            sp.run = lambda *a, **k: sp.CompletedProcess(  # type: ignore
                a, 0, '"INFO: No tasks are running which match the specified criteria."\r\n', ""
            )
            self.assertFalse(helper.is_steam_running())
            # Well-formed CSV with an exact steam.exe row -> running.
            sp.run = lambda *a, **k: sp.CompletedProcess(  # type: ignore
                a, 0, '"steam.exe","12948","Console","1","100,000 K"\r\n', ""
            )
            self.assertTrue(helper.is_steam_running())
            # A substring match (e.g. not-steam.exe) is an unexpected response
            # under an exact steam.exe filter, so fail closed.
            sp.run = lambda *a, **k: sp.CompletedProcess(  # type: ignore
                a, 0, '"not-steam.exe","1","Console","1","1 K"\r\n', ""
            )
            self.assertTrue(helper.is_steam_running())
            # tasklist unavailable -> assume running.
            def boom(*a, **k):
                raise OSError("no tasklist")
            sp.run = boom  # type: ignore
            self.assertTrue(helper.is_steam_running())
        finally:
            sp.run = orig_run  # type: ignore

    def test_backup_is_exclusive(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            target = Path(tmp) / "shortcuts.vdf"
            target.write_bytes(b"original")
            first = helper.write_backup_exclusive(target, b"original")
            # Occupy the microsecond/pid name once more to force a collision.
            second = helper.write_backup_exclusive(target, b"original")
            self.assertNotEqual(first, second)
            self.assertTrue(first.is_file())
            self.assertTrue(second.is_file())
            self.assertEqual(first.read_bytes(), b"original")

    def test_appid_matches_steam_algorithm(self):
        exe = '"K:\\Games\\Example\\game.exe"'
        name = "Example Game"
        expected = struct.unpack(
            "<i",
            struct.pack(
                "<I", (zlib.crc32((exe + name).encode()) & 0xFFFFFFFF) | 0x80000000
            ),
        )[0]
        self.assertEqual(helper.shortcut_appid_signed(exe, name), expected)
        self.assertLess(helper.shortcut_appid_signed(exe, name), 0)  # top bit set

    def test_synthetic_round_trip_preserves_bytes(self):
        spec = other_spec()
        data = helper.serialize_shortcuts([("0", helper.new_entry_fields(spec))])
        entries, _ = helper.parse_shortcuts(data)
        self.assertEqual(helper.serialize_shortcuts(entries), data)
        self.assertEqual(helper.find_exact_entry(entries, spec), "0")

    def test_plan_preserves_existing_prefix_and_idempotent(self):
        first = other_spec()
        base = helper.serialize_shortcuts([("0", helper.new_entry_fields(first))])
        owned = synthetic_spec()
        planned, index = helper.plan_new_bytes(base, owned)
        self.assertEqual(index, "1")
        self.assertTrue(planned.startswith(base[:-2]))
        entries, _ = helper.parse_shortcuts(planned)
        self.assertEqual(helper.find_exact_entry(entries, owned), "1")
        # Idempotence: planning again on the result refuses as already present.
        with self.assertRaises(helper.SetupError):
            helper.plan_new_bytes(planned, owned)
        # Full re-serialization round-trips.
        self.assertEqual(helper.serialize_shortcuts(entries), planned)

    def test_plan_refuses_appid_collision(self):
        first = other_spec()
        base = helper.serialize_shortcuts([("0", helper.new_entry_fields(first))])
        clash = dict(synthetic_spec())
        clash["appid_signed"] = helper.parse_shortcuts(base)[0][0][1][0][2]
        clash["app_name"] = "Different Name"
        with self.assertRaises(helper.SetupError):
            helper.plan_new_bytes(base, clash)

    def test_install_refuses_while_steam_running(self):
        # main() with --install must fail closed when Steam is live, without
        # touching any file. Patch the probes to avoid depending on the real
        # process list or Steam files.
        import tempfile

        seen_writes: list = []
        real_parse = helper.parse_shortcuts
        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "shortcuts.vdf"
            fake.write_bytes(
                helper.serialize_shortcuts([("0", helper.new_entry_fields(other_spec()))])
            )
            orig_resolve = helper.resolve_shortcuts_file
            orig_running = helper.is_steam_running
            orig_spec = helper.build_launch_spec
            try:
                helper.resolve_shortcuts_file = lambda *a, **k: fake  # type: ignore
                helper.is_steam_running = lambda: True  # type: ignore
                helper.build_launch_spec = lambda *a, **k: synthetic_spec()  # type: ignore
                before = fake.read_bytes()
                code = helper.main(["--install", "--steam-root", tmp])
                self.assertEqual(code, 3)
                self.assertEqual(fake.read_bytes(), before)
                self.assertEqual(seen_writes, [])
                _ = real_parse
            finally:
                helper.resolve_shortcuts_file = orig_resolve  # type: ignore
                helper.is_steam_running = orig_running  # type: ignore
                helper.build_launch_spec = orig_spec  # type: ignore

    def test_install_refuses_concurrent_change(self):
        import tempfile

        with tempfile.TemporaryDirectory() as tmp:
            fake = Path(tmp) / "shortcuts.vdf"
            fake.write_bytes(
                helper.serialize_shortcuts([("0", helper.new_entry_fields(other_spec()))])
            )
            orig_resolve = helper.resolve_shortcuts_file
            orig_running = helper.is_steam_running
            orig_spec = helper.build_launch_spec
            orig_read = Path.read_bytes
            calls = []
            try:
                helper.resolve_shortcuts_file = lambda *a, **k: fake  # type: ignore
                helper.is_steam_running = lambda: False  # type: ignore
                helper.build_launch_spec = lambda *a, **k: synthetic_spec()  # type: ignore

                def counting(self):
                    data = orig_read(self)
                    if self == fake:
                        calls.append(bytes(data))
                        if len(calls) == 2:
                            return data + b"\x00"  # concurrent mutation
                    return data

                Path.read_bytes = counting  # type: ignore
                code = helper.main(["--install", "--steam-root", tmp])
                self.assertEqual(code, 2)
            finally:
                Path.read_bytes = orig_read  # type: ignore
                helper.resolve_shortcuts_file = orig_resolve  # type: ignore
                helper.is_steam_running = orig_running  # type: ignore
                helper.build_launch_spec = orig_spec  # type: ignore

    def test_live_shortcuts_parse_round_trip_readonly(self):
        if not LIVE.is_file():
            self.skipTest("live shortcuts.vdf not present")
        data = LIVE.read_bytes()  # read-only; Steam keeps running
        entries, _ = helper.parse_shortcuts(data)
        self.assertGreaterEqual(len(entries), 1)
        self.assertEqual(helper.serialize_shortcuts(entries), data)


if __name__ == "__main__":
    unittest.main()

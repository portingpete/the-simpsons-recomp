"""Check the authored prompt assets and their reproducible native embedding."""

from pathlib import Path
import hashlib
import json
import sys
import unittest

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import compile_prompt_icons as prompts


# Pin the existing runtime values independently of the compiler's source lists.
ORIGINAL_NAMES = (
    "Space", "Enter", "Esc", "Tab", "MouseLeft", "MouseRight", "MouseMiddle", "MouseMove",
    "Shift", "Ctrl", "Q", "E", "R", "F", "G", "W", "A", "S", "D",
    "Up", "Down", "Left", "Right", "One", "Two", "Three", "Four", "Question",
    "Move", "Directions", "Z", "X", "C", "V", "Five", "Six",
)

# Each physical key on a full-size ANSI keyboard has its own prompt. In
# particular, side-specific modifiers and numpad keys must not collapse onto
# their generic/main-row counterparts when a player chooses a new binding.
STANDARD_ANSI_KEYS = {
    "Esc", *(f"F{number}" for number in range(1, 13)), "PrintScreen", "ScrollLock", "Pause",
    "Grave", "Zero", "One", "Two", "Three", "Four", "Five", "Six", "Seven", "Eight", "Nine",
    "Minus", "Equals", "Backspace", "Tab", *"QWERTYUIOP", "LeftBracket", "RightBracket", "Backslash",
    "CapsLock", *"ASDFGHJKL", "Semicolon", "Apostrophe", "Enter",
    "LeftShift", *"ZXCVBNM", "Comma", "Period", "Slash", "RightShift",
    "LeftCtrl", "LeftWin", "LeftAlt", "Space", "RightAlt", "RightWin", "Menu", "RightCtrl",
    "Insert", "Home", "PageUp", "Delete", "End", "PageDown", "Up", "Down", "Left", "Right",
    "NumLock", *(f"Numpad{number}" for number in range(10)),
    "NumpadDivide", "NumpadMultiply", "NumpadSubtract", "NumpadAdd", "NumpadEnter", "NumpadDecimal",
}
OTHER_SINGLE_PROMPTS = {"Shift", "Ctrl", "Question", "MouseLeft", "MouseRight", "MouseMiddle", "MouseMove"}


class PromptIconTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.sources = prompts.load_sources()
        cls.icons = prompts.compile_loaded(cls.sources)
        cls.by_name = {icon.name: icon.image() for icon in cls.icons}

    def test_all_supplied_and_supplementary_sprites_are_used(self):
        self.assertEqual(len(self.sources.artworks), 111)
        self.assertEqual(len(self.icons), 113)
        self.assertEqual(len(self.icons), len(self.by_name))
        self.assertEqual(set(self.sources.artworks) | {name for name, _ in prompts.GROUPS}, set(self.by_name))
        for sheet, expected in zip(prompts.SHEETS, (27, 7, 17, 12, 20, 28), strict=True):
            self.assertEqual(sum(crop["source"] == sheet.path for crop in self.sources.manifest["icons"].values()), expected)

    def test_existing_enumeration_order_is_stable(self):
        self.assertEqual(tuple(icon.name for icon in self.icons[:36]), ORIGINAL_NAMES)
        self.assertEqual([icon.name for icon in self.icons[36:]], list(prompts.EXTRA_NAMES[6:]))

    def test_standard_104_key_keyboard_is_complete(self):
        self.assertEqual(len(STANDARD_ANSI_KEYS), 104)
        self.assertTrue(STANDARD_ANSI_KEYS.isdisjoint(OTHER_SINGLE_PROMPTS))
        self.assertEqual(set(self.sources.artworks), STANDARD_ANSI_KEYS | OTHER_SINGLE_PROMPTS)

    def test_source_sheet_mappings_are_unique_and_complete(self):
        names = [name for sheet in prompts.SHEETS for row in sheet.rows for name in row]
        self.assertEqual(len(names), len(set(names)), "A sprite name is assigned to more than one source crop")
        self.assertEqual(set(names), set(self.sources.artworks))
        self.assertEqual(set(names), {name for name, _ in prompts.SOURCES} | set(prompts.EXTRA_NAMES))

    def test_source_provenance_and_crop_identity(self):
        sheets = {}
        for entry in self.sources.manifest["sources"]:
            path = ROOT / entry["path"]
            self.assertEqual(hashlib.sha256(path.read_bytes()).hexdigest(), entry["sha256"])
            with Image.open(path) as image:
                sheets[entry["path"]] = image.convert("RGBA")
            self.assertEqual(list(sheets[entry["path"]].size), entry["size"])
        for name, entry in self.sources.manifest["icons"].items():
            with self.subTest(icon=name):
                expected = sheets[entry["source"]].crop(entry["bounds"])
                self.assertEqual(self.sources.artworks[name].tobytes(), expected.tobytes())
                self.assertEqual(hashlib.sha256(expected.tobytes()).hexdigest(), entry["rgba_sha256"])
                left, top, right, bottom = entry["bounds"]
                core = entry["core_bounds"]
                self.assertLessEqual(left, core[0]); self.assertLessEqual(top, core[1])
                self.assertGreaterEqual(right, core[2]); self.assertGreaterEqual(bottom, core[3])
                # Strong alpha belonging to a different key must never enter
                # this crop, especially the close F / Space / Shift row.
                for other_name, other in self.sources.manifest["icons"].items():
                    if other_name == name or other["source"] != entry["source"]:
                        continue
                    x0, y0, x1, y1 = other["core_bounds"]
                    self.assertFalse(left < x1 and right > x0 and top < y1 and bottom > y0)

    def test_rgba_dimensions_alpha_and_transparent_border(self):
        for icon in self.icons:
            with self.subTest(icon=icon.name):
                self.assertEqual(len(icon.rgba), 64 * 64 * 4)
                image = icon.image()
                self.assertEqual(image.mode, "RGBA")
                self.assertEqual(image.size, (64, 64))
                alpha = image.getchannel("A")
                self.assertEqual(alpha.getextrema(), (0, 255))
                left, top, right, bottom = alpha.getbbox()
                self.assertGreaterEqual(left, prompts.PADDING)
                self.assertGreaterEqual(top, prompts.PADDING)
                self.assertLessEqual(right, 64 - prompts.PADDING)
                self.assertLessEqual(bottom, 64 - prompts.PADDING)

    def test_single_icons_keep_authored_aspect_ratio(self):
        for name, original in self.sources.artworks.items():
            with self.subTest(icon=name):
                left, top, right, bottom = self.by_name[name].getchannel("A").getbbox()
                width, height = right - left, bottom - top
                # Account for integer fitting and subpixel soft edges which
                # disappear during premultiplied-alpha downsampling.
                self.assertLessEqual(abs(height - width * original.height / original.width), 2.1)

    def test_groups_keep_four_separate_authored_keycaps(self):
        for name in ("Move", "Directions"):
            with self.subTest(icon=name):
                alpha = self.by_name[name].getchannel("A")
                # Four connected components ensure the composition neither clips
                # a key nor merges its silhouette into its neighbours.
                visible = {(x, y) for y in range(64) for x in range(64) if alpha.getpixel((x, y)) >= 32}
                components = []
                while visible:
                    pending = [visible.pop()]
                    count = 0
                    while pending:
                        x, y = pending.pop()
                        count += 1
                        for point in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
                            if point in visible:
                                visible.remove(point)
                                pending.append(point)
                    components.append(count)
                self.assertEqual(len(components), 4)
                self.assertTrue(all(count > 100 for count in components))

    def test_empty_asset_is_rejected(self):
        import tempfile
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "empty.png"
            Image.new("RGBA", (100, 100)).save(path)
            with self.assertRaisesRegex(ValueError, "no visible pixels"):
                prompts.crop_artwork(path)

    def test_regeneration_matches_checked_in_header(self):
        first = prompts.render_header(self.icons)
        self.assertEqual(first, prompts.render_header(prompts.compile_icons()))
        self.assertEqual(first, prompts.OUTPUT.read_bytes())
        self.assertEqual(self.sources.manifest, json.loads(prompts.MANIFEST.read_text(encoding="utf-8")))

    def test_reference_theme_keeps_the_same_runtime_interface(self):
        icons = prompts.compile_icons(theme="xelu")
        header = prompts.render_header(icons, theme="xelu")
        self.assertIn(b"namespace Simpsons::Graphics::NativePromptIcons", header)
        self.assertIn(b"CC0", header)
        self.assertEqual(tuple(icon.name for icon in icons), ORIGINAL_NAMES[:30])
        self.assertNotEqual(icons[0].rgba, self.icons[0].rgba)


if __name__ == "__main__":
    unittest.main()

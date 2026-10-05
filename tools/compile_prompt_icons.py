#!/usr/bin/env python3
"""Import yellow keyboard/mouse sheets as native RGBA8 prompt textures.

Only regeneration and contact sheets need Pillow. The checked-in C++ header has
no image decoder, filesystem, or Python dependency. Source PNGs are never edited.
Use --theme xelu to regenerate the original CC0 reference theme.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path
import sys

from PIL import Image, ImageDraw, ImageFont


ROOT = Path(__file__).resolve().parents[1]
ASSET_DIR = ROOT / "assets/xelu-light"
OUTPUT = ROOT / "renderer/native_prompt_icons.generated.h"
MANIFEST = ROOT / "assets/native-input-prompts/manifest.json"
DEFAULT_THEME = "yellow"
ICON_SIZE = 64
PADDING = 2
COMPONENT_ALPHA = 32
COMPONENT_MIN_PIXELS = 1000
SOURCE_FRINGE = 4

# Keep this order stable: it is also the C++ Icon enumeration order.
SOURCES = (
    ("Space", "Space_Key_Light.png"),
    ("Enter", "Enter_Key_Light.png"),
    ("Esc", "Esc_Key_Light.png"),
    ("Tab", "Tab_Key_Light.png"),
    ("MouseLeft", "Mouse_Left_Key_Light.png"),
    ("MouseRight", "Mouse_Right_Key_Light.png"),
    ("MouseMiddle", "Mouse_Middle_Key_Light.png"),
    ("MouseMove", "Mouse_Simple_Key_Light.png"),
    ("Shift", "Shift_Key_Light.png"),
    ("Ctrl", "Ctrl_Key_Light.png"),
    ("Q", "Q_Key_Light.png"),
    ("E", "E_Key_Light.png"),
    ("R", "R_Key_Light.png"),
    ("F", "F_Key_Light.png"),
    ("G", "G_Key_Light.png"),
    ("W", "W_Key_Light.png"),
    ("A", "A_Key_Light.png"),
    ("S", "S_Key_Light.png"),
    ("D", "D_Key_Light.png"),
    ("Up", "Arrow_Up_Key_Light.png"),
    ("Down", "Arrow_Down_Key_Light.png"),
    ("Left", "Arrow_Left_Key_Light.png"),
    ("Right", "Arrow_Right_Key_Light.png"),
    ("One", "1_Key_Light.png"),
    ("Two", "2_Key_Light.png"),
    ("Three", "3_Key_Light.png"),
    ("Four", "4_Key_Light.png"),
    ("Question", "Question_Key_Light.png"),
)
GROUPS = (
    ("Move", ("W", "A", "S", "D")),
    ("Directions", ("Up", "Left", "Down", "Right")),
)
EXTRA_NAMES = ("Z", "X", "C", "V", "Five", "Six")


@dataclass(frozen=True)
class Sheet:
    path: str
    origin: str
    sha256: str
    rows: tuple[tuple[str, ...], ...]


SHEETS = (
    Sheet("assets/ChatGPT Image Sep 26, 2026, 10_10_55 PM.png", "user-supplied-hand-drawn-revision",
          "8cac4b3d396f8d8ed08c1a50d60fe1c5d736d39b92fb7fa537eceda66c23057d",
          (("W", "A", "S", "D"), ("Z", "X", "C", "V"),
           ("E", "F", "Space", "Shift"), ("Ctrl", "Tab", "Enter", "Esc"),
           ("MouseLeft", "MouseRight", "MouseMiddle", "Q"),
           ("R", "One", "Two", "Three"), ("Four", "Five", "Six"))),
    Sheet("assets/native-input-prompts/yellow-supplement.png", "generated-supplement",
          "d2ec270bd8d0880439abe0778c87e4b76afc081e129074a089561f525aa3c028",
          (("Up", "Left", "Down", "Right"), ("MouseMove", "G", "Question"))),
)


@dataclass(frozen=True)
class CompiledIcon:
    name: str
    rgba: bytes

    def image(self) -> Image.Image:
        return Image.frombytes("RGBA", (ICON_SIZE, ICON_SIZE), self.rgba)


@dataclass
class ThemeSources:
    artworks: dict[str, Image.Image]
    manifest: dict


def alpha_components(image: Image.Image) -> list[tuple[tuple[int, int, int, int], int]]:
    """Find substantial four-connected silhouettes; faint export dust is ignored."""
    width, height = image.size
    remaining = bytearray(value >= COMPONENT_ALPHA for value in image.getchannel("A").tobytes())
    components = []
    for start in range(width * height):
        if not remaining[start]:
            continue
        remaining[start] = 0
        pending = [start]
        count = 0
        left = right = start % width
        top = bottom = start // width
        while pending:
            index = pending.pop()
            x, y = index % width, index // width
            count += 1
            left, right = min(left, x), max(right, x)
            top, bottom = min(top, y), max(bottom, y)
            neighbours = (index - 1 if x else -1, index + 1 if x + 1 < width else -1,
                          index - width, index + width)
            for neighbour in neighbours:
                if 0 <= neighbour < width * height and remaining[neighbour]:
                    remaining[neighbour] = 0
                    pending.append(neighbour)
        if count >= COMPONENT_MIN_PIXELS:
            components.append(((left, top, right + 1, bottom + 1), count))
    return components


def component_rows(components):
    rows = []
    for component in sorted(components, key=lambda item: (item[0][1], item[0][0])):
        if not rows or component[0][1] - rows[-1][0][0][1] > 64:
            rows.append([])
        rows[-1].append(component)
    return [sorted(row, key=lambda item: item[0][0]) for row in rows]


def load_sources(theme: str = DEFAULT_THEME, asset_dir: Path = ASSET_DIR) -> ThemeSources:
    if theme == "xelu":
        originals = {name: crop_artwork(asset_dir / filename) for name, filename in SOURCES}
        return ThemeSources(originals, {
            "version": 1, "theme": theme, "icon_size": ICON_SIZE,
            "provenance": "Nicolae (Xelu) Berbece / Those Awesome Guys, CC0",
            "source_digest": source_digest(asset_dir, theme=theme),
            "sources": [{"path": "assets/xelu-light/" + filename,
                         "sha256": hashlib.sha256((asset_dir / filename).read_bytes()).hexdigest()}
                        for _, filename in SOURCES],
        })
    if theme != "yellow":
        raise ValueError(f"Unknown prompt theme: {theme}")
    originals, files, crops = {}, [], {}
    for specification in SHEETS:
        path = ROOT / specification.path
        digest = hashlib.sha256(path.read_bytes()).hexdigest()
        if digest != specification.sha256:
            raise ValueError(f"Source sheet changed; review sprite identity and bounds: {path}")
        with Image.open(path) as source:
            sheet = source.convert("RGBA")
        rows = component_rows(alpha_components(sheet))
        if tuple(map(len, rows)) != tuple(map(len, specification.rows)):
            raise ValueError(f"Unexpected connected sprite layout in {path}: {tuple(map(len, rows))}")
        files.append({"path": specification.path, "origin": specification.origin,
                      "sha256": digest, "size": list(sheet.size)})
        for names, row in zip(specification.rows, rows, strict=True):
            for name, (core, count) in zip(names, row, strict=True):
                left, top, right, bottom = core
                # Retain the unmodified soft alpha fringe around the measured
                # silhouette. This margin cannot reach any neighbouring sprite.
                region = (max(0, left - SOURCE_FRINGE), max(0, top - SOURCE_FRINGE),
                          min(sheet.width, right + SOURCE_FRINGE), min(sheet.height, bottom + SOURCE_FRINGE))
                visible = sheet.crop(region).getchannel("A").getbbox()
                bounds = (region[0] + visible[0], region[1] + visible[1],
                          region[0] + visible[2], region[1] + visible[3])
                artwork = sheet.crop(bounds)
                originals[name] = artwork
                crops[name] = {"source": specification.path, "core_bounds": list(core),
                               "bounds": list(bounds), "core_pixels": count,
                               "rgba_sha256": hashlib.sha256(artwork.tobytes()).hexdigest()}
    manifest = {"version": 1, "theme": theme, "icon_size": ICON_SIZE,
                "provenance": "User-supplied yellow sheet with a matching generated supplement; no CC0 license is asserted.",
                "extraction": {"component_alpha_minimum": COMPONENT_ALPHA,
                               "minimum_component_pixels": COMPONENT_MIN_PIXELS,
                               "soft_alpha_fringe_pixels": SOURCE_FRINGE,
                               "connectivity": 4, "source_pixels_modified": False},
                "sources": files, "icons": crops,
                "groups": {name: list(members) for name, members in GROUPS}}
    return ThemeSources(originals, manifest)


def crop_artwork(path: Path) -> Image.Image:
    """Remove the author's transparent export padding, not visible pixels."""
    with Image.open(path) as source:
        artwork = source.convert("RGBA")
    bounds = artwork.getchannel("A").getbbox()
    if bounds is None:
        raise ValueError(f"Prompt asset has no visible pixels: {path}")
    return artwork.crop(bounds)


def fit_artwork(artwork: Image.Image, width: int, height: int) -> Image.Image:
    """Fit without stretching; filter premultiplied alpha to avoid dark edges."""
    scale = min(width / artwork.width, height / artwork.height)
    size = (
        max(1, min(width, round(artwork.width * scale))),
        max(1, min(height, round(artwork.height * scale))),
    )
    return artwork.convert("RGBa").resize(size, Image.Resampling.LANCZOS).convert("RGBA")


def single_icon(artwork: Image.Image) -> Image.Image:
    icon = Image.new("RGBA", (ICON_SIZE, ICON_SIZE))
    fitted = fit_artwork(artwork, ICON_SIZE - 2 * PADDING, ICON_SIZE - 2 * PADDING)
    icon.alpha_composite(fitted, ((ICON_SIZE - fitted.width) // 2, (ICON_SIZE - fitted.height) // 2))
    return icon


def group_icon(artworks: tuple[Image.Image, ...]) -> Image.Image:
    """Arrange original keycaps as one upper key and three lower keys."""
    gap = 1
    key_width = (ICON_SIZE - 2 * PADDING - 2 * gap) // 3
    keys = tuple(fit_artwork(art, key_width, key_width) for art in artworks)
    row_height = max(key.height for key in keys)
    group_width = 3 * key_width + 2 * gap
    group_height = 2 * row_height + gap
    left = (ICON_SIZE - group_width) // 2
    top = (ICON_SIZE - group_height) // 2
    icon = Image.new("RGBA", (ICON_SIZE, ICON_SIZE))
    for key, (column, row) in zip(keys, ((1, 0), (0, 1), (1, 1), (2, 1)), strict=True):
        x = left + column * (key_width + gap) + (key_width - key.width) // 2
        y = top + row * (row_height + gap) + (row_height - key.height) // 2
        icon.alpha_composite(key, (x, y))
    return icon


def compile_loaded(sources: ThemeSources, theme: str = DEFAULT_THEME) -> tuple[CompiledIcon, ...]:
    originals = sources.artworks
    icons = [CompiledIcon(name, single_icon(originals[name]).tobytes()) for name, _ in SOURCES]
    for name, members in GROUPS:
        icons.append(CompiledIcon(name, group_icon(tuple(originals[member] for member in members)).tobytes()))
    if theme == "yellow":
        icons.extend(CompiledIcon(name, single_icon(originals[name]).tobytes()) for name in EXTRA_NAMES)
    return tuple(icons)


def compile_icons(asset_dir: Path = ASSET_DIR, *, theme: str = DEFAULT_THEME) -> tuple[CompiledIcon, ...]:
    return compile_loaded(load_sources(theme, asset_dir), theme)


def source_digest(asset_dir: Path = ASSET_DIR, *, theme: str = DEFAULT_THEME) -> str:
    digest = hashlib.sha256()
    if theme == "xelu":
        files = [(filename, asset_dir / filename) for _, filename in SOURCES]
    else:
        files = [(sheet.path, ROOT / sheet.path) for sheet in SHEETS]
    for name, path in files:
        digest.update(name.encode("ascii") + b"\0")
        digest.update(path.read_bytes())
    return digest.hexdigest()


def render_header(icons: tuple[CompiledIcon, ...], asset_dir: Path = ASSET_DIR, *, theme: str = DEFAULT_THEME) -> bytes:
    attribution = ("Original artwork: Nicolae (Xelu) Berbece / Those Awesome Guys, CC0." if theme == "xelu" else
                   "Artwork: user-supplied yellow sheet and matching generated supplement; see source manifest.")
    lines = [
        "// Generated by tools/compile_prompt_icons.py. Do not edit.",
        f"// {attribution}",
        f"// Theme: {theme}; provenance: assets/native-input-prompts/manifest.json",
        f"// Source PNG SHA-256: {source_digest(asset_dir, theme=theme)}",
        "// Pixels are row-major, straight-alpha RGBA8; transparent padding is included.",
        "#pragma once",
        "#include <cstddef>",
        "#include <cstdint>",
        "#include <span>",
        "",
        "namespace Simpsons::Graphics::NativePromptIcons {",
        "enum class Icon : std::uint8_t {",
        *(f"    {icon.name}," for icon in icons),
        "    Count,",
        "};",
        "",
        "struct Image {",
        "    std::uint32_t width;",
        "    std::uint32_t height;",
        "    std::span<const std::uint8_t> rgba;",
        "};",
        f"inline constexpr std::uint32_t kWidth = {ICON_SIZE};",
        f"inline constexpr std::uint32_t kHeight = {ICON_SIZE};",
        "",
        "namespace detail {",
    ]
    for icon in icons:
        lines.append(f"inline constexpr std::uint8_t k{icon.name}[] = {{")
        for offset in range(0, len(icon.rgba), 32):
            lines.append("    " + ",".join(str(value) for value in icon.rgba[offset:offset + 32]) + ",")
        lines.extend(("};", ""))
    lines.append("inline constexpr Image kImages[] = {")
    lines.extend(f"    {{kWidth, kHeight, k{icon.name}}}," for icon in icons)
    lines.extend((
        "};",
        "static_assert(sizeof(kImages) / sizeof(kImages[0]) == static_cast<std::size_t>(Icon::Count));",
        "}  // namespace detail",
        "",
        "constexpr Image lookup(Icon icon) noexcept {",
        "    const auto index = static_cast<std::size_t>(icon);",
        "    return index < static_cast<std::size_t>(Icon::Count) ? detail::kImages[index] : Image{};",
        "}",
        "}  // namespace Simpsons::Graphics::NativePromptIcons",
        "",
    ))
    return "\n".join(lines).encode("ascii")


def write_preview(icons: tuple[CompiledIcon, ...], path: Path) -> None:
    columns = 6
    cell_width, cell_height = 144, 170
    rows = (len(icons) + columns - 1) // columns
    sheet = Image.new("RGB", (columns * cell_width, rows * cell_height), (37, 43, 52))
    draw = ImageDraw.Draw(sheet)
    font = ImageFont.load_default(size=14)
    for index, icon in enumerate(icons):
        x = (index % columns) * cell_width
        y = (index // columns) * cell_height
        # Show actual compiled pixels at 2x, so group glyphs can be inspected.
        preview = icon.image().resize((128, 128), Image.Resampling.NEAREST)
        sheet.paste(preview, (x + 8, y + 5), preview)
        draw.text((x + cell_width // 2, y + 140), icon.name, font=font, fill="white", anchor="mt")
    path.parent.mkdir(parents=True, exist_ok=True)
    sheet.save(path)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--theme", choices=("yellow", "xelu"), default=DEFAULT_THEME)
    parser.add_argument("--check", action="store_true", help="Verify the checked-in header without writing it")
    parser.add_argument("--preview", type=Path, metavar="PATH.png", help="Write a contact sheet of compiled icons")
    args = parser.parse_args(argv)
    sources = load_sources(args.theme)
    icons = compile_loaded(sources, args.theme)
    header = render_header(icons, theme=args.theme)
    manifest = (json.dumps(sources.manifest, indent=2, ensure_ascii=True) + "\n").encode("utf-8")
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_bytes() != header or not MANIFEST.exists() or MANIFEST.read_bytes() != manifest:
            print(f"Stale generated prompt artwork: {OUTPUT}. Run {Path(__file__).name}.", file=sys.stderr)
            return 1
        print(f"Verified {len(icons)} embedded {ICON_SIZE}x{ICON_SIZE} {args.theme} prompts.")
    else:
        OUTPUT.write_bytes(header)
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        MANIFEST.write_bytes(manifest)
        print(f"Wrote {len(icons)} embedded {args.theme} prompts to {OUTPUT}.")
    if args.preview:
        write_preview(icons, args.preview)
        print(f"Wrote contact sheet to {args.preview}.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

"""Synthetic text observations; these tests do not verify live gameplay."""

from pathlib import Path
import struct
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from gameplay_evidence import EvidenceError
from jev_observation import GRID_HEIGHT, GRID_WIDTH, observe_frame


WIDTH, HEIGHT = 1280, 720


def packed(r: int, g: int, b: int, alpha: int = 0) -> bytes:
    return struct.pack("<I", r | (g << 10) | (b << 20) | (alpha << 30))


def solid(r: int, g: int, b: int, alpha: int = 0) -> bytes:
    return packed(r, g, b, alpha) * (WIDTH * HEIGHT)


def metadata(**changes: object) -> dict[str, object]:
    return {
        "width": WIDTH,
        "height": HEIGHT,
        "format": "R10G10B10A2_UNORM_LE",
        "capture_source": "completed_front_renderer_readback",
        "front_copy_completed": True,
        "display_accepted": True,
        "presentation": 101,
        "draws": 20,
        "rigid_mesh_draws": 5,
        "skin_mesh_draws": 2,
        "sky_mesh_draws": 1,
        "scene_geometry_draws": 8,
        "frame_scene_geometry_draws": 2,
        **changes,
    }


class JevObservationTests(unittest.TestCase):
    def test_spatial_color_and_luminance_grid(self):
        row = packed(1023, 0, 0) * (WIDTH // 2) + packed(0, 0, 1023) * (WIDTH // 2)
        result = observe_frame(row * HEIGHT, metadata(), minimum_presentation=100)
        self.assertEqual((result["grid_width"], result["grid_height"]), (GRID_WIDTH, GRID_HEIGHT))
        self.assertEqual(result["color_grid"], ["R" * 16 + "B" * 16] * GRID_HEIGHT)
        self.assertEqual(len(result["luminance_grid"]), GRID_HEIGHT)
        self.assertTrue(all(len(line) == GRID_WIDTH for line in result["luminance_grid"]))
        self.assertNotEqual(result["luminance_grid"][0][0], result["luminance_grid"][0][-1])
        self.assertIsNone(result["frame_change_fraction"])
        self.assertEqual(result["presentation"], 101)

    def test_change_fraction_uses_rgb_and_ignores_alpha(self):
        red = solid(1023, 0, 0)
        blue = solid(0, 0, 1023)
        result = observe_frame(blue, metadata(), minimum_presentation=100, previous_data=red)
        self.assertAlmostEqual(result["frame_change_fraction"], 2 / 3, places=6)
        unchanged = observe_frame(red, metadata(), minimum_presentation=100,
                                  previous_data=solid(1023, 0, 0, 3))
        self.assertEqual(unchanged["frame_change_fraction"], 0)

    def test_rejects_unqualified_or_stale_capture(self):
        visible = solid(1, 0, 0)
        for invalid in (
            metadata(front_copy_completed=False),
            metadata(display_accepted=False),
            metadata(capture_source="private_movie_target_renderer_readback"),
            metadata(presentation=100),
            metadata(frame_scene_geometry_draws=0),
        ):
            with self.subTest(invalid=invalid), self.assertRaises(EvidenceError):
                observe_frame(visible, invalid, minimum_presentation=100)
        with self.assertRaises(EvidenceError):
            observe_frame(b"", metadata(), minimum_presentation=100)
        with self.assertRaises(EvidenceError):
            observe_frame(solid(0, 0, 0, 3), metadata(), minimum_presentation=100)

    def test_rejects_incomplete_previous_frame(self):
        with self.assertRaisesRegex(EvidenceError, "Previous packed frame"):
            observe_frame(solid(1, 0, 0), metadata(), minimum_presentation=100,
                          previous_data=b"short")

    def test_occluded_scene_is_opt_in_and_status_is_preserved(self):
        frame = solid(1023, 0, 0)
        occluded = metadata(display_accepted=False)
        with self.assertRaises(EvidenceError):
            observe_frame(frame, occluded, minimum_presentation=100)
        result = observe_frame(frame, occluded, minimum_presentation=100,
                               allow_occluded=True)
        self.assertIs(result["display_accepted"], False)
        self.assertEqual(result["color_grid"][0], "R" * GRID_WIDTH)


if __name__ == "__main__":
    unittest.main()

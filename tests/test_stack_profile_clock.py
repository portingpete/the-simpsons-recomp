from __future__ import annotations

import importlib.util
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("stack_clock", ROOT / "tools/stack_profile_clock.py")
clock = importlib.util.module_from_spec(spec)
spec.loader.exec_module(clock)


def capture(intervals, pid=55, frequency=1000):
    lines = ["M 0 K:\\Owned Game\\SimpsonsNative.exe", "M 1 C:\\Windows\\ntdll.dll",
             f"Q schema=1 pid={pid} frequency={frequency} capture_begin_qpc=100 capture_end_qpc=1000 sample_count={len(intervals)}"]
    for ordinal, (tid, begin, context, end) in enumerate(intervals):
        lines += [f"T sample={ordinal} tid={tid} begin_qpc={begin} context_qpc={context} end_qpc={end}",
                  f"S {tid} 0:abc 1:def -1:123"]
    return "\n".join(lines)


def frame(number, begin, end, tid=7, pid=55, frequency=1000, generation=2):
    return (f"[STACK FRAME CLOCK] schema=1 pid={pid} tid={tid} generation={generation} frame={number} "
            f"owner=1 begin_qpc={begin} end_qpc={end} frequency={frequency}")


class StackClockTests(unittest.TestCase):
    def test_legacy_modules_and_samples_are_unchanged(self):
        text = capture([(7, 150, 160, 170)])
        legacy_modules, legacy_samples = {}, []
        for line in text.splitlines():
            parts = line.split()
            if parts[0] == 'M':
                legacy_modules[int(parts[1])] = ' '.join(parts[2:])
            elif parts[0] == 'S':
                legacy_samples.append((int(parts[1]), [[int(m), int(r, 16)] for m, r in
                    (token.split(':') for token in parts[2:])]))
        parsed = clock.parse_capture(text)
        self.assertEqual(parsed['modules'], legacy_modules)
        self.assertEqual([(s['tid'], s['stack']) for s in parsed['samples']], legacy_samples)
        legacy = clock.parse_capture('\n'.join(line for line in text.splitlines() if line.startswith(('M ', 'S '))))
        report = clock.correlate(legacy, clock.parse_frame_clocks(frame(1, 100, 200)))
        self.assertEqual(report['status_counts'], {'unknown': 1})
        self.assertIsNone(report['samples'][0]['clock'])
        self.assertEqual(report['samples'][0]['raw'], 'S 7 0:abc 1:def -1:123')

    def test_matches_exact_process_thread_frame_and_conservative_interval(self):
        text = capture([(7, 150, 160, 210), (7, 250, 260, 270), (9, 310, 320, 330), (7, 850, 860, 870)])
        frames = clock.parse_frame_clocks('\n'.join([frame(1, 100, 200), frame(2, 200, 300), frame(3, 300, 400)]))
        report = clock.correlate(clock.parse_capture(text), frames, 55)
        self.assertEqual([s['status'] for s in report['samples']], ['matched', 'matched', 'unknown', 'unknown'])
        self.assertEqual([s['frame']['frame'] for s in report['samples'][:2]], [1, 2])
        # Resume may occur after the frame end; the captured context remains in frame1.
        self.assertEqual(report['samples'][0]['clock']['end_qpc'], 210)
        self.assertEqual(len(report['samples']), 4)

    def test_boundary_crossing_and_exact_touches_remain_ambiguous(self):
        text = capture([(7, 190, 205, 210), (7, 300, 305, 310), (7, 390, 400, 410)])
        frames = clock.parse_frame_clocks('\n'.join([frame(1, 100, 200), frame(2, 200, 300), frame(3, 300, 400)]))
        report = clock.correlate(clock.parse_capture(text), frames)
        self.assertEqual(report['status_counts'], {'boundary_ambiguous': 3})
        self.assertEqual([f['frame'] for f in report['samples'][0]['candidate_frames']], [1, 2])
        self.assertEqual([f['frame'] for f in report['samples'][1]['candidate_frames']], [2, 3])
        self.assertIsNone(report['samples'][2]['frame'])

    def test_foreign_pid_is_not_assigned_and_expected_pid_is_required_if_supplied(self):
        parsed = clock.parse_capture(capture([(7, 150, 160, 170)]))
        report = clock.correlate(parsed, clock.parse_frame_clocks(frame(1, 100, 200, pid=99)))
        self.assertEqual(report['status_counts'], {'unknown': 1})
        with self.assertRaisesRegex(ValueError, 'another owned PID'):
            clock.correlate(parsed, [], expected_pid=99)

    def test_missing_duplicate_out_of_range_and_wrong_thread_ordinals_rejected(self):
        text = capture([(7, 150, 160, 170)])
        variants = [text.replace('T sample=0 tid=7 begin_qpc=150 context_qpc=160 end_qpc=170\n', ''),
                    text + '\nT sample=0 tid=7 begin_qpc=150 context_qpc=160 end_qpc=170',
                    text.replace('T sample=0', 'T sample=1'), text.replace('T sample=0 tid=7', 'T sample=0 tid=8'),
                    text.replace('sample_count=1', 'sample_count=2')]
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                clock.parse_capture(variant)

    def test_qpc_regression_capture_escape_and_frequency_mismatch_rejected(self):
        variants = [capture([(7, 170, 160, 180)]), capture([(7, 90, 160, 170)]),
                    capture([(7, 150, 160, 1100)]), capture([(7, 150, 160, 170), (7, 165, 180, 190)]),
                    capture([(7, 150, 160, 170)]).replace('capture_end_qpc=1000', 'capture_end_qpc=90'),
                    capture([(7, 150, 160, 170)], frequency=0)]
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                clock.parse_capture(variant)
        parsed = clock.parse_capture(capture([(7, 150, 160, 170)]))
        with self.assertRaisesRegex(ValueError, 'frequencies differ'):
            clock.correlate(parsed, clock.parse_frame_clocks(frame(1, 100, 200, frequency=2000)))

    def test_duplicate_nonmonotonic_and_invalid_owner_frame_clocks_rejected(self):
        variants = ['\n'.join([frame(1, 100, 200), frame(1, 100, 200)]),
                    '\n'.join([frame(1, 100, 200), frame(2, 190, 300)]),
                    '\n'.join([frame(2, 100, 200), frame(1, 200, 300)]),
                    '\n'.join([frame(1, 100, 200), frame(2, 200, 300, frequency=2000)]),
                    frame(1, 200, 100), frame(1, 100, 200).replace('owner=1', 'owner=0')]
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                clock.parse_frame_clocks(variant)

    def test_generation_reset_and_missing_frame_gap_stay_explicit(self):
        frames = clock.parse_frame_clocks('\n'.join([frame(1, 100, 200), frame(3, 300, 400),
                                                     frame(1, 500, 600, generation=4)]))
        report = clock.correlate(clock.parse_capture(capture([(7, 250, 260, 270), (7, 550, 560, 570)])), frames)
        self.assertEqual(report['samples'][0]['status'], 'unknown')
        self.assertEqual(report['samples'][1]['frame']['generation'], 4)
        self.assertEqual(report['samples'][1]['frame']['frame'], 1)

    def test_empty_valid_capture_and_malformed_metadata_do_not_invent_samples(self):
        parsed = clock.parse_capture(capture([]))
        self.assertEqual(clock.correlate(parsed, [])['samples'], [])
        self.assertEqual(clock.correlate(parsed, [])['status_counts'], {})
        variants = [capture([]).replace('schema=1', 'schema=2'),
                    capture([]).replace('pid=55', 'pid=55 pid=55'),
                    capture([]).replace('frequency=1000', 'frequency=oops'),
                    capture([]).replace(' capture_end_qpc=1000', ''),
                    'T sample=0 tid=7 begin_qpc=150 context_qpc=160 end_qpc=170\nS 7 0:abc']
        for variant in variants:
            with self.subTest(variant=variant), self.assertRaises(ValueError):
                clock.parse_capture(variant)


if __name__ == '__main__':
    unittest.main()

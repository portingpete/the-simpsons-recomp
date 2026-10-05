"""Bounded IDA probe for original shared FX pool construction/merge/query.

Uses the already-qualified flat-image loader and independent word/call checker.
Pseudocode remains navigation evidence, never recovered executable guest C.
No original or reference file is modified. Run with a fresh run tag.
"""
from pathlib import Path
import importlib.util
import sys

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location('ida_pool_base', Path(__file__).with_name('ida_probe.py'))
probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(probe)
# The global lookup leaf 826B2528 has no pdata record. Its already byte-checked
# body is used separately; select its real manager caller here rather than
# claiming a guessed IDA function extent.
probe.SELECTED = (0x82C17DC0, 0x82C18530, 0x82C1D0C0, 0x826B7218)

if __name__ == '__main__':
    if '--check-image' in sys.argv:
        data, extents = probe.image_evidence()
        print(probe.IMAGE_SHA256, {probe.hx(pc): probe.hx(size) for pc, size in extents.items()})
    elif len(sys.argv) == 3 and sys.argv[1] == '--compare':
        probe.compare(sys.argv[2])
    else:
        probe.run()

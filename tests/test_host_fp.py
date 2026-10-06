"""Build the real FP template and native-to-AOT wrappers in an isolated fixture.

Run: python -B tests/test_host_fp.py
Requires Windows x64 ClangCL and the Visual Studio C++ tools. No original image,
generated files, CMake files, or parent build products are modified. The fixture
substitutes only guest memory/entry functions and the SEH diagnostic callback.
"""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def toolchain():
    import shutil
    compiler = Path(os.environ.get("CLANG_CL", "C:/Program Files/LLVM/bin/clang-cl.exe"))
    vswhere_candidates = [
        os.environ.get("VSWHERE", ""),
        "C:/Program Files (x86)/Microsoft Visual Studio/Installer/vswhere.exe",
    ]
    which_vswhere = shutil.which("vswhere.exe")
    if which_vswhere:
        vswhere_candidates.append(which_vswhere)
    vswhere = next((Path(p) for p in vswhere_candidates if p and Path(p).is_file()), None)
    if os.name != "nt" or not compiler.is_file() or vswhere is None:
        raise RuntimeError("Windows x64 ClangCL and Visual Studio C++ tools are required")
    install = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-property", "installationPath"], text=True, encoding="utf-8", errors="replace", timeout=30).strip()
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.is_file():
        raise RuntimeError(f"vcvars64.bat not found at {vcvars}")
    result = subprocess.run(f'"{vcvars}" >nul && set', shell=True,
                            capture_output=True, text=True, encoding="utf-8", errors="replace", check=True, timeout=60)
    env = dict(os.environ)
    for line in result.stdout.splitlines():
        if "=" in line and not line.startswith("="):
            key, value = line.split("=", 1)
            env[key.upper()] = value
    return compiler, env


SHIM = r'''
#pragma once
#include "ppc_context.h"
#include <windows.h>
#include <stdexcept>
namespace Simpsons {
struct Failure:std::runtime_error {using std::runtime_error::runtime_error;};
struct Runtime {
    uint8_t* base;
    uint8_t* pointer(uint32_t address,unsigned width,bool) {
        if(uint64_t(address)+width>0x1000) throw Failure("fixture memory bounds");
        return base+address;
    }
};
extern Runtime* active;
extern thread_local PPCContext* currentContext;
// This isolated FP fixture has no audio-reader state to unwind.
inline void unwindAudioReaderCall(PPCContext&) noexcept {}
int runOriginal(PPCContext&,uint8_t*);
uint32_t runThreadEntry(PPCContext&,uint8_t*,uint32_t);
LONG exceptionFilter(EXCEPTION_POINTERS*);
}
'''


class HostFPTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler, cls.env = toolchain()
        cls.temp = tempfile.TemporaryDirectory(prefix="simpsons-host-fp-")
        cls.addClassCleanup(cls.temp.cleanup)
        cls.build = Path(cls.temp.name)
        for source, destination in (
            ("runtime/ppc_context.template.h", "ppc_context.h"),
            ("runtime/engine_cpu_calls.h", "engine_cpu_calls.h"),
            ("runtime/guest_runner.cpp", "guest_runner.cpp"),
            ("runtime/stall_profiler.h", "stall_profiler.h"),
            ("runtime/stall_profiler.cpp", "stall_profiler.cpp"),
            ("tests/test_host_fp.cpp", "test_host_fp.cpp"),
        ):
            shutil.copyfile(ROOT / source, cls.build / destination)
        (cls.build / "runtime.h").write_text(SHIM, encoding="utf-8")
        (cls.build / "ppc_config.h").write_text(
            "#pragma once\n#define PPC_CONFIG_H_INCLUDED\n"
            "#define PPC_IMAGE_BASE 0x100\n#define PPC_IMAGE_SIZE 0x100\n"
            "#define PPC_CODE_BASE 0x100\n#define PPC_CODE_SIZE 0x100\n")
        (cls.build / "ppc_recomp_shared.h").write_text(
            '#pragma once\n#include "ppc_context.h"\nPPC_EXTERN_FUNC(_xstart);\n')
        result = subprocess.run([
            str(compiler), "/nologo", "/std:c++20", "/EHsc", "/O2", "/fp:strict",
            "/clang:-mssse3", "/DNOMINMAX", "/DWIN32_LEAN_AND_MEAN", "/I.",
            f"/I{ROOT / 'third_party/XenonRecomp/thirdparty/simde'}",
            "test_host_fp.cpp", "guest_runner.cpp", "stall_profiler.cpp", "/Fe:host_fp.exe"],
            cwd=cls.build, env=cls.env, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    def run_case(self, name):
        result = subprocess.run([str(self.build / "host_fp.exe"), name],
                                cwd=self.build, env=self.env, capture_output=True,
                                text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("PASS", result.stdout)

    def test_fresh_context_and_zero_cached_updates(self):
        self.run_case("helpers")

    def test_actual_inexact_scalar_and_vmx_arithmetic(self):
        self.run_case("arithmetic")

    def test_guest_rounding_and_flush_semantics(self):
        self.run_case("rounding")

    def test_main_entry_restores_host_on_return_and_throw(self):
        self.run_case("main")

    def test_worker_entry_restores_host_on_return_and_throw(self):
        self.run_case("worker")

    def test_engine_callback_restores_host_on_return_and_throw(self):
        self.run_case("callback")

    def test_nested_callbacks_restore_caller_guest_and_outer_host(self):
        self.run_case("nested")

    def test_seh_exit_restores_host(self):
        self.run_case("seh")

    def test_exception_unwind_host_destructor_can_do_inexact_math(self):
        self.run_case("unwind")


if __name__ == "__main__":
    unittest.main(verbosity=2)

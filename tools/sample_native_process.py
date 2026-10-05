"""Read-only LLDB stack sampling of this workspace's SimpsonsNative.exe only."""
from __future__ import annotations

import argparse
import contextlib
import ctypes
from ctypes import wintypes
import datetime as dt
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
EXPECTED = ROOT / "build/native/SimpsonsNative.exe"
OUTPUT_DIR = ROOT / "build/native-process-sampling"
LLDB = Path(r"C:\Program Files\LLVM\bin\lldb.exe")
PYTHON_DLL_DIR = Path(r"C:\Program Files\Blender Foundation\Blender 5.0")
PYTHON_HOME = PYTHON_DLL_DIR / "5.0/python"


def utc_now():
    return dt.datetime.now(dt.timezone.utc).isoformat()


@contextlib.contextmanager
def verified_process(pid, expected=EXPECTED):
    """Hold a query-only process handle throughout attach, preventing PID reuse."""
    expected = Path(expected).resolve()
    if expected.name != "SimpsonsNative.exe" or not expected.is_relative_to((ROOT / "build").resolve()) or not expected.is_file():
        raise ValueError("Expected executable must be an existing SimpsonsNative.exe beneath this workspace build directory")
    if os.name != "nt" or ctypes.sizeof(ctypes.c_void_p) != 8:
        raise RuntimeError("Requires 64-bit Windows Python")
    if not 1 <= pid < 0xFFFFFFFF:
        raise ValueError("PID must be a positive Windows process ID")
    k32 = ctypes.WinDLL("kernel32", use_last_error=True)
    k32.OpenProcess.argtypes = [wintypes.DWORD, wintypes.BOOL, wintypes.DWORD]
    k32.OpenProcess.restype = wintypes.HANDLE
    k32.CloseHandle.argtypes = [wintypes.HANDLE]
    k32.CloseHandle.restype = wintypes.BOOL
    k32.QueryFullProcessImageNameW.argtypes = [wintypes.HANDLE, wintypes.DWORD, wintypes.LPWSTR, ctypes.POINTER(wintypes.DWORD)]
    k32.QueryFullProcessImageNameW.restype = wintypes.BOOL
    k32.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    k32.GetExitCodeProcess.restype = wintypes.BOOL
    k32.GetProcessTimes.argtypes = [wintypes.HANDLE] + [ctypes.POINTER(wintypes.FILETIME)] * 4
    k32.GetProcessTimes.restype = wintypes.BOOL
    handle = k32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        raise ctypes.WinError(ctypes.get_last_error())
    try:
        name = ctypes.create_unicode_buffer(32768)
        size = wintypes.DWORD(len(name))
        if not k32.QueryFullProcessImageNameW(handle, 0, name, ctypes.byref(size)):
            raise ctypes.WinError(ctypes.get_last_error())
        actual = Path(name.value)
        if os.path.normcase(os.path.abspath(actual)) != os.path.normcase(str(expected)):
            raise ValueError(f"Refusing PID {pid}: executable is {actual}; required {expected}")
        status = wintypes.DWORD()
        if not k32.GetExitCodeProcess(handle, ctypes.byref(status)):
            raise ctypes.WinError(ctypes.get_last_error())
        if status.value != 259:
            raise ValueError(f"Refusing PID {pid}: process has exited ({status.value})")
        times = [wintypes.FILETIME() for _ in range(4)]
        if not k32.GetProcessTimes(handle, *(ctypes.byref(value) for value in times)):
            raise ctypes.WinError(ctypes.get_last_error())
        created = times[0].dwLowDateTime | (times[0].dwHighDateTime << 32)
        yield {"pid": pid, "image": str(actual), "process_created_filetime": created}
    finally:
        k32.CloseHandle(handle)


def debugger_environment():
    if not LLDB.is_file() or not (PYTHON_DLL_DIR / "python311.dll").is_file():
        raise RuntimeError("Verified LLDB / Blender Python 3.11 dependency is missing")
    if not (PYTHON_HOME / "lib/encodings/__init__.py").is_file():
        raise RuntimeError("Verified Blender Python 3.11 standard library is missing")
    env = os.environ.copy()
    env["PATH"] = str(PYTHON_DLL_DIR) + os.pathsep + env.get("PATH", "")
    env["PYTHONHOME"] = str(PYTHON_HOME)
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    env.pop("PYTHONPATH", None)
    # Only local PDBs, with no inherited symbol-server setting.
    env["_NT_SYMBOL_PATH"] = str(EXPECTED.parent)
    env.pop("_NT_ALT_SYMBOL_PATH", None)
    return env


def _frame_record(frame, target):
    pc, sp = frame.GetPC(), frame.GetSP()
    function = frame.GetFunctionName() or frame.GetSymbol().GetName()
    module = frame.GetModule().GetFileSpec().GetFilename()
    line = frame.GetLineEntry()
    address = frame.GetPCAddress()
    symbol = frame.GetSymbol()
    start = symbol.GetStartAddress().GetLoadAddress(target) if symbol.IsValid() else None
    result = {"index": frame.GetFrameID(), "pc": f"0x{pc:016X}",
              "sp": f"0x{sp:016X}", "module": module, "function": function,
              "inline": frame.IsInlined(), "file_address": f"0x{address.GetFileAddress():016X}"}
    if start is not None and start <= pc:
        result["symbol_offset"] = pc - start
    if line.IsValid():
        spec = line.GetFileSpec()
        result["source"] = str(Path(spec.GetDirectory() or "") / (spec.GetFilename() or ""))
        result["line"] = line.GetLine()
    if function:
        originals = re.findall(r"(?:sub_|__imp__sub_)([0-9A-Fa-f]{8})(?![0-9A-Fa-f])", function)
        if originals:
            result["aot_original_addresses"] = ["0x" + value.upper() for value in sorted(set(originals))]
    return result


def lldb_sample(debugger, pid, output, max_frames, max_threads, budget):
    """Run inside LLDB. Never evaluate target expressions, write memory, or kill."""
    import lldb
    report = {"schema": 1, "started_utc": utc_now(), "pid": pid,
              "debugger": debugger.GetVersionString(), "threads": [], "detached": False}
    target = None
    previous_async = debugger.GetAsync()
    try:
        with verified_process(pid) as identity:
            report.update(identity)
            debugger.SetAsync(False)
            debugger.HandleCommand("settings set target.load-script-from-symbol-file false")
            target = debugger.CreateTarget(str(EXPECTED))
            if not target.IsValid():
                raise RuntimeError("Cannot create symbol target for the verified executable")
            error = lldb.SBError()
            try:
                process = target.AttachToProcessWithID(debugger.GetListener(), pid, error)
                if error.Fail() or not process.IsValid():
                    raise RuntimeError("Attach failed: " + str(error))
                if process.GetProcessID() != pid:
                    raise RuntimeError("LLDB returned an unexpected PID")
                report["attached_utc"] = utc_now()
                start = time.monotonic()
                report["thread_count"] = process.GetNumThreads()
                if report["thread_count"] > max_threads:
                    report["thread_limit_reached"] = True
                for index in range(min(report["thread_count"], max_threads)):
                    if time.monotonic() - start >= budget:
                        report["truncated_for_time"] = True
                        break
                    thread = process.GetThreadAtIndex(index)
                    item = {"tid": thread.GetThreadID(), "name": thread.GetName(),
                            "stop_reason": thread.GetStopReason(), "frames": []}
                    report["threads"].append(item)
                    for number in range(max_frames):
                        if time.monotonic() - start >= budget:
                            report["truncated_for_time"] = True
                            break
                        frame = thread.GetFrameAtIndex(number)
                        if not frame.IsValid():
                            break
                        item["frames"].append(_frame_record(frame, target))
                    if len(item["frames"]) == max_frames:
                        item["frame_limit_reached"] = True
                report["sampling_seconds"] = time.monotonic() - start
            finally:
                # A failed attach may still leave a valid process on the target.
                attached = target.GetProcess()
                if attached.IsValid() and attached.GetState() not in (lldb.eStateExited, lldb.eStateDetached, lldb.eStateInvalid):
                    result = attached.Detach(False)  # Explicitly resume on detach.
                    report["detached"] = result.Success()
                    if result.Fail():
                        report["detach_error"] = str(result)
                else:
                    report["target_state_at_cleanup"] = attached.GetState()
    except Exception as exc:
        report["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        debugger.SetAsync(previous_async)
        report["finished_utc"] = utc_now()
        Path(output).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("NATIVE_SAMPLE_RESULT " + json.dumps({k: report.get(k) for k in ("pid", "thread_count", "detached", "error", "detach_error")}), flush=True)


def self_test():
    checks = []
    for pid, label in ((0, "zero PID"), (0xFFFFFFFF, "invalid PID"), (os.getpid(), "wrong executable")):
        try:
            with verified_process(pid):
                raise AssertionError("unsafe PID accepted")
        except (ValueError, OSError) as exc:
            checks.append({"check": label, "rejected": True, "detail": str(exc)})
    # PID 0xFFFFFFFC is not a discoverable or selected unrelated process.
    try:
        with verified_process(0xFFFFFFFC):
            raise AssertionError("nonexistent PID accepted")
    except (ValueError, OSError) as exc:
        checks.append({"check": "nonexistent PID", "rejected": True, "detail": str(exc)})
    env = debugger_environment()
    _creation = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
    run = subprocess.run([str(LLDB), "--no-lldbinit", "--script-language", "python", "--batch",
                          "-o", 'script import sys; print("NATIVE_PYTHON_OK", sys.version, flush=True)'],
                         env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace", timeout=15, creationflags=_creation)
    if run.returncode or "NATIVE_PYTHON_OK 3.11." not in run.stdout:
        raise RuntimeError("LLDB startup check failed: " + run.stdout)
    checks.append({"check": "LLDB Python startup without target", "passed": True, "output": run.stdout})
    print(json.dumps({"checks": checks, "game_attached": False}, indent=2))
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--max-frames", type=int, default=48)
    parser.add_argument("--max-threads", type=int, default=128)
    parser.add_argument("--budget", type=float, default=8.0, help="Soft stack-reading budget in seconds; always detach afterward")
    parser.add_argument("--self-test", action="store_true", help="Reject unsafe PIDs and check LLDB startup; never attach")
    args = parser.parse_args()
    if args.self_test:
        return self_test()
    if args.pid is None:
        parser.error("--pid is required")
    if not 1 <= args.max_frames <= 128 or not 1 <= args.max_threads <= 256 or not 0.1 <= args.budget <= 30:
        parser.error("Bounds: frames 1..128, threads 1..256, budget 0.1..30 seconds")
    # Validate identity before creating artifacts or starting the debugger.
    with verified_process(args.pid):
        output = args.output or OUTPUT_DIR / ("sample-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%S.%fZ") + f"-{args.pid}.json")
        output = output.resolve()
        if output.parent != OUTPUT_DIR.resolve() or output.suffix.lower() != ".json":
            parser.error("--output must be a .json file directly in build/native-process-sampling")
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
        # Exclusive creation prevents overwriting an earlier run's evidence.
        with output.open("x", encoding="utf-8") as handle:
            handle.write(json.dumps({"pid": args.pid, "status": "starting", "started_utc": utc_now()}) + "\n")
        log_path = output.with_suffix(".lldb.log")
        sample_arguments = ", ".join(repr(value) for value in
                                     (args.pid, str(output), args.max_frames, args.max_threads, args.budget))
        expression = ("script import runpy; runpy.run_path(" + repr(str(Path(__file__).resolve()))
                      + ", run_name='native_sampling_lldb')['lldb_sample'](lldb.debugger, "
                      + sample_arguments + ")")
        command = [str(LLDB), "--no-lldbinit", "--script-language", "python", "--batch", "-o", expression]
        print(f"Sampling verified game PID {args.pid}; output {output}", flush=True)
        with log_path.open("x", encoding="utf-8") as log:
            _popen_creation = getattr(subprocess, 'CREATE_NO_WINDOW', 0)
            process = subprocess.Popen(command, env=debugger_environment(), cwd=ROOT, stdin=subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT, creationflags=_popen_creation)
            # Never forcibly kill an attached debugger (or the game) on timeout.
            warned = False
            launched = time.monotonic()
            while True:
                try:
                    status = process.wait(timeout=1)
                    break
                except subprocess.TimeoutExpired:
                    if not warned and process.poll() is None and time.monotonic() - launched > 30:
                        print(f"Debugger PID {process.pid} is still running; not terminating it. Cleanup remains responsible for detaching.", file=sys.stderr, flush=True)
                        warned = True
        report = json.loads(output.read_text(encoding="utf-8"))
        print(f"Threads captured: {len(report.get('threads', []))}; detached: {report.get('detached')}; LLDB exit: {status}")
        print(f"Debugger log: {log_path}")
        if report.get("error") or report.get("detach_error"):
            print(report.get("error") or report.get("detach_error"), file=sys.stderr)
        return 0 if status == 0 and report.get("detached") and not report.get("error") else 1


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, RuntimeError) as exc:
        print(f"Sampling refused/failed: {exc}", file=sys.stderr)
        raise SystemExit(2)

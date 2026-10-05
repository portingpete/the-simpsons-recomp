"""Launch a command on the interactive Windows desktop and wait for it.

Codex terminals can run on a separate desktop in the same RDP session. A game
launched there has a window, but DXGI reports it occluded to the user. This
wrapper puts the benchmark controller and its game child on WinSta0\\Default.
"""

from __future__ import annotations

import argparse
import ctypes
import subprocess
import sys
from ctypes import wintypes
from pathlib import Path


class STARTUPINFOW(ctypes.Structure):
    _fields_ = [
        ("cb", wintypes.DWORD),
        ("lpReserved", wintypes.LPWSTR),
        ("lpDesktop", wintypes.LPWSTR),
        ("lpTitle", wintypes.LPWSTR),
        ("dwX", wintypes.DWORD),
        ("dwY", wintypes.DWORD),
        ("dwXSize", wintypes.DWORD),
        ("dwYSize", wintypes.DWORD),
        ("dwXCountChars", wintypes.DWORD),
        ("dwYCountChars", wintypes.DWORD),
        ("dwFillAttribute", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("wShowWindow", wintypes.WORD),
        ("cbReserved2", wintypes.WORD),
        ("lpReserved2", ctypes.POINTER(wintypes.BYTE)),
        ("hStdInput", wintypes.HANDLE),
        ("hStdOutput", wintypes.HANDLE),
        ("hStdError", wintypes.HANDLE),
    ]


class PROCESS_INFORMATION(ctypes.Structure):
    _fields_ = [
        ("hProcess", wintypes.HANDLE),
        ("hThread", wintypes.HANDLE),
        ("dwProcessId", wintypes.DWORD),
        ("dwThreadId", wintypes.DWORD),
    ]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", nargs=argparse.REMAINDER)
    args = parser.parse_args()
    command = args.command[1:] if args.command[:1] == ["--"] else args.command
    if not command:
        parser.error("a command must follow --")
    executable = str(Path(command[0]).resolve())
    line = ctypes.create_unicode_buffer(subprocess.list2cmdline([executable, *command[1:]]))
    startup = STARTUPINFOW()
    startup.cb = ctypes.sizeof(startup)
    startup.lpDesktop = "WinSta0\\Default"
    process = PROCESS_INFORMATION()
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateProcessW.argtypes = [
        wintypes.LPCWSTR, wintypes.LPWSTR, wintypes.LPVOID, wintypes.LPVOID,
        wintypes.BOOL, wintypes.DWORD, wintypes.LPVOID, wintypes.LPCWSTR,
        ctypes.POINTER(STARTUPINFOW), ctypes.POINTER(PROCESS_INFORMATION),
    ]
    kernel.CreateProcessW.restype = wintypes.BOOL
    kernel.WaitForSingleObject.argtypes = [wintypes.HANDLE, wintypes.DWORD]
    kernel.WaitForSingleObject.restype = wintypes.DWORD
    kernel.GetExitCodeProcess.argtypes = [wintypes.HANDLE, ctypes.POINTER(wintypes.DWORD)]
    kernel.GetExitCodeProcess.restype = wintypes.BOOL
    kernel.CloseHandle.argtypes = [wintypes.HANDLE]
    kernel.CloseHandle.restype = wintypes.BOOL
    # Hide only the controller's console; its GUI child is launched normally.
    if not kernel.CreateProcessW(executable, line, None, None, False, 0x08000000,
                                 None, str(Path.cwd()), ctypes.byref(startup),
                                 ctypes.byref(process)):
        raise OSError(ctypes.get_last_error(), "Could not launch on the interactive desktop")
    print(f"Visible-desktop controller PID {process.dwProcessId}", flush=True)
    try:
        if kernel.WaitForSingleObject(process.hProcess, 0xFFFFFFFF) != 0:
            raise OSError(ctypes.get_last_error(), "Could not wait for visible-desktop controller")
        code = wintypes.DWORD()
        if not kernel.GetExitCodeProcess(process.hProcess, ctypes.byref(code)):
            raise OSError(ctypes.get_last_error(), "Could not read controller exit code")
        return int(code.value)
    finally:
        kernel.CloseHandle(process.hThread)
        kernel.CloseHandle(process.hProcess)


if __name__ == "__main__":
    raise SystemExit(main())

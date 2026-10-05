#pragma once
#include "runtime.h"

// Xbox ABI wrappers. NtOpenFile has SIX arguments (r7=share, r8=options).
// Existing Runtime owns guest IDs and closes shared KernelHandle objects.
PPC_EXTERN_FUNC(__imp__NtCreateFile);
PPC_EXTERN_FUNC(__imp__NtOpenFile);
PPC_EXTERN_FUNC(__imp__NtReadFile);
PPC_EXTERN_FUNC(__imp__NtQueryInformationFile);
PPC_EXTERN_FUNC(__imp__NtQueryDirectoryFile);
PPC_EXTERN_FUNC(__imp__NtQueryVolumeInformationFile);
PPC_EXTERN_FUNC(__imp__NtSetInformationFile);
PPC_EXTERN_FUNC(__imp__NtWriteFile);
PPC_EXTERN_FUNC(__imp__NtFlushBuffersFile);
PPC_EXTERN_FUNC(__imp__NtDeleteFile);

#include "file_read_source.h"
#include <memory>
#include <string>
namespace Simpsons {
// Sequence of the newest recorded read. Take it BEFORE copying the bytes being attributed and pass it as
// maxSequence so a read that completes after the copy (another thread) cannot be mistaken for its source.
uint64_t fileReadSequence();
FileReadSource recentFileReadCovering(uint32_t address,uint32_t length,uint64_t maxSequence=UINT64_MAX);
}

#pragma once

#include <Windows.h>
#include <atomic>
#include <string>

#include "cameraunlock/logging/file_log.h"

namespace headtracking {

// Opens Titanfall2HeadTracking.log next to the game EXE (truncated each launch).
// Called once from the bootstrap thread before the first HT_LOG so the
// loader-presence line is captured.
inline void OpenLogFile() {
    wchar_t buf[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring dir;
    if (len > 0 && len < MAX_PATH) {
        std::wstring exe(buf, len);
        const auto slash = exe.find_last_of(L"\\/");
        if (slash != std::wstring::npos) dir = exe.substr(0, slash + 1);
    }
    const std::wstring logPath = dir + L"Titanfall2HeadTracking.log";
    // Keep one previous generation. The fault handler's report asks the user to
    // send this log, and the user relaunches the game before they go looking for
    // it - which would otherwise truncate away the crash they are reporting.
    DWORD rotateError = 0;
    // Named, not temporaries in the condition: they are destroyed at the end of
    // the if-condition, so the deallocation runs before GetLastError() below.
    const std::wstring prevPath = dir + L"Titanfall2HeadTracking.prev.log";
    if (!MoveFileExW(logPath.c_str(), prevPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING)) {
        rotateError = GetLastError();
    }
    cameraunlock::logging::Open(logPath);
    // Open truncates, so a failed rotation has already destroyed the generation
    // the rotation existed to keep. A missing source is the first-run case.
    if (rotateError != 0 && rotateError != ERROR_FILE_NOT_FOUND) {
        cameraunlock::logging::Line(
            "Could not rotate the previous log (error %lu) - the previous session log was overwritten",
            rotateError);
    }
}

// The log has two tiers, and the distinction is what makes it usable as the
// first thing we ask a user for.
//
// HT_LOG is the lifecycle channel and is ALWAYS written: which build was
// fingerprinted, which profile matched or did not, whether the hook installed,
// which map the gates saw, and the fault report. Every one of those lines is
// the answer to a bug report, and every one of them is produced before or
// independently of any INI setting. Gating them on `[Debug] LogToFile` (which
// defaults to false) closed the file three lines into startup and left a
// zero-byte log next to the EXE - so the README's "turn logging on and send me
// the log" instruction produced a file with none of the lines it asks for, and
// the fault handler's "please report this log" wrote into a closed handle.
//
// HT_TRACE is the per-frame channel - the [view] line and the one-shot struct
// dump - and stays behind `[Debug] LogToFile`, which is what that key is for.
inline std::atomic<bool>& VerboseFlag() {
    static std::atomic<bool> flag{false};
    return flag;
}

inline void SetVerboseLogging(bool enabled) { VerboseFlag().store(enabled, std::memory_order_relaxed); }
inline bool VerboseLogging() { return VerboseFlag().load(std::memory_order_relaxed); }

}  // namespace headtracking

#define HT_LOG(...) ::cameraunlock::logging::Line(__VA_ARGS__)

#define HT_TRACE(...)                                                    \
    do {                                                                 \
        if (::headtracking::VerboseLogging()) {                          \
            ::cameraunlock::logging::Line(__VA_ARGS__);                  \
        }                                                                \
    } while (false)

// Lock-free, WriteFile-direct. ONLY for the SEH handler in the render detour:
// a fault taken inside the normal logger orphans its mutex, and calling the
// normal logger from the handler would then block the render thread forever on
// a lock it can never take - a hang, on the one path whose whole job is to
// leave the game running.
#define HT_LOG_FAULT(...) ::cameraunlock::logging::EmergencyLine(__VA_ARGS__)

#pragma once

// Calling a Source interface method by vtable slot.
//
// The game exposes IVEngineClient and ICvar as pointers held in client.dll's
// data, and this mod reaches three methods that way: GetLevelName and
// GetViewAngles on the engine client, FindVar on the cvar interface. All three
// are the same four-step chase - read the interface pointer out of the module,
// read its vtable, index the slot, call it - and it was written out three times
// before, which is three chances for the next one to get a null check wrong and
// three places to fix when a build moves something.
//
// NO SEH in here, deliberately. Each caller already wraps its own call in
// __try, and the guard has to sit where the CALL is: a helper that swallowed
// faults internally would report "no interface" for a fault taken inside the
// game's own method, which is a different failure with a different answer.

#include <cstdint>

#include "build_profile.h"

namespace headtracking {

// The interface pointer at `interfacePtrRva` within client.dll, or nullptr if
// there is no active profile, the offset is unset, or the pointer is not up yet.
inline void* EngineInterface(uint32_t interfacePtrRva) {
    if (!HasActiveProfile() || interfacePtrRva == 0) return nullptr;
    return *reinterpret_cast<void**>(ClientBase() + interfacePtrRva);
}

// The function in `iface`'s vtable at byte offset `slotOffset`, or nullptr if
// any link in the chain is missing. `Fn` is the method's signature with the
// `this` pointer written out as the first parameter (MS x64 passes it in RCX).
template <typename Fn>
inline Fn InterfaceMethod(void* iface, uint32_t slotOffset) {
    if (!iface) return nullptr;
    auto* vtable = *reinterpret_cast<void***>(iface);
    if (!vtable) return nullptr;
    return reinterpret_cast<Fn>(
        *reinterpret_cast<void**>(reinterpret_cast<uint8_t*>(vtable) + slotOffset));
}

}  // namespace headtracking

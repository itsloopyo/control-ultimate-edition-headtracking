#pragma once

#include <cstdint>

namespace ControlHT {

// Reads a trivially-copyable value from an address that may not be mapped.
//
// Every game address this mod dereferences comes from a build profile or from
// walking the game's own structures, so a wrong one is a possibility rather than
// a contract violation - and reading it has to fail rather than take the
// player's session down. Returns false when the read faulted, leaving `out`
// untouched.
template <typename T>
bool SafeRead(uintptr_t address, T& out) {
    __try {
        out = *reinterpret_cast<const T*>(address);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

} // namespace ControlHT

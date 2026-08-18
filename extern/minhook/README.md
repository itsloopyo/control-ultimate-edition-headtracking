# MinHook (vendored)

Bundled copy of MinHook, compiled into `ControlHeadTracking.asi` by the
`minhook` CMake target and reached through `cameraunlock_hooks`.

## Snapshot

- Upstream: https://github.com/TsudaKageyu/minhook
- Tag: `v1.3.4`
- Commit: `c3fcafdc10146beb5919319d0683e44e3c30d537`
- License: BSD-2-Clause, see `LICENSE.txt` (verbatim from upstream)
- Fetched at: 2026-08-18

Only the sources the build needs are vendored (`include/`, `src/`), not the
upstream solution files, samples or tests. Nothing else is removed or reformatted.

## Local modifications

This copy is **not** stock v1.3.4. One change, in `src/hook.c`:

- `Initialize()` takes MinHook's heap from `GetProcessHeap()` rather than
  `HeapCreate()`, and `Uninitialize()` correspondingly does not `HeapDestroy()`
  it. A private heap created inside a hooking library that a game loads late has
  no owner once the game tears down, so this avoids handing MinHook a heap whose
  lifetime the host process does not know about.

Both edit sites are marked `MODIFIED (control-ultimate-edition-headtracking)`
in the source. BSD-2-Clause permits modification; it is recorded here and in
THIRD-PARTY-NOTICES.md so nobody mistakes this tree for an unmodified upstream
checkout, and so the change can be re-applied when the snapshot is bumped.

## Hacker Disassembler Engine

`src/hde/` is Hacker Disassembler Engine 32/64, Copyright (c) 2008-2009
Vyacheslav Patkov, redistributed by MinHook under its own BSD license. Its full
conditions and disclaimer are in `LICENSE.txt` and must travel with both the
source and any binary built from it.

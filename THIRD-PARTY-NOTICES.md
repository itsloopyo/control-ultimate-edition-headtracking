# Third-Party Notices

Control Head Tracking itself is MIT licensed, Copyright (c) 2026 itsloopyo -
see the LICENSE file shipped alongside this one.

It ships and/or links against the following third-party components, which keep
their own licenses.

## Ultimate ASI Loader

- **Version:** v9.7.2
- **License:** MIT
- **Upstream:** https://github.com/ThirteenAG/Ultimate-ASI-Loader
- **Usage:** `dinput8.dll` from the upstream release is copied into the game directory as `winmm.dll` by `install.cmd` to host `ControlHeadTracking.asi`. Bundled in the release ZIP and used as the install-time source.
- **Bundled:** yes in the installer ZIP (`vendor/ultimate-asi-loader/`); no in
  the Nexus ZIP, where the loader is the user's own.

Copyright (c) 2023 ThirteenAG

---

## MinHook

- **Version:** v1.3.4 (commit `c3fcafdc10146beb5919319d0683e44e3c30d537`), vendored at `extern/minhook`
- **License:** BSD-2-Clause
- **Upstream:** https://github.com/TsudaKageyu/minhook
- **Usage:** Statically linked into `ControlHeadTracking.asi` to provide inline x86/x64 function hooking.
- **Bundled:** yes (compiled into the shipped `.asi`).
- **Modified:** yes. One change, in `src/hook.c`: MinHook takes its heap from
  `GetProcessHeap()` rather than creating a private one with `HeapCreate()`, and
  does not `HeapDestroy()` it on shutdown. Both sites are marked `MODIFIED` in
  the source and described in `extern/minhook/README.md`. This is a modified
  copy, not stock upstream.

MinHook - The Minimalistic API Hooking Library for x64/x86
Copyright (C) 2009-2017 Tsuda Kageyu. All rights reserved.

The full license text, including the Hacker Disassembler Engine conditions
below, is reproduced verbatim at `extern/minhook/LICENSE.txt` in this repository
and ships as `licenses/minhook-LICENSE.txt` in both release ZIPs, alongside the
binary it is compiled into.

---

## Hacker Disassembler Engine 32/64

- **Version:** as redistributed inside MinHook v1.3.4 (`extern/minhook/src/hde`)
- **License:** BSD-2-Clause
- **Upstream:** no live upstream of its own; redistributed inside MinHook at https://github.com/TsudaKageyu/minhook
- **Usage:** MinHook's length-disassembler, used to measure the instructions it relocates when building a trampoline. Statically linked into `ControlHeadTracking.asi`.
- **Bundled:** yes (source at `extern/minhook/src/hde`, compiled into the shipped `.asi`).

Hacker Disassembler Engine 32 C / Hacker Disassembler Engine 64 C
Copyright (c) 2008-2009, Vyacheslav Patkov. All rights reserved.

Its license requires the copyright notice, the list of conditions and the
disclaimer to travel with both source and binary redistributions. They are in
`extern/minhook/LICENSE.txt` here, and in `licenses/minhook-LICENSE.txt` in the
installer ZIP and the Nexus ZIP, next to the `.asi` they are compiled into.

---

## OpenTrack

- **Version:** protocol only (no code)
- **License:** ISC
- **Upstream:** https://github.com/opentrack/opentrack
- **Usage:** This mod implements the OpenTrack UDP wire format (six little-endian doubles per packet). No OpenTrack code is linked.
- **Bundled:** no.

---

## CameraUnlock Core

- **Version:** submodule `3465659`
- **License:** MIT
- **Upstream:** https://github.com/itsloopyo/cameraunlock-core
- **Usage:** Built from source as a static library via git submodule.
- **Bundled:** yes (compiled into the shipped `.asi`).

Copyright (c) 2026 CameraUnlock

MIT requires its copyright and permission notice to travel with every copy,
including a binary one, so the upstream text ships as
`licenses/cameraunlock-core-LICENSE.txt` in both release ZIPs next to the `.asi`
it is compiled into. It is a different copyright holder from this mod's own
LICENSE, which is why it gets its own file rather than being covered by it.

---

## Coherent GT

- **Version:** whatever ships with the game (`coherentuigt.dll`)
- **License:** proprietary, Coherent Labs AD
- **Upstream:** https://coherent-labs.com
- **Usage:** Control renders its HUD through Coherent GT. To keep the crosshair
  on the point shots actually land, this mod attaches to the copy already loaded
  in the game process and asks it to run a small script of our own against the
  HUD document. It refuses to attach unless the loaded library matches the exact
  build the addresses were derived against.
- **Bundled:** no. Nothing from Coherent GT is copied, redistributed,
  decompiled, or shipped in either release ZIP; the mod only calls into the copy
  the user's own game installed. Listed here for transparency about what the mod
  touches at runtime, not because anything is redistributed.

---

## Game credits

Control: Ultimate Edition is developed and published by Remedy Entertainment Plc
and 505 Games. This mod is an unofficial modification, not affiliated with or
endorsed by either party.

No game code, extracted assets, or data files are contained in this repository
or in any release it produces. The mod is built entirely from our own source
plus the permissively-licensed components listed above, and requires a
legitimate copy of the game to do anything at all.

The one piece of game-derived material here is `assets/readme-clip.gif`, a
ten-second clip captured while playing, shown in the README to demonstrate what
the mod does. Copyright in the footage stays with Remedy Entertainment Plc and
505 Games. It identifies the game this mod is for and shows the mod working,
which is the customary use of gameplay footage on a mod page, and it ships in
neither release ZIP. The names Control, Remedy Entertainment and 505 Games are
likewise used only to say which game this mod is for.

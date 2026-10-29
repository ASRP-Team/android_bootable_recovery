# ASRP — Another Super Recovery Project

**English** · [简体中文](README_CN.md)

ASRP (Another Super Recovery Project) is an open-source, native Android recovery
environment. It is a fork of [AERA Recovery](https://github.com/AERA-Recovery),
which in turn builds on Android Recovery, TeamWin Recovery Project (TWRP) and
OrangeFox Recovery. ASRP adds only a limited set of features and broader device
compatibility, and supports Android 14–17.

- **Platform:** Android 14–17, A/B and non-A/B devices, GKI and legacy kernels
- **Interface:** native LVGL UI for touch and hardware keys, portrait and landscape
- **License:** GNU GPL v3 or later (see [License](#license))

## Features

- **Recovery workspace** — home screen, app launcher, quick settings, status bar,
  multiple UI densities and community translations.
- **Flashing** — ZIP, raw/sparse IMG and fastboot images; A/B slot selection,
  logical/dynamic partitions, payload inspection, ARB downgrade protection and
  optional "keep ABL / keep recovery" handling. Fast flash for standalone payloads
  is available as an experimental path.
- **Backup and restore** — partition backups with compression and checksum
  verification, restores to device or directly to a folder on the connected
  computer, and an encrypted App Backup Vault (Restic repository) for per-app
  backups.
- **Recovery updates** — in-recovery release checks and signed update downloads.
- **Root Manager** — patches the target slot without rebooting, keeps a verified
  full rollback image, manages KernelSU modules, and installs or removes the
  manager APK. It works in two levels: first the patching **family**, then the
  concrete **distribution**.
  - KernelSU family (patches `init_boot` through `ksud`): KernelSU, KernelSU Next,
    SukiSU Ultra, BakaSU, FinaSU, YukiSU.
  - APatch / KernelPatch family (patches the kernel in `boot` through `kptools`):
    APatch, FolkPatch.
  - Optional **SuperKey** for YukiSU, APatch and FolkPatch, entered in memory only
    and never written to storage or receipts.
- **PC workbench** — a browser workspace served by recovery itself over HTTPS on
  port 443. No PC application, wireless ADB or device-tree flag is required.
  Physical approval on the phone authorizes each browser; sessions use bearer
  tokens. Includes upload → verify → install/flash, a terminal tab, backups,
  file management, device information, log export, power actions and SSH controls.
  USB access is possible with `adb forward` when Wi-Fi is unavailable.
- **Remote screen mirroring** — optional MJPEG view of the recovery screen over
  HTTP port 80.
- **Networking** — Wi-Fi, optional mobile data builds, SFTP/NAS network storage,
  SSH/SFTP server (Dropbear) and a plain FTP server for legacy clients.
- **Plugins** — signed AERAP packages, plugin manager and a documented host API
  for extensions such as browser, media player, gallery, recorder, streams and
  optional retro games (Doom, RetroArch, Artillery).
- **Tools** — file manager, image viewer, media player, screen recorder, browser,
  terminal, app backup vault and diagnostic log export.

## How ASRP differs from AERA

ASRP is not a theme-only rename. Current differences:

1. **New identity** — the recovery UI and PC workbench present ASRP. Upstream
   licenses, copyright notices and AERA references are preserved, and internal
   storage paths and protocols keep their original names for compatibility.
2. **Two-level Root Manager** — AERA offers a flat provider list. ASRP separates
   the patching family from the distribution, adds BakaSU, FinaSU, YukiSU,
   FolkPatch and APatch to the same workflow, and applies the same interaction
   model to both the recovery UI and the browser workbench.
3. **SuperKey handling** — YukiSU and the KernelPatch managers accept an optional
   SuperKey before patching; it is passed as part of the root request and never
   echoed back by the API.
4. **SSH and SFTP** — an integrated Dropbear-based server with start/stop,
   password and authorized-key management, controllable from the PC workbench.
5. **FTP** — an optional plain FTP server that reuses the SSH root password for
   legacy clients. FTP is unencrypted; SFTP remains the recommended option.
6. **Broader PC root controls** — the workbench exposes every distribution,
   KernelPatch targets (`boot_a` / `boot_b`) and SuperKey, not only the KSU
   providers.
7. **Upstream tracking** — ASRP periodically merges the AERA `aera-16.0`
   branch while keeping its own features on top.

## Building

ASRP is the recovery module of an AOSP source tree. It uses the standard
`Android.mk` / `Android.bp` layout and the existing recovery and device-tree
configuration flags; no separate toolchain or PC client is needed. Recovery
images are produced with the normal AOSP build for your device tree (Android
14–17). Device-specific integration stays in the device tree.

## Acknowledgments

ASRP would not exist without the work of many projects and people:

- **AERA Recovery Project** — the direct upstream. Founders and lead developers
  Jonas Salo (koaaN) and Daniel Springer (Daniel210191), plus all AERA
  contributors, for the recovery architecture and interface.
- **TeamWin Recovery Project (TWRP)** and **OrangeFox Recovery** — the recovery
  foundations that AERA and ASRP build upon.
- **Android Open Source Project / Android Recovery** — the platform, update
  engine, `magiskboot`-style tooling interfaces and partition handling.
- **LVGL** — the graphics library behind the recovery interface.
- **KernelSU, KernelSU Next, SukiSU Ultra, BakaSU, FinaSU, YukiSU, APatch,
  KernelPatch and FolkPatch** — root solutions and their `ksud`, `kptools` and
  manager ecosystems.
- **Dropbear SSH** (Matt Johnston) — SSH/SFTP support.
- Third-party libraries: **mjansson/mdns** (Unlicense), **hash-wasm** (MIT),
  **Lucide** icons (ISC), **jsoncpp**, **OpenSSL**, **Restic** and their
  contributors.
- Everyone who reports issues, tests on real devices and contributes
  translations and device trees.

## License

ASRP is free software licensed under the **GNU General Public License, version 3
or later** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the project notice and
[NOTICE](NOTICE) for the complete license text.

ASRP is a modified version of AERA Recovery, which is derived from OrangeFox
Recovery and TWRP. Original copyright and license notices are preserved in the
corresponding source files. Third-party components remain under their own
licenses, as stated in their source directories and license files.

## Links

- Repository: <https://github.com/ASRP-Team/android_bootable_recovery>
- Issues and device reports: <https://github.com/ASRP-Team/android_bootable_recovery/issues>
- Upstream: <https://github.com/AERA-Recovery>

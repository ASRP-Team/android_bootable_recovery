# ASRP — Another Super Recovery Project

**English** · [简体中文](#简体中文)

---

## English

ASRP (Another Super Recovery Project) is an open-source, native Android recovery
environment. It is a fork of [AERA Recovery](https://github.com/AERA-Recovery),
which in turn builds on Android Recovery, TeamWin Recovery Project (TWRP) and
OrangeFox Recovery. ASRP adds only a limited set of features and broader device
compatibility, and supports Android 14–17.

- **Platform:** Android 14–17, A/B and non-A/B devices, GKI and legacy kernels
- **Interface:** native LVGL UI for touch and hardware keys, portrait and landscape
- **License:** GNU GPL v3 or later (see [License](#license))

### Features

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

### How ASRP differs from AERA

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

### Building

ASRP is the recovery module of an AOSP source tree. It uses the standard
`Android.mk` / `Android.bp` layout and the existing recovery and device-tree
configuration flags; no separate toolchain or PC client is needed. Recovery
images are produced with the normal AOSP build for your device tree (Android
14–17). Device-specific integration stays in the device tree.

### Acknowledgments

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

### License

ASRP is free software licensed under the **GNU General Public License, version 3
or later** (`GPL-3.0-or-later`). See [LICENSE](LICENSE) for the project notice and
[NOTICE](NOTICE) for the complete license text.

ASRP is a modified version of AERA Recovery, which is derived from OrangeFox
Recovery and TWRP. Original copyright and license notices are preserved in the
corresponding source files. Third-party components remain under their own
licenses, as stated in their source directories and license files.

### Links

- Repository: <https://github.com/ASRP-Team/android_bootable_recovery>
- Issues and device reports: <https://github.com/ASRP-Team/android_bootable_recovery/issues>
- Upstream: <https://github.com/AERA-Recovery>

---

<a id="简体中文"></a>
## 简体中文

ASRP（Another Super Recovery Project）是一个开源的 Android 原生 Recovery 环境，
是 [AERA Recovery](https://github.com/AERA-Recovery) 的 fork 版本；AERA 又建立在
Android Recovery、TeamWin Recovery Project（TWRP）与 OrangeFox Recovery 之上。
ASRP 仅添加了部分功能以及更多设备的兼容性，支持 Android 14–17。

- **支持平台：** Android 14–17，A/B 与非 A/B 设备，GKI 与传统内核
- **界面：** 原生 LVGL，支持触屏与实体按键，横竖屏自适应
- **许可证：** GNU GPL v3 或更高版本（见[许可证](#许可证)）

### 功能特性

- **Recovery 工作台** — 主屏幕、应用启动器、快捷设置、状态栏，支持多种界面
  密度与社区翻译。
- **刷写** — ZIP、raw/sparse IMG 与 fastboot 镜像；A/B 插槽选择、逻辑/动态分区、
  payload 检查、ARB 降级保护，以及可选的“保留 ABL / 保留 Recovery”处理。
  独立 payload 提供实验性的快速刷写方式。
- **备份与恢复** — 分区备份支持压缩与校验和验证，可恢复到设备或直接写入电脑
  文件夹；应用备份使用加密的 App Backup Vault（Restic 仓库）。
- **Recovery 更新** — 在 Recovery 内检查版本并下载已签名的更新。
- **Root 管理器** — 无需重启即可修补目标插槽，保留可验证的完整回滚镜像，
  管理 KernelSU 模块，并可安装或卸载管理器 APK。采用两级选择：先选修补
  **大类**，再选具体**发行版**。
  - KernelSU 系列（通过 `ksud` 修补 `init_boot`）：KernelSU、KernelSU Next、
    SukiSU Ultra、BakaSU、FinaSU、YukiSU。
  - APatch / KernelPatch 系列（通过 `kptools` 修补 `boot` 中的内核）：APatch、
    FolkPatch。
  - YukiSU、APatch、FolkPatch 支持可选的 **SuperKey**，仅保存在内存中，
    不会写入存储或回执。
- **PC 工作台** — 由 Recovery 自身通过 443 端口 HTTPS 提供，无需电脑端程序、
  无线 ADB 或设备树开关。首次需在手机上物理批准浏览器，会话使用 Bearer
  Token。提供上传 → 校验 → 安装/刷写、终端、备份、文件管理、设备信息、
  日志导出、电源操作与 SSH 控制。没有 Wi-Fi 时可通过 `adb forward` 走 USB。
- **实时画面镜像** — 可选，通过 80 端口 HTTP 输出 Recovery 画面的 MJPEG 流。
- **网络** — Wi-Fi、可选的移动数据构建、SFTP/NAS 网络存储、SSH/SFTP 服务
  （Dropbear），以及面向老旧客户端的明文 FTP 服务。
- **插件** — 签名的 AERAP 插件包、插件管理器与公开的宿主 API，可扩展浏览器、
  媒体播放器、图库、录屏、流媒体以及可选的小游戏（Doom、RetroArch、
  Artillery）。
- **工具** — 文件管理器、图片查看器、媒体播放器、录屏、浏览器、终端、
  应用备份库与诊断日志导出。

### 与 AERA 的区别

ASRP 并非只改了名字，当前差异包括：

1. **全新标识** — Recovery 界面与 PC 工作台统一显示 ASRP。上游许可证、版权
   声明与 AERA 致谢均保留；内部存储路径与协议继续沿用原名以保持兼容。
2. **两级 Root 管理器** — AERA 是平铺的 provider 列表；ASRP 将修补大类与发行版
   分离，把 BakaSU、FinaSU、YukiSU、FolkPatch、APatch 纳入同一流程，并在
   Recovery 界面和浏览器工作台上保持一致的两级交互。
3. **SuperKey 处理** — YukiSU 与 KernelPatch 系管理器可在修补前输入可选
   SuperKey；它随 Root 请求传递，API 不会回显。
4. **SSH 与 SFTP** — 集成基于 Dropbear 的服务，支持启停、密码与公钥管理，
   并可在 PC 工作台直接控制。
5. **FTP** — 可选的明文 FTP 服务，复用 SSH root 密码以兼容老旧客户端。
   FTP 不加密，仍推荐使用 SFTP。
6. **更完整的 PC Root 控制** — 工作台暴露全部发行版、KernelPatch 目标
   （`boot_a` / `boot_b`）与 SuperKey，而不限于 KSU 系列。
7. **跟随上游** — ASRP 会定期合并 AERA 的 `aera-16.0` 分支，并在其上维护
   自有功能。

### 构建

ASRP 是 AOSP 源码树中的 Recovery 模块，使用标准的 `Android.mk` / `Android.bp`
布局与既有的 Recovery、设备树配置开关，不需要额外工具链或电脑端程序。
Recovery 镜像使用对应设备树的常规 AOSP 构建流程生成（Android 14–17），
设备相关适配仍放在设备树中。

### 致谢

ASRP 离不开众多项目与开发者的工作：

- **AERA Recovery Project** — 直接上游。创始人及核心开发者 Jonas Salo（koaaN）
  与 Daniel Springer（Daniel210191），以及所有 AERA 贡献者，提供了 Recovery
  架构与界面基础。
- **TeamWin Recovery Project（TWRP）** 与 **OrangeFox Recovery** — AERA 与
  ASRP 所依赖的 Recovery 基础。
- **Android Open Source Project / Android Recovery** — 平台、更新引擎、
  镜像工具接口与分区处理。
- **LVGL** — Recovery 界面使用的图形库。
- **KernelSU、KernelSU Next、SukiSU Ultra、BakaSU、FinaSU、YukiSU、APatch、
  KernelPatch 与 FolkPatch** — Root 方案及其 `ksud`、`kptools` 与管理器生态。
- **Dropbear SSH**（Matt Johnston） — SSH/SFTP 支持。
- 第三方库：**mjansson/mdns**（Unlicense）、**hash-wasm**（MIT）、
  **Lucide** 图标（ISC）、**jsoncpp**、**OpenSSL**、**Restic** 及其贡献者。
- 所有提交问题、在真机上测试、贡献翻译与设备树的社区成员。

### 许可证

ASRP 是自由软件，采用 **GNU General Public License 第 3 版或更高版本**
（`GPL-3.0-or-later`）。项目声明见 [LICENSE](LICENSE)，完整许可证文本见
[NOTICE](NOTICE)。

ASRP 是 AERA Recovery 的修改版本，AERA 又源自 OrangeFox Recovery 与 TWRP。
相应源文件中保留了原始版权与许可证声明。第三方组件仍遵循其各自目录或许可证
文件中的授权条款。

### 相关链接

- 仓库：<https://github.com/ASRP-Team/android_bootable_recovery>
- 问题与设备反馈：<https://github.com/ASRP-Team/android_bootable_recovery/issues>
- 上游：<https://github.com/AERA-Recovery>

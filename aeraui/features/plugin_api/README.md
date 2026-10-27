# AERA Host API 2

Host API 2 lets a plugin with a previously unknown ID install and run without
rebuilding recovery. The Host API surface remains rendered by AERA. The plugin
is an ARM64 root worker that sends bounded declarative UI messages over
a `SOCK_SEQPACKET` channel inherited as file descriptor 4. It runs in recovery's
filesystem and network namespaces as UID/GID 0, with recovery's capabilities,
devices, mounted partitions and data available directly. Running the plugin
therefore grants it the same access as recovery itself.

## Manifest contract

An API 2 manifest uses `type: "ui-runtime"`, `entry: "main"`,
`min_host_api: 2`, `protocol_version: 2`, and
`executable: "usr/bin/aera-plugin"`. It must request `display` and
`touch-input`. Supported privileged permissions are:

- `settings-backup` / `settings-restore` for AERA Recovery preferences
- `android-settings-backup` / `android-settings-restore` for Android user
  0's SettingsProvider `system`, `secure`, and `global` databases, plus
  the LineageSettingsProvider database when the installed ROM supplies it
- `screen-mirror` to start or stop AERA's existing USB-only display stream;
  framebuffer capture and input injection remain in the trusted host
- `desktop-surface` to request the optional host-managed desktop frame/input
  surface. This grants no extra filesystem privilege beyond API 2's root worker.

Host-mediated operations are denied unless the permission is declared and the
user confirms the individual request in trusted AERA UI. Root plugins may also
operate on recovery resources directly. The Android settings operations use
wire operation IDs 3 and 4; USB mirror start/stop use IDs 5 and 6. The Android
settings convenience implementation includes ROM customization values such as
Infinity-X and Lineage settings but excludes lock credentials, accounts, app
data and SettingsProvider SSAIDs. Restore validates and stages the complete
snapshot before transactionally replacing live files.

API 1 manifests and their built-in scene routing remain compatible.

## Protocol and limits

`protocol.hpp` is the canonical wire definition. Every packet is exactly 1144
bytes, includes the `A2PI` magic and contains only fixed-size strings. The
worker starts with `HELLO`, placing its minimum protocol version in `value` and
maximum in `flags`. AERA replies with `HELLO_ACK` and `LIFECYCLE/RESUME`.

The initial declarative surface supports a page title/body, up to 12 action
buttons, status updates, lifecycle events and mediated operations. The host
advertises optional extensions in the `HELLO_ACK` flags. `kFeatureMetrics`
adds compact section headers, up to 64 keyed metric rows, and in-place metric
updates. `kFeatureBackNavigation` lets a page declare one enabled action as its
parent with `SET_BACK_ACTION`; AERA's edge-back gesture then invokes that
action before leaving the plugin. A worker must not send extension messages
unless the corresponding feature bit was advertised; this preserves
compatibility with older API 2 hosts. AERA rejects bad sequences, duplicate
action or metric IDs, unknown flags, oversized packets and more than 128
worker messages per second.

The host expands a hash-verified runtime into private RAM and launches its musl
loader directly as root. It does not apply a chroot, Minijail, namespaces,
seccomp, capability removal or resource limits to Host API 2 plugins. Leaving
the scene terminates the plugin's process group and closes the channel unless
the worker explicitly opts into the background extension below. `PATH`
contains both the plugin's `usr/bin` and recovery command directories, while
`AERA_PLUGIN_ROOT` names the extracted runtime for bundled resources.
`AERA_LOCALE` contains the selected recovery locale (for example `de_DE` or
`zh_CN`) and `LANG` carries the matching UTF-8 locale. Runtime plugins own
their visible strings and must fall back to English when a locale or string is
missing. Relaunching a plugin after a language change supplies the new locale
without changing the Host API 2 wire format.

## Optional desktop surface

Request `kFeatureDesktop` (bit 7) and optionally `kFeatureDesktopResolution`
(bit 8) through the opt-in HELLO feature mask. Older workers retain their
original ACK flags. These additions do not renumber background/activity APIs.

Only send `OPEN_DESKTOP` (kind 21, nonzero request ID, zero value/flags)
when `HELLO_ACK` advertises `kFeatureDesktop` and the manifest declares
`desktop-surface`. The host retains the plugin's setup page and opens a
landscape overlay. It replies with `DESKTOP_READY` (kind 72), the same ID,
value width and flags height, with exactly two `SCM_RIGHTS` descriptors: a host-owned
size-sealed double framebuffer and a `SOCK_SEQPACKET` input channel. Failure
has zero dimensions and no descriptors. There is no executable path in this API.

Zero value/flags request the legacy 960x540 default. When the additional
`kFeatureDesktopResolution` bit is advertised, workers may request width in
value and height in flags: 640x360, 960x540, 1280x720 or 1920x1080. Other sizes
are rejected before allocation. Workers must validate the returned dimensions.

Frames are BGRA at the negotiated size, two slots, and use the bounded `PlayMessage` protocol
in `components/play_protocol.h`. Publish sequence N into slot N%2, then send
FRAME with the dimensions and sequence; do not reuse a slot until ACK.
The host validates every packet and exposes desktop-pointer/game-relative
controls, an on-screen keyboard, and physical mouse/keyboard input. EXIT
sends RELEASE/CLOSE, closes the overlay, and restores the setup orientation.
Workers must stop their guest cleanly and preserve user data when the channel
closes. No recovery-specific or device-specific runtime is baked into the host.

`AERA-settings-backup-plugin` is the reference implementation. It proves a new
ID can render and request backup/restore without being added to recovery's
launcher source.

## Optional background, activities and presentation

These extensions keep protocol version 2 and the same packet size. A new worker
requests `kFeatureRequestMagic | feature_bits` in `HELLO.request_id`. The marker
prevents legacy request IDs from being mistaken for feature requests. The host
acknowledges only the supported requested bits, plus the original metrics/back
bits. An old worker still receives the exact original flags (`3`); an old host
ignores the request and advertises no new features. Check the acknowledgement
before sending any extension message. Store and manually installed API 2
packages use the same implementation; API 1 scenes are unchanged.

- `kFeatureBackground`: `SET_BACKGROUND` uses `value=1` to opt in, `0` to opt out.
  `BACKGROUND_RESULT` echoes the request ID and reports the enabled state in
  `value`. At most four runtimes can opt in. Leaving the view sends PAUSE but
  retains the process, private runtime and page model. Reopening the same plugin
  sends RESUME and attaches a new view to the same worker. PAUSE means no visible
  view, not cancellation. Messages are polled even while the display is off.
  CLOSE, disconnection, uninstall and recovery shutdown terminate the runtime.
- `kFeatureActivities`: send `SET_ACTIVITY_STORAGE` with a nonzero activity ID
  in `request_id` and the absolute working storage path in `text` (empty for a
  task that does not use mounted storage). Then send `SET_ACTIVITY` with the
  same ID, a title, detail, progress `0..10000` or `kProgressUnknown`, and exactly
  one of `kActivityActive`, `kActivityComplete`, `kActivityFailed`. Active tasks
  may add `kActivityCancellable`. **Wait for a positive ACTIVITY_RESULT before
  starting file work.** An active storage claim prevents conflicting native
  recovery/storage operations; a native operation can deny a new claim. Only
  `/tmp` work is exempt. The claim cannot change while active. Close all files
  and finish cleanup before reporting completion/failure. Progress updates use
  the same ID and do not require acknowledgement. AERA shows a global task
  badge and details with reopen/cancel actions; `ACTIVITY_CANCEL` carries the
  activity ID. The worker must stop cooperatively, clean up only its own partial
  files and then report completion/failure. Force stop is a separate explicit
  user choice, not an automatic timeout. Finished tasks remain visible briefly.
- `kFeatureRichPresentation`: between BEGIN_PAGE and COMMIT_PAGE,
  `SET_PAGE_LAYOUT.value=1` selects the original list or `2` a two-column action
  grid where space permits. `ADD_TILE` has the same IDs/flags/text as ADD_BUTTON,
  with an `Icon` enum in `value`. `ADD_PROGRESS` adds a keyed native progress row
  (`value=0..10000`, title/detail and existing metric colour flags), updated by
  `UPDATE_PROGRESS`. Progress rows share the 64-metric limit; tiles share the
  12-button limit. Old page messages retain their original rendering.
- `kFeatureReadablePresentation`: `SET_PAGE_LAYOUT.value=3` selects a larger
  list and `4` a larger grid, with wrapped action text and content-sized rows.
  Use only after this additional bit was acknowledged. Values 1/2 and legacy
  pages keep their existing font sizes and layout.

Background is opt-in for each worker, not permission to draw over recovery. All
global UI is rendered by the host and hidden during lock/critical operation
surfaces. Workers remain responsible for their own filesystem behaviour and
must not start file work after a denied claim or advertise completion early.

`kFeaturePackageReview` adds `SET_ACTIVITY_FILE` (activity ID in `request_id`,
absolute ZIP path in `text`). Register only after closing and verifying the
finished ZIP. The host checks that it is a regular, non-symlink ZIP file inside
the activity's storage and replies with `ACTIVITY_FILE_RESULT`. Then report
activity completion. Completed ZIP tasks retain an Install ZIP action until
dismissed or the worker is stopped; other finished tasks still expire briefly.
`REVIEW_PACKAGE` with an absolute ZIP path opens the same native review used by
the file explorer. It requires a foreground view and no active storage task.
Neither message installs anything: the normal metadata, ARB checks, installer
selection, confirmation and preservation settings remain in the recovery flow.
Clear all in Recents stops opted-in runtimes as well as the browser, prompting
before stopping active plugin storage tasks. A later launch starts at Home.

## Optional shared text clipboard

Request `kFeatureClipboard` (bit 9) in the opt-in HELLO feature mask, and check
the ACK before sending clipboard messages. Protocol version 2, packet size,
old message IDs and legacy ACK flags are unchanged. No new manifest permission
is needed: API 2 workers already run as root. This is AERA's native Copy/Cut/Paste
clipboard, not Android's or the PC's clipboard. It is text-only, RAM-only and
not automatically synchronized with other devices.

Access requires the plugin's foreground view with no host confirmation modal
or pending mediated operation. A detached/background or locked-out view receives
an error. There is no per-copy confirmation dialog. Use explicit user actions
such as Copy or Send clipboard; do not poll the clipboard in the background.

All requests have a nonzero request ID and an empty title. The reply kind is
`CLIPBOARD_RESULT` (73), echoes the ID and carries exactly one result flag:
`kClipboardReady=1`, `kClipboardMore=2`, `kClipboardComplete=4`, or
`kClipboardError=8`. On error, title is an English diagnostic, value is zero,
and the shared clipboard is unchanged. Localize plugin UI independently.

- **Get:** `GET_CLIPBOARD` (22), value/flags zero and empty text, snapshots the
  clipboard. Each reply supplies up to 1023 bytes in text, with total UTF-8 byte
  length in value. More means send another GET with the same ID,
  `flags=kClipboardContinue` (1), and value equal to bytes received so far.
  Complete ends the read, including an empty clipboard. Reassemble bytes before
  UTF-8 decoding: an individual packet can end inside a multibyte character.
- **Set:** `SET_CLIPBOARD` (23), value equal to the total UTF-8 byte count,
  zero flags and empty text, starts a write. Wait for Ready, then send
  `CLIPBOARD_CHUNK` (24) with the same ID, value equal to the byte offset, zero
  flags and up to 1023 bytes in text. Each Ready reply acknowledges bytes staged
  in value. The final reply is Complete; only then is the shared clipboard
  replaced. SET with length zero clears it immediately and returns Complete.

The limit is `kMaxClipboardBytes` (1 MiB). Embedded NULs, invalid UTF-8,
oversized declarations and incorrect offsets/IDs are rejected, never silently
truncated. A new GET/SET cancels the runtime's previous incomplete transaction;
errors, leaving its view and disconnection discard staged data. Reads are stable
snapshots even if the native clipboard changes while transferring. Always wait
for each acknowledgement before sending the next packet and respect the existing
128-worker-messages/second limit, including the plugin's other updates.

## Optional console commands

Store and manually installed plugins may declare up to eight optional commands:

```json
"commands": [{"name": "example", "executable": "usr/bin/example"}]
```

Names start with a lowercase ASCII letter and contain at most 32 lowercase
letters, digits, hyphens or underscores. Executables are relative payload paths
of at most 200 characters; absolute paths, empty components, `.` and `..` are
rejected. Existing manifests without this field are unchanged. Older recoveries
ignore it and do not provide automatic console commands.

After storage becomes available and after plugin install/update/removal, AERA
registers small launchers in `/system/bin`. Opening the plugin UI is not needed.
Existing recovery tools and foreign files are never overwritten. Conflicting
plugin command names are not registered. Removing a plugin removes its owned
launchers, without deleting unrelated files or plugin state.

Each invocation revalidates the installed manifest and expands the existing
hash-verified payload into a private RAM runtime, then executes the declared
file with `AERA_PLUGIN_ROOT` set, forwarding arguments, terminal streams, signals
and exit status. It does not initialize another recovery UI. The temporary
runtime is removed when the command exits. Persistent state must live outside
that runtime. Commands run with normal recovery shell privileges; declaring a
command does not start a background process or bypass the invoked tool's own
approval rules.

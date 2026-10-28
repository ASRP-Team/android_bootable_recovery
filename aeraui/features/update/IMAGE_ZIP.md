# Fastboot image ZIPs

An archive with root-level `android-info.txt` and partition `.img` entries uses
the image-package review flow. Payload ZIPs take precedence and continue using
the existing payload implementation. Other installer ZIPs retain their normal
installer path.

The review offers individual image selection, Select all, extraction, and an
optional **Apply ROM partition layout** setting. Select all honors the existing
ABL, recovery and splash preservation preferences. There is no automatic reboot,
slot switch, data wipe or execution of `fastboot-info.txt`/shell instructions.

Board/product requirements are checked before flashing against device properties
and kernel SoC identifiers. Exact `require`/`reject` alternatives are supported;
unknown or conditional requirements block flashing. Extraction remains available
without a matching device. Only verified existing flash targets, or logical
targets defined by a validated ROM layout, may be written. Shared data/security
partitions and whole Super images are excluded from this path.

Selected stored/deflated images are staged under a new `AERA/Extracted` directory
on the selected storage. Explicit ZIP CRC checks are required because
libziparchive disables them by default. Android sparse RAW/FILL/DONT_CARE/CRC
chunks are validated and expanded using bounded buffers; DONT_CARE emits zeros.
All selected images finish staging and SHA-256 validation before partition
preparation starts. Every output device is claimed before the first image write;
each written image is flushed and SHA-256 checked by reading its target back.
These hashes verify the extracted/written bytes, not publisher authenticity.
Staged images are deleted after success and retained after a flash failure.

`super_empty.img` is never offered as a flash image. A bounded, validated liblp
empty image may supply symmetric A/B partition/group definitions. With layout
replacement off, unselected partitions and group limits remain intact. Enabling
it requires selecting every logical image in that definition. The in-memory plan
removes stock-only logical partitions in the current slot, creates missing ROM
partitions and applies ROM groups. Device geometry, block devices, shared
partitions and the other slot remain intact. If those constraints cannot fit the
ROM, preparation fails. The complete plan is exported and committed once; stale
COW cleanup uses the existing snapshot-state guards. Original metadata/mappings
are restored after preparation failures before image writing begins. No metadata
rollback is attempted once image writes start.

This is a same-slot image installation, not an update_engine/snapshot OTA.
After changing ROM layout, follow the ROM's separate data-format requirements.

Tests: `mka aera_payload_inspector_test recoveryimage`; run the host test binary
with optional `AERA_IMAGE_ZIP_TEST_FILE=/path/to/image.zip` to inspect a real
archive and extract its small `vbmeta`/`xbl_config` images without device writes.
The UI harness `ui_check --payload-review` covers selection, preservation,
layout confirmation, incompatible-device blocking and the existing OTA views.

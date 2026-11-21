<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->
<!-- Copyright (c) 2026 Garrett Dothard -->

# Native codeplug (draft)

Status: **draft**. Today the radio stores the open-firmware layout (`open-firmware-cps.md` 7), so qdmr and the open-firmware CPS
work unchanged. This page sets out the model the core already uses, which stays when the storage changes, and the
plan for a native format. Nothing in section 4 is built yet.

## 1. Goals

- One editing model for the radio's own menus, the open-firmware CPS shim and a future native CPS protocol.
- Edits on the radio are safe against power cuts (`settings-store.md` 4) and cost one sector rewrite each, not a
  whole-image rewrite.
- The open-firmware layout keeps working for as long as qdmr and the open-firmware CPS are the tools people have.
- The native format carries what the open-firmware one can't: per-channel privacy key slot, more zones, longer names, and
  per-channel features without overloading vendor flag bits.
- The CPS can explicitly convert legacy operating talkgroup assignments to saved group-contact references.
  Conversion reuses matching contacts, creates missing contacts, and preserves the legacy bytes in the output.
- The channel editor stages new talkgroup contacts until Save channel. It saves the channel and any newly
  assigned contacts together as one undoable edit.
- Opening a file over unsaved edits requires confirmation. Channel and membership dialogs ask before discarding
  modified fields or lists, including dismissal with Escape. Browser drafts are written in edit order and report
  saving, saved, or unavailable; a restored draft preserves whether it had unexported or unwritten edits.
- Record creation is staged until the editor is saved. A new channel and any talkgroup contacts created in its
  editor are one undoable change. The Problems view opens the affected record section and labels its field.
- A fresh CPS visit offers radio reading, file opening, or explicit codeplug creation. A working draft restores
  directly; an untouched blank draft from an older CPS returns to the start screen. Explicitly created empty
  codeplugs remain editing sessions. The workspace and its dialogs follow the selected site theme.
- Radio-wide state, keys included, lives in the settings store, not in the codeplug. A codeplug file can be shared
  without leaking keys or the owner's last channel.

### CPS system editor

The system editor separates identity, radio settings, and each entry-table class into keyboard-accessible
tabs. Entry tables use the full panel width and scroll horizontally on narrow displays. Save and Cancel
remain visible while the panel scrolls. Switching tabs retains unsaved fields and rows. Save validates
every tab and opens the first section with an error. Cancel discards the staged edits after confirmation.
Unknown entry classes remain intact when a supported class is edited.

### CPS contact views

The Contacts page offers All, DMR TG, P25 TG, Private Calls (PC), and Shared TG subpages.
All views use the same contact objects, record numbers, storage format, and capacity limit.
Missing protocol scope means DMR. Shared groups appear in Shared TG and All. Existing all-call
records remain in All; DTMF contacts remain a separate table.

Each subpage retains its search and sort. Switching subpages clears selection. Actions target
visible records; renumbering is explicitly global. Creation defaults follow the active subpage.
Reclassification refreshes the view and reports the destination. Undo restores the original
record and references. Protocol and type changes refuse incompatible channel, VFO, and RX-list
assignments. P25-capable group IDs remain limited to 16 bits. Bulk edits validate the entire
batch before changing any record. Validation links open the matching subpage and record.

## 2. The editing model (built)

`core/codeplug/codeplug.h` is the model. Callers see records and never touch addresses:

| Record | Read | Edit |
|---|---|---|
| channel 1..1024 (`channel_t`) | `codeplug_channel` | `codeplug_set_channel`, `codeplug_delete_channel`, `codeplug_free_channel` |
| zone 0..67 (`zone_t`, up to 80 channels) | `codeplug_zone`, `codeplug_zone_count`, `codeplug_zones_stored` | `codeplug_set_zone`, `codeplug_delete_zone` |
| contact 1..1024 (`contact_t`) | `codeplug_contact`, `codeplug_contact_find` | `codeplug_set_contact`, `codeplug_delete_contact` |
| RX group list 1..76 (`listen_list_t`) | `codeplug_listen_list` | `codeplug_set_listen_list`, `codeplug_delete_listen_list` |
| radio name, own DMR ID, hang times | `codeplug_radio_name`, `codeplug_dmr_id`, `codeplug_dmr_settings` | `codeplug_set_radio_name`, `codeplug_set_dmr_id` |

Rules:

- **Every call takes `storage_lock`**, so any task may call. An edit costs one journaled sector rewrite (about
  100 ms on the W25Q128). A new record also rewrites its bitmap's sector: the record goes first, then its in-use bit.
- **Fields the model doesn't own are kept.** An edit reads the stored record, changes the bytes the model knows and
  writes it back. Vendor bytes and flags that no field covers survive a radio-side edit.
- **`codeplug_serial()`** goes up on every change. That covers edits and `codeplug_reload()` after the CPS shim
  wrote the image underneath. The radio core polls it and re-reads its zone and channel (`RREQ_RELOAD`). It falls
  back to the saved zone position when the saved channel number is gone. It also forgets the contact names it looked
  up for IDs (a "no name" answer is cached too), so a contact added after its ID was heard or called shows by name.
- **The built-in set.** With no codeplug stored (blank flash, `codeplug=builtin`), the radio has a small built-in
  list of channels. The first edit *materializes* it: the built-in channels are written as stored records, the
  bitmaps and list counts are cleared, and the edit applies on top. After that the source is `open-firmware`.
- **Zones.** With no zone stored there is one virtual zone, "All channels". The first `codeplug_set_zone(0, ...)`
  replaces it with a stored zone table, using the 80-channel zone layout.
- **Validation.** A channel number out of range or a malformed frequency (0, 1 GHz or more), a zone naming a
  channel that isn't stored, or a list naming a contact index out of range is refused. The caller gets `false` and
  nothing is written. Band limits are checked when the radio tunes, not at storage.

`codeplug_openfw_decode()` and `codeplug_openfw_encode()` expose the 56-byte channel record conversion for the CPS
shim's VFO handling (`cps-protocol.md` 2.3).

## 3. The native channel record (plan)

The fields `channel_t` holds today, plus what the open-firmware record can't hold:

| Field | Size | Notes |
|---|---|---|
| name | 16 | UTF-8, NUL padded (open-firmware: 16 ASCII, 0xFF padded) |
| rx_hz, tx_hz | 4 + 4 | Hz, binary (open-firmware: BCD of 10 Hz) |
| mode, bw, power, flags | 4 | flags: rx_only, dmo, ... |
| rx_tone, tx_tone | 2 + 2 | type in the top 2 bits, value |
| color_code, timeslot | 2 | |
| contact, listen_list | 2 + 2 | indices |
| dmr_id | 4 | 0 = the radio's |
| tot_s | 2 | 0 = the radio's |
| alias_tx | 1 | `CODEPLUG_ALIAS_*` bits |
| **privacy_slot** | 1 | 0 = radio default, 1..4 = private AES slot (`settings-store.md` 7), 0xFF = privacy off on this channel |
| reserved | to 64 bytes | 0, append only |

The privacy key itself is never in the codeplug. A channel names a slot. The open-firmware layout carries the built extension below; the future native record keeps the same selector values.

## 4. Storage plan (not built)

- **Area.** `hal_flash_map_t.native_addr` / `native_size`, set by the board: 1 MB above anything an open-firmware tool
  writes (`openfw_end`). The CPS shim refuses writes there. Each radio's flash map is in its hardware spec.
  With `CONFIG_DMR_AES256`, its last two sectors belong to the private key journal and are excluded from native
  codeplug data and erase operations ([settings-store.md](settings-store.md#7-private-aes-key-bank)).
- **Layout.** A header sector (magic, format version, table offsets, CRC) then fixed-size tables (channels, zones,
  contacts, lists) with a used bitmap each, sector-aligned so an edit rewrites one sector through the journal.
  The magic is the header's first word, `STORAGE_NATIVE_MAGIC` 0x4E52444F (bytes 4F 44 52 4E in memory order), already fixed
  because factory reset keys on it.
- **Factory reset (built).** `storage_native_erase()` erases the native area only when it starts with the magic
  (non-blank sectors from the end, the header sector last, so a power cut leaves the magic in place and the next
  reset finishes the job), reports it blank when it is all 0xFF, and otherwise leaves it alone as
  foreign (another firmware's data). It never touches the open-firmware image, the calibration or a foreign-guarded region.
- **Switching.** `codeplug_init()` prefers a valid native header and otherwise uses the open-firmware image. A one-way
  "convert" (UI or CPS) writes the native area from the open-firmware image. The open-firmware image is left as it was, so a
  firmware downgrade still finds it.
- **Open-firmware tools after conversion.** Two options, open: (a) the open-firmware CPS shim keeps reading and writing the open-firmware
  image and a commit triggers a re-import into the native area, or (b) the shim serves an open-firmware view generated from
  the native records. (a) is simpler; (b) never lets the two drift apart.
- **Native protocol.** `cps-protocol.md` 3 drafts the record-level protocol a native CPS (and qdmr, if it adds a
  driver) would use: read and write records by type and index, no addresses.

## 5. Open decisions

1. Where the native area goes on radios whose board has no flash map yet, and whether 1 MB is the right size.
2. Re-import (4a) or generated open-firmware view (4b).
3. Whether the radio-side editor should be allowed while a CPS session is open. Today edits fail cleanly while the
   CPS holds a pending sector, and otherwise interleave with CPS commits at sector granularity.
4. UTF-8 names: the font has to cover them first.

## 6. Channel encryption bindings (built)

The RadioFoundry extension at EEPROM image 0x5400..0x5C07 names private AES slots without changing vendor
channel bytes. The entire extension fits in sector 0x5000 and uses the existing sector journal. This range is
included in binary codeplugs, normal CPS transfers and TFTP codeplugs. Keys remain outside these images.

| Offset from 0x5400 | Content |
|---|---|
| 0..7 | `52 46 41 45 53 01 00 00` (RFAES, version 1, reserved zero) |
| 8 + 2*(n-1) | Channel n's selector: 0 radio default, 1..4 AES slot, 255 off |
| 9 + 2*(n-1) | Selector XOR 255 |

Without the RFAES prefix, channels inherit the radio default. With the prefix but an unknown version, or a
malformed entry, the binding is invalid (internal value 254) and DMR voice TX is refused. Uninitialized pairs
FF/FF and 00/00 inherit the default. The encoder claims an unrecognized table only if every byte is 00 or FF;
it refuses other data. Ordinary edits with no binding preserve unknown tables. Deleting/reusing a channel in
the web CPS clears/replaces its binding. Renumbering moves the reference with the channel.

The CPS exposes this as **Encryption** in DMR channel forms, the table and Bulk Edit. CSV uses **AES Key Slot**:
0 default, 1..4 slot, 255 off. YAML export reports that assignments are omitted. Older firmware cannot enforce
these selectors; the web CPS refuses explicit assignments unless VERSION advertises `aes_channel_keys=1`.
Third-party tools and firmware may discard or ignore the extension. Do not use them to program a codeplug
whose channel encryption assignments must be retained.

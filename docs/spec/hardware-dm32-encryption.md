<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# DM-32UV stock AES voice format

This page records interoperability facts recovered from one user-supplied stock image. The independent
implementation and its current privacy limits are in [dmr-voice.md](dmr-voice.md#5-privacy).
The modem interface is in [dmr-modem.md](dmr-modem.md#3-privacy-hooks), and the board's memory map is in
[hardware-dm32.md](hardware-dm32.md#2-memory-map).

Status: static analysis and offline execution of selected instruction sequences. No radio, RF transmission, or
audio recording was used. Compatibility with another firmware release or with Motorola equipment is untested.
This is an observed vendor format, not a copy of the DMR Association encryption specification.

## 1. Image and address mapping

| Item | Value |
|---|---|
| Embedded version | `DM32.01.L01.048` |
| Supplied filename | `DM32_L01_048_20250821.bin` |
| File length | 860,416 bytes |
| SHA-256 | `fda860febfcf1a234eed7fa73272112891074aac83746e4f8dfe224a2a700f8f` |
| Container | 256-byte `BFUV32-V2` header; application at file offset `0x100` |
| XIP base | `0x0300C000`; file offset = XIP address minus `0x0300BF00` |
| RAM copy | Flash `0x030BD768`, length `0x1F098`, copied to `0x00010000` |
| Modem RAM copy | Flash `0x0306FDC0`, length `0x43FC`, copied to `0x18000800` |

RAM routine addresses below refer to the copied code's execution address. Disassembling it at its flash load address
gives incorrect call targets. The RAM copy parameters come from the application's startup sequence.

| Runtime address | Observed role |
|---|---|
| `0x0001CFAC` | AES key expansion |
| `0x0001D07C` | AES block encryption |
| `0x0001D3E0` | Expand a four-byte MI into a 16-byte IV |
| `0x0001D46C` | Voice OFB keystream generation |
| `0x00015BD4` | TX key setup, superframe keystream, MI advance, and late-entry preparation |
| `0x00015728` | RX key setup and superframe keystream |
| `0x000157F4` | TX parameter-bit XOR followed by AMBE FEC encoding |
| `0x00015880` | RX AMBE FEC decoding followed by parameter-bit XOR |
| `0x00014860` | Advance the MI |
| `0x00015B70` / `0x00015CD4` | Encode / recover a late-entry MI |
| `0x00015478` / `0x0001556C` | Insert / extract late-entry fragments in voice frames |
| `0x030230E8` | Construct the PI header payload |
| `0x0305B4AA` | Initialize MI bytes while loading a selected key |

## 2. Cipher and keys

AES is implemented in software. AES-256 uses all 32 key bytes, eight initial key words, and 14 rounds. AES-128
uses 16 bytes, four initial words, and 10 rounds. Key words are assembled from bytes in big-endian order. No key
hashing or password derivation occurs in the traced key-expansion path.

The internal algorithm selectors are 4 for AES-128 and 5 for AES-256. The PI builder sets bit 5 of the selector,
giving on-air algorithm identifiers `0x24` and `0x25`. These selectors are distinct from the channel's menu values.

The block routine uses global scratch state at `0x00033C88`. Treating that stock routine as reentrant would be
incorrect. An independent implementation need not keep this storage arrangement.

## 3. Message indicator and IV

The message indicator (MI) is four bytes, interpreted as a big-endian 32-bit unsigned value. IV expansion preserves
those four bytes and appends three more words from a shift register.

For each shift, the new low bit is the XOR of the previous word's bits 31, 21, 1, and 0. Shift the word left by one,
insert that bit, and retain 32 bits. After each group of 32 shifts, append the resulting word in big-endian order.
Three groups produce the remaining 12 IV bytes.

| Input MI | Expanded IV |
|---|---|
| `12345678` | `12345678b451463a41d78991a49a6402` |

Advancing an AES MI for the next superframe uses one group of 32 shifts. It does not take the last word of the
expanded IV. Thus the next MI after `12345678` is `b451463a`.

The TX setup routine first generates the current superframe's keystream, then advances the stored MI and prepares
late-entry information for that next MI. The PI header builder copies the current stored MI. Its callers build
the initial header before preparing the first superframe's keystream.

### Initialization

The selected-key loader fills six bytes starting at TX context offset 34 with low bytes returned by a pseudorandom
generator. The AES path uses the first four of these bytes as the MI. This observed path does not load a literal
`12345678`; that value is only a test input on this page.

The generator at `0x0306DC64` updates a 32-bit state by multiplying by 1,103,515,245 and adding 12,345, modulo
2^32. It returns that state shifted right by one. The loaded state at modem RAM address `0x18000C2C` is 1.
The traced primitive is an LCG, not a cryptographic random generator. Complete seed provenance, every writer to
the MI, and the timing of key-loader calls have not been established. Fresh, unpredictable per-call MIs have not
been demonstrated.

## 4. Voice keystream

The voice generator uses AES in output feedback mode (OFB): encrypt the IV, then repeatedly encrypt the previous
16-byte output with the same key. Discard the first output block. Concatenate the next eight blocks to form a
128-byte buffer.

Each voice superframe contains six bursts, with three AMBE frames per burst. The TX XOR stage consumes seven
keystream bytes per frame: six whole bytes and the most significant bit of the seventh byte. The other seven bits
of that seventh byte are unused. The next frame starts at the next byte, without packing its bits into those gaps.

For zero-based burst index `s` and frame index `f`, the first keystream byte is `21*s + 7*f`. There are 18 frames,
so TX addresses bytes 0 through 125 of the generated buffer. Bytes 126 and 127 are unused by the TX XOR stage.
The transform acts on packed 49-bit vocoder parameters before FEC encoding on TX and after FEC decoding on RX.

### AES-256 synthetic test vector

This vector uses a public test key and synthetic input, not a captured transmission or a user's key.

| Item | Value |
|---|---|
| Key | `000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f` |
| MI | `12345678` |
| IV | `12345678b451463a41d78991a49a6402` |
| First 16 usable keystream bytes, after discarding the first block | `3c30d1a337dd055d1032b6dd22262c8a` |

### Receive-path discrepancy

The stock RX XOR stage uses the same six whole keystream bytes per frame as TX. For the final parameter bit,
however, its offsets within a burst's keystream are 6, 15, and 24. The corresponding TX offsets are 6, 13, and 20.
These offsets were checked by executing both XOR routines with their separate FEC stages bypassed.

For the last burst, RX's third final-bit read reaches byte 129, past the 128-byte keystream region and into
neighboring context storage. This is an observed discrepancy, not a recommendation for a new implementation.
Its effect on decoded speech and whether another stock build fixes it remain untested.

## 5. PI header

The PI builder writes this layout. Byte offsets refer to the decoded 12-byte payload, before channel coding.

| Offset | Length | Meaning |
|---|---|---|
| 0 | 1 | Algorithm selector with bit 5 set: `0x25` for AES-256 |
| 1 | 1 | Manufacturer ID `0x10` |
| 2 | 1 | Configured key ID |
| 3 | 4 | Current MI, big-endian byte order |
| 7 | 3 | Destination ID, most significant byte first |
| 10 | 2 | Builder writes `08 53`; final CRC handling was not traced by the offline builder check |

With key ID 7, MI `12345678`, and destination `123456`, the first ten bytes are
`25 10 07 12 34 56 78 12 34 56`.

RadioFoundry currently retains PI octets 0 and 1 as algorithm and key ID without interpreting the format. For
this vendor layout the key ID is octet 2; adding AES support will require parsing the complete header, including
manufacturer ID and MI. A software CRC implementation must follow the modem contract and the PI CRC mask in the
shared [voice specification](dmr-voice.md#5-privacy), not reuse the builder's two constant bytes as a valid CRC.

## 6. Late entry

The late-entry builder splits the 32-bit MI into the upper 12 bits, the next 12 bits, and the last eight bits
followed by a four-bit check. The check processes MI bytes most significant bit first, with initial remainder 0,
polynomial x^4 + x + 1, and final XOR `0xF`.

Each resulting 12-bit quantity becomes a systematic 24-bit coded word. The stock encoder and decoder were
checked together. Full equivalence of that encoder's parity equations to a standard Golay encoder has not been
established; use the observed vector when checking an independent implementation.

For MI `12345678`, the three words are `0x1230AC`, `0x45696D`, and `0x789810`.

One coded word travels in each of the three voice frames. Bursts A through F carry successive nibbles of that
word, most significant nibble first. For a nibble numbered from bit 0 (least significant) to bit 3, insertion into
each nine-byte voice frame is:

| Nibble bit | Voice-frame byte | Bit in byte |
|---|---|---|
| 0 | 7 | 4 |
| 1 | 7 | 0 |
| 2 | 8 | 4 |
| 3 | 8 | 0 |

The insertion helper ORs these bits into the frame and shifts the saved coded words left by four bits after each
burst. Its behavior with nonzero preexisting bits at those locations was not validated against a physical modem.
The RX helper assembles the six nibbles; the recovery routine corrects the coded words, checks the four-bit check,
and returns the MI. The RX superframe path uses recovered late-entry information when valid and otherwise advances
the previous MI before preparing the next keystream.

## 7. Offline verification and remaining work

Local web updates require the **raw application image**, optionally merged with its codec. The BFUV32 file adds
a 256-byte header for the vendor updater and must not be passed to the web application's image picker. The web
importer rejects that wrapper before interpreting the executable or codec offsets. The CLI loader strips it.

A limited C-SKY instruction interpreter executed selected routines from this exact image. It maps the startup
copies, interprets the routines' arithmetic and memory operations, and stops on unsupported instructions. Only
memory-copy helpers, an inert diagnostic callback, and explicitly identified FEC stages were substituted.

| Check | Result |
|---|---|
| AES-128 block against FIPS known answer and Python `cryptography` | Pass |
| AES-256 block against FIPS known answer and Python `cryptography` | Pass |
| Both voice OFB generators against independent AES calculations | Pass |
| MI expansion and advance, including zero and all-one inputs | Pass |
| Late-entry MI encode/decode | Pass |
| Late-entry insertion/extraction over all six burst positions | Pass |
| PI builder's first ten payload bytes | Pass |
| TX key setup, keystream, MI advance, and late entry across two superframes | Pass |
| TX/RX parameter XOR stages over all six burst positions, FEC bypassed | Offsets above reproduced |
| Initialization PRNG primitive | LCG recurrence reproduced |

The interpreter, disassembly, extracted RAM image, and detailed results are temporary analysis artifacts under
ignored `build/aes-analysis/`. They require the supplied firmware file and are not part of the firmware or release.
No stock firmware bytes or disassembly are included in this document.

RadioFoundry now implements an independent AES-256/OFB engine, full-width RAM session provisioning, PI signaling,
MI advance and late-entry recovery behind `CONFIG_DMR_AES256` in the shared core
([voice specification](dmr-voice.md#51-aes-256-sessions)). Host tests cover the AES known answer, the observed OFB
vector, framing, call integration, loss recovery and late entry. It uses standard Golay parity and consistent TX/RX
parameter offsets; it does not reproduce the observed stock RX final-bit discrepancy or the stock LCG.

Before claiming interoperability, remaining work includes:

- Resolve the receive final-bit discrepancy and late-entry parity details with independent vectors and an
  authorized interoperability test.
- Verify the complete FEC/modem path, PI CRC handling, header-to-first-burst timing, loss recovery, and late entry
  with stock equipment.
- Validate the host-seeded, bounded-session MI policy in deployment; avoid seed/sequence reuse with the same key.
- Verify boot, runtime heap margin and actual two-way voice interoperability. The target build, AES-disabled
  build, task-stack check and codec/updater packaging passed offline on 2026-11-02
  ([bring-up record](../testing/dm32-bringup.md)). AES with persistent key slots adds 5,240 bytes of application flash
  and 456 bytes of static RAM.

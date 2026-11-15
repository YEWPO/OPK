# EOP compatibility

[简体中文](eop-compatibility.zh-CN.md)

The C++ reader is an independent implementation informed by the [eop2midi specification](https://github.com/emizuki/eop2midi/blob/main/specification.md). No Go source is included or invoked. Reference documentation describes a repeating XOR-obfuscated binary container with recorded physical keys and absolute millisecond timestamps, rather than standard MIDI pitches stored directly in each event.

| Layout | Implemented support |
| --- | --- |
| v200 | Fixed 255-key mapping table, block counts 1–4, per-hand dynamics, status-zero terminator, external raw NUL |
| v201 | Length-prefixed mapping layers; zero-first-section variant only when segmentation is unique |
| v301 | Length-prefixed mapping layers |
| Other versions/ambiguous containers | Rejected with an explicit format error |

Limits: files up to 64 MiB, at most 1,000,000 expanded events, 24-hour timeline, up to 256 keys per mapping section and 32 mapping layers. More than 8,192 events due in one callback triggers fail-silent overload protection.

The importer validates container boundaries, record sizes, finite monotonic timestamps, note/status/velocity ranges, reserved event bytes and note pairing. Known v201/v301 layouts contain a trailing non-event record, excluded according to the reference layout. Parallel layers are merged by pitch and hand: identical pairs are deduplicated, while the same pitch assigned to different hands remains separate. Extended Set-1 key codes are normalized from EOP bit 7 to the Raw Input E0 bit 8 representation. Imported keys remain attached to events for display; changing the live keyboard map never changes imported pitches or hands.

### v201/v301 key signatures

The first little-endian 32-bit word of each 28-byte mapping-section header is a key-signature ID in 0–14. This interpretation was checked against the configuration and key-signature table in the [Everyone Piano installer](https://www.everyonepiano.cn/Software.html). IDs include enharmonic entries and map to signed semitone shifts:

| ID | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 | 10 | 11 | 12 | 13 | 14 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Shift | -4 | -3 | -2 | -1 | -1 | 0 | 1 | 1 | 2 | 3 | 4 | 5 | 6 | 6 | 7 |

ID 5 is neutral (C). Invalid IDs are rejected. Each layer retains its own ID; note-on computes `pitch = mapped note + shift` before merging and deduplicating pitch/hand pairs. This also applies to the v201 unprefixed first section. Negative shifts remain negative; pitches outside 0–127 are rejected rather than clamped or wrapped. Playback and piano highlights use the resulting absolute pitches independently of live-keyboard transpose. Native `.eopiano.json` saves keep absolute pitches without applying the shift again. Earlier native recordings are not automatically changed; reimport the original EOP to correct its interpretation. v200 behavior is unchanged.

### Playback configuration and key changes

Mapping actions `0x0190` and `0x0290` advance/retreat each affected layer's key-signature ID by one entry, wrapping within 0–14, independently of the assigned scan code. Enharmonic entries remain distinct, so one action does not always change pitch. Repeated key-down records each count; key-up records do not change the configuration. Subsequent notes use the layer's current signed shift; held notes retain the pitch and hand saved at press time until release. The locally supplied v301 sample has two up actions at approximately 155.882 seconds, moving IDs 9 → 10 → 11 (Eb → E → F).

Imports retain a timestamped configuration timeline. The first section supplies the displayed pitch class (signed shift normalized to 0–11); v301 header words at +4/+8 and +12/+16 supply per-hand octave and dynamics when in range. These header interpretations are based on the supplied sample, not a comprehensive format specification. Initial octaves are displayed but are not applied again to already absolute mapped pitches. v201 octave/dynamics remain unknown, and v200 retains its existing note/dynamics import without a configuration timeline. Multi-layer display uses the first section's settings; it does not describe differing per-layer settings.

During playback and pause, the existing performance controls show the file configuration read-only; unknown values are labeled explicitly. Stop, completion, and file replacement restore live settings without saving playback values to the keyboard profile. Native version-1 recordings can optionally carry a `configurations` array with `timeUs`, `transpose`, and optional `leftOctave`, `rightOctave`, `leftVelocity`, `rightVelocity` fields. Older recordings remain readable; note pitches remain absolute.

Other non-note control events are not interpreted as notes. They are skipped with a visible warning because their semantics have not been verified. This can affect sustain or other original performance controls. An event referring to an absent key mapping is rejected rather than guessed. Positive note-on velocity is required; no attempt is made to infer a nonzero velocity from an undocumented zero value.

### Hand assignments

v200 retains its documented left/right actions and per-hand dynamics. For v201/v301 note mappings, the little-endian 32-bit word at record offset `+8` selects the hand: `1` is left, `0` is right, and other values remain Unknown. This interpretation was checked against the locally supplied v301 sample; v201 uses the same record prefix and is covered by synthetic tests, including the unprefixed first-section variant, but has not been checked against a real sample for this field.

Each mapped note retains its hand through layer merging, transpose changes, release matching, seeking, and replay. Both keyboard views use green for left, purple for right, and gold for Unknown, independently of the live keymap or pitch range. Native recordings preserve these assignments in the existing `hand` field. Previously saved recordings with Unknown hands are not rewritten; reimport the original EOP to recover its assignments.

Metadata is decoded as UTF-8, then GB18030 where supported. Metadata decoding does not change pitch/timing interpretation.

Interface language changes display text only, preserving file formats, error codes, configuration fields and user metadata.

First release reads EOP only. Native recordings use versioned `.eopiano.json`; EOP export, EOPM, MusicXML, sheet-music engraving and screenshot/PDF interpretation are outside this importer.

## Validation status

Tests build synthetic containers for each supported version, multi-layer maps, hand assignments and unknown values, the v201 zero-section variants, v200 dynamics, all 15 signature IDs, invalid IDs, signed pitch boundaries, enharmonic steps, bidirectional ID wrapping, independent layer changes, releases across key changes, malformed events, truncation, and 3,000 deterministic single-byte mutations. UI tests import synthetic EOP files and check both keyboard views during playback, pause, seeking, and replay, including assignments that contradict the live keymap. These exercise parser behavior but do not establish coverage across a real-world EOP corpus.

Local checks use the user-supplied Flower Dance EOP corresponding to [this score](https://www.everyonepiano.cn/Music-15915.html): 3,772 events, duration 261,616,667 microseconds, initial C configuration, and opening left F3 / right E5. The supplied Hotarubi sample retains its initial Eb configuration and transition to F at approximately 155.882 seconds. These checks do not establish compatibility with every arrangement or container. Third-party scores, music and the reference installer are used locally only and are not included in this repository.

Before declaring a Windows release fully EOP-compatible, compare locally supplied or appropriately licensed real samples with Everyone Piano: note order, pitch, key highlights, timing, velocity and ending behavior. Keep third-party music out of the source repository unless redistribution permission is known. Any remaining unsupported controls/layouts must remain visible in release notes.

### Dynamics scale and seeking

EOP per-hand header dynamics use 0–100 percent: v301 configuration values and v200 mapped-note dynamics are converted to MIDI 0–127 with rounding. Zero is a known silent setting; v200 silent presses retain release matching but generate no notes. Out-of-range v301 values remain unknown; invalid v200 dynamics are rejected. Per-event velocities retain MIDI semantics. Native configuration dynamics remain MIDI values and now also accept zero; existing native files are not reinterpreted.

Playback seeking preserves play/pause state and reconstructs held notes, pedal state, keyboard highlights and configuration at the destination. Dragging previews the target time and seeks on release. Seeking to the end completes playback; Play then restarts from the beginning.

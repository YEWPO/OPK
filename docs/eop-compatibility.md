# EOP compatibility

The C++ reader is an independent implementation informed by the [eop2midi specification](https://github.com/emizuki/eop2midi/blob/main/specification.md). No Go source is included or invoked. Reference documentation describes a repeating XOR-obfuscated binary container with recorded physical keys and absolute millisecond timestamps, rather than standard MIDI pitches stored directly in each event.

| Layout | Implemented support |
| --- | --- |
| v200 | Fixed 255-key mapping table, block counts 1–4, per-hand dynamics, status-zero terminator, external raw NUL |
| v201 | Length-prefixed mapping layers; zero-first-section variant only when segmentation is unique |
| v301 | Length-prefixed mapping layers |
| Other versions/ambiguous containers | Rejected with an explicit format error |

Limits: files up to 64 MiB, at most 1,000,000 expanded events, 24-hour timeline, up to 256 keys per mapping section and 32 mapping layers. More than 8,192 events due in one callback triggers fail-silent overload protection.

The importer validates container boundaries, record sizes, finite monotonic timestamps, note/status/velocity ranges, reserved event bytes and note pairing. Known v201/v301 layouts contain a trailing non-event record, excluded according to the reference layout. Pitch sets from parallel layers are merged and duplicate pitches removed. Extended Set-1 key codes are normalized from EOP bit 7 to the Raw Input E0 bit 8 representation. Imported keys remain attached to events for display; changing the live keyboard map never changes imported pitches.

### Experimental v201/v301 mapping offset

As an experimental compatibility assumption, the first little-endian signed 32-bit word of each 28-byte mapping-section header selects a pitch class. Its negative is normalized to an upward shift of 0–11 semitones: `shift = ((-int64(offset) % 12) + 12) % 12`, then `pitch = mapped note + shift`. This rule has not been verified against Everyone Piano. The locally supplied v301 sample has value 9, so a mapped C becomes E-flat three semitones higher. Zero and multiples of 12 leave pitches unchanged. Other header parameters, including possible octave settings, remain uninterpreted; this is not general key-signature detection.

The shift is applied separately to each layer before merging and deduplicating pitches, including the v201 unprefixed first-section variant. Both note-on and note-off use the resulting absolute pitch, and values outside 0–127 are rejected rather than clamped or wrapped. Playback and piano highlights use these pitches independently of live-keyboard transpose. Native `.eopiano.json` saves contain absolute pitches and do not apply the shift again. Existing native recordings saved with the earlier downward-offset rule are not automatically changed; reimport the original EOP to use this rule. v200 behavior is unchanged.

Known non-note control events are not interpreted as notes. They are skipped with a visible warning because their semantics have not been verified. This can affect sustain or other original performance controls. An event referring to an absent key mapping is rejected rather than guessed. Positive note-on velocity is required; no attempt is made to infer a nonzero velocity from an undocumented zero value.

v200 has documented left/right actions. v201/v301 hand assignments are retained as Unknown, not inferred from the user's live keyboard or current note range. Metadata is decoded as UTF-8, then GB18030 where supported. Metadata decoding does not change pitch/timing interpretation.

First release reads EOP only. Native recordings use versioned `.eopiano.json`; EOP export, EOPM, MusicXML, sheet-music engraving and screenshot/PDF interpretation are outside this importer.

## Validation status

Tests build synthetic containers for each supported version, multi-layer maps, the v201 zero-section variants, v200 dynamics, malformed offsets/events, truncation, and 3,000 deterministic single-byte mutations. These exercise parser behavior but do not establish coverage across a real-world EOP corpus.

Before declaring a Windows release fully EOP-compatible, compare locally supplied or appropriately licensed real samples with Everyone Piano: note order, pitch, key highlights, timing, velocity and ending behavior. Keep third-party music out of the source repository unless redistribution permission is known. Any remaining unsupported controls/layouts must remain visible in release notes.

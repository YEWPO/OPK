# EOP / Recording Guide

[简体中文](performance.zh-CN.md)

This guide opens offline from **Help → EOP / Recording Guide**. To hear a performance, first use **Soundfont → Import / Replace Soundfont** to import an SF2. See the [Soundfont Import Guide](soundfont.md).

## Open a performance

1. Choose **File → Open EOP / Recording**.
2. Select an Everyone Piano `.eop` file or an OpenKeys `.eopiano.json` recording.
3. Check the displayed title and any compatibility notice, then click **Play**.

PDF scores, pictures, EOPM, MIDI, MusicXML and audio files are not supported by this command. Renaming a file does not convert it. An unreadable, damaged or unsupported file produces an error; some unsupported EOP controls are skipped with a warning, so their effects can differ from the original.

## Playback controls

- **Play / Pause** starts or pauses playback. **Stop** returns to the beginning; Play after the end starts again.
- **Speed** adjusts playback from 0.25× to 2× without changing pitches.
- Click the progress bar to seek, or drag it and release to jump. Seeking preserves playing or paused state.
- Playback uses the file's pitches, hand assignments and sustain. When a file supplies performance settings, the controls show those settings read-only during playback and pause; unavailable values show **Unknown**. Stopping restores the live settings.
- The **Sustain** switch in **Performance Settings** affects live playing only. It does not add sustain to a loaded file.

For a local example, choose **File → Demo**, then **Play**. No music download is required.

## Record, save and reopen

1. Import a soundfont and connect an audio output through **Output → Audio Settings**.
2. Set the live key, octaves and velocities. Enable **Sustain** if desired; it stays on until switched off, without holding Space.
3. Click **Record**, then play with the computer keyboard or either on-screen keyboard. Sustain changes are recorded. Sustain already enabled at the start is included too.
4. Click **Finish Recording** (or **Stop**), then choose **File → Save Recording**.
5. Save as `.eopiano.json`. Use **File → Open EOP / Recording** to replay it later.

A recording stores performance events, not microphone audio or a WAV/MP3 file. Finishing releases remaining notes and pedals so the saved performance ends cleanly. The live Sustain selection remains available for your next notes; every application startup resets it to off. Emergency stop (`Ctrl+Alt+F12`) turns it off and silences all notes.

Opening another file or closing with an unsaved recording asks whether to save it. Choose Cancel to keep working. OpenKeys does not export EOP files.

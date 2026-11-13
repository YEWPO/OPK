# Validation and release acceptance

[简体中文](validation.zh-CN.md)

## Automated checks

```sh
ctest --test-dir build --output-on-failure
python3 tools/verify_resources.py
```

The suite covers:

- Held-key repeat suppression, release snapshots across transposition, pedal reference counts, velocity modifiers, out-of-range notes and keymap persistence.
- Native recording roundtrip including Chinese titles, atomic save failure preservation, malformed events and pedal/note pairing.
- EOP v200/v201/v301, layered notes, v201 zero-section containers, v200 dynamics, truncation and deterministic mutation stress.
- Audio-clock play/pause/resume/stop/restart and speed changes, queue overflow, actual FluidSynth loading/rendering with a generated SF2, panic and volume.
- System-language fallback, saved preferences, installation-language initialization and user preference precedence, process-only language overrides, embedded translations, bilingual UI and offline guide links.
- GUI import/error preservation, text-field typing isolation, emergency shortcut and startup without an audio device.
- Mouse playing on both keyboard regions: drag switching, black-key hit priority, releases outside the widget, focus/hide/grab cancellation, right-click editing, silent visual feedback and an embedded window icon. Physical/mouse source merging covers notes, sustain and velocity modifiers, both press/release orders, repeat suppression and paired recording events across transposition changes.
- Managed SF2 copies: Unicode paths, persistence, duplicate imports, source deletion, replacement cleanup, invalid files, cancellation, write/configuration failure and malformed metadata recovery.
- First-use guidance and the embedded offline tutorial, GUI import failure preservation, restart loading and explicit migration from legacy external paths.
- Headless full-range/velocity sampler checks and PCM16 WAV output using generated fixtures. An optional `real_soundfont` integration test accepts `OPENKEYS_TEST_SOUNDFONT`; without a user-supplied bank it is explicitly skipped.

Local automated validation uses Linux with Qt 6.11.2, FluidSynth 2.6.1 and PortAudio 19. Test soundfonts are generated fixtures, not a claim that any third-party piano has passed listening tests. No instrument bank is required to build or distribute this application.

AddressSanitizer and UndefinedBehaviorSanitizer checks use `ASAN_OPTIONS=detect_leaks=0 UBSAN_OPTIONS=halt_on_error=1`. LeakSanitizer cannot run in this environment's ptrace restrictions, so leak detection is not claimed. Windows CI automatically verifies silent installation, upgrade, shortcuts and uninstall. Physical output, interactive installer pages and DPI behavior still require real-machine acceptance.

## Windows real-machine acceptance (pending)

Target Windows 11 x64 and Windows 10 22H2 x64. Record OS build, CPU, RAM, soundfont checksum, keyboard, audio device/driver, wired output type, shared/exclusive mode, sample rate and buffer size.

1. Install on a clean, offline machine without developer tools. No soundfont is bundled or automatically downloaded. The first-use status shows import guidance, and the complete tutorial opens without internet or source files. Import a locally supplied SF2, wait for copy/load, delete its original source, restart and confirm the managed copy loads. Repeat with Chinese paths and at 100%, 150% and 200% DPI.
   Test both installer languages, every standard/custom page and uninstall language recovery. Confirm first-launch inheritance and that upgrades preserve existing application language preferences, including Follow System. Switch application languages and restart; check labels, dialogs, errors, keyboard legends and offline guide language links. Confirm desktop and Start Menu shortcuts are checked by default; repeat installation with it unchecked and confirm no desktop link is created. Both desktop and Start Menu links must launch the installed application. Test reinstall/upgrade and uninstall cleanup. Check the icon in Explorer, the window/taskbar, shortcuts and installer/uninstaller, including light/dark desktops at each DPI. Silent install/link-target/startup/uninstall checks are automated in Windows CI; these interactive and visual checks still require Windows acceptance.
2. Import a replacement, cancel/interrupt an import, try a damaged SF2 and simulate full/unwritable storage. The source files and previous working configuration survive failures. A successful replacement clears the previous application copy after the old sampler is released.
3. Exercise rapid notes, chords, two physical keys mapped to one pitch, sustain, changing transpose/octave while holding a key, text editing, focus changes and modal dialogs. No repeats, mismatched releases or stuck notes. Test two sustain keys and simultaneous left/right modifiers.
   Click and drag through both on-screen keyboards, across black/white piano keys and into blank space. Hold a physical key and its on-screen counterpart together, then release them in both orders, including Space and Shift. Release the mouse outside the window; switch windows, open dialogs and use emergency stop while holding it. In background mode, switching windows releases mouse notes while physical input continues. Without a soundfont, hover/press feedback remains visible and the status bar explains how to enable sound.
4. Enable background mode, switch to a score viewer, and play. `Ctrl+Alt+F12` silences and disables background input. Verify the unavailable-hotkey case and unplugging the keyboard. Ordinary input remains visible to the foreground application.
5. Import actual EOP examples for each version; compare with Everyone Piano and record differences. Check unknown controls are warned about. Pause during a held note/pedal, change speed and resume. Stop and replay from the beginning.
6. Record a performance containing both hands and sustain. Finish with keys still held. Save, reopen and compare timing/velocity/sustain; canceling an overwrite/close prompt must retain the recording.
7. Run 30 minutes of dense playback and live performance, including long sustain. Record underrun count, peak process memory and CPU, and check for clicks/dropouts. The synthetic automated tests do not replace this listening test.
8. Unplug/switch the audio output. No hung input or endless sound. Reopen audio settings to rediscover/reconnect; test unsupported exclusive mode returns to shared mode with a clear message. A sample-rate change must reload the sampler without changing musical pitch.
9. Measure at least 100 key-to-sound trials on wired output using a time-correlated key actuation marker and captured analog output (external recorder/loopback apparatus). Report P50/P95/max and measurement method. Target low-latency-mode P95 ≤20 ms. Driver latency and requested buffer duration alone do not satisfy this measurement.

## Record template

| Field | Result |
| --- | --- |
| Build / dependency baseline / soundfont checksum | |
| OS / CPU / RAM / keyboard | |
| Output device / driver / wired connection | |
| Sample rate / requested frames / actual reported latency | |
| Shared / exclusive / fallback behavior | |
| 30-minute run / underruns / peak memory / clicks | |
| Real EOP samples and mismatches | |
| End-to-end P50 / P95 / max / trial count / method | |
| No-bank install / import / restart / Chinese paths / DPI | |
| Installer / uninstaller languages / saved preferences / offline language links | |

Resource checks validate English-default documents, matching Chinese pages, language navigation, relative links, image references, translation coverage and placeholders, icon dimensions and wordmark rendering, and both previews. Screenshots are captured from the current application without a soundfont or audio hardware.

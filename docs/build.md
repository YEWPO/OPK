# Build and packaging

[简体中文](build.zh-CN.md)

OpenKeys Piano does not download, embed or bundle soundfonts. Every application package includes the offline import guide; the user supplies an SF2 after installation. Building and packaging require no SF2 or network access to an instrument provider.

## Linux development

Ubuntu 24.04 dependencies:

```sh
sudo apt-get install cmake ninja-build g++ pkg-config qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-translations-l10n libqt6concurrent6 libfluidsynth-dev portaudio19-dev
cmake --preset linux-dev
cmake --build --preset linux-dev --parallel 4
ctest --preset linux-dev
```

Requires Qt 6.5+, a C++20 compiler, FluidSynth 2.3+ and PortAudio 19. Qt needs GB18030 support (ICU in the Windows manifest) for legacy Chinese EOP metadata.

```sh
./build/openkeys --inspect assets/demo.eopiano.json
QT_QPA_PLATFORM=offscreen ./build/openkeys --screenshot /tmp/openkeys.png
```

`--inspect` validates a recording/EOP and prints JSON without initializing GUI/audio. `--smoke-test` and `--screenshot` avoid hardware initialization. `--soundfont PATH` follows the same managed import flow as the GUI; it is an explicit user import, not a default instrument.

## Windows x64

Install Visual Studio 2022 Desktop Development with C++, a Windows SDK, CMake 3.24+ and NSIS for installers. Dependencies use dynamic `x64-windows` libraries.

```powershell
git clone --branch 2025.06.13 https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = 'C:/dev/vcpkg'
cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release
./tools/package_windows.ps1
```

The vcpkg manifest pins registry baseline `ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0`. Dependencies still need to be installed or available in the build cache. The project never downloads instrument samples.

The `windows-release` preset uses an overlay triplet that builds only Release dependency libraries. The `windows-dev` preset retains both Debug and Release libraries. CI restores the newest compiled dependency cache for the manifest/triplet, falling back to older Windows caches; vcpkg checks package ABIs before reuse. Each run saves an updated cache after configuration, including available packages if configuration fails or is cancelled. CI explicitly supplies Qt's plugin and DLL directories to the test processes.

The packaging script builds and tests, deploys into a fresh staging directory, verifies no SF2/SF3 files are present, runs the staged executable, and produces NSIS/ZIP artifacts in `dist/`. Qt deployment and CMake install the required DLLs/platform plugins and dependency notices. The optional real-SF2 check is skipped when no user bank is configured.

Windows CI builds the application, checks the staged contents and starts it with developer toolchains removed from PATH. Every push also creates an NSIS installer and a portable ZIP, available in the run's `OpenKeysPiano-Windows-installer` artifact. A manual workflow run with `build_installer=true` creates the same packages. Pull requests build and test without creating installers. No public release is published by the workflow.

The NSIS install-options page defaults to creating the **OpenKeys Piano** desktop shortcut; users can uncheck it. PATH modification remains off by default. Silent installation uses the same desktop-shortcut default. Desktop and Start Menu shortcuts target `openkeys.exe` at the installation root, and uninstall removes them. CI runs `tools/verify_windows_installer.ps1` on its disposable Windows worker to install silently to a Unicode path containing spaces, inspect both shortcuts through the Unicode `IShellLinkW` interface, start the executable directly and through both shortcuts without developer PATH entries, and verify uninstall cleanup. Do not run that script on a workstation with an existing OpenKeys installation.

### Application icon assets

`assets/icons/openkeys.svg` is the editable icon source: the OKP wordmark above one octave of seven white keys and five black keys in two-plus-three groups on a dark rounded background. Black keys are about 60% of the white-key length. The wordmark uses DejaVu Sans Bold with Liberation Sans and Arial fallbacks, so install one of those fonts before regenerating to keep exports consistent. Its PNG exports and multi-resolution `openkeys.ico` are checked in, so ordinary builds need no image conversion tools. Qt embeds the PNG sizes; the Windows executable resource, shortcuts and NSIS installer/uninstaller use the ICO. To regenerate after editing the SVG, install `rsvg-convert` (librsvg) and Python Pillow, then run:

```sh
python3 tools/generate_icons.py
```

The exported sizes are 16, 24, 32, 48, 64, 128 and 256 pixels. Keep every export synchronized with the SVG. Resource verification checks key counts and two-plus-three placement, the OKP wordmark, transparent corners, distinguishable black keys at every size, and matching ICO/PNG pixels.

## Imported instrument storage

SF2 copies live in `QStandardPaths::AppLocalDataLocation/soundfonts/` (`LOCALAPPDATA` on Windows). Their filenames are SHA-256 hashes; `current.json` atomically records the current hash and display name. It is updated only after copying and sampler loading succeed.

Copies and sampler loading run on a worker thread. Failed/cancelled imports remove their temporary/new files while preserving the previous configuration and source files. After a successful switch and release of the old sampler, the old owned copy is removed. A cleanup failure does not invalidate the newly imported bank. External source paths are never deleted.

Startup loads only the explicitly imported bank identified by this metadata. There is no bundled/development-directory fallback. Old `QSettings` external paths are migration hints requiring an explicit import. Sample-rate changes reload the managed copy without importing it again.

Both import guides are embedded as Qt resources in the UI library and installed as `docs/soundfont.md` and `docs/soundfont.zh-CN.md`. Their language links also work in the offline viewer. It works without network access, a working directory or the source checkout.

## Optional soundfont diagnostics

```sh
./build/openkeys-soundfont-check --soundfont /path/to/piano.sf2 --output build/soundfont-check
OPENKEYS_TEST_SOUNDFONT=/path/to/piano.sf2 ctest --test-dir build -R '^real_soundfont$' --output-on-failure
```

The standalone tool opens no audio device or network connection. It tests 44.1/48 kHz, A0–C8 at three velocities, sustain/release, and an embedded demo; it generates PCM16 WAV previews and a JSON report including the bank's hash and invalid/clipped-sample counters. This does not measure physical output latency or certify third-party sound quality. Unset integration-test input is marked skipped, not passed.

## Installer languages and cleanup

The installer and uninstaller support English and Simplified Chinese. Installation starts with a language selector, defaulting to Simplified Chinese on Chinese systems and English otherwise. The selected language is recorded for uninstall and written to `install-language.ini` for the application's first launch. Existing application language preferences, including Follow System, take priority. The uninstaller falls back to the system when upgrading an old installation without a language record.

Silent installation accepts `/LANG=1033` (English) or `/LANG=2052` (Simplified Chinese), `/DESKTOP=0|1` and `/STARTMENU=0|1`. Both shortcut options default to enabled; PATH is never modified. `/D=installation path` must be last. Silent installation shows no language selector. An invalid explicit language returns a failure code.

Upgrade retains uninstall-before-install. Uninstall removes recorded shortcuts and the entire installation directory, including user-added files. Configuration and soundfonts outside that directory are preserved. Locked files or insufficient permissions cause uninstall to fail and retain a retry entry point rather than report success.

`cmake/installer/NSIS.template.in` is adapted from CMake CPack; its upstream license is included as `CMake-LICENSE.rst`. Recheck compatibility after changing CMake versions.

## Translation and preview maintenance

User-visible source strings are English. Simplified Chinese translations live in `translations/openkeys_zh_CN.ts`. Build with Qt LinguistTools; on Ubuntu add `qt6-tools-dev`, `qt6-tools-dev-tools` and `qt6-translations-l10n` to the dependencies above. Windows vcpkg supplies `qttools[linguist]` and `qttranslations`. Application and Qt standard-widget Chinese catalogs are embedded; runtime translation needs no source checkout or network.

To update the catalog, run Qt's `lupdate src -ts translations/openkeys_zh_CN.ts`, review and translate every new entry, then rebuild. Normal builds compile the catalog without rewriting it. `python3 tools/verify_resources.py` checks translation coverage, placeholders, bilingual links, icon dimensions and previews; it requires Python Pillow. CI runs this check in addition to CTest.

Language preference is saved as `ui/language` in the existing `OpenKeys/Piano` settings (`system`, `en`, `zh_CN`). Changing it through Language / 语言 takes effect on restart. `--language system|en|zh_CN` overrides one process without writing the preference. Smoke tests and screenshots isolate their settings.

Regenerate both screenshots from the built application:

```sh
python3 tools/generate_previews.py --executable /absolute/path/to/openkeys
```

The script fixes Fusion style, scaling, font size and the initial window state. `docs/preview.png` is English; `docs/preview.zh-CN.png` is Chinese. They are actual application captures, not mockups. Regenerate all PNG and ICO assets from the shared SVG with `python3 tools/generate_icons.py`; the mark combines the OKP wordmark with ivory piano keys on a dark rounded square. Review both screenshots and every icon size before committing.

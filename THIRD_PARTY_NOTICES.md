# Third-party components

[简体中文](THIRD_PARTY_NOTICES.zh-CN.md)

OpenKeys Piano source is licensed under GPL-3.0-or-later; see LICENSE.

- Qt 6 Core, GUI, Widgets, Concurrent, Test, LinguistTools and Qt translations: Qt Project / The Qt Company, LGPL-3.0 or GPL-3.0 / commercial licensing. https://www.qt.io/licensing/
- FluidSynth: FluidSynth developers, LGPL-2.1-or-later. https://github.com/FluidSynth/fluidsynth
- PortAudio: PortAudio contributors, MIT license. https://github.com/PortAudio/portaudio

No instrument soundfonts or samples are bundled with the application. Users obtain and import third-party SF2 files themselves. Names and links in the import guide identify optional external resources and do not imply affiliation or endorsement. Consult each provider for instrument authorship and licensing.

EOP binary-layout research: https://github.com/emizuki/eop2midi/blob/main/specification.md . The C++ parser is an independent implementation; no Go source is included. EOP/Everyone Piano names identify compatibility with third-party file formats, without affiliation.

The sampler fixture in tests/fixtures.h is generated from a mathematical sine wave, is used only for automated tests, and is not a bundled piano instrument. The included demo is an original event recording, not an audio sample library. Application icon assets are generated from the repository-owned SVG.

The Windows installer template is adapted from CMake CPack (Kitware and contributors), BSD-3-Clause. The full notice is in `cmake/installer/CMake-LICENSE.rst`.

Original licenses and copyright notices retain their original wording. Translated explanatory notices do not replace the original license texts.

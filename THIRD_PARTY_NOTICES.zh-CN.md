# 第三方组件

[English](THIRD_PARTY_NOTICES.md)

OpenKeys Piano 源码采用 GPL-3.0-or-later，见 [LICENSE](LICENSE)。

- Qt 6 Core、GUI、Widgets、Concurrent、Test、LinguistTools 及 Qt 翻译：Qt Project / The Qt Company；采用 LGPL-3.0、GPL-3.0 或商业许可，具体文件以其许可声明为准。参见 [Qt 许可说明](https://www.qt.io/licensing/)。
- FluidSynth：FluidSynth 开发者，LGPL-2.1-or-later。[项目主页](https://github.com/FluidSynth/fluidsynth)。
- PortAudio：PortAudio 贡献者，MIT 许可。[项目主页](https://github.com/PortAudio/portaudio)。

本应用不附带任何乐器音源或采样。用户自行获取并导入第三方 SF2。导入教程中的名称和链接仅标明可选的外部资源，不表示关联或背书。音源作者及许可请查询提供方说明。

EOP 二进制布局研究参考 [eop2midi 格式说明](https://github.com/emizuki/eop2midi/blob/main/specification.md)。C++ 解析器为独立实现，不包含 Go 源码。EOP / Everyone Piano 名称仅表示第三方格式兼容性，不表示关联。

`tests/fixtures.h` 的采样夹具由数学正弦波生成，仅用于自动测试，不是随附的钢琴音源。内置示例为原创演奏事件录制，不是音频采样库。应用图标以仓库中的 SVG 为源生成。

Windows 安装器模板改编自 CMake CPack（Kitware 及贡献者），采用 BSD-3-Clause；完整声明见 `cmake/installer/CMake-LICENSE.rst`。原始许可证和版权声明保持原文；本中文说明用于阅读便利，不替代原始许可文本。

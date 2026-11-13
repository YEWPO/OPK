# 构建与打包

[English](build.md)

OpenKeys Piano 不下载、嵌入或附带音源。所有应用包均包含双语离线导入教程，用户安装后自行导入 SF2。构建和打包不需要音源，也不需要访问音源提供方。

## Linux 开发

Ubuntu 24.04 开发依赖及命令：

```sh
sudo apt-get install cmake ninja-build g++ pkg-config qt6-base-dev qt6-tools-dev qt6-tools-dev-tools qt6-translations-l10n libqt6concurrent6 libfluidsynth-dev portaudio19-dev
cmake --preset linux-dev
cmake --build --preset linux-dev --parallel 4
ctest --preset linux-dev
```

需要 Qt 6.5+、C++20 编译器、FluidSynth 2.3+ 和 PortAudio 19。读取旧版中文 EOP 元数据需要 Qt 的 GB18030 支持，Windows 清单中通过 ICU 提供。

```sh
./build/openkeys --inspect assets/demo.eopiano.json
QT_QPA_PLATFORM=offscreen ./build/openkeys --language en --screenshot /tmp/openkeys.png
```

`--inspect` 不初始化 GUI/音频，验证录制或 EOP 并输出 JSON。`--smoke-test` 和 `--screenshot` 避免硬件初始化并隔离设置。`--soundfont PATH` 使用与 GUI 相同的应用副本导入流程，是用户主动导入，不是默认音源。

## Windows x64

安装 Visual Studio 2022 的 C++ 桌面开发工具、Windows SDK、CMake 3.24+ 和 NSIS。依赖采用动态 `x64-windows` 库。

```powershell
git clone --branch 2025.06.13 https://github.com/microsoft/vcpkg C:/dev/vcpkg
C:/dev/vcpkg/bootstrap-vcpkg.bat -disableMetrics
$env:VCPKG_ROOT = 'C:/dev/vcpkg'
cmake --preset windows-release
cmake --build --preset windows-release --parallel 4
ctest --preset windows-release
./tools/package_windows.ps1
```

vcpkg 清单固定基线 `ef7dbf94b9198bc58f45951adcf1f041fcbc5ea0`。依赖需要安装或存在于构建缓存中，项目从不下载乐器采样。`windows-release` 使用只构建 Release 依赖的覆盖 triplet；`windows-dev` 保留 Debug/Release。CI 在测试前保存编译依赖缓存，明确设置 Qt 插件及 DLL 搜索目录。

打包脚本构建并测试，部署到新的暂存目录，检查无 SF2/SF3，启动暂存应用，然后在 `dist/` 生成 NSIS 和 ZIP。Qt 部署与 CMake 安装所需 DLL、平台插件和第三方声明。未配置用户音源时，可选真实 SF2 测试明确跳过。

每次推送的 Windows CI 都会构建、部署、验证脱离开发 PATH 的启动，并生成安装器和便携 ZIP，位于 `OpenKeysPiano-Windows-installer` 工件。手动工作流选择 `build_installer=true` 也生成相同包；PR 运行构建和测试，不生成安装包。工作流不发布公开 Release。

CI 在一次性 Windows 机器执行 `tools/verify_windows_installer.ps1`，覆盖两种安装语言、带空格的中文路径、默认与全部快捷方式选项、Unicode `IShellLinkW` 目标读取、脱离开发 PATH 的直接/快捷方式启动、升级、占用文件失败和卸载清理。不得在已有 OpenKeys 安装的工作站运行该脚本。交互界面和 DPI 检查仍需实机完成，见 [验证文档](validation.zh-CN.md)。

## 应用图标

`assets/icons/openkeys.svg` 是唯一可编辑源，采用简洁黑白钢琴键。7 个尺寸的 PNG 及多分辨率 `openkeys.ico` 已纳入仓库，普通构建无需图像转换工具。Qt 嵌入 PNG，Windows 程序及安装/卸载程序使用 ICO。

修改 SVG 后，安装 librsvg 的 `rsvg-convert` 和 Python Pillow，再运行：

```sh
python3 tools/generate_icons.py
```

所有 16、24、32、48、64、128、256 像素导出和 ICO 必须同步更新，检查小尺寸辨识度及透明边缘。

## 音源应用副本

SF2 副本位于 `QStandardPaths::AppLocalDataLocation/soundfonts/`（Windows 为 `LOCALAPPDATA`）。文件名采用 SHA-256，`current.json` 原子保存当前哈希及显示名，只在复制和采样器加载成功后更新。

复制和加载在工作线程进行，失败/取消时清理临时及新文件，保留原配置和源文件。成功替换并释放旧采样器后删除旧应用副本；清理失败不使新音源失效。外部源路径从不被删除。

启动仅加载元数据指向的已导入音源，没有随附或开发目录回退。旧 `QSettings` 外部路径只作为迁移提示，必须显式导入。采样率变化时重载副本，不再次导入。

双语教程作为 Qt 资源嵌入 UI 库，也安装到 `docs/soundfont.md` 和 `docs/soundfont.zh-CN.md`。无需联网、特定工作目录或源码，教程内语言链接也支持离线跳转。

## 可选音源诊断

```sh
./build/openkeys-soundfont-check --soundfont /path/to/piano.sf2 --output build/soundfont-check
OPENKEYS_TEST_SOUNDFONT=/path/to/piano.sf2 ctest --test-dir build -R '^real_soundfont$' --output-on-failure
```

独立工具不打开音频设备或网络；测试 44.1/48 kHz、A0–C8 的三档力度、延音/释放和内置示例，输出 PCM16 WAV 和包含音源哈希、无效/削波采样计数的 JSON 报告。这不测量实体输出延迟，也不认证第三方音质。未设置集成测试输入时明确跳过。

## 安装语言与卸载

安装和卸载支持英文、简体中文。交互安装先选语言，中文系统默认简体中文，其余默认英文。选择记录供卸载使用，并写入 `install-language.ini` 作为应用首次启动的默认语言。已有应用语言偏好（含跟随系统）优先。缺少语言记录的旧安装按系统语言回退。

静默安装接受 `/LANG=1033`（英文）或 `/LANG=2052`（简体中文）、`/DESKTOP=0|1` 和 `/STARTMENU=0|1`。快捷方式默认均开启，不修改 PATH。`/D=安装路径` 必须在最后。静默安装不弹语言选择，显式语言值无效时返回失败。

升级先卸载旧版。卸载删除记录的快捷方式和整个安装目录，包含用户放入的文件；目录外配置和音源保留。占用文件或权限不足导致失败时保留重试入口，不显示成功。

`cmake/installer/NSIS.template.in` 改编自 CMake CPack，上游许可随附为 `CMake-LICENSE.rst`。更换 CMake 版本后需重新验证模板兼容性。

## 翻译与预览维护

用户可见源文案为英文，简体中文翻译位于 `translations/openkeys_zh_CN.ts`。构建需要 Qt LinguistTools，Linux 依赖见上方；Windows vcpkg 使用 `qttools[linguist]` 和 `qttranslations`。应用及 Qt 标准控件翻译均嵌入，运行不依赖网络或源码。

使用 Qt 的 `lupdate src -ts translations/openkeys_zh_CN.ts` 更新目录，审阅并翻译所有新增条目后重建。正常构建只编译翻译，不改写源文件。`python3 tools/verify_resources.py` 检查翻译覆盖、占位符、双语文档链接、图标尺寸及预览，需要 Python Pillow，CI 与 CTest 一起执行。

语言保存在现有 `OpenKeys/Piano` 设置的 `ui/language`，值为 `system`、`en` 或 `zh_CN`。通过 Language / 语言 修改后重启生效。`--language system|en|zh_CN` 只覆盖当前进程，不写入偏好。

从当前构建重新生成双语预览：

```sh
python3 tools/generate_previews.py --executable /absolute/path/to/openkeys
```

脚本固定 Fusion 样式、缩放、字体大小和初始窗口状态。`docs/preview.png` 为英文，`docs/preview.zh-CN.png` 为中文，均为真实截图。提交前检查双语截图、所有尺寸图标与 SVG 的一致性。

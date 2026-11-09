# ASCIIArt

[English](README.md) | **简体中文**

一个用 Visual C++ 编写的 Windows 控制台程序：选择图片或视频，把画面转换成字符画。

- 图片：使用 Windows 自带的 **WIC** 解码，可输出到控制台，也可保存为 TXT / HTML。
- 视频：使用 Windows 自带的 **Media Foundation** 解码，优先 NV12 并直接降采样，在控制台里实时播放字符画动画。
- 音频：视频中的音轨通过 **XAudio2** 播放，与画面同步；按空格暂停时音频也暂停。
- 纯 Windows API，不依赖 OpenCV、FFmpeg 或任何第三方库。
- 支持 24 位真彩色 ANSI 输出、Unicode 字符集、自动适配窗口大小。
- 中英双语界面：默认跟随系统 UI 语言（中文显示中文，英文显示英文，其他语言回退英语），也可在主菜单切换，或使用 `--lang zh` / `--lang en`。
- 清屏时会同时清除回滚缓冲区，向上滚动也不会看到之前的字符画输出。
- 支持自定义字符网格分辨率（列 × 行），尺寸不设上限（如 `10000x0` 只固定宽度）；超大尺寸在生成/保存时会提示可能耗时较久并占用大量内存。
- 大分辨率预览时自动缩小控制台字体、扩大窗口；Windows Terminal 中会引导手动缩放。

```
  ==================================================
       ASCIIArt  -  image / video  ->  ASCII art
  ==================================================

  1. Choose an image and convert
  2. Choose a video and play
  3. Conversion settings
  4. Help
  5. Switch language (中文)
  0. Exit
```

## 环境要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 / 11（64 位） |
| 编译器 | Visual Studio 2022 或 2026，安装「使用 C++ 的桌面开发」 |
| SDK | Windows 10 / 11 SDK |
| 构建工具 | CMake 3.20+（Visual Studio 自带） |
| 终端 | Windows Terminal 或系统默认控制台；需要 ANSI 转义支持 |

程序链接静态 C++ 运行库，生成 EXE 后不依赖单独的 Visual C++ 运行库安装。

## 构建

在项目目录中运行：

```bat
build.bat
```

脚本会自动查找 Visual Studio 自带的 CMake，生成 x64 Release 版本：

```
build\Release\ASCIIArt.exe
```

也可以手动构建：

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

如果想用 Visual Studio 的图形界面：

1. 先运行一次 `build.bat`（或上面的 CMake 命令）。
2. 打开 `build\ASCIIArt.slnx`（Visual Studio 2026）或 `build\ASCIIArt.sln`（Visual Studio 2022）。
3. 将 `ASCIIArt` 设为启动项目，按 `Ctrl+F5` 运行。

## 快速开始

直接双击 `run.bat`，或运行：

```bat
build\Release\ASCIIArt.exe
```

进入交互菜单后：

1. 选择图片：打开文件选择框 → 自动转换 → 询问是否保存为 TXT / HTML。
2. 选择视频：打开文件选择框 → 在控制台中播放。
3. 修改设置：输出尺寸、彩色、反色、字符集、宽高比、视频帧率、大分辨率适配。
4. 使用说明：查看命令行帮助和交互提示。
5. 切换语言：在英语和中文之间切换，界面立即刷新。

也可以直接把图片或视频拖到 `run.bat` 上；脚本会在结束后暂停，方便查看结果。

## 命令行用法

```text
ASCIIArt                              进入交互式菜单
ASCIIArt <图片或视频>                 直接转换 / 播放
ASCIIArt <文件> [选项]                指定参数转换 / 播放
```

### 选项

| 选项 | 说明 |
| --- | --- |
| `-w, --width <n>` | 目标宽度（字符列数）。`0` 或省略为自动，默认 100；超出窗口时会尝试自动适配 |
| `--height <n>` | 目标高度（字符行数）。省略或 `0` 时按宽度和宽高比自动计算 |
| `-r, --resolution <w>x<h>` | 固定字符网格分辨率，例如 `120x40`；也支持 `120x0`（只固定宽度）和 `0x40`（只固定高度） |
| `-c, --color` | 使用 24 位真彩色 |
| `--no-color` | 不使用颜色 |
| `-i, --invert` | 反转亮度 |
| `-a, --aspect <x>` | 字符宽高比，默认 `0.50`，范围 `0.20-1.00` |
| `-g, --gamma <x>` | 伽马校正，默认 `1.0`；`>1` 更暗，`<1` 更亮 |
| `-f, --fps <n>` | 视频帧率上限，默认 24，范围 `1-120` |
| `-l, --loop` | 循环播放视频 |
| `-s, --save <文件>` | 保存图片字符画；`.txt` 为纯文本，`.html` 为彩色网页 |
| `--no-display` | 只保存，不在控制台显示图片字符画 |
| `--fit` | 大分辨率时自动缩小字体/放大窗口（默认开启） |
| `--no-fit` | 禁止自动调整字体和窗口 |
| `--charset <字符>` | 自定义字符集（从暗到亮），也可填预设名 |
| `--preset <名称>` | 字符集预设：`standard` / `detailed` / `blocks` / `minimal` |
| `--lang <en\|zh>` | 界面语言；默认跟随系统 UI 语言，非中文/英文时回退 `en` |
| `-h, --help` | 显示帮助（按当前语言输出） |

### 示例

```bat
:: 交互式菜单（界面语言跟随系统）
ASCIIArt

:: 使用中文界面启动
ASCIIArt --lang zh

:: 转换图片，宽度 120，彩色显示
ASCIIArt photo.jpg -w 120 -c

:: 固定输出为 120 列 x 50 行字符
ASCIIArt photo.jpg --resolution 120x50 -c

:: 2560 列 x 1440 行字符（保存不受窗口限制）
ASCIIArt photo.jpg --resolution 2560x1440 --no-display --save big.txt

:: 只固定高度为 40 行，宽度按原图比例自动计算
ASCIIArt photo.jpg --height 40

:: 保存为纯文本
ASCIIArt photo.png --no-color --save art.txt

:: 保存为彩色网页（不显示预览）
ASCIIArt logo.png --preset blocks --no-display --save logo.html

:: 播放视频，限制宽度和帧率
ASCIIArt clip.mp4 -w 100 -f 30

:: 循环播放
ASCIIArt clip.mp4 -l
```

### 关于输出分辨率

这里的「分辨率」指字符网格的 **列数 × 行数**，不是像素分辨率。例如：

```bat
ASCIIArt photo.png --resolution 120x50 --no-display --save art.txt
```

会严格生成 120 列、50 行字符（保存文件中每行最多 120 个字符，共 50 行）。

- 同时指定 `--width` 和 `--height` 等价于 `--resolution 宽x高`。
- 同时固定列数和行数会按目标比例拉伸/压缩画面；想保持原图比例时只指定一边。
- 只指定 `--width` 时，高度按原图比例和字符宽高比自动计算，并尝试自动适配大分辨率。
- 只指定 `--height` 时，宽度自动计算，同样会尝试自动适配。
- 如果自动/手动适配都失败，预览会缩到窗口大小，并在「网格」行显示目标和实际尺寸。
- 固定分辨率大于当前控制台窗口时，程序会自动尝试：
  1. 最大化控制台窗口；
  2. 必要时临时缩小控制台字体（最低约 4×6 像素）；
  3. 调整窗口大小以完整显示字符画。
- 在传统控制台（conhost）中，预览/播放期间窗口会固定在计算网格时的大小（不能手动拖动调整），退出预览或播放后自动恢复原来的字体、窗口大小和位置。
- Windows Terminal 不允许程序修改字体。程序会提示你按 `Ctrl+-` 缩小终端字体或调大窗口，然后按 Enter 重试（重试次数不限）；输入 `s` 可跳过预览仅保存，输入 `q` 取消。预览结束后可按 `Ctrl+0` 恢复字体。
- 如果字符画刚好占满整个窗口，程序会进入**全屏字符画模式**：不显示文件信息头/播放状态行，标题栏显示尺寸和按键提示；图片按任意键返回。
- 如果调整后仍放不下，输入 `s` 可跳过预览并按目标尺寸生成/保存，输入 `q` 取消；视频只提供重试或取消。
- 输出尺寸不设上限；字符总数达到 400 万时，会在生成/保存前提示可能耗时较久并占用大量内存。
- 使用 `--no-fit` 可关闭自动字体/窗口调整，保持原来的报错行为。
- 清屏会连同终端的回滚缓冲区一起清除（ANSI `2J` + `3J`），因此向上滚动不会看到之前的字符画；同时也会清除启动程序之前在同一个终端里的历史输出。

> 2560×1440 字符网格（3,686,400 个字符）已经支持。保存文件不受窗口限制；终端预览需要终端在当前字体下确实能提供至少 2560 列 × 1440 行（全屏模式）。

## 字符集预设

字符从「暗」到「亮」排列：

| 名称 | 字符 |
| --- | --- |
| `standard`（默认） | ` .:-=+*#%@` |
| `blocks` | ` ░▒▓█` |
| `minimal` | ` .#@` |
| `detailed` | 一组约 70 级的 ASCII 字符，适合细节较多的图片 |

自定义示例：

```bat
ASCIIArt photo.png --charset " .oO@"
ASCIIArt photo.png --charset " .:-=+*#%@" -w 80
ASCIIArt photo.png --preset blocks
```

注意：

- 字符集至少需要 2 个可见字符，最多 255 个 Unicode 码点。
- 建议使用等宽的单列字符；中文等双列字符会破坏字符画的行宽对齐。
- 含空格的字符集需要用引号包起来。

## 保存格式

### TXT

纯字符画，UTF-8 编码，不含颜色，适合放进代码注释、日志或文本文件。

### HTML

独立网页，黑底、等宽字体、保留 24 位颜色，可直接用浏览器打开。例如：

```bat
ASCIIArt photo.png -w 100 --save art.html
```

没有扩展名时默认保存为 `.txt`。

## 视频播放按键

| 按键 | 功能 |
| --- | --- |
| `空格` | 暂停 / 继续 |
| `Q` / `Esc` | 退出播放 |

视频会同时播放文件中的音频轨道。暂停时音频一起暂停，继续时一起继续；循环播放会从头重新播放音频。没有音轨或音频设备不可用时，视频仍然可以静音播放。

## 实现说明

| 模块 | 技术 |
| --- | --- |
| 图片解码 | Windows Imaging Component（WIC），统一转换为 32bpp RGBA |
| 视频解码 | Media Foundation Source Reader，优先 NV12，回退 RGB32 / RGB24 |
| 视频缩放 | 读取时直接按 2× 字符网格降采样，跳过全分辨率 RGBA 转换和拷贝 |
| 音频解码/播放 | Media Foundation 流式解码为 PCM，XAudio2 播放，只保留约 16 MB 缓冲区 |
| 缩放/采样 | 按字符网格对源像素做区域采样（area sampling） |
| 亮度映射 | `0.2126R + 0.7152G + 0.0722B`，可选伽马 / 反色 |
| 颜色输出 | ANSI `ESC[38;2;R;G;Bm` 24 位真彩色 |
| 终端控制 | `WriteConsoleW` + VT 转义序列；自动处理 UTF-8 和换行 |
| 文件选择 | Win32 通用文件对话框 `GetOpenFileNameW` / `GetSaveFileNameW` |

核心文件：

```text
src/
  main.cpp           命令行解析、初始化
  App.cpp            交互菜单、图片/视频流程
  AsciiConverter.cpp 网格计算、像素采样、字符映射、ANSI/HTML 输出
  Charset.cpp        字符集预设与自定义 Unicode 字符集
  Localization.h/.cpp 中英文界面文案与语言切换
  ImageLoader.cpp    WIC 图片解码
  VideoPlayer.cpp    Media Foundation 视频解码
  AudioPlayer.cpp    Media Foundation 音频解码 + XAudio2 播放
  AsciiExporter.cpp  TXT / HTML 文件写入
  Console.cpp        控制台 UTF-8、VT、输入输出
  FileDialog.cpp     Win32 文件选择/保存对话框
  Util.cpp           字符串、路径、错误信息工具
```

## 性能优化

针对高分辨率视频，播放管线做了以下几层优化：

1. **NV12 解码输出**：不再把每帧转换为全分辨率 RGB32，避免最昂贵的色彩转换。
2. **读取时降采样**：视频帧直接采样到约 2× 字符网格（例如 100×33 字符只处理 200×66 像素），不再复制完整 4K 帧。
3. **轻量字符渲染**：颜色转换只在采样后的少量像素上执行；伽马/反色改为 256 级查表，不再逐格调用 `pow()`。
4. **快速 ANSI 生成**：颜色转义序列用手写整数格式化代替 `snprintf`，并直接生成 UTF-16 屏幕缓冲，省去每帧「UTF-8 → 换行规范化 → UTF-16」的完整拷贝。
5. **后台写屏线程**：终端写入与下一帧的解码/渲染并行执行（最多缓存 1 帧），让解码和终端解析互相掩盖耗时。
6. **帧缓冲复用**：屏幕缓冲、字符集宽字符表在播放期间只分配一次。
7. **1 ms 定时器精度**：播放节奏不再被系统默认约 15.6 ms 的定时器粒度拖慢。

本机实测（每帧 CPU 开销，320×120 字符、24 位真彩色）：

| 阶段 | 优化前 | 优化后 |
| --- | ---: | ---: |
| ASCII 渲染 | 约 1.3 ms | 约 1.0 ms |
| ANSI 生成 | 约 9.9 ms | 约 1.7 ms |
| UTF-8 → UTF-16 转换 | 约 1.8 ms | 0（直接生成 UTF-16） |
| 合计 | 约 13.0 ms | 约 2.7 ms |

终端写入的耗时现在与下一帧的处理重叠，不再串行累加。字符网格越大，终端解析仍越容易成为瓶颈，可适当降低 `--width` 或使用 `--no-color`。

## 限制与注意事项

- 视频能否播放取决于系统已安装的解码器。MP4 / H.264 / AVI 通常可用；MKV / WebM 等格式可能需要额外解码器。
- 音频需要系统有可用的播放设备。音频采用流式解码，只保留约 16 MB 待播放缓冲区，不再把整条音轨预加载到内存；长音轨也可以播放。
- 控制台渲染较慢时，音频和画面可能产生轻微不同步。
- 图片 GIF 只读取第一帧，不支持 GIF 动画。
- 视频暂不支持导出为字符画文件；`--save` 对视频无效，会给出提示。
- 视频播放必须直接在控制台窗口中进行，不能把 stdout 重定向到文件。
- 图片预览会根据控制台窗口大小自动缩小；使用 `--no-display --save` 时不受窗口大小限制。
- 使用固定分辨率且大于控制台窗口时，图片预览会报错（配合 `--save` 会自动只保存）；视频必须能完整显示在控制台内。
- 控制台颜色需要 Windows 10/11 的 ANSI/VT 支持。重定向输出或终端不支持时，预览自动退化为黑白。
- 高分辨率视频已经使用 NV12 和 2× 网格降采样；实际帧率仍可能受控制台/终端渲染速度限制，可以降低 `--width`、使用 `--no-color` 或 `--fps`。
- 图片的 EXIF 旋转信息未处理；手机竖拍照片可能显示为横向。
- 部分手机视频的旋转元数据未处理，画面方向可能不正确。
- 保存的 TXT 是 UTF-8 无 BOM；如果编辑器按 ANSI 打开出现乱码，请选择 UTF-8。

## 开发与测试

`test-assets/test-image.png` 是一张可直接用来试用的示例图片：

```bat
build\Release\ASCIIArt.exe test-assets\test-image.png -w 100 -c
```

如果需要测试视频播放，可生成一个无需第三方编码器的未压缩 AVI（约 12 MB，需要 Python + numpy）：

```bat
python tools\make_test_avi.py
build\Release\ASCIIArt.exe test-assets\test-video.avi -w 80
```

## 常见问题

**视频提示「需要直接在控制台窗口中运行」**

不要在命令后面使用 `>` 重定向，也不要通过某些 IDE 的「输出」窗口运行。请在 Windows Terminal 或系统控制台中直接运行。

**颜色显示为乱码或转义字符**

当前终端不支持 VT。请使用 Windows Terminal 或 Windows 10/11 默认控制台；也可以加 `--no-color` 输出纯字符画。

**视频打不开**

先确认文件能用系统播放器（如「电影和电视」）播放；如果系统缺少解码器，本程序也无法解码。优先尝试 MP4/H.264 或 AVI。

**中文乱码**

程序内部和输出文件均为 UTF-8。控制台需要 UTF-8 代码页（程序会自动设置）；保存的 TXT 请用 UTF-8 方式打开。

**界面语言不对 / 想切换语言**

程序默认跟随系统 UI 语言：中文系统显示中文，英文系统显示英文，其他语言回退英语。启动时加 `--lang zh` 或 `--lang en` 可强制指定；也可以在交互主菜单选择 `5. Switch language` 即时切换。

**设置了 `--width 300`，预览却是 85×24**

这是旧版的行为：`--width` 只是目标宽度，预览高度不足时会按高度反推并缩小列数。  
现在会先尝试自动缩小字体/扩大窗口；如果仍然放不下，才会缩到窗口大小，并在标题行标注：

```text
网格: 85 x 24（目标 300x84，已适配窗口）
```

想严格固定为 300×84，请使用：

```bat
ASCIIArt image.png --resolution 300x84
```

想保存完整 300×84 而不受窗口限制：

```bat
ASCIIArt image.png --resolution 300x84 --no-display --save art.txt
```

**播放卡顿**

降低宽度和帧率：

```bat
ASCIIArt clip.mp4 -w 80 -f 20
```

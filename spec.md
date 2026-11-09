# Spec: 修复图片转换时的 std::length\_error 未处理异常

## 1. 背景与问题

用户报告：在转换图片时，程序崩溃并弹出“未经处理的异常”：

```
0x00007FFF2CC3569A 处(位于 ASCIIArt.exe 中)有未经处理的异常:
Microsoft C++ 异常: std::length_error，位于内存位置 0x00000026DF9CDDC0 处。
```

这是 MSVC 调试器对 `std::length_error` 逃逸出 `wmain` 的提示。当前 `wmain` 没有任何异常兜底，因此任何标准库异常都会变成未处理异常导致进程终止。

## 2. 现状排查结论

1. 网格尺寸在 `DesiredGrid` / `FitGrid` 的所有分支最终都会调用 `ClampGridCells`，单元数上限 `kMaxAsciiCells = 16,777,216`，`RenderAscii` 的 `cells.resize` 也基于该网格。因此在常规路径下 `frame.cells.size() <= 16,777,216`。
2. 已回归验证且**未复现**的命令（说明常规大尺寸路径本身不崩溃）：
   * `--resolution 65536x256 --no-color --no-display --save build\test-output\max.txt`（16,777,472 字节，exit 0）
   * `--resolution 10000x0 -c --no-display --save build\test-output\c.html`（28,673,595 字节，exit 0）
   * `--resolution 65536x256 --no-color --no-display --save build\test-output\max.html`（16,778,164 字节，exit 0）
3. 与 `std::length_error` 抛出机理吻合的隐患：
   * `src/AsciiConverter.cpp:421`：`result.reserve(static_cast<std::size_t>(frame.cols * frame.rows * 18));`
     括号内先做 `int` 乘法（每单元最多 18 字节的 HTML 转义预算）。若某个调用方直接传入未夹紧的超预算 `GridSize`（例如 `65536x20000`，乘积 1.31e9，乘 18 后溢出 `int`），有符号溢出产生负数，转 `size_t` 后变为极大值，`std::string::reserve` 抛出 `std::length_error`。
   * `src/RenderAscii` 信任传入的 `grid`，自身不做预算校验；`AsciiFrame` 没有不变量保护。
4. 次要隐患（同类缺陷，一并加固）：
   * `src/Util.cpp` 的 `ToUtf8` / `ToWide` 使用 `static_cast<int>(text.size())`，字符串超过 `INT_MAX` 时截断为负数。
   * `src/AsciiExporter.cpp:83` 的 `frame.cells.size() * 18 + 2048` 未显式约束帧大小，也没有超限的错误提示。
5. `wmain` 无 `try/catch`，是“未处理异常弹窗”的直接原因。

## 3. 目标

1. 任何 `std::exception`（含 `std::length_error`、`std::bad_alloc`）都不再导致未处理异常弹窗；输出本地化错误信息并返回非零退出码。
2. 消除 `int` 乘法溢出转 `size_t` 的可能，所有尺寸/容量计算全程使用 `size_t` / `int64_t`。
3. `RenderAscii` 自身对网格做预算夹紧，保证 `AsciiFrame` 不变量：`cells.size() <= kMaxAsciiCells`。
4. 保存时若帧异常超大，返回明确的本地化错误，而不是崩溃。
5. 构建无警告；中英双语、图片/视频既有功能不回退。

## 4. 非目标

* 不修改 65536 / 20000 / 16,777,216 这些上限值。
* 不重构渲染管线，不改变输出格式与颜色行为。
* 不新增命令行选项或设置项。
* 不排查 WIC/Media Foundation 的系统级异常（与本次 std::length\_error 无关）。

## 5. 变更清单

### 5.1 src/main.cpp — `wmain`

* 保留 `RuntimeGuard runtime;` 在 `try` 之外，确保栈展开时析构正常执行（`MFShutdown` / `console::Shutdown` / `CoUninitialize`）。
* 将现有函数体（COM/MF 初始化、参数解析、`App` 运行）整体包入：
  * `catch (const std::exception& ex)`：先 `console::Initialize()`（幂等，保证 catch 内可输出），再
    `console::Err(Tr8("\n  Error: ", "\n  错误: ") + std::string(ex.what()) + "\n");`，`return 1;`
  * `catch (...)`：输出 `Tr8("\n  Error: an unknown exception occurred.\n", "\n  错误: 发生未知异常。\n")`，`return 1;`
* 新增 include：`<exception>`（`<string>` 已存在）。

### 5.2 src/AsciiConverter.cpp

* `RenderAscii`：进入时对网格做二次防御夹紧，再写入 `frame`：

  ```cpp
  GridSize safe = grid;
  ClampGridCells(safe);
  if (image.Empty() || charset.Empty() || safe.cols <= 0 || safe.rows <= 0) {
      return frame;
  }
  frame.cols = safe.cols;
  frame.rows = safe.rows;
  frame.cells.resize(static_cast<std::size_t>(safe.cols) * safe.rows);
  ```

* `FrameToHtmlFragment`（第 421 行）：
  `result.reserve(static_cast<std::size_t>(frame.cols) * static_cast<std::size_t>(frame.rows) * 18u);`

* 复核 `FrameToText` / `FrameToAnsi` 的 reserve 已是 `size_t` 计算，保持不变。

### 5.3 src/AsciiExporter.cpp — `SaveAsciiFile`

* 在 `if (html)` 分支前统一增加防御：

  ```cpp
  if (frame.cells.size() > static_cast<std::size_t>(kMaxAsciiCells)) {
      error = Tr(L"The character art is too large to save.",
                 L"字符画过大，无法保存。");
      return false;
  }
  ```

  需要确认 `AsciiExporter.h` 已间接包含 `AsciiConverter.h`；若未包含则在 `.cpp` 中补充 `#include "AsciiConverter.h"`。
* 第 83 行改为：
  `document.reserve(static_cast<std::size_t>(frame.cells.size()) * 18u + 2048u);`

### 5.4 src/Util.cpp

* `ToUtf8` / `ToWide`：在调用 Win32 转换前把长度夹到 `INT_MAX`：

  ```cpp
  const int length = static_cast<int>(
      std::min(std::size_t{std::numeric_limits<int>::max()}, text.size()));
  ```

  后续 `static_cast<int>(text.size())` 全部替换为 `length`；新增 `#include <limits>`（`<algorithm>` 若未包含则一并补充）。

## 6. 验证步骤

1. 构建：PowerShell 执行 `& .\build.bat`，要求 0 警告 0 错误，产物 `build\Release\ASCIIArt.exe`。
2. 常规图片回归（准备一张普通 PNG/JPG）：
   * `build\Release\ASCIIArt.exe <img> --no-color --no-display --save build\test-output\t1.txt`
   * `build\Release\ASCIIArt.exe <img> -c --no-display --save build\test-output\t2.html`
   * 输出文件非空，退出码 0。
3. 大尺寸回归：
   * `--resolution 10000x0 -c --no-display --save build\test-output\t3.html`
   * `--resolution 65536x256 --no-color --no-display --save build\test-output\t4.html`
   * `--width 65536 --height 20000 -c --no-display --save build\test-output\t5.html`
   * 三者均退出码 0，无未处理异常。
4. 显式超预算网格防御：临时用调试器或在测试中以绕过参数校验的方式验证 `RenderAscii` 会被夹紧（可由代码审查确认，不保留测试代码）。
5. 异常兜底验证：构造一次失败场景（例如 `--save` 指向不存在的目录），确认输出本地化错误、退出码非 0、无弹窗。
6. 视频冒烟（可选但建议）：`--resolution 80x24` 播放 3 秒视频，按 Q 退出正常。

## 7. 验收标准

* 图片转换（常规/大尺寸、TXT/HTML、彩色/黑白）不再出现“未经处理的异常”弹窗。
* 所有异常路径输出本地化错误，退出码为 1（参数错误仍为 2）。
* `& .\build.bat` 无警告无错误。
* 中英双语切换、系统语言检测、`--lang en|zh` 行为不变。
* 视频播放回归正常。

## 8. 风险与仍未确定项

* 若加固后用户仍能复现同一异常，说明触发路径不在当前已审阅的分配点；届时需用户提供：具体命令或菜单操作步骤、设置的分辨率、图片文件（或尺寸/格式）。该信息将用于追加定位，不在本规格范围内猜测修复。
* `RenderAscii` 夹紧属于防御性行为：仅在调用方传入非法网格时生效，正常路径结果不变。

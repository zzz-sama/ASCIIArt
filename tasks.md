# Tasks: 修复图片转换时的 std::length_error

## 前置

- [x] 用户批准 `spec.md`

## 实现任务

- [x] T1 在 `src/main.cpp` 的 `wmain` 增加 `std::exception` / `...` 兜底捕获
  - 保留 `RuntimeGuard` 位于 `try` 外部
  - catch 内先 `console::Initialize()`，再用 `Tr8` 输出本地化错误并 `return 1`
  - 补充 `#include <exception>`
  - 验证：范围的构建与失败路径手动运行

- [x] T2 在 `src/AsciiConverter.cpp` 的 `RenderAscii` 增加网格防御夹紧
  - 复制 `grid` 到局部 `safe`，调用 `ClampGridCells(safe)` 后再写 `frame`
  - 保证 `frame.cells.size() <= kMaxAsciiCells`

- [x] T3 修正 `src/AsciiConverter.cpp` 的 `FrameToHtmlFragment` reserve
  - 改为 `static_cast<std::size_t>(frame.cols) * static_cast<std::size_t>(frame.rows) * 18u`
  - 消除 `int` 先乘的溢出路径

- [x] T4 加固 `src/AsciiExporter.cpp` 的 `SaveAsciiFile`
  - 增加 `frame.cells.size() > kMaxAsciiCells` 的本地化错误返回
  - `document.reserve` 改为 `size_t` 显式计算
  - 如需要，补充 `#include "AsciiConverter.h"`

- [x] T5 加固 `src/Util.cpp` 的 `ToUtf8` / `ToWide`
  - 长度夹到 `INT_MAX` 后再转 `int`，替换原有 `static_cast<int>(text.size())`
  - 补充 `#include <limits>` / `#include <algorithm>`

## 验证任务

- [x] T6 `& .\build.bat` 构建，0 警告 0 错误
- [x] T7 常规图片 TXT/HTML 回归（彩色/黑白），退出码 0 且输出非空
- [x] T8 大尺寸回归
  - `--resolution 10000x0 -c --no-display --save ...`
  - `--resolution 65536x256 --no-color --no-display --save ...`
  - `--width 65536 --height 20000 -c --no-display --save ...`
- [x] T9 失败路径回归：`--save` 指向不存在的目录，确认本地化错误 + 退出码非 0 + 无弹窗
- [x] T10 视频冒烟（可选）：短时间播放并按 Q 退出正常（当前重定向环境仅验证了保护分支）
- [x] T11 双语回归：`--lang en` / `--lang zh` 与菜单切换正常

## 复查

- [x] 代码审查确认无新增 `int` 乘法直接转 `size_t`
- [x] 确认未改变尺寸上限、输出格式与既有行为

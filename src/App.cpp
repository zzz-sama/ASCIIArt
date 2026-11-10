#include "App.h"

#include "AsciiExporter.h"
#include "AudioPlayer.h"
#include "Console.h"
#include "FileDialog.h"
#include "ImageLoader.h"
#include "Localization.h"
#include "Util.h"
#include "VideoPlayer.h"

#include <mmsystem.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cwchar>
#include <filesystem>
#include <limits>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

namespace {

using Clock = std::chrono::steady_clock;

// Outputs at least this large trigger a "may take a long time" notice when
// they are about to be generated for saving.
constexpr std::int64_t kLargeOutputCells = 4'000'000;

std::string ToUtf8(const std::wstring& text) { return util::ToUtf8(text); }

bool IsImageExtension(const std::wstring& ext) {
    return ext == L".png" || ext == L".jpg" || ext == L".jpeg" ||
           ext == L".bmp" || ext == L".gif" || ext == L".tif" ||
           ext == L".tiff" || ext == L".ico" || ext == L".webp" ||
           ext == L".jxr" || ext == L".wdp";
}

bool IsVideoExtension(const std::wstring& ext) {
    return ext == L".mp4" || ext == L".m4v" || ext == L".mov" ||
           ext == L".avi" || ext == L".wmv" || ext == L".mkv" ||
           ext == L".webm" || ext == L".mpg" || ext == L".mpeg" ||
           ext == L".ts" || ext == L".m2ts" || ext == L".3gp" ||
           ext == L".3g2" || ext == L".asf";
}

std::string FormatTime(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0.0) {
        seconds = 0.0;
    }
    const int total = static_cast<int>(seconds);
    const int hours = total / 3600;
    const int minutes = (total % 3600) / 60;
    const int secs = total % 60;
    char buffer[32];
    if (hours > 0) {
        std::snprintf(buffer, sizeof(buffer), "%d:%02d:%02d", hours, minutes,
                      secs);
    } else {
        std::snprintf(buffer, sizeof(buffer), "%02d:%02d", minutes, secs);
    }
    return buffer;
}

bool ParseInt(const std::wstring& text, int& value) {
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != L'\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDouble(const std::wstring& text, double& value) {
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != L'\0' ||
        !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

// Accepts "120x40", "120X40", "120*40", "120" (width only), "x40" (height
// only), or "0" (automatic). Zero dimensions mean "derive this dimension".
bool ParseSize(const std::wstring& text, int& width, int& height) {
    const std::wstring value = util::Trim(text);
    if (value.empty()) {
        return false;
    }

    width = 0;
    height = 0;
    const std::size_t separator = value.find_first_of(L"xX*");
    if (separator == std::wstring::npos) {
        if (!ParseInt(value, width)) {
            return false;
        }
    } else {
        const std::wstring left = util::Trim(value.substr(0, separator));
        const std::wstring right = util::Trim(value.substr(separator + 1));
        if (!left.empty() && !ParseInt(left, width)) {
            return false;
        }
        if (!right.empty() && !ParseInt(right, height)) {
            return false;
        }
    }

    return width >= 0 && height >= 0;
}

std::string SizeText(int width, int height) {
    if (width > 0 && height > 0) {
        return std::to_string(width) + "x" + std::to_string(height);
    }
    if (width > 0) {
        return Tr8("width ", "宽 ") + std::to_string(width) +
               Tr8(" (height auto)", "（高自动）");
    }
    if (height > 0) {
        return Tr8("height ", "高 ") + std::to_string(height) +
               Tr8(" (width auto)", "（宽自动）");
    }
    return Tr8("auto", "自动");
}

bool AskYesNo(const std::string& prompt) {
    const std::wstring answer = console::ReadLine(prompt);
    return util::EqualsIgnoreCase(answer, L"y") ||
           util::EqualsIgnoreCase(answer, L"yes") ||
           util::EqualsIgnoreCase(answer, L"是");
}

enum class ManualFitMode { None, Normal, Compact, SaveOnly, Cancel };

// Last-resort fallback when the console API cannot resize the terminal, most
// notably inside Windows Terminal. The user shrinks the terminal font or
// enlarges the window manually, then presses Enter. Retrying is unlimited;
// the user can also skip the preview and save only, or cancel.
ManualFitMode TryManualConsoleFit(int artCols, int artRows, int reservedRows,
                                  bool allowSaveOnly) {
    if (!console::IsConsole() || !console::IsInputConsole()) {
        return ManualFitMode::None;
    }

    const bool windowsTerminal = console::IsWindowsTerminalHost();
    for (;;) {
        const int availableCols = std::max(1, console::Width());
        const int availableRows = std::max(1, console::Height());
        if (artCols <= availableCols) {
            if (artRows + reservedRows <= availableRows) {
                return ManualFitMode::Normal;
            }
            if (artRows <= availableRows) {
                return ManualFitMode::Compact;
            }
        }

        console::Info(
            Tr8("\n  The current window can hold at most ",
                "\n  当前窗口最多容纳 ") +
            std::to_string(availableCols) + "x" +
            std::to_string(availableRows) +
            Tr8(" characters.\n", " 字符。\n"));
        console::Info(
            Tr8("  The full layout (including the header) needs at least ",
                "  完整显示（含标题信息）至少需要 ") +
            std::to_string(artCols) + "x" +
            std::to_string(artRows + reservedRows) +
            Tr8("; full-screen art needs ", "；全屏字符画需要 ") +
            std::to_string(artCols) + "x" + std::to_string(artRows) +
            Tr8(".\n", "。\n"));
        if (windowsTerminal) {
            console::Info(Tr8(
                "  Press Ctrl+- to shrink the Windows Terminal font, or make "
                "the window larger.\n",
                "  请按 Ctrl+- 缩小 Windows Terminal 字体，或把窗口调大。\n"));
        } else {
            console::Info(Tr8(
                "  Drag the console window edge to enlarge it, or use "
                "Ctrl+wheel to shrink the font.\n",
                "  请拖动控制台窗口边缘调大窗口，或使用 Ctrl+滚轮 缩小字体。\n"));
        }
        if (allowSaveOnly) {
            console::Info(Tr8(
                "  Press Enter to retry after adjusting, type s to skip the "
                "preview and save only, or type q to cancel.\n",
                "  调整后按 Enter 重试，输入 s 跳过预览仅保存，输入 q 取消。\n"));
        } else {
            console::Info(Tr8(
                "  Press Enter to retry after adjusting, or type q to "
                "cancel.\n",
                "  调整后按 Enter 重试，输入 q 取消。\n"));
        }
        const std::wstring answer = console::ReadLine(
            allowSaveOnly
                ? Tr8("  Press Enter to retry (s: save only, q: cancel): ",
                      "  按 Enter 重试（s: 仅保存，q: 取消）: ")
                : Tr8("  Press Enter to retry (q: cancel): ",
                      "  按 Enter 重试（q: 取消）: "));
        if (util::EqualsIgnoreCase(answer, L"q")) {
            return ManualFitMode::Cancel;
        }
        if (allowSaveOnly && util::EqualsIgnoreCase(answer, L"s")) {
            return ManualFitMode::SaveOnly;
        }
        if (answer.empty() && console::InputEnded()) {
            // Input is gone; fall back to the fitted preview instead of
            // looping forever.
            return ManualFitMode::None;
        }
    }
}

struct PlaybackInput {
    bool quit = false;
    bool togglePause = false;
    int aspectSteps = 0;  // -1 = wider picture, +1 = taller picture
};

PlaybackInput PollPlaybackInput() {
    PlaybackInput input;
    while (console::KeyAvailable()) {
        const int key = console::ReadKey();
        if (key == 27 || key == L'q' || key == L'Q') {
            input.quit = true;
        } else if (key == L' ') {
            input.togglePause = true;
        } else if (key == L'-' || key == L'_' || key == L'[') {
            --input.aspectSteps;
        } else if (key == L'+' || key == L'=' || key == L']') {
            ++input.aspectSteps;
        }
    }
    return input;
}

std::string MakeStatusLine(double position, const VideoInfo& info,
                           bool paused) {
    const int width = console::Width();
    std::string status;
    if (width >= 78) {
        status = "  " + FormatTime(position);
        if (info.duration > 0.0) {
            status += " / " + FormatTime(info.duration);
        }
        status += Tr8("   [Space] Pause   [-/+] Ratio   [Q] Quit",
                      "   [空格] 暂停   [-/+] 比例   [Q] 退出");
        if (paused) {
            status += Tr8("   ** PAUSED **", "   ** 已暂停 **");
        }
    } else if (width >= 62) {
        status = "  " + FormatTime(position);
        if (info.duration > 0.0) {
            status += " / " + FormatTime(info.duration);
        }
        status += Tr8("   [Space] Pause   [Q] Quit",
                      "   [空格] 暂停   [Q] 退出");
        if (paused) {
            status += Tr8("   ** PAUSED **", "   ** 已暂停 **");
        }
    } else if (width >= 34) {
        status = "  " + FormatTime(position);
        if (info.duration > 0.0) {
            status += "/" + FormatTime(info.duration);
        }
        status += Tr8("  Space:Pause Q:Quit", "  空格:暂停 Q:退出");
        if (paused) {
            status += Tr8(" [PAUSED]", " [已暂停]");
        }
    } else {
        // Keep this short enough for a narrow console so it cannot wrap and
        // scroll the character art above it.
        status = "  " + FormatTime(position);
        if (info.duration > 0.0) {
            status += "/" + FormatTime(info.duration);
        }
        if (paused) {
            status += " P";
        }
    }
    return status;
}

void AppendInt(std::wstring& out, int value) {
    wchar_t digits[12];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value > 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

// Builds a full-screen terminal update as UTF-16. The buffer is owned by the
// caller and reused for every frame so the large allocations happen once.
void BuildVideoScreen(std::wstring& screen, const AsciiFrame& frame,
                      const std::vector<std::wstring>& glyphs, bool color,
                      int statusRow, const std::wstring& status,
                      bool compact, bool clearFirst = false) {
    screen.clear();
    // Synchronized output: the terminal buffers the whole frame and presents
    // it at once, so a frame split into several console writes cannot tear.
    screen += L"\x1b[?2026h";
    if (clearFirst) {
        // The grid changed size; drop rows left over from the previous grid
        // and any frame that briefly overflowed into the scrollback while the
        // terminal was being resized.
        screen += L"\x1b[2J\x1b[3J";
    }
    screen += L"\x1b[H";
    AppendFrameAnsiWide(screen, frame, glyphs, color);
    screen += L"\x1b[0m\x1b[K";
    if (compact) {
        screen += L"\x1b[?2026l";
        return;
    }
    screen += L"\x1b[";
    AppendInt(screen, statusRow);
    screen += L";1H\x1b[2K";
    if (color) {
        screen += L"\x1b[90m";
    }
    screen += status;
    if (color) {
        screen += L"\x1b[0m";
    }
    screen += L"\x1b[?2026l";
}

// Writes frames on a background thread so decoding and rendering of the next
// frame overlap with the terminal write, which dominates playback time for
// large character grids. At most one frame is queued; ordering is preserved.
class AsyncScreenWriter {
public:
    ~AsyncScreenWriter() { Stop(); }

    void Start() {
        if (thread_.joinable()) {
            return;
        }
        thread_ = std::thread([this] { Run(); });
    }

    // Returns as soon as the writer has taken the previous frame, not when it
    // has finished writing it. The caller's buffer is swapped rather than
    // copied, so a full frame is never duplicated; on return it holds the
    // previous (already written) frame and can be rebuilt in place.
    void Submit(std::wstring& screen) {
        if (!thread_.joinable()) {
            console::OutWide(screen);
            return;
        }
        std::unique_lock<std::mutex> lock(mutex_);
        ready_.wait(lock, [this] { return !hasWork_ || stopping_; });
        if (stopping_) {
            return;
        }
        buffer_.swap(screen);
        hasWork_ = true;
        work_.notify_one();
    }

    void Stop() {
        if (!thread_.joinable()) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        ready_.notify_all();
        work_.notify_all();
        thread_.join();
    }

private:
    void Run() {
        std::wstring frame;
        for (;;) {
            {
                std::unique_lock<std::mutex> lock(mutex_);
                work_.wait(lock, [this] { return hasWork_ || stopping_; });
                if (!hasWork_) {
                    return;  // stopping_ and nothing left to write
                }
                frame.swap(buffer_);
                hasWork_ = false;
                ready_.notify_all();
            }
            console::OutWide(frame);
        }
    }

    std::thread thread_;
    std::mutex mutex_;
    std::condition_variable ready_;
    std::condition_variable work_;
    std::wstring buffer_;
    bool hasWork_ = false;
    bool stopping_ = false;
};

// Raises the system timer resolution for the lifetime of the object so the
// pace loop can sleep in 1 ms steps instead of being rounded up to ~15.6 ms,
// which otherwise caps the achievable playback rate.
class TimerResolution {
public:
    TimerResolution() { timeBeginPeriod(1); }
    ~TimerResolution() { timeEndPeriod(1); }
    TimerResolution(const TimerResolution&) = delete;
    TimerResolution& operator=(const TimerResolution&) = delete;
};

struct CursorGuard {
    ~CursorGuard() { console::HideCursor(false); }
};

}  // namespace

AppOptions::AppOptions() {
    std::wstring error;
    LoadCharset(L"standard", render.charset, error);
    render.color = true;
}

void PrintCommandLineHelp() {
    console::Out(Tr8(
        "ASCIIArt 1.0 - image / video to ASCII art\n"
        "\n"
        "Usage:\n"
        "  ASCIIArt                              start the interactive menu\n"
        "  ASCIIArt <image or video>             convert / play directly\n"
        "  ASCIIArt <file> [options]             convert / play with options\n"
        "\n"
        "Options:\n"
        "  -w, --width <n>       target width in character columns; 0 or omitted = auto, default 100;\n"
        "                        the program tries to shrink the font / enlarge the window if too large\n"
        "      --height <n>      target height in character rows; omitted or 0 = derived from width\n"
        "  -r, --resolution <w>x<h>\n"
        "                        fixed output resolution (columns x rows), e.g. 120x40;\n"
        "                        also 120x0 (width only) or 0x40 (height only)\n"
        "  -c, --color           use 24-bit true color\n"
        "      --no-color        disable color\n"
        "  -i, --invert          invert brightness\n"
        "  -a, --aspect <x>      character aspect ratio, default 0.50 (terminal chars are about twice as tall as wide)\n"
        "  -g, --gamma <x>       gamma correction, default 1.0; >1 darker, <1 brighter\n"
        "  -f, --fps <n>         video playback frame-rate cap, default 24, range 1-120\n"
        "  -l, --loop            loop the video\n"
        "  -s, --save <file>     save image art (.txt or .html; .txt when no extension)\n"
        "      --no-display      save only, do not display image art\n"
        "      --fit             shrink font / enlarge window for large resolutions (default)\n"
        "      --no-fit          never change the font or window\n"
        "      --charset <chars> custom charset (dark to light), or a preset name\n"
        "      --preset <name>   charset preset: standard / detailed / blocks / minimal\n"
        "      --lang <en|zh>    interface language (default: system language, English fallback)\n"
        "  -h, --help            show this help\n"
        "\n"
        "Examples:\n"
        "  ASCIIArt\n"
        "  ASCIIArt photo.jpg -w 120 -c\n"
        "  ASCIIArt photo.png --resolution 120x50 --no-display --save art.txt\n"
        "  ASCIIArt photo.png --no-color --save art.txt\n"
        "  ASCIIArt logo.png --preset blocks --no-display --save logo.html\n"
        "  ASCIIArt clip.mp4 --resolution 100x30 -f 30\n"
        "\n"
        "Notes:\n"
        "  * Images are decoded by Windows WIC and support PNG / JPEG / BMP / GIF / TIFF and more.\n"
        "  * Videos are decoded by Windows Media Foundation; playback depends on the installed decoders.\n"
        "    MP4 / H.264 / AVI usually work. Audio tracks play through XAudio2.\n"
        "  * During video playback, Space pauses / resumes, -/+ adjusts the character\n"
        "    aspect ratio (useful after zooming the terminal), and Q or Esc quits.\n"
        "  * Image previews adapt to the current console size; --no-display is not limited by the window.\n"
        "  * With a fixed --resolution larger than the console, the program tries to temporarily\n"
        "    shrink the font and enlarge the window; if that fails it skips the preview or reports an error.\n"
        "  * Windows Terminal does not allow the program to change the font; it asks you to press Ctrl+-\n"
        "    or enlarge the window and then press Enter. Retrying is unlimited: press s to skip the\n"
        "    preview and save only, or q to cancel. Press Ctrl+0 to restore the font afterwards.\n"
        "  * When the art fills the whole window it enters full-screen mode without the header/status row;\n"
        "    press any key to return. Output dimensions are not capped; very large\n"
        "    sizes only show a warning when generating/saving.\n"
        "\n",
        "ASCIIArt 1.0 - 图片 / 视频转字符画\n"
        "\n"
        "用法:\n"
        "  ASCIIArt                              进入交互式菜单\n"
        "  ASCIIArt <图片或视频>                 直接转换 / 播放\n"
        "  ASCIIArt <文件> [选项]                指定参数转换 / 播放\n"
        "\n"
        "选项:\n"
        "  -w, --width <n>       目标宽度（字符列数），0 或省略为自动，默认 100；\n"
        "                        超出窗口时会尝试自动缩小字体/放大窗口\n"
        "      --height <n>      目标高度（字符行数），省略或 0 时按宽度自动计算\n"
        "  -r, --resolution <w>x<h>\n"
        "                        固定输出分辨率（列x行），例如 120x40；\n"
        "                        也可写 120x0 只固定宽度，或 0x40 只固定高度\n"
        "  -c, --color           使用 24 位真彩色\n"
        "      --no-color        不使用颜色\n"
        "  -i, --invert          反转亮度\n"
        "  -a, --aspect <x>      字符宽高比，默认 0.50（终端字符通常高约为宽的两倍）\n"
        "  -g, --gamma <x>       伽马校正，默认 1.0；大于 1 更暗，小于 1 更亮\n"
        "  -f, --fps <n>         视频播放帧率上限，默认 24，范围 1-120\n"
        "  -l, --loop            循环播放视频\n"
        "  -s, --save <文件>     保存图片字符画（.txt 或 .html；无扩展名时保存为 .txt）\n"
        "      --no-display      只保存，不显示图片字符画\n"
        "      --fit             大分辨率时自动缩小字体/放大窗口（默认开启）\n"
        "      --no-fit          禁止自动调整字体和窗口\n"
        "      --charset <字符>  自定义字符集（从暗到亮），也可填预设名\n"
        "      --preset <名称>   字符集预设：standard / detailed / blocks / minimal\n"
        "      --lang <en|zh>    界面语言（默认跟随系统，非中/英时回退 en）\n"
        "  -h, --help            显示此帮助\n"
        "\n"
        "示例:\n"
        "  ASCIIArt\n"
        "  ASCIIArt photo.jpg -w 120 -c\n"
        "  ASCIIArt photo.png --resolution 120x50 --no-display --save art.txt\n"
        "  ASCIIArt photo.png --no-color --save art.txt\n"
        "  ASCIIArt logo.png --preset blocks --no-display --save logo.html\n"
        "  ASCIIArt clip.mp4 --resolution 100x30 -f 30\n"
        "\n"
        "说明:\n"
        "  * 图片由 Windows WIC 解码，支持 PNG / JPEG / BMP / GIF / TIFF 等常用格式。\n"
        "  * 视频由 Windows Media Foundation 解码，能否播放取决于系统已安装的解码器；\n"
        "    MP4 / H.264 / AVI 通常可以直接使用。视频自带的音频会通过 XAudio2 播放。\n"
        "  * 播放视频时按空格暂停 / 继续，按 -/+ 调整字符宽高比（终端缩放后很有用），\n"
        "    按 Q 或 Esc 退出。\n"
        "  * 图片预览会自动适配当前控制台大小；--no-display 保存时不受窗口大小限制。\n"
        "  * 使用 --resolution 固定分辨率且大于控制台窗口时，程序会尝试临时\n"
        "    缩小字体、放大窗口；无法调整时会跳过预览或提示错误。\n"
        "  * Windows Terminal 不允许程序修改字体；程序会提示你按 Ctrl+- 缩小\n"
        "    字体或调大窗口后按 Enter 重试（重试次数不限）；输入 s 可跳过预览仅保存，\n"
        "    输入 q 取消。预览结束后可按 Ctrl+0 恢复字体。\n"
        "  * 字符画占满整个窗口时会进入全屏模式，不显示信息头/状态行；\n"
        "    图片按任意键返回。输出尺寸不设上限，仅在生成/保存超大尺寸时提示可能耗时较久。\n"
        "\n"));
}

App::App(AppOptions options) : options_(std::move(options)) {}

int App::RunFile(const std::wstring& path) {
    return ProcessFile(path, false);
}

void App::RunInteractive() {
    console::Clear();
    for (;;) {
        DrawMainMenu();
        const std::wstring choice = console::ReadLine(
            Tr8("  Choose [0-5]: ", "  请选择 [0-5]: "));
        if (choice.empty()) {
            if (console::InputEnded()) {
                break;
            }
            continue;
        }
        if (choice == L"0" || util::EqualsIgnoreCase(choice, L"q") ||
            util::EqualsIgnoreCase(choice, L"exit")) {
            break;
        }
        if (choice == L"1" || choice == L"2") {
            std::wstring path;
            if (!PickMediaFile(path)) {
                continue;
            }
            ProcessFile(path, true);
            continue;
        }
        if (choice == L"3") {
            ShowSettings();
            continue;
        }
        if (choice == L"4") {
            ShowHelp();
            continue;
        }
        if (choice == L"5") {
            SetLanguage(CurrentLanguage() == Language::Chinese
                            ? Language::English
                            : Language::Chinese);
            continue;
        }
    }
    console::Clear();
    console::Out(Tr8("  Thanks for using ASCIIArt.\n\n",
                     "  感谢使用 ASCIIArt。\n\n"));
}

App::MediaKind App::DetectKind(const std::wstring& path) const {
    const std::wstring ext = util::ExtensionOf(path);
    if (IsImageExtension(ext)) {
        return MediaKind::Image;
    }
    if (IsVideoExtension(ext)) {
        return MediaKind::Video;
    }
    return MediaKind::Unknown;
}

int App::ProcessFile(const std::wstring& path, bool interactive) {
    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES ||
        (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        ReportError(std::wstring(Tr(L"Cannot find the file, or the path is "
                                     L"not a file:\n  ",
                                     L"找不到文件或路径不是文件：\n  ")) +
                        path,
                    interactive);
        return 1;
    }

    const MediaKind kind = DetectKind(path);
    if (kind == MediaKind::Image) {
        return ProcessImage(path, interactive) ? 0 : 1;
    }
    if (kind == MediaKind::Video) {
        return ProcessVideo(path, interactive) ? 0 : 1;
    }

    ReportError(
        Tr(L"Cannot determine the file type from its extension.\n"
           L"Use a common image extension (.png/.jpg/.bmp/.gif etc.) or video "
           L"extension (.mp4/.avi/.mov/.wmv etc.).",
           L"无法根据扩展名判断文件类型。\n"
           L"请使用常见的图片扩展名（.png/.jpg/.bmp/.gif 等）或视频扩展名"
           L"（.mp4/.avi/.mov/.wmv 等）。"),
        interactive);
    return 1;
}

bool App::ProcessImage(const std::wstring& path, bool interactive) {
    RgbaImage image;
    std::wstring error;
    if (!LoadImageFile(path, image, error)) {
        ReportError(error, interactive);
        return false;
    }

    bool display = !options_.noDisplay;
    if (!display && options_.savePath.empty()) {
        ReportError(
            Tr(L"--no-display was given but no output file was specified "
               L"with --save.",
               L"指定了 --no-display，但没有指定 --save 输出文件。"),
            interactive);
        return false;
    }

    int maxCols = std::numeric_limits<int>::max();
    int maxRows = std::numeric_limits<int>::max();
    if (display && console::IsConsole()) {
        maxCols = std::max(10, console::Width());
        maxRows = std::max(4, console::Height() - 6);
    }

    const bool exactSize =
        options_.render.width > 0 && options_.render.height > 0;
    const bool explicitSize =
        options_.render.width > 0 || options_.render.height > 0;
    const GridSize desired =
        DesiredGrid(image.width, image.height, options_.render.width,
                    options_.render.height, options_.render.aspect);
    console::ConsoleFit fit;
    std::wstring fitError;
    bool fitTried = false;
    bool compact = false;

    if (display && console::IsConsole() && explicitSize &&
        options_.fitConsole &&
        (desired.cols > maxCols || desired.rows > maxRows)) {
        fitTried = true;
        bool fitCompact = false;
        if (fit.FitForGrid(desired.cols, desired.rows, 6, true, fitCompact,
                           fitError)) {
            compact = fitCompact;
            maxCols = std::max(10, console::Width());
            maxRows = std::max(4, console::Height() - (compact ? 0 : 6));
        }
    }

    GridSize grid = FitGrid(
        image.width, image.height, options_.render.width,
        options_.render.height, maxCols, maxRows, options_.render.aspect);
    bool manualFit = false;

    if (display && console::IsConsole() && explicitSize &&
        options_.fitConsole &&
        (desired.cols > maxCols || desired.rows > maxRows)) {
        const bool canSaveOnly = interactive || !options_.savePath.empty();
        const ManualFitMode mode = TryManualConsoleFit(
            desired.cols, desired.rows, 6, canSaveOnly);
        if (mode == ManualFitMode::Cancel) {
            console::Info(
                Tr8("  Conversion cancelled.\n", "  已取消本次转换。\n"));
            return false;
        }
        if (mode == ManualFitMode::SaveOnly) {
            // Skip the preview but keep the requested target size instead of
            // the window-sized fallback.
            display = false;
            grid = desired;
            console::Info(Tr8(
                "  Preview skipped; rendering at the target size.\n",
                "  已跳过预览，将按目标尺寸生成。\n"));
        } else if (mode != ManualFitMode::None) {
            compact = mode == ManualFitMode::Compact;
            maxCols = std::max(10, console::Width());
            maxRows = std::max(4, console::Height() - (compact ? 0 : 6));
            grid = FitGrid(image.width, image.height, options_.render.width,
                           options_.render.height, maxCols, maxRows,
                           options_.render.aspect);
            manualFit = true;
        }
    }

    if (display && console::IsConsole() && exactSize &&
        (grid.cols > maxCols || grid.rows > maxRows)) {
        if (!options_.savePath.empty()) {
            display = false;
            console::Info(Tr8(
                "  Note: the window cannot hold the requested resolution; "
                "skipping the preview and saving the file only.\n",
                "  提示: 当前窗口无法容纳指定分辨率，已跳过预览，只保存文件。\n"));
        } else if (interactive) {
            display = false;
            console::Info(Tr8(
                "  Note: the window cannot hold the requested resolution; "
                "skipping the preview.\n"
                "  You can save the result to a file when it is ready.\n",
                "  提示: 当前窗口无法容纳指定分辨率，已跳过预览。\n"
                "  转换完成后可以保存为文件。\n"));
        } else {
            if (fitTried && !fitError.empty()) {
                ReportError(fitError, interactive);
            } else {
                ReportError(
                    std::wstring(Tr(L"The requested resolution ", L"指定的分辨率 ")) +
                        std::to_wstring(grid.cols) + L"x" +
                        std::to_wstring(grid.rows) +
                        Tr(L" exceeds the available console area ",
                           L" 超出当前控制台可用区域 ") +
                        std::to_wstring(maxCols) + L"x" +
                        std::to_wstring(maxRows) +
                        Tr(L".\nEnlarge the console window, lower the "
                           L"resolution, or use --no-display --save to save "
                           L"the file only.",
                           L"。\n请调大控制台窗口、缩小分辨率，或使用 "
                           L"--no-display --save 只保存文件。"),
                    interactive);
            }
            return false;
        }
    }

    const std::int64_t cellCount =
        static_cast<std::int64_t>(grid.cols) * grid.rows;
    if ((interactive || !options_.savePath.empty()) &&
        cellCount >= kLargeOutputCells) {
        console::Info(
            Tr8("  Warning: the output is ", "  警告: 输出尺寸为 ") +
            std::to_string(grid.cols) + "x" + std::to_string(grid.rows) +
            Tr8(" (", "（") + std::to_string(cellCount) +
            Tr8(" characters). Generating and saving it may take a long time "
                "and a lot of memory.\n",
                " 个字符），生成和保存可能耗时较久并占用大量内存。\n"));
    }

    const AsciiFrame frame =
        RenderAscii(image, options_.render.charset, grid, options_.render);

    const bool color = options_.render.color && console::SupportsAnsi();
    if (display) {
        if (console::IsConsole()) {
            // Keep the window at the size the grid was computed for; it is
            // restored when the preview ends.
            console::LockWindowResize lockResize;
            console::Clear();
            if (compact) {
                console::SetTitle(
                    L"ASCIIArt - " + util::FileNameOf(path) + L" - " +
                    std::to_wstring(frame.cols) + L"x" +
                    std::to_wstring(frame.rows) +
                    Tr(L" - press any key to return",
                       L" - 按任意键返回"));
                {
                    console::NoWrap noWrap;
                    std::string art =
                        FrameToAnsi(frame, options_.render.charset, color);
                    while (!art.empty() && art.back() == '\n') {
                        art.pop_back();
                    }
                    console::Out(art);
                }
                if (console::IsInputConsole()) {
                    console::ReadKey();
                } else {
                    Sleep(1200);
                }
                console::Clear();
                console::SetTitle(L"ASCIIArt");
                if (fit.Changed()) {
                    fit.Restore();
                }
                if (manualFit) {
                    console::Info(Tr8(
                        "  If you are using Windows Terminal, press Ctrl+0 "
                        "to restore the font.\n",
                        "  如使用 Windows Terminal，可按 Ctrl+0 恢复字体。\n"));
                    manualFit = false;
                }
            } else {
                console::Out("\n  ASCIIArt\n");
                console::Out(Tr8("  File: ", "  文件: ") +
                             ToUtf8(util::FileNameOf(path)) + "\n");
                std::string gridInfo = std::to_string(frame.cols) + " x " +
                                       std::to_string(frame.rows);
                if (explicitSize &&
                    (frame.cols != desired.cols ||
                     frame.rows != desired.rows)) {
                    gridInfo +=
                        Tr8(" (target ", "（目标 ") +
                        std::to_string(desired.cols) + "x" +
                        std::to_string(desired.rows) +
                        Tr8(", fitted to window)", "，已适配窗口）");
                }
                console::Out(
                    Tr8("  Grid: ", "  网格: ") + gridInfo +
                    Tr8("  |  charset: ", "  |  字符集: ") +
                    ToUtf8(options_.render.charset.DisplayName()) +
                    Tr8("  |  ", "  |  ") +
                    Tr8(color ? "color" : "mono",
                        color ? "彩色" : "黑白") +
                    "\n\n");
                {
                    console::NoWrap noWrap;
                    console::Out(
                        FrameToAnsi(frame, options_.render.charset, color));
                }
                console::Out("\n");
            }
        } else {
            console::Out(FrameToText(frame, options_.render.charset));
        }
    }

    if (!options_.savePath.empty()) {
        std::wstring actualPath = options_.savePath;
        if (util::ExtensionOf(actualPath).empty()) {
            actualPath += L".txt";
        }
        if (!SaveAsciiFile(options_.savePath, frame, options_.render.charset,
                           options_.render.color, util::FileNameOf(path),
                           error)) {
            ReportError(error, interactive);
            return false;
        }
        console::Info(Tr8("  Saved: ", "  已保存: ") +
                      ToUtf8(actualPath) + "\n");
    }

    if (interactive) {
        if (options_.savePath.empty()) {
            PromptSaveImage(frame, options_.render.charset, path);
        }
        if (fit.Changed()) {
            console::Pause(Tr8(
                "\n  Press Enter to return to the main menu (the console "
                "font and window will be restored)...",
                "\n  按 Enter 返回主菜单（将恢复控制台字体和窗口）..."));
        } else if (manualFit) {
            console::Pause(Tr8(
                "\n  Press Enter to return to the main menu (press Ctrl+0 "
                "to restore the font if you are using Windows Terminal)...",
                "\n  按 Enter 返回主菜单（如使用 Windows Terminal，"
                "可按 Ctrl+0 恢复字体）..."));
        } else {
            console::Pause(
                Tr8("\n  Press Enter to return to the main menu...",
                    "\n  按 Enter 返回主菜单..."));
        }
    } else if (fit.Changed()) {
        console::Pause(Tr8(
            "\n  Press Enter to restore the console font and window...",
            "\n  按 Enter 恢复控制台字体和窗口..."));
    } else if (manualFit) {
        console::Pause(Tr8(
            "\n  If you are using Windows Terminal, press Ctrl+0 to restore "
            "the font.\n  Press Enter to exit...",
            "\n  如使用 Windows Terminal，可按 Ctrl+0 恢复字体。"
            "\n  按 Enter 退出..."));
    }

    return true;
}

bool App::ProcessVideo(const std::wstring& path, bool interactive) {
    if (!console::IsConsole()) {
        ReportError(
            Tr(L"Video playback must run directly in a console window; "
               L"output cannot be redirected to a file.",
               L"视频播放需要直接在控制台窗口中运行，不能把输出重定向到文件。"),
            false);
        return false;
    }
    if (!console::SupportsAnsi()) {
        ReportError(
            Tr(L"The current console does not support ANSI escape "
               L"sequences, so video cannot be played.\n"
               L"Use Windows Terminal or the default console on Windows "
               L"10/11.",
               L"当前控制台不支持 ANSI 转义序列，无法播放视频。\n"
               L"请使用 Windows 10/11 的 Windows Terminal 或默认控制台。"),
            interactive);
        return false;
    }

    if (console::Width() < 20 || console::Height() < 8) {
        ReportError(Tr(L"The console window is too small; enlarge it before "
                       L"playing a video.",
                       L"控制台窗口太小，请调大窗口后再播放视频。"),
                    interactive);
        return false;
    }

    VideoPlayer player;
    std::wstring error;
    if (!player.Open(path, error)) {
        ReportError(error, interactive);
        return false;
    }

    AudioPlayer audio;
    std::wstring audioError;
    bool hasAudio = audio.Open(path, audioError);
    bool audioStarted = false;

    const VideoInfo info = player.Info();
    int maxCols = std::max(10, console::Width());
    int maxRows = std::max(5, console::Height() - 3);
    const bool exactSize =
        options_.render.width > 0 && options_.render.height > 0;
    const bool explicitSize =
        options_.render.width > 0 || options_.render.height > 0;
    const GridSize desired =
        DesiredGrid(info.width, info.height, options_.render.width,
                    options_.render.height, options_.render.aspect);
    console::ConsoleFit fit;
    std::wstring fitError;
    bool compact = false;

    if (explicitSize && options_.fitConsole &&
        (desired.cols > maxCols || desired.rows > maxRows)) {
        bool fitCompact = false;
        if (fit.FitForGrid(desired.cols, desired.rows, 3, true, fitCompact,
                           fitError)) {
            compact = fitCompact;
            maxCols = std::max(10, console::Width());
            maxRows = std::max(5, console::Height() - (compact ? 0 : 3));
        }
    }

    GridSize grid = FitGrid(
        info.width, info.height, options_.render.width,
        options_.render.height, maxCols, maxRows, options_.render.aspect);
    bool manualFit = false;

    if (explicitSize && options_.fitConsole &&
        (desired.cols > maxCols || desired.rows > maxRows)) {
        const ManualFitMode mode =
            TryManualConsoleFit(desired.cols, desired.rows, 3, false);
        if (mode == ManualFitMode::Cancel) {
            console::Info(
                Tr8("  Playback cancelled.\n", "  已取消播放。\n"));
            return false;
        }
        if (mode != ManualFitMode::None) {
            compact = mode == ManualFitMode::Compact;
            maxCols = std::max(10, console::Width());
            maxRows = std::max(5, console::Height() - (compact ? 0 : 3));
            grid = FitGrid(info.width, info.height, options_.render.width,
                           options_.render.height, maxCols, maxRows,
                           options_.render.aspect);
            manualFit = true;
        }
    }

    if (exactSize && (grid.cols > maxCols || grid.rows > maxRows)) {
        if (!fitError.empty()) {
            ReportError(fitError, interactive);
        } else {
            ReportError(
                std::wstring(Tr(L"The requested resolution ",
                                L"指定的分辨率 ")) +
                    std::to_wstring(grid.cols) + L"x" +
                    std::to_wstring(grid.rows) +
                    Tr(L" exceeds the available console area ",
                       L" 超出当前控制台可用区域 ") +
                    std::to_wstring(maxCols) + L"x" +
                    std::to_wstring(maxRows) +
                    Tr(L".\nLower the resolution, or enlarge the console "
                       L"window and try again.",
                       L"。\n请调小分辨率，或调大控制台窗口后重试。"),
                interactive);
        }
        return false;
    }

    const bool color = options_.render.color;
    int statusRow = grid.rows + 2;
    const int targetFps =
        std::max(1, std::min(120, options_.render.fps));
    const double frameInterval = 1.0 / static_cast<double>(targetFps);

    // Decode/scale to 2x the character grid. Media Foundation can scale before
    // the frame is copied to the CPU, which is the main win for
    // high-resolution video; software downsampling is the fallback.
    player.SetTargetSize(std::max(1, grid.cols * 2),
                         std::max(1, grid.rows * 2));

    const long long cellCount =
        static_cast<long long>(grid.cols) * grid.rows;
    if (cellCount > 1000000 && console::IsInputConsole()) {
        console::Info(Tr8("  Note: the current art has ",
                          "  提示: 当前字符画有 ") +
                      std::to_string(cellCount) +
                      Tr8(" characters; video playback may be very slow.\n",
                          " 个字符，视频播放可能非常慢。\n"));
        const std::wstring answer = console::ReadLine(
            Tr8("  Press Enter to continue, or type q to cancel: ",
                "  按 Enter 继续，输入 q 取消: "));
        if (util::EqualsIgnoreCase(answer, L"q")) {
            return false;
        }
    }

    // Keep the window at the size the grid was computed for during playback.
    console::LockWindowResize lockResize;
    console::Clear();
    console::HideCursor(true);
    console::NoWrap noWrap;
    CursorGuard cursorGuard;
    console::SetTitle(
        compact ? L"ASCIIArt - " + util::FileNameOf(path) +
                      Tr(L" - Space:Pause  Q:Quit",
                         L" - 空格暂停  Q退出")
                : L"ASCIIArt - " + util::FileNameOf(path));

    const std::vector<std::wstring> wideGlyphs =
        WideGlyphs(options_.render.charset);
    std::wstring screen;
    screen.reserve(static_cast<std::size_t>(grid.cols) * grid.rows * 16 +
                   256);
    TimerResolution timerResolution;
    AsyncScreenWriter writer;
    writer.Start();

    bool quit = false;
    bool paused = false;
    bool completed = false;
    double basePts = -1.0;
    double nextRender = 0.0;
    Clock::time_point start = Clock::now();
    Clock::time_point pausedAt = Clock::now();

    // Reused across frames: both buffers keep their capacity, so playback does
    // not allocate a new frame image / cell grid for every decoded frame.
    RgbaImage videoFrame;
    AsciiFrame ascii;

    // Track the character grid so terminal zoom (Ctrl+wheel) can be handled.
    int lastConsoleCols = console::Width();
    int lastConsoleRows = console::Height();

    // Re-renders the last decoded frame, used after a live aspect change.
    auto repaint = [&](double position, bool pausedNow, bool clearFirst) {
        RenderAsciiInto(videoFrame, options_.render.charset, grid,
                        options_.render, ascii);
        BuildVideoScreen(screen, ascii, wideGlyphs, color, statusRow,
                         util::ToWide(MakeStatusLine(position, info, pausedNow)),
                         compact, clearFirst);
        writer.Submit(screen);
    };

    // Terminal zoom (Ctrl+wheel) changes the real cell shape, which a fixed
    // aspect ratio cannot track; the user can correct it while playing.
    auto applyAspectSteps = [&](int steps) {
        double next = options_.render.aspect + steps * 0.02;
        next = std::max(0.20, std::min(1.00, next));
        if (next == options_.render.aspect) {
            return false;
        }
        options_.render.aspect = next;
        const GridSize updated =
            FitGrid(info.width, info.height, options_.render.width,
                    options_.render.height, maxCols, maxRows,
                    options_.render.aspect);
        if (updated.cols == grid.cols && updated.rows == grid.rows) {
            return false;
        }
        grid = updated;
        statusRow = grid.rows + 2;
        return true;
    };

    while (!quit) {
        double pts = 0.0;
        bool eof = false;
        if (!player.ReadFrame(pts, eof, error)) {
            audio.Stop();
            writer.Stop();
            console::HideCursor(false);
            console::Clear();
            ReportError(error, interactive);
            return false;
        }

        if (eof) {
            if (options_.render.loop) {
                if (!player.Rewind(error)) {
                    audio.Stop();
                    writer.Stop();
                    console::HideCursor(false);
                    console::Clear();
                    ReportError(error, interactive);
                    return false;
                }
                audio.Stop();
                audioStarted = false;
                basePts = -1.0;
                nextRender = 0.0;
                start = Clock::now();
                continue;
            }
            completed = true;
            break;
        }

        if (basePts < 0.0) {
            basePts = pts;
            start = Clock::now();
        }
        const double relative = std::max(0.0, pts - basePts);

        // Terminal zoom changes how many characters fit on screen. Re-fit the
        // grid so the art keeps fitting instead of scrolling the whole screen.
        bool gridRefitted = false;
        {
            const int cols = console::Width();
            const int rows = console::Height();
            if (cols != lastConsoleCols || rows != lastConsoleRows) {
                lastConsoleCols = cols;
                lastConsoleRows = rows;
                if (cols >= 20 && rows >= 8) {
                    maxCols = std::max(10, cols);
                    maxRows = std::max(5, rows - (compact ? 0 : 3));
                    const GridSize updated =
                        FitGrid(info.width, info.height,
                                options_.render.width, options_.render.height,
                                maxCols, maxRows, options_.render.aspect);
                    if (updated.cols != grid.cols ||
                        updated.rows != grid.rows) {
                        grid = updated;
                        statusRow = grid.rows + 2;
                        gridRefitted = true;
                    }
                }
            }
        }

        // Skip frames when the source is faster than the requested rate.
        // nextRender is advanced by a fixed interval, so 29.97/60 fps sources
        // are decimated to roughly the requested rate instead of sneaking
        // through one extra frame each time.
        if (relative + 0.0005 < nextRender) {
            player.DiscardFrame();
            const PlaybackInput input = PollPlaybackInput();
            if (input.quit) {
                quit = true;
            }
            if (input.aspectSteps != 0 &&
                applyAspectSteps(input.aspectSteps)) {
                repaint(relative, paused, true);
            } else if (gridRefitted) {
                repaint(relative, paused, true);
            }
            continue;
        }

        // Convert only the frames that are actually shown; frame skipping
        // above never pays for the NV12/RGB to RGBA conversion.
        if (!player.DecodeFrame(videoFrame, error)) {
            audio.Stop();
            writer.Stop();
            console::HideCursor(false);
            console::Clear();
            ReportError(error, interactive);
            return false;
        }

        const Clock::time_point target =
            start + std::chrono::duration_cast<Clock::duration>(
                        std::chrono::duration<double>(relative));
        while (Clock::now() < target) {
            const auto remaining = target - Clock::now();
            if (remaining > std::chrono::milliseconds(2)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                std::this_thread::yield();
            }
        }
        if (quit) {
            break;
        }

        repaint(relative, paused, gridRefitted);
        if (hasAudio && !audioStarted) {
            std::wstring startError;
            if (audio.Start(startError)) {
                audioStarted = true;
            } else {
                hasAudio = false;
                audioError = startError.empty()
                                 ? std::wstring(Tr(L"Cannot start audio "
                                                   L"playback.",
                                                   L"无法启动音频播放。"))
                                 : startError;
            }
        }
        nextRender += frameInterval;
        if (nextRender <= relative) {
            nextRender = relative + frameInterval;
        }

        const PlaybackInput input = PollPlaybackInput();
        if (input.quit) {
            quit = true;
            break;
        }
        if (input.aspectSteps != 0 &&
            applyAspectSteps(input.aspectSteps)) {
            repaint(relative, paused, true);
        }
        if (input.togglePause) {
            paused = true;
            pausedAt = Clock::now();
            audio.Pause();
            repaint(relative, true, false);
        }

        while (paused && !quit) {
            const PlaybackInput pauseInput = PollPlaybackInput();
            if (pauseInput.quit) {
                quit = true;
                break;
            }
            if (pauseInput.aspectSteps != 0 &&
                applyAspectSteps(pauseInput.aspectSteps)) {
                repaint(relative, true, true);
            }
            if (pauseInput.togglePause) {
                paused = false;
                start += Clock::now() - pausedAt;
                audio.Resume();
            }
            if (paused) {
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }
        }
    }

    audio.Stop();
    writer.Stop();  // writes any queued frame before the screen is cleared
    console::HideCursor(false);
    if (completed && !quit) {
        std::this_thread::sleep_for(std::chrono::milliseconds(350));
    }
    console::Clear();
    lockResize.Restore();
    fit.Restore();
    console::SetTitle(L"ASCIIArt");

    if (quit) {
        console::Out(Tr8("  Playback stopped.\n", "  已停止播放。\n"));
    } else {
        console::Out(Tr8("  Playback finished: ", "  播放结束: ") +
                     ToUtf8(util::FileNameOf(path)) + "\n");
    }
    if (info.duration > 0.0) {
        console::Out(Tr8("  Duration: ", "  视频时长: ") +
                     FormatTime(info.duration) + "\n");
    }
    if (!audioError.empty()) {
        console::Out(Tr8("  Audio: not played (", "  音频: 未播放（") +
                     ToUtf8(audioError) + ")\n");
    }
    if (!options_.savePath.empty()) {
        console::Out(Tr8(
            "  Note: saving video as character art is not supported yet.\n",
            "  提示: 视频暂不支持保存为字符画文件。\n"));
    }
    if (explicitSize &&
        (grid.cols != desired.cols || grid.rows != desired.rows)) {
        console::Out(Tr8("  Note: target ", "  提示: 目标 ") +
                     std::to_string(desired.cols) + "x" +
                     std::to_string(desired.rows) +
                     Tr8(" was fitted to ", " 已适配为 ") +
                     std::to_string(grid.cols) + "x" +
                     std::to_string(grid.rows) + "。\n");
    }
    if (manualFit) {
        console::Out(Tr8(
            "  Note: if you are using Windows Terminal, press Ctrl+0 to "
            "restore the font.\n",
            "  提示: 如使用 Windows Terminal，可按 Ctrl+0 恢复字体。\n"));
    }
    console::Out("\n");

    if (interactive) {
        console::Pause(manualFit
                           ? Tr8("  Press Enter to return to the main menu "
                                 "(press Ctrl+0 to restore the font if you "
                                 "scaled it)...",
                                 "  按 Enter 返回主菜单（如字体已缩放，"
                                 "可按 Ctrl+0 恢复）...")
                           : Tr8("  Press Enter to return to the main "
                                 "menu...",
                                 "  按 Enter 返回主菜单..."));
    }
    return true;
}

void App::DrawMainMenu() const {
    const std::string size =
        SizeText(options_.render.width, options_.render.height);
    const std::string color = Tr8("on", "开");
    const std::string invert = options_.render.invert ? Tr8("on", "开")
                                                      : Tr8("off", "关");

    console::Clear();
    console::Out(Tr8(
        "\n"
        "  ==================================================\n"
        "       ASCIIArt  -  image / video  ->  ASCII art\n"
        "  ==================================================\n"
        "\n",
        "\n"
        "  ==================================================\n"
        "       ASCIIArt  -  图片 / 视频  ->  字符画\n"
        "  ==================================================\n"
        "\n"));
    console::Out(Tr8("  Current settings: size ", "  当前设置: 尺寸 ") +
                 size + Tr8("  |  color ", "  |  彩色 ") +
                 (options_.render.color ? color : Tr8("off", "关")) +
                 Tr8("  |  invert ", "  |  反色 ") + invert +
                 Tr8("  |  charset ", "  |  字符集 ") +
                 ToUtf8(options_.render.charset.DisplayName()) + "\n");
    console::Out(Tr8("            video fps ", "            视频帧率 ") +
                 std::to_string(options_.render.fps) +
                 Tr8("  |  aspect ", "  |  宽高比 ") +
                 util::Number(options_.render.aspect, 2) + "\n\n");
    console::Out(Tr8(
        "  1. Choose an image and convert\n"
        "  2. Choose a video and play\n"
        "  3. Conversion settings\n"
        "  4. Help\n"
        "  5. Switch language (中文)\n"
        "  0. Exit\n"
        "\n",
        "  1. 选择图片并转换\n"
        "  2. 选择视频并播放\n"
        "  3. 修改转换设置\n"
        "  4. 使用说明\n"
        "  5. 切换语言 (English)\n"
        "  0. 退出\n"
        "\n"));
}

void App::ShowSettings() {
    for (;;) {
        const std::string size =
            SizeText(options_.render.width, options_.render.height);
        console::Clear();
        console::Out(Tr8(
            "\n"
            "  ================ Conversion settings ================\n\n",
            "\n"
            "  ================ 转换设置 ================\n\n"));
        console::Out(Tr8("  1. Output size    : ", "  1. 输出尺寸      : ") +
                     size + "\n");
        console::Out(
            Tr8("  2. Color output   : ", "  2. 彩色输出      : ") +
            Tr8(options_.render.color ? "on" : "off",
                options_.render.color ? "开" : "关") +
            "\n");
        console::Out(
            Tr8("  3. Invert         : ", "  3. 亮度反转      : ") +
            Tr8(options_.render.invert ? "on" : "off",
                options_.render.invert ? "开" : "关") +
            "\n");
        console::Out(Tr8("  4. Charset        : ", "  4. 字符集        : ") +
                     ToUtf8(options_.render.charset.DisplayName()) + "\n");
        console::Out(Tr8("  5. Aspect ratio   : ", "  5. 字符宽高比    : ") +
                     util::Number(options_.render.aspect, 2) + "\n");
        console::Out(Tr8("  6. Video fps cap  : ", "  6. 视频帧率上限  : ") +
                     std::to_string(options_.render.fps) + "\n");
        console::Out(
            Tr8("  7. Large-resolution fit: ", "  7. 大分辨率适配  : ") +
            Tr8(options_.fitConsole ? "on" : "off",
                options_.fitConsole ? "开" : "关") +
            "\n");
        console::Out(Tr8(
            "\n"
            "  0. Back to main menu\n\n",
            "\n"
            "  0. 返回主菜单\n\n"));

        const std::wstring choice = console::ReadLine(
            Tr8("  Choose [0-7]: ", "  请选择 [0-7]: "));
        if (choice == L"0" || choice.empty()) {
            return;
        }

        if (choice == L"1") {
            const std::wstring input = console::ReadLine(Tr8(
                "  Enter WxH (e.g. 120x40; 0 = auto; 120x0 = width only): ",
                "  输入宽x高（如 120x40；0 = 自动；120x0 = 只固定宽度）: "));
            if (input.empty()) {
                continue;
            }
            int width = 0;
            int height = 0;
            if (ParseSize(input, width, height)) {
                options_.render.width = width;
                options_.render.height = height;
            } else {
                console::Info(Tr8(
                    "  Invalid input; the size is unchanged. Examples: 120x40, "
                    "120x0, 0.\n",
                    "  输入无效，尺寸保持不变。格式示例：120x40、120x0、0。\n"));
                console::Pause();
            }
            continue;
        }
        if (choice == L"2") {
            options_.render.color = !options_.render.color;
            continue;
        }
        if (choice == L"3") {
            options_.render.invert = !options_.render.invert;
            continue;
        }
        if (choice == L"4") {
            ShowCharsetMenu();
            continue;
        }
        if (choice == L"5") {
            const std::wstring input = console::ReadLine(Tr8(
                "  Character aspect ratio (0.20-1.00, default 0.50): ",
                "  字符宽高比（0.20-1.00，默认 0.50）: "));
            double value = 0.0;
            if (ParseDouble(input, value) && value >= 0.2 && value <= 1.0) {
                options_.render.aspect = value;
            } else {
                console::Info(Tr8(
                    "  Invalid input; the aspect ratio is unchanged.\n",
                    "  输入无效，宽高比保持不变。\n"));
                console::Pause();
            }
            continue;
        }
        if (choice == L"6") {
            const std::wstring input = console::ReadLine(Tr8(
                "  Video frame-rate cap (1-120, default 24): ",
                "  视频帧率上限（1-120，默认 24）: "));
            int value = 0;
            if (ParseInt(input, value) && value >= 1 && value <= 120) {
                options_.render.fps = value;
            } else {
                console::Info(Tr8("  Invalid input; the frame rate is "
                                  "unchanged.\n",
                                  "  输入无效，帧率保持不变。\n"));
                console::Pause();
            }
            continue;
        }
        if (choice == L"7") {
            options_.fitConsole = !options_.fitConsole;
            continue;
        }
    }
}

void App::ShowCharsetMenu() {
    for (;;) {
        console::Clear();
        console::Out(Tr8(
            "\n"
            "  ================ Charset ================\n\n",
            "\n"
            "  ================ 字符集 ================\n\n"));
        const auto& presets = CharsetPresets();
        const bool chinese = CurrentLanguage() == Language::Chinese;
        for (std::size_t i = 0; i < presets.size(); ++i) {
            console::Out(
                "  " + std::to_string(i + 1) + ". " +
                ToUtf8(chinese ? presets[i].labelZh : presets[i].labelEn) +
                "\n");
        }
        console::Out(Tr8("  5. Custom charset\n", "  5. 自定义字符集\n"));
        console::Out(Tr8("  0. Back\n\n", "  0. 返回\n\n"));
        console::Out(Tr8("  Current: ", "  当前: ") +
                     ToUtf8(options_.render.charset.DisplayName()) +
                     "\n\n");

        const std::wstring choice = console::ReadLine(
            Tr8("  Choose [0-5]: ", "  请选择 [0-5]: "));
        if (choice == L"0" || choice.empty()) {
            return;
        }

        std::wstring spec;
        if (choice == L"1") {
            spec = L"standard";
        } else if (choice == L"2") {
            spec = L"detailed";
        } else if (choice == L"3") {
            spec = L"blocks";
        } else if (choice == L"4") {
            spec = L"minimal";
        } else if (choice == L"5") {
            spec = console::ReadLineRaw(Tr8(
                "  Enter characters (dark to light, 4-70 recommended; spaces "
                "allowed): ",
                "  输入字符（从暗到亮，建议 4-70 个；可用空格）: "));
            if (spec.empty()) {
                continue;
            }
        } else {
            continue;
        }

        Charset loaded;
        std::wstring error;
        if (LoadCharset(spec, loaded, error)) {
            options_.render.charset = std::move(loaded);
        } else {
            ReportError(error, false);
            console::Pause();
        }
        return;
    }
}

void App::ShowHelp() {
    console::Clear();
    PrintCommandLineHelp();
    console::Out(Tr8(
        "Interactive mode tips:\n"
        "  * Menu 1 opens a file dialog; pick an image to convert it "
        "immediately.\n"
        "  * Menu 2 plays a selected video; press Space to pause, -/+ to adjust "
        "the\n"
        "    character aspect ratio, and Q to quit.\n"
        "  * After converting an image you are asked whether to save it as "
        "TXT or HTML.\n"
        "  * HTML keeps the colors; TXT is plain character art.\n"
        "\n",
        "交互模式提示:\n"
        "  * 菜单 1 会打开文件选择框，选择图片后立即转换。\n"
        "  * 菜单 2 选择视频后直接播放；按空格暂停，按 -/+ 调整字符宽高比，按 Q 退出。\n"
        "  * 图片转换完成后会询问是否保存为 TXT 或 HTML。\n"
        "  * HTML 文件会保留颜色，TXT 是纯字符画。\n"
        "\n"));
    console::Pause(
        Tr8("  Press Enter to return to the main menu...",
            "  按 Enter 返回主菜单..."));
}

void App::ReportError(const std::wstring& message, bool pause) const {
    console::Err(Tr8("\n  [Error] ", "\n  [错误] ") + ToUtf8(message) + "\n");
    if (pause && console::IsConsole()) {
        console::Pause(Tr8("\n  Press Enter to return...",
                           "\n  按 Enter 返回..."));
    }
}

bool App::PromptSaveImage(const AsciiFrame& frame, const Charset& charset,
                          const std::wstring& sourcePath) const {
    if (!AskYesNo(Tr8("\n  Save to a file? [y/N]: ",
                      "\n  是否保存为文件？[y/N]: "))) {
        return false;
    }

    console::Out(Tr8("  1. Text file (.txt)\n", "  1. 文本文件 (.txt)\n"));
    console::Out(Tr8("  2. Web page (.html, keeps colors)\n",
                     "  2. 网页文件 (.html，保留颜色)\n"));
    const std::wstring format =
        console::ReadLine(Tr8("  Choose format [1]: ",
                              "  选择格式 [1]: "));
    const bool html = format == L"2";
    const std::wstring ext = html ? L"html" : L"txt";
    const std::wstring defaultName =
        util::StemOf(sourcePath) + L"_ascii." + ext;

    std::wstring outputPath;
    if (!PickSaveFile(defaultName, ext, outputPath)) {
        console::Info(Tr8("  Save cancelled.\n", "  已取消保存。\n"));
        return false;
    }

    std::wstring error;
    if (!SaveAsciiFile(outputPath, frame, charset, options_.render.color,
                       util::FileNameOf(sourcePath), error)) {
        ReportError(error, false);
        return false;
    }
    console::Info(Tr8("  Saved: ", "  已保存: ") + ToUtf8(outputPath) + "\n");
    return true;
}

#include "Console.h"

#include "Localization.h"
#include "Util.h"

#include <conio.h>

#include <algorithm>
#include <cstdio>
#include <cwchar>
#include <iostream>
#include <iterator>
#include <string>

namespace console {
namespace {

HANDLE g_stdout = INVALID_HANDLE_VALUE;
HANDLE g_stderr = INVALID_HANDLE_VALUE;
HANDLE g_stdin = INVALID_HANDLE_VALUE;

bool g_initialized = false;
bool g_consoleOut = false;
bool g_consoleIn = false;
bool g_ansi = false;
bool g_inputEnded = false;

DWORD g_oldOutMode = 0;
DWORD g_oldInMode = 0;
bool g_haveOutMode = false;
bool g_haveInMode = false;
UINT g_oldOutputCodePage = 0;
UINT g_oldInputCodePage = 0;

std::string NormalizeNewlines(const std::string& text) {
    std::string result;
    result.reserve(text.size() + text.size() / 16);
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' && (i == 0 || text[i - 1] != '\r')) {
            result.push_back('\r');
        }
        result.push_back(text[i]);
    }
    return result;
}

void WriteConsoleText(HANDLE handle, const std::string& utf8Text, bool consoleOut) {
    if (handle == INVALID_HANDLE_VALUE) {
        return;
    }
    if (!consoleOut) {
        std::fwrite(utf8Text.data(), 1, utf8Text.size(),
                    handle == g_stderr ? stderr : stdout);
        std::fflush(handle == g_stderr ? stderr : stdout);
        return;
    }

    const std::wstring wide = util::ToWide(utf8Text);
    if (wide.empty()) {
        return;
    }
    std::size_t offset = 0;
    while (offset < wide.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(8192, wide.size() - offset));
        DWORD written = 0;
        if (!WriteConsoleW(handle, wide.data() + offset, chunk, &written,
                           nullptr)) {
            // Extremely unlikely, but never lose the text entirely.
            std::fwrite(utf8Text.data(), 1, utf8Text.size(),
                        handle == g_stderr ? stderr : stdout);
            std::fflush(handle == g_stderr ? stderr : stdout);
            return;
        }
        offset += written;
        if (written == 0) {
            break;
        }
    }
}

void WriteTo(HANDLE handle, const std::string& utf8Text, bool consoleOut) {
    if (!consoleOut) {
        WriteConsoleText(handle, utf8Text, false);
        return;
    }

    // Large frames can be tens of megabytes. Convert and write them line by
    // line instead of making two additional full-size copies.
    if (utf8Text.size() > 256 * 1024) {
        std::size_t start = 0;
        while (start < utf8Text.size()) {
            const std::size_t end = utf8Text.find('\n', start);
            if (end == std::string::npos) {
                WriteConsoleText(handle,
                                 NormalizeNewlines(utf8Text.substr(start)),
                                 true);
                break;
            }
            WriteConsoleText(
                handle,
                NormalizeNewlines(utf8Text.substr(start, end - start + 1)),
                true);
            start = end + 1;
        }
        return;
    }

    WriteConsoleText(handle, NormalizeNewlines(utf8Text), true);
}

}  // namespace

bool Initialize() {
    if (g_initialized) {
        return true;
    }

    g_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    g_stderr = GetStdHandle(STD_ERROR_HANDLE);
    g_stdin = GetStdHandle(STD_INPUT_HANDLE);

    DWORD mode = 0;
    g_consoleOut = (g_stdout != INVALID_HANDLE_VALUE &&
                    GetConsoleMode(g_stdout, &mode) != FALSE);
    g_consoleIn = (g_stdin != INVALID_HANDLE_VALUE &&
                   GetConsoleMode(g_stdin, &mode) != FALSE);

    if (g_consoleOut) {
        g_haveOutMode = GetConsoleMode(g_stdout, &g_oldOutMode) != FALSE;
        DWORD newMode = g_oldOutMode | ENABLE_PROCESSED_OUTPUT;
        // 0x0004 == ENABLE_VIRTUAL_TERMINAL_PROCESSING. Keep the literal for
        // older SDK headers that do not define it.
        constexpr DWORD kEnableVirtualTerminalProcessing = 0x0004;
        newMode |= kEnableVirtualTerminalProcessing;
        if (SetConsoleMode(g_stdout, newMode)) {
            g_ansi = true;
        } else if (g_haveOutMode) {
            SetConsoleMode(g_stdout, g_oldOutMode);
        }
    }

    if (g_consoleIn) {
        g_haveInMode = GetConsoleMode(g_stdin, &g_oldInMode) != FALSE;
    }

    g_oldOutputCodePage = GetConsoleOutputCP();
    g_oldInputCodePage = GetConsoleCP();
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);

    g_inputEnded = false;
    g_initialized = true;
    return true;
}

void Shutdown() {
    if (!g_initialized) {
        return;
    }

    HideCursor(false);

    if (g_consoleOut && g_haveOutMode) {
        SetConsoleMode(g_stdout, g_oldOutMode);
    }
    if (g_consoleIn && g_haveInMode) {
        SetConsoleMode(g_stdin, g_oldInMode);
    }
    if (g_oldOutputCodePage != 0) {
        SetConsoleOutputCP(g_oldOutputCodePage);
    }
    if (g_oldInputCodePage != 0) {
        SetConsoleCP(g_oldInputCodePage);
    }

    g_initialized = false;
    g_ansi = false;
}

bool IsConsole() { return g_consoleOut; }
bool IsInputConsole() { return g_consoleIn; }
bool SupportsAnsi() { return g_consoleOut && g_ansi; }

bool IsWindowsTerminalHost() {
    HWND window = GetConsoleWindow();
    if (window != nullptr) {
        wchar_t className[64]{};
        if (GetClassNameW(window, className,
                          static_cast<int>(std::size(className))) > 0) {
            if (_wcsicmp(className, L"ConsoleWindowClass") == 0) {
                return false;
            }
            if (_wcsicmp(className, L"PseudoConsoleWindow") == 0 ||
                _wcsicmp(className, L"CASCADIA_HOSTING_WINDOW_CLASS") == 0) {
                return true;
            }
        }
    }

    wchar_t session[8]{};
    return GetEnvironmentVariableW(L"WT_SESSION", session,
                                   static_cast<DWORD>(std::size(session))) > 0;
}

int Width() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (g_consoleOut &&
        GetConsoleScreenBufferInfo(g_stdout, &info) != FALSE) {
        return std::max<int>(1, info.srWindow.Right - info.srWindow.Left + 1);
    }
    return 120;
}

int Height() {
    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (g_consoleOut &&
        GetConsoleScreenBufferInfo(g_stdout, &info) != FALSE) {
        return std::max<int>(1, info.srWindow.Bottom - info.srWindow.Top + 1);
    }
    return 40;
}

void Out(const std::string& utf8Text) {
    WriteTo(g_stdout, utf8Text, g_consoleOut);
}

void OutWide(const std::wstring& text) {
    if (!g_consoleOut || g_stdout == INVALID_HANDLE_VALUE || text.empty()) {
        if (!text.empty()) {
            Out(util::ToUtf8(text));
        }
        return;
    }

    // Large frames are written in a few big chunks instead of one call per
    // line. WriteConsoleW is thread-safe, so the playback writer thread can
    // use it directly. Big chunks keep a single frame in as few calls as
    // possible, which also reduces visible tearing on terminals that do not
    // support synchronized output.
    std::size_t offset = 0;
    while (offset < text.size()) {
        const DWORD chunk = static_cast<DWORD>(
            std::min<std::size_t>(262144, text.size() - offset));
        DWORD written = 0;
        if (!WriteConsoleW(g_stdout, text.data() + offset, chunk, &written,
                           nullptr)) {
            Out(util::ToUtf8(text.substr(offset)));
            return;
        }
        if (written == 0) {
            break;
        }
        offset += written;
    }
}

void Err(const std::string& utf8Text) {
    DWORD mode = 0;
    const bool stderrIsConsole =
        (g_stderr != INVALID_HANDLE_VALUE &&
         GetConsoleMode(g_stderr, &mode) != FALSE);
    WriteTo(g_stderr, utf8Text, stderrIsConsole);
}

void Info(const std::string& utf8Text) {
    if (g_consoleOut) {
        Out(utf8Text);
    } else {
        Err(utf8Text);
    }
}

void Clear() {
    if (!g_consoleOut) {
        return;
    }
    if (g_ansi) {
        // 2J clears the visible screen; 3J clears the saved scrollback too, so
        // scrolling up does not reveal output from earlier conversions.
        Out("\x1b[2J\x1b[3J\x1b[H");
    } else {
        CONSOLE_SCREEN_BUFFER_INFO info{};
        if (GetConsoleScreenBufferInfo(g_stdout, &info) != FALSE) {
            const DWORD cells = static_cast<DWORD>(info.dwSize.X) *
                                static_cast<DWORD>(info.dwSize.Y);
            DWORD written = 0;
            COORD home{0, 0};
            FillConsoleOutputCharacterW(g_stdout, L' ', cells, home, &written);
            FillConsoleOutputAttribute(g_stdout, info.wAttributes, cells, home,
                                       &written);
            SetConsoleCursorPosition(g_stdout, home);
        }
    }
}

void Home() {
    if (!g_consoleOut) {
        return;
    }
    if (g_ansi) {
        Out("\x1b[H");
    } else {
        SetConsoleCursorPosition(g_stdout, COORD{0, 0});
    }
}

void HideCursor(bool hide) {
    if (!g_consoleOut) {
        return;
    }
    if (g_ansi) {
        Out(hide ? "\x1b[?25l" : "\x1b[?25h");
    } else {
        CONSOLE_CURSOR_INFO info{};
        if (GetConsoleCursorInfo(g_stdout, &info) != FALSE) {
            info.bVisible = hide ? FALSE : TRUE;
            SetConsoleCursorInfo(g_stdout, &info);
        }
    }
}

void SetTitle(const std::wstring& title) {
    SetConsoleTitleW(title.c_str());
}

std::wstring ReadLineRaw(const std::string& prompt) {
    if (!prompt.empty()) {
        Out(prompt);
    }
    if (g_consoleIn) {
        std::wstring result;
        wchar_t buffer[1024];
        for (;;) {
            DWORD read = 0;
            if (!ReadConsoleW(g_stdin, buffer, 1023, &read, nullptr) ||
                read == 0) {
                g_inputEnded = true;
                break;
            }
            result.append(buffer, buffer + read);
            if (result.find(L'\n') != std::wstring::npos) {
                break;
            }
            if (result.size() > 8192) {
                break;
            }
        }
        while (!result.empty() &&
               (result.back() == L'\n' || result.back() == L'\r')) {
            result.pop_back();
        }
        return result;
    }

    std::string line;
    if (!std::getline(std::cin, line)) {
        g_inputEnded = true;
        return {};
    }
    return util::ToWide(line);
}

std::wstring ReadLine(const std::string& prompt) {
    return util::Trim(ReadLineRaw(prompt));
}

bool InputEnded() { return g_inputEnded; }

void Pause(const std::string& prompt) {
    if (g_consoleIn) {
        ReadLine(prompt.empty()
                     ? Tr8("Press Enter to continue...", "按 Enter 继续...")
                     : prompt);
    }
}

bool KeyAvailable() {
    if (!g_consoleIn) {
        return false;
    }
    return _kbhit() != 0;
}

int ReadKey() {
    if (!g_consoleIn) {
        return 0;
    }
    const int ch = _getch();
    if (ch == 0 || ch == 224) {
        _getch();  // consume the second byte of an extended key
        return 0;
    }
    return ch;
}

NoWrap::NoWrap() {
    if (!g_consoleOut || g_stdout == INVALID_HANDLE_VALUE) {
        return;
    }
    DWORD mode = 0;
    if (!GetConsoleMode(g_stdout, &mode)) {
        return;
    }
    handle_ = g_stdout;
    oldMode_ = mode;
    if (SetConsoleMode(handle_, mode & ~ENABLE_WRAP_AT_EOL_OUTPUT)) {
        active_ = true;
    }
}

NoWrap::~NoWrap() {
    if (active_ && handle_ != INVALID_HANDLE_VALUE) {
        SetConsoleMode(handle_, oldMode_);
    }
}

ConsoleFit::~ConsoleFit() { Restore(); }

bool ConsoleFit::IsWindowsTerminal() const {
    return console::IsWindowsTerminalHost();
}

bool ConsoleFit::TrySetFont(SHORT width, SHORT height) {
    if (console_ == INVALID_HANDLE_VALUE || width < 1 || height < 1) {
        return false;
    }

    CONSOLE_FONT_INFOEX font = originalFont_;
    font.dwFontSize.X = width;
    font.dwFontSize.Y = height;
    font.FontFamily = FF_DONTCARE;
    font.FontWeight = FW_NORMAL;

    if (SetCurrentConsoleFontEx(console_, FALSE, &font)) {
        fontChanged_ = true;
        changed_ = true;
        return true;
    }

    // Some raster/default faces cannot be resized. Consolas is present on
    // every supported Windows version and can be scaled much smaller.
    if (_wcsicmp(font.FaceName, L"Consolas") != 0) {
        wcscpy_s(font.FaceName, L"Consolas");
        if (SetCurrentConsoleFontEx(console_, FALSE, &font)) {
            fontChanged_ = true;
            changed_ = true;
            return true;
        }
    }
    return false;
}

bool ConsoleFit::ResizeConsoleWindow(int neededCols, int neededRows) {
    if (console_ == INVALID_HANDLE_VALUE || neededCols < 1 ||
        neededRows < 1 || neededCols > 32767 || neededRows > 32767) {
        return false;
    }

    CONSOLE_SCREEN_BUFFER_INFO info{};
    if (!GetConsoleScreenBufferInfo(console_, &info)) {
        return false;
    }

    COORD buffer = info.dwSize;
    if (buffer.X < neededCols) {
        buffer.X = static_cast<SHORT>(neededCols);
    }
    if (buffer.Y < neededRows) {
        buffer.Y = static_cast<SHORT>(neededRows);
    }
    if (buffer.X != info.dwSize.X || buffer.Y != info.dwSize.Y) {
        if (!SetConsoleScreenBufferSize(console_, buffer)) {
            return false;
        }
    }

    SMALL_RECT target{};
    target.Left = 0;
    target.Top = 0;
    target.Right = static_cast<SHORT>(neededCols - 1);
    target.Bottom = static_cast<SHORT>(neededRows - 1);
    if (!SetConsoleWindowInfo(console_, TRUE, &target)) {
        // Retry with an exactly-sized buffer; this also helps when the
        // previous buffer had a different width/height relationship.
        const COORD exact{static_cast<SHORT>(neededCols),
                          static_cast<SHORT>(neededRows)};
        if (!SetConsoleScreenBufferSize(console_, exact) ||
            !SetConsoleWindowInfo(console_, TRUE, &target)) {
            return false;
        }
    }

    if (!GetConsoleScreenBufferInfo(console_, &info)) {
        return false;
    }
    const int actualCols = info.srWindow.Right - info.srWindow.Left + 1;
    const int actualRows = info.srWindow.Bottom - info.srWindow.Top + 1;
    return actualCols >= neededCols && actualRows >= neededRows;
}

bool ConsoleFit::FitForGrid(int artCols, int artRows, int reservedRows,
                            bool allowCompact, bool& compact,
                            std::wstring& error) {
    error.clear();
    compact = false;
    if (artCols <= 0 || artRows <= 0 || reservedRows < 0) {
        error = Tr(L"Invalid character-art size.", L"无效的字符画尺寸。");
        return false;
    }
    if (console_ != INVALID_HANDLE_VALUE) {
        Restore();
    }

    console_ = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, 0, nullptr);
    if (console_ == INVALID_HANDLE_VALUE) {
        error = Tr(L"Cannot access the current console output.",
                   L"无法访问当前控制台输出。");
        return false;
    }

    window_ = GetConsoleWindow();
    if (!GetConsoleScreenBufferInfo(console_, &originalInfo_)) {
        error = Tr(L"Cannot read the current console window information.",
                   L"无法读取当前控制台窗口信息。");
        CloseHandle(console_);
        console_ = INVALID_HANDLE_VALUE;
        return false;
    }

    const int neededCols = artCols;
    const int normalRows = artRows + reservedRows;
    const int compactRows = artRows;
    const int currentCols =
        originalInfo_.srWindow.Right - originalInfo_.srWindow.Left + 1;
    const int currentRows =
        originalInfo_.srWindow.Bottom - originalInfo_.srWindow.Top + 1;
    if (currentCols >= neededCols && currentRows >= normalRows) {
        CloseHandle(console_);
        console_ = INVALID_HANDLE_VALUE;
        return true;
    }
    if (allowCompact && currentCols >= neededCols &&
        currentRows >= compactRows) {
        compact = true;
        CloseHandle(console_);
        console_ = INVALID_HANDLE_VALUE;
        return true;
    }

    if (IsWindowsTerminal()) {
        error = Tr(
            L"The current terminal is Windows Terminal, which does not allow "
            L"the program to change the font size.\n"
            L"Press Ctrl+- to shrink the terminal font or enlarge the window, "
            L"or use --no-display --save to save large character art.",
            L"当前终端是 Windows Terminal，程序无法修改终端字体大小。\n"
            L"请按 Ctrl+- 缩小终端字体或调大窗口，或者使用 "
            L"--no-display --save 保存大分辨率字符画。");
        CloseHandle(console_);
        console_ = INVALID_HANDLE_VALUE;
        return false;
    }

    originalFont_.cbSize = sizeof(originalFont_);
    if (window_ == nullptr ||
        !GetCurrentConsoleFontEx(console_, FALSE, &originalFont_)) {
        error = Tr(L"Cannot read the current console font information.",
                   L"无法读取当前控制台字体信息。");
        CloseHandle(console_);
        console_ = INVALID_HANDLE_VALUE;
        return false;
    }
    originalPlacement_.length = sizeof(originalPlacement_);
    havePlacement_ = GetWindowPlacement(window_, &originalPlacement_) != FALSE;

    if (!IsZoomed(window_)) {
        ShowWindow(window_, SW_MAXIMIZE);
        changed_ = true;
        Sleep(100);
    }

    const SHORT candidates[][2] = {
        {8, 14}, {7, 12}, {6, 12}, {6, 10}, {6, 8}, {5, 8}, {4, 6},
    };
    auto fitsNormal = [&](COORD largest) {
        return largest.X >= neededCols && largest.Y >= normalRows;
    };
    auto fitsCompact = [&](COORD largest) {
        return largest.X >= neededCols && largest.Y >= compactRows;
    };

    COORD largest = GetLargestConsoleWindowSize(console_);
    bool chosen = false;
    compact = false;

    if (fitsNormal(largest)) {
        chosen = true;
    } else {
        for (const auto& candidate : candidates) {
            if (candidate[1] >= originalFont_.dwFontSize.Y) {
                continue;
            }
            if (!TrySetFont(candidate[0], candidate[1])) {
                continue;
            }
            Sleep(40);
            largest = GetLargestConsoleWindowSize(console_);
            if (fitsNormal(largest)) {
                chosen = true;
                break;
            }
        }
    }

    if (!chosen && allowCompact) {
        // Prefer the full layout. If it cannot fit at any usable font, retry
        // the art alone from the original font so the largest readable font
        // that fits the compact layout is selected.
        SetCurrentConsoleFontEx(console_, FALSE, &originalFont_);
        largest = GetLargestConsoleWindowSize(console_);
        if (fitsCompact(largest)) {
            chosen = true;
            compact = true;
        } else {
            for (const auto& candidate : candidates) {
                if (candidate[1] >= originalFont_.dwFontSize.Y) {
                    continue;
                }
                if (!TrySetFont(candidate[0], candidate[1])) {
                    continue;
                }
                Sleep(40);
                largest = GetLargestConsoleWindowSize(console_);
                if (fitsCompact(largest)) {
                    chosen = true;
                    compact = true;
                    break;
                }
            }
        }
    }

    if (!chosen) {
        error = std::wstring(
                    Tr(L"Cannot fit ", L"无法在当前屏幕和控制台字体下容纳 ")) +
                std::to_wstring(artCols) + L"x" + std::to_wstring(artRows) +
                Tr(L" characters of art on the current screen and font.\n"
                   L"Lower the resolution, enlarge the console window, or use "
                   L"--no-display --save to save large character art.",
                   L" 字符画。\n"
                   L"请降低分辨率、调大控制台窗口，或使用 --no-display --save "
                   L"保存大分辨率字符画。");
        Restore();
        return false;
    }

    const int chosenRows = compact ? compactRows : normalRows;
    changed_ = true;
    if (!ResizeConsoleWindow(neededCols, chosenRows)) {
        error = Tr(
            L"The console does not support resizing to the required window "
            L"size.\n"
            L"Enlarge the window / shrink the font manually, or use "
            L"--no-display --save to save large character art.",
            L"控制台不支持调整为所需窗口大小。\n"
            L"请手动调大窗口/缩小字体，或使用 --no-display --save "
            L"保存大分辨率字符画。");
        Restore();
        return false;
    }

    return true;
}

void ConsoleFit::Restore() {
    if (console_ == INVALID_HANDLE_VALUE) {
        return;
    }

    if (fontChanged_) {
        SetCurrentConsoleFontEx(console_, FALSE, &originalFont_);
    }

    if (changed_) {
        CONSOLE_SCREEN_BUFFER_INFO current{};
        if (GetConsoleScreenBufferInfo(console_, &current)) {
            COORD buffer = current.dwSize;
            if (buffer.X < originalInfo_.dwSize.X) {
                buffer.X = originalInfo_.dwSize.X;
            }
            if (buffer.Y < originalInfo_.dwSize.Y) {
                buffer.Y = originalInfo_.dwSize.Y;
            }
            SetConsoleScreenBufferSize(console_, buffer);
        }

        SetConsoleWindowInfo(console_, TRUE, &originalInfo_.srWindow);
        SetConsoleScreenBufferSize(console_, originalInfo_.dwSize);
        SetConsoleCursorPosition(console_, originalInfo_.dwCursorPosition);
    }

    if (havePlacement_ && window_ != nullptr) {
        SetWindowPlacement(window_, &originalPlacement_);
    }

    CloseHandle(console_);
    console_ = INVALID_HANDLE_VALUE;
    window_ = nullptr;
    changed_ = false;
    fontChanged_ = false;
    havePlacement_ = false;
}

LockWindowResize::LockWindowResize() {
    window_ = GetConsoleWindow();
    if (window_ == nullptr || console::IsWindowsTerminalHost()) {
        window_ = nullptr;
        return;
    }

    originalStyle_ = GetWindowLongPtrW(window_, GWL_STYLE);
    const LONG_PTR locked = originalStyle_ & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
    if (locked == originalStyle_) {
        window_ = nullptr;
        return;
    }
    SetWindowLongPtrW(window_, GWL_STYLE, locked);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
}

LockWindowResize::~LockWindowResize() { Restore(); }

void LockWindowResize::Restore() {
    if (window_ == nullptr) {
        return;
    }
    SetWindowLongPtrW(window_, GWL_STYLE, originalStyle_);
    SetWindowPos(window_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    window_ = nullptr;
}

}  // namespace console

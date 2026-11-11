#pragma once

#include "Common.h"

#include <string>

namespace console {

// Puts the console into UTF-8 / virtual-terminal mode and saves the old state.
bool Initialize();
void Shutdown();

bool IsConsole();
bool IsInputConsole();
bool SupportsAnsi();
bool IsWindowsTerminalHost();

int Width();
int Height();

// Writes UTF-8 text to stdout. In a console window, bare '\n' is turned into
// "\r\n" because WriteConsoleW does not translate line endings.
void Out(const std::string& utf8Text);

// Writes UTF-16 text straight to the console, skipping the UTF-8 -> UTF-16
// conversion. Video playback uses this for every frame; the caller supplies
// its own CRLF line endings. Falls back to Out() when stdout is redirected.
void OutWide(const std::wstring& text);

// Writes UTF-8 text to stderr.
void Err(const std::string& utf8Text);

// Writes to stdout in an interactive console, but to stderr when stdout is
// redirected, so that `ASCIIArt image.png > art.txt` keeps art.txt clean.
void Info(const std::string& utf8Text);

void Clear();
void Home();
void HideCursor(bool hide);
void SetTitle(const std::wstring& title);
void Pause(const std::string& prompt = std::string());

std::wstring ReadLine(const std::string& prompt);

// Same as ReadLine but preserves leading/trailing spaces (only the line
// ending is removed). Used for custom charsets where a space is meaningful.
std::wstring ReadLineRaw(const std::string& prompt);

// True after ReadLine encountered end-of-input. Interactive mode uses this to
// avoid spinning forever when stdin is redirected from an empty file.
bool InputEnded();

bool KeyAvailable();
int ReadKey();  // returns 0 for extended keys, otherwise the character

// Measurements reported by the terminal for the current text area.
struct TerminalMetrics {
    int cols = 0;
    int rows = 0;
    int pixelWidth = 0;
    int pixelHeight = 0;
    int cellWidth = 0;
    int cellHeight = 0;
};

// Asks the terminal for its text-area size using the xterm window
// manipulation queries (CSI 18 t / CSI 14 t / CSI 16 t). Returns false when
// stdin/stdout is not a console or the terminal does not answer. `raw`
// receives the unparsed response for diagnostics.
bool ProbeTerminal(TerminalMetrics& metrics, std::wstring& raw,
                   unsigned long timeoutMs = 600);

// Temporarily disables automatic wrap at end-of-line. This lets a line that
// exactly fills the console width be printed one row at a time without the
// terminal inserting an extra line.
class NoWrap {
public:
    NoWrap();
    ~NoWrap();
    NoWrap(const NoWrap&) = delete;
    NoWrap& operator=(const NoWrap&) = delete;

private:
    HANDLE handle_ = INVALID_HANDLE_VALUE;
    DWORD oldMode_ = 0;
    bool active_ = false;
};

// Temporarily shrinks the classic console font and expands the console window
// so a larger character grid can be displayed. The original font, buffer and
// window placement are restored by Restore() or the destructor.
class ConsoleFit {
public:
    ConsoleFit() = default;
    ~ConsoleFit();
    ConsoleFit(const ConsoleFit&) = delete;
    ConsoleFit& operator=(const ConsoleFit&) = delete;

    // artCols x artRows is the character-art size. reservedRows is the number
    // of extra rows needed for headers/status. If the full layout does not
    // fit but the art alone can, allowCompact permits a full-screen layout;
    // compact is set accordingly.
    bool FitForGrid(int artCols, int artRows, int reservedRows,
                    bool allowCompact, bool& compact, std::wstring& error);
    bool Changed() const { return changed_; }
    void Restore();

private:
    bool TrySetFont(SHORT width, SHORT height);
    bool ResizeConsoleWindow(int neededCols, int neededRows);
    bool IsWindowsTerminal() const;

    HANDLE console_ = INVALID_HANDLE_VALUE;
    HWND window_ = nullptr;
    CONSOLE_FONT_INFOEX originalFont_{};
    CONSOLE_SCREEN_BUFFER_INFO originalInfo_{};
    WINDOWPLACEMENT originalPlacement_{};
    bool havePlacement_ = false;
    bool changed_ = false;
    bool fontChanged_ = false;
};

// Keeps the classic console window at a fixed size while it is alive: the
// resize border and maximize box are removed so an accidental drag cannot
// break the character grid. Does nothing in Windows Terminal, where the
// console window does not belong to the application.
class LockWindowResize {
public:
    LockWindowResize();
    ~LockWindowResize();
    LockWindowResize(const LockWindowResize&) = delete;
    LockWindowResize& operator=(const LockWindowResize&) = delete;

    void Restore();

private:
    HWND window_ = nullptr;
    LONG_PTR originalStyle_ = 0;
};

}  // namespace console

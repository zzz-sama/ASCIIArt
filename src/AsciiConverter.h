#pragma once

#include "Charset.h"
#include "Common.h"

#include <string>
#include <vector>

// Output dimensions are not capped. Extremely large grids are attempted and
// any allocation failure is reported by the application.

struct RenderOptions {
    Charset charset;
    int width = 0;          // 0 = automatic
    int height = 0;         // 0 = derived from width / aspect
    bool color = true;      // used for terminal / HTML output
    bool invert = false;
    double aspect = 0.5;    // character cell width / height
    double gamma = 1.0;     // >1 darkens the mid-tones, <1 brightens them
    int fps = 24;           // video playback cap
    bool loop = false;
};

struct GridSize {
    int cols = 0;
    int rows = 0;
};

struct AsciiCell {
    std::uint8_t glyph = 0;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct AsciiFrame {
    int cols = 0;
    int rows = 0;
    std::vector<AsciiCell> cells;  // row-major

    bool Empty() const { return cols <= 0 || rows <= 0 || cells.empty(); }
};

// The grid the user asked for, before fitting it to the console window.
// A zero requested dimension is derived from the source aspect and the
// character aspect correction. Both non-zero dimensions are exact.
GridSize DesiredGrid(int sourceWidth, int sourceHeight, int requestedWidth,
                     int requestedHeight, double aspect);

// Chooses the character grid that fits maxCols x maxRows. When both requested
// dimensions are non-zero and fit, they are kept exactly; otherwise the
// remaining dimension is derived from the source aspect.
GridSize FitGrid(int sourceWidth, int sourceHeight, int requestedWidth,
                 int requestedHeight, int maxCols, int maxRows, double aspect);

AsciiFrame RenderAscii(const RgbaImage& image, const Charset& charset,
                       GridSize grid, const RenderOptions& options);

// Renders into a caller-owned frame so video playback can reuse the cell
// buffer instead of allocating a new one for every frame.
void RenderAsciiInto(const RgbaImage& image, const Charset& charset,
                     GridSize grid, const RenderOptions& options,
                     AsciiFrame& frame);

std::string FrameToText(const AsciiFrame& frame, const Charset& charset);
std::string FrameToAnsi(const AsciiFrame& frame, const Charset& charset,
                        bool color);
std::string FrameToHtmlFragment(const AsciiFrame& frame, const Charset& charset,
                                bool color);

// UTF-16 glyphs for the same charset, prepared once so the video player does
// not convert every frame from UTF-8.
std::vector<std::wstring> WideGlyphs(const Charset& charset);

// Appends one frame as UTF-16 ANSI escape sequences. Rows are separated by
// CRLF and the last row has no trailing newline. Writing UTF-16 straight to
// WriteConsoleW avoids building a large UTF-8 copy and converting it again
// for every video frame.
void AppendFrameAnsiWide(std::wstring& out, const AsciiFrame& frame,
                         const std::vector<std::wstring>& glyphs, bool color);

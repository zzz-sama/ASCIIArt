#include "AsciiConverter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>

namespace {

int ClampInt(int value, int low, int high) {
    return std::max(low, std::min(high, value));
}

// Keeps a grid usable without an artificial size cap: only the minimum of one
// column/row is enforced. Allocation failures for extremely large grids are
// reported by the caller instead of being silently clamped.
void NormalizeGrid(GridSize& grid) {
    if (grid.cols < 1) {
        grid.cols = 1;
    }
    if (grid.rows < 1) {
        grid.rows = 1;
    }
}

// Saturates instead of overflowing when a one-sided extreme request derives a
// dimension beyond the int range.
int ToGridInt(double value) {
    if (!std::isfinite(value) || value <= 1.0) {
        return 1;
    }
    constexpr double kIntMax =
        static_cast<double>(std::numeric_limits<int>::max());
    if (value >= kIntMax) {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(value);
}

// Appends an unsigned byte as decimal digits. Hand-rolled formatting is much
// faster than snprintf, which used to dominate the per-frame ANSI build.
void AppendDecimal(std::string& out, unsigned value) {
    char digits[3];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

void AppendDecimal(std::wstring& out, unsigned value) {
    wchar_t digits[3];
    std::size_t count = 0;
    do {
        digits[count++] = static_cast<wchar_t>(L'0' + value % 10);
        value /= 10;
    } while (value != 0);
    while (count > 0) {
        out.push_back(digits[--count]);
    }
}

void AppendColorCode(std::string& out, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b) {
    out += "\x1b[38;2;";
    AppendDecimal(out, r);
    out.push_back(';');
    AppendDecimal(out, g);
    out.push_back(';');
    AppendDecimal(out, b);
    out.push_back('m');
}

void AppendColorCode(std::wstring& out, std::uint8_t r, std::uint8_t g,
                     std::uint8_t b) {
    out += L"\x1b[38;2;";
    AppendDecimal(out, r);
    out.push_back(L';');
    AppendDecimal(out, g);
    out.push_back(L';');
    AppendDecimal(out, b);
    out.push_back(L'm');
}

std::string ColorHex(std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    char buffer[8];
    std::snprintf(buffer, sizeof(buffer), "#%02X%02X%02X",
                  static_cast<unsigned>(r), static_cast<unsigned>(g),
                  static_cast<unsigned>(b));
    return buffer;
}

std::string HtmlEscape(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        switch (ch) {
            case '&':
                result += "&amp;";
                break;
            case '<':
                result += "&lt;";
                break;
            case '>':
                result += "&gt;";
                break;
            case '"':
                result += "&quot;";
                break;
            case '\'':
                result += "&#39;";
                break;
            default:
                result.push_back(ch);
                break;
        }
    }
    return result;
}

bool IsSpaceGlyph(const std::string& glyph) {
    return glyph == " ";
}

std::size_t LastVisibleColumn(const AsciiFrame& frame, const Charset& charset,
                              int row) {
    for (int col = frame.cols - 1; col >= 0; --col) {
        const auto& cell = frame.cells[static_cast<std::size_t>(row) *
                                           frame.cols +
                                       col];
        if (cell.glyph < charset.glyphs.size() &&
            !IsSpaceGlyph(charset.glyphs[cell.glyph])) {
            return static_cast<std::size_t>(col);
        }
    }
    return 0;
}

}  // namespace

GridSize DesiredGrid(int sourceWidth, int sourceHeight, int requestedWidth,
                     int requestedHeight, double aspect) {
    GridSize result{1, 1};
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return result;
    }

    aspect = std::max(0.15, std::min(1.0, aspect));
    if (requestedWidth > 0 && requestedHeight > 0) {
        result.cols = std::max(1, requestedWidth);
        result.rows = std::max(1, requestedHeight);
        return result;
    }

    const double sourceRatio =
        static_cast<double>(sourceHeight) / static_cast<double>(sourceWidth);
    if (requestedWidth > 0) {
        result.cols = std::max(1, requestedWidth);
        result.rows = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(result.cols) * sourceRatio * aspect)));
    } else if (requestedHeight > 0) {
        result.rows = std::max(1, requestedHeight);
        result.cols = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(result.rows) / (sourceRatio * aspect))));
    } else {
        result.cols = 100;
        result.rows = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(result.cols) * sourceRatio * aspect)));
    }
    NormalizeGrid(result);
    return result;
}

GridSize FitGrid(int sourceWidth, int sourceHeight, int requestedWidth,
                 int requestedHeight, int maxCols, int maxRows,
                 double aspect) {
    GridSize result{1, 1};
    if (sourceWidth <= 0 || sourceHeight <= 0) {
        return result;
    }

    aspect = std::max(0.15, std::min(1.0, aspect));

    // Both dimensions requested: keep them exactly. The application checks
    // whether this fits the console before showing a preview.
    if (requestedWidth > 0 && requestedHeight > 0) {
        result.cols = std::max(1, requestedWidth);
        result.rows = std::max(1, requestedHeight);
        return result;
    }

    maxCols = std::max(1, maxCols);
    maxRows = std::max(1, maxRows);

    const double sourceRatio =
        static_cast<double>(sourceHeight) / static_cast<double>(sourceWidth);

    int cols = 0;
    int rows = 0;
    if (requestedWidth > 0) {
        cols = ClampInt(requestedWidth, 1, maxCols);
        rows = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(cols) * sourceRatio * aspect)));
    } else if (requestedHeight > 0) {
        rows = ClampInt(requestedHeight, 1, maxRows);
        cols = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(rows) / (sourceRatio * aspect))));
        if (cols > maxCols) {
            cols = maxCols;
            rows = ToGridInt(static_cast<double>(std::lround(
                static_cast<double>(cols) * sourceRatio * aspect)));
            rows = std::min(rows, maxRows);
        }
    } else {
        cols = std::min(100, maxCols);
        rows = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(cols) * sourceRatio * aspect)));
    }

    if (rows > maxRows) {
        rows = maxRows;
        cols = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(rows) / (sourceRatio * aspect))));
        cols = ClampInt(cols, 1, maxCols);
        rows = ToGridInt(static_cast<double>(std::lround(
            static_cast<double>(cols) * sourceRatio * aspect)));
        rows = std::min(rows, maxRows);
    }

    result.cols = cols;
    result.rows = rows;
    NormalizeGrid(result);
    return result;
}

AsciiFrame RenderAscii(const RgbaImage& image, const Charset& charset,
                       GridSize grid, const RenderOptions& options) {
    AsciiFrame frame;
    RenderAsciiInto(image, charset, grid, options, frame);
    return frame;
}

void RenderAsciiInto(const RgbaImage& image, const Charset& charset,
                     GridSize grid, const RenderOptions& options,
                     AsciiFrame& frame) {
    frame.cols = 0;
    frame.rows = 0;
    frame.cells.clear();
    if (image.Empty() || charset.Empty() || grid.cols <= 0 || grid.rows <= 0) {
        return;
    }

    // There is no artificial size cap. The only check left rejects a cell
    // count that cannot be represented as a vector size; anything smaller is
    // attempted and a genuine allocation failure surfaces as std::bad_alloc.
    GridSize safe = grid;
    NormalizeGrid(safe);
    const std::uint64_t cellCount =
        static_cast<std::uint64_t>(safe.cols) *
        static_cast<std::uint64_t>(safe.rows);
    if (cellCount > static_cast<std::uint64_t>(frame.cells.max_size())) {
        return;
    }
    frame.cols = safe.cols;
    frame.rows = safe.rows;
    frame.cells.resize(static_cast<std::size_t>(cellCount));

    const int srcW = image.width;
    const int srcH = image.height;
    const std::size_t glyphCount = charset.glyphs.size();
    const double gamma = std::max(0.1, std::min(4.0, options.gamma));
    const double maxGlyph = static_cast<double>(glyphCount - 1);

    // Gamma and inversion only depend on luminance, so map all 256 possible
    // brightness values once instead of calling pow() for every cell.
    std::uint8_t glyphLut[256];
    for (int level = 0; level < 256; ++level) {
        double luminance = static_cast<double>(level) / 255.0;
        luminance = std::pow(luminance, gamma);
        if (options.invert) {
            luminance = 1.0 - luminance;
        }
        glyphLut[level] = static_cast<std::uint8_t>(
            ClampInt(static_cast<int>(luminance * maxGlyph + 0.5), 0,
                     static_cast<int>(glyphCount - 1)));
    }

    for (int row = 0; row < grid.rows; ++row) {
        const int sy0 = static_cast<int>(static_cast<long long>(row) * srcH /
                                         grid.rows);
        int sy1 = static_cast<int>(static_cast<long long>(row + 1) * srcH /
                                   grid.rows);
        if (sy1 <= sy0) {
            sy1 = sy0 + 1;
        }
        sy1 = std::min(sy1, srcH);

        for (int col = 0; col < grid.cols; ++col) {
            const int sx0 = static_cast<int>(static_cast<long long>(col) *
                                             srcW / grid.cols);
            int sx1 = static_cast<int>(static_cast<long long>(col + 1) *
                                       srcW / grid.cols);
            if (sx1 <= sx0) {
                sx1 = sx0 + 1;
            }
            sx1 = std::min(sx1, srcW);

            std::uint64_t sumR = 0;
            std::uint64_t sumG = 0;
            std::uint64_t sumB = 0;
            std::uint64_t count = 0;

            for (int y = sy0; y < sy1; ++y) {
                const std::uint8_t* line =
                    image.pixels.data() +
                    (static_cast<std::size_t>(y) * srcW + sx0) * 4;
                for (int x = sx0; x < sx1; ++x) {
                    const std::uint8_t alpha = line[3];
                    if (alpha == 255) {
                        // Opaque pixels (all video frames and most photos)
                        // skip the per-channel compositing arithmetic.
                        sumR += line[0];
                        sumG += line[1];
                        sumB += line[2];
                    } else {
                        // Composite the image over a black background.
                        sumR +=
                            static_cast<std::uint64_t>(line[0]) * alpha / 255;
                        sumG +=
                            static_cast<std::uint64_t>(line[1]) * alpha / 255;
                        sumB +=
                            static_cast<std::uint64_t>(line[2]) * alpha / 255;
                    }
                    line += 4;
                    ++count;
                }
            }

            if (count == 0) {
                count = 1;
            }
            const std::uint8_t avgR =
                static_cast<std::uint8_t>(sumR / count);
            const std::uint8_t avgG =
                static_cast<std::uint8_t>(sumG / count);
            const std::uint8_t avgB =
                static_cast<std::uint8_t>(sumB / count);

            // Integer BT.709 luma (weights sum to 256).
            const int luma =
                (54 * avgR + 183 * avgG + 19 * avgB + 128) >> 8;

            AsciiCell& cell = frame.cells[static_cast<std::size_t>(row) *
                                              grid.cols +
                                          col];
            cell.glyph = glyphLut[luma];
            cell.r = avgR;
            cell.g = avgG;
            cell.b = avgB;
        }
    }
}

std::string FrameToText(const AsciiFrame& frame, const Charset& charset) {
    if (frame.Empty() || charset.Empty()) {
        return {};
    }

    std::string result;
    result.reserve(static_cast<std::size_t>(frame.cols + 1) * frame.rows);
    for (int row = 0; row < frame.rows; ++row) {
        std::string line;
        line.reserve(static_cast<std::size_t>(frame.cols));
        for (int col = 0; col < frame.cols; ++col) {
            const auto& cell = frame.cells[static_cast<std::size_t>(row) *
                                               frame.cols +
                                           col];
            if (cell.glyph < charset.glyphs.size()) {
                line += charset.glyphs[cell.glyph];
            } else {
                line.push_back(' ');
            }
        }
        while (!line.empty() && line.back() == ' ') {
            line.pop_back();
        }
        result += line;
        result.push_back('\n');
    }
    return result;
}

std::string FrameToAnsi(const AsciiFrame& frame, const Charset& charset,
                        bool color) {
    if (frame.Empty() || charset.Empty()) {
        return {};
    }

    std::string result;
    result.reserve(static_cast<std::size_t>(frame.cols) * frame.rows *
                        (color ? 12 : 4) +
                    static_cast<std::size_t>(frame.rows) * 8);
    for (int row = 0; row < frame.rows; ++row) {
        const std::size_t lastCol = LastVisibleColumn(frame, charset, row);
        std::uint8_t currentR = 0;
        std::uint8_t currentG = 0;
        std::uint8_t currentB = 0;
        bool haveColor = false;

        for (int col = 0; col <= static_cast<int>(lastCol); ++col) {
            const auto& cell = frame.cells[static_cast<std::size_t>(row) *
                                               frame.cols +
                                           col];
            if (cell.glyph >= charset.glyphs.size()) {
                result.push_back(' ');
                continue;
            }
            const std::string& glyph = charset.glyphs[cell.glyph];
            if (color && !IsSpaceGlyph(glyph)) {
                if (!haveColor || cell.r != currentR || cell.g != currentG ||
                    cell.b != currentB) {
                    AppendColorCode(result, cell.r, cell.g, cell.b);
                    currentR = cell.r;
                    currentG = cell.g;
                    currentB = cell.b;
                    haveColor = true;
                }
            }
            result += glyph;
        }

        if (color && haveColor) {
            result += "\x1b[0m";
        }
        result.push_back('\n');
    }
    return result;
}

std::string FrameToHtmlFragment(const AsciiFrame& frame, const Charset& charset,
                                bool color) {
    if (frame.Empty() || charset.Empty()) {
        return {};
    }

    std::string result;
    result.reserve(static_cast<std::size_t>(frame.cols) *
                   static_cast<std::size_t>(frame.rows) * 18u);
    for (int row = 0; row < frame.rows; ++row) {
        const std::size_t lastCol = LastVisibleColumn(frame, charset, row);
        std::uint8_t currentR = 0;
        std::uint8_t currentG = 0;
        std::uint8_t currentB = 0;
        bool haveColor = false;

        for (int col = 0; col <= static_cast<int>(lastCol); ++col) {
            const auto& cell = frame.cells[static_cast<std::size_t>(row) *
                                               frame.cols +
                                           col];
            if (cell.glyph >= charset.glyphs.size()) {
                result.push_back(' ');
                continue;
            }
            const std::string& glyph = charset.glyphs[cell.glyph];
            if (color && !IsSpaceGlyph(glyph)) {
                if (!haveColor || cell.r != currentR || cell.g != currentG ||
                    cell.b != currentB) {
                    if (haveColor) {
                        result += "</span>";
                    }
                    result += "<span style=\"color:" +
                              ColorHex(cell.r, cell.g, cell.b) + "\">";
                    currentR = cell.r;
                    currentG = cell.g;
                    currentB = cell.b;
                    haveColor = true;
                }
            }
            result += HtmlEscape(glyph);
        }
        if (haveColor) {
            result += "</span>";
        }
        result.push_back('\n');
    }
    return result;
}

std::vector<std::wstring> WideGlyphs(const Charset& charset) {
    std::vector<std::wstring> result;
    result.reserve(charset.glyphs.size());
    for (const std::string& glyph : charset.glyphs) {
        if (glyph.empty()) {
            result.emplace_back();
            continue;
        }
        const int needed = MultiByteToWideChar(
            CP_UTF8, 0, glyph.data(), static_cast<int>(glyph.size()), nullptr,
            0);
        if (needed <= 0) {
            result.emplace_back();
            continue;
        }
        std::wstring wide(static_cast<std::size_t>(needed), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, glyph.data(),
                            static_cast<int>(glyph.size()), wide.data(),
                            needed);
        result.push_back(std::move(wide));
    }
    return result;
}

void AppendFrameAnsiWide(std::wstring& out, const AsciiFrame& frame,
                         const std::vector<std::wstring>& glyphs, bool color) {
    if (frame.Empty() || glyphs.empty()) {
        return;
    }

    for (int row = 0; row < frame.rows; ++row) {
        // Find the last cell that is not a space, so trailing blanks are not
        // sent to the terminal.
        int lastCol = frame.cols - 1;
        while (lastCol >= 0) {
            const AsciiCell& cell =
                frame.cells[static_cast<std::size_t>(row) * frame.cols +
                            lastCol];
            if (cell.glyph < glyphs.size() && glyphs[cell.glyph] != L" ") {
                break;
            }
            --lastCol;
        }

        std::uint8_t currentR = 0;
        std::uint8_t currentG = 0;
        std::uint8_t currentB = 0;
        bool haveColor = false;

        for (int col = 0; col <= lastCol; ++col) {
            const AsciiCell& cell =
                frame.cells[static_cast<std::size_t>(row) * frame.cols + col];
            if (cell.glyph >= glyphs.size()) {
                out.push_back(L' ');
                continue;
            }
            const std::wstring& glyph = glyphs[cell.glyph];
            if (color && glyph != L" ") {
                if (!haveColor || cell.r != currentR || cell.g != currentG ||
                    cell.b != currentB) {
                    AppendColorCode(out, cell.r, cell.g, cell.b);
                    currentR = cell.r;
                    currentG = cell.g;
                    currentB = cell.b;
                    haveColor = true;
                }
            }
            out += glyph;
        }

        if (color && haveColor) {
            out += L"\x1b[0m";
        }
        if (row + 1 < frame.rows) {
            out += L"\r\n";
        }
    }
}

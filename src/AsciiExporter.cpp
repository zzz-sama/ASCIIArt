#include "AsciiExporter.h"

#include "Localization.h"
#include "Util.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>

namespace {

std::string HtmlEscape(const std::wstring& text) {
    const std::string utf8 = util::ToUtf8(text);
    std::string result;
    result.reserve(utf8.size());
    for (const char ch : utf8) {
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

bool WriteUtf8File(const std::filesystem::path& path, const std::string& text,
                   std::wstring& error) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        error = Tr(L"Cannot write the file:\n  ", L"无法写入文件：\n  ") +
                path.wstring() +
                Tr(L"\nCheck that the folder exists, the file is not in use, "
                   L"and you have write permission.",
                   L"\n请检查目录是否存在、文件是否被占用，以及写入权限。");
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        error = Tr(L"An error occurred while writing the file:\n  ",
                   L"写入文件时发生错误：\n  ") +
                path.wstring();
        return false;
    }
    return true;
}

}  // namespace

bool SaveAsciiFile(const std::wstring& path, const AsciiFrame& frame,
                   const Charset& charset, bool color,
                   const std::wstring& sourceName, std::wstring& error) {
    if (frame.Empty()) {
        error = Tr(L"There is no character art to save.",
                   L"没有可保存的字符画。");
        return false;
    }
    // Only a physical overflow guard remains; there is no size cap.
    const std::size_t maxReserve =
        (std::numeric_limits<std::size_t>::max() - 2048u) / 18u;
    if (frame.cells.size() > maxReserve) {
        error = Tr(L"The character art is too large to save.",
                   L"字符画过大，无法保存。");
        return false;
    }

    std::filesystem::path outputPath(path);
    std::wstring extension = util::ToLower(outputPath.extension().wstring());
    if (extension.empty()) {
        outputPath += L".txt";
        extension = L".txt";
    }

    const bool html = extension == L".html" || extension == L".htm";
    if (html) {
        std::string document;
        document.reserve(static_cast<std::size_t>(frame.cells.size()) * 18u +
                         2048u);
        const std::wstring title =
            sourceName.empty() ? L"ASCIIArt" : sourceName + L" - ASCIIArt";
        document += "<!doctype html>\n";
        document += Tr8("<html lang=\"en\">\n", "<html lang=\"zh-CN\">\n");
        document += "<head>\n";
        document += "<meta charset=\"utf-8\">\n";
        document += "<meta name=\"viewport\" content=\"width=device-width, "
                    "initial-scale=1\">\n";
        document += "<title>" + HtmlEscape(title) + "</title>\n";
        document +=
            "<style>\n"
            "html,body{margin:0;padding:24px;background:#0b0e14;color:#e6e6e6;}\n"
            "body{font-family:'Cascadia Mono','Consolas','Courier New',"
            "monospace;}\n"
            ".meta{color:#7d8590;font-size:13px;margin-bottom:14px;"
            "font-family:'Segoe UI','Microsoft YaHei',sans-serif;}\n"
            "pre{margin:0;font-family:inherit;font-size:14px;line-height:1.0;"
            "letter-spacing:0;white-space:pre;}\n"
            "</style>\n</head>\n<body>\n";
        document += "<div class=\"meta\">ASCIIArt &middot; " +
                    std::to_string(frame.cols) + " &times; " +
                    std::to_string(frame.rows) + " &middot; " +
                    HtmlEscape(charset.DisplayName());
        if (!sourceName.empty()) {
            document += " &middot; " + HtmlEscape(sourceName);
        }
        document += "</div>\n<pre>";
        document += FrameToHtmlFragment(frame, charset, color);
        document += "</pre>\n</body>\n</html>\n";

        if (!WriteUtf8File(outputPath, document, error)) {
            return false;
        }
    } else {
        const std::string text = FrameToText(frame, charset);
        if (!WriteUtf8File(outputPath, text, error)) {
            return false;
        }
    }

    return true;
}

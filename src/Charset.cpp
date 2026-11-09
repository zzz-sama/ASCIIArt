#include "Charset.h"

#include "Localization.h"
#include "Util.h"

#include <cwchar>
#include <utility>

namespace {

std::vector<CharsetPreset> BuildPresets() {
    return {
        {L"standard", L"Standard (10 levels)", L"标准（10 级）", L" .:-=+*#%@"},
        {L"detailed", L"Detailed (70 levels)", L"精细（70 级）",
         L" .'`^\",:;Il!i><~+_-?][}{1)(|\\/tfjrxnuvczXYUJCLQ0OZmwqpdbkhao*#MW&8%B@$"},
        {L"blocks", L"Blocks (5 levels)", L"方块（5 级）", L" ░▒▓█"},
        {L"minimal", L"Minimal (4 levels)", L"简约（4 级）", L" .#@"},
    };
}

// Splits UTF-16 text into Unicode code points, returning one UTF-8 glyph per
// code point. This keeps block characters like ░ usable as a charset.
bool SplitGlyphs(const std::wstring& text, std::vector<std::string>& glyphs,
                 std::wstring& error) {
    glyphs.clear();
    for (std::size_t i = 0; i < text.size();) {
        const wchar_t first = text[i];
        std::size_t length = 1;
        if (first >= 0xD800 && first <= 0xDBFF && i + 1 < text.size()) {
            const wchar_t second = text[i + 1];
            if (second >= 0xDC00 && second <= 0xDFFF) {
                length = 2;
            }
        } else if (first >= 0xDC00 && first <= 0xDFFF) {
            error = Tr(L"The charset contains an invalid Unicode surrogate.",
                       L"字符集中包含无效的 Unicode 代理项。");
            return false;
        }

        const std::wstring one = text.substr(i, length);
        const unsigned int codePoint =
            length == 1 ? static_cast<unsigned int>(first) : 0x10000u;
        if (codePoint < 0x20 || codePoint == 0x7F) {
            i += length;
            continue;  // control characters are not drawable
        }

        glyphs.push_back(util::ToUtf8(one));
        if (glyphs.size() > 255) {
            error = Tr(L"A charset supports at most 255 characters.",
                       L"字符集最多支持 255 个字符。");
            return false;
        }
        i += length;
    }

    if (glyphs.size() < 2) {
        error = Tr(L"A charset needs at least 2 visible characters.",
                   L"字符集至少需要 2 个可见字符。");
        return false;
    }
    return true;
}

}  // namespace

const std::wstring& Charset::DisplayName() const {
    return CurrentLanguage() == Language::Chinese ? nameZh : nameEn;
}

const std::vector<CharsetPreset>& CharsetPresets() {
    static const std::vector<CharsetPreset> presets = BuildPresets();
    return presets;
}

bool LoadCustomCharset(const std::wstring& chars, Charset& out,
                       std::wstring& error) {
    Charset result;
    if (!SplitGlyphs(chars, result.glyphs, error)) {
        return false;
    }
    const std::wstring count = std::to_wstring(result.glyphs.size());
    result.nameEn = L"Custom (" + count + L" levels)";
    result.nameZh = L"自定义（" + count + L" 级）";
    out = std::move(result);
    return true;
}

bool LoadCharset(const std::wstring& spec, Charset& out, std::wstring& error) {
    for (const auto& preset : CharsetPresets()) {
        if (util::EqualsIgnoreCase(spec, preset.id) ||
            util::EqualsIgnoreCase(spec, preset.labelEn) ||
            util::EqualsIgnoreCase(spec, preset.labelZh)) {
            if (!LoadCustomCharset(preset.chars, out, error)) {
                return false;
            }
            out.nameEn = preset.labelEn;
            out.nameZh = preset.labelZh;
            return true;
        }
    }
    // The spec is not a known preset; treat it as a literal charset.
    return LoadCustomCharset(spec, out, error);
}

std::string CharsetPreviewText(const Charset& charset) {
    std::string result;
    for (const auto& glyph : charset.glyphs) {
        if (!result.empty()) {
            result.push_back(' ');
        }
        result += glyph;
    }
    return result;
}

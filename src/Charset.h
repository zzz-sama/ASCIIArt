#pragma once

#include "Common.h"

#include <string>
#include <vector>

struct CharsetPreset {
    std::wstring id;       // stable id used on the command line
    std::wstring labelEn;  // English display name
    std::wstring labelZh;  // Chinese display name
    std::wstring chars;    // from dark to light
};

struct Charset {
    std::wstring nameEn = L"Standard (10 levels)";
    std::wstring nameZh = L"标准（10 级）";
    std::vector<std::string> glyphs;  // UTF-8, from dark to light

    std::size_t Size() const { return glyphs.size(); }
    bool Empty() const { return glyphs.empty(); }
    const std::wstring& DisplayName() const;
};

const std::vector<CharsetPreset>& CharsetPresets();

// Accepts a preset id ("standard", "detailed", "blocks", "minimal"),
// a preset label in either language, or a custom string of characters.
bool LoadCharset(const std::wstring& spec, Charset& out, std::wstring& error);

bool LoadCustomCharset(const std::wstring& chars, Charset& out,
                       std::wstring& error);

std::string CharsetPreviewText(const Charset& charset);

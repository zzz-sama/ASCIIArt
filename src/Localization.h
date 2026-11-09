#pragma once

#include <string>

// Interface language. The Windows UI language is used at startup; Chinese and
// English are supported, anything else falls back to English.
enum class Language {
    English,
    Chinese,
};

Language CurrentLanguage();
void SetLanguage(Language language);

// The Windows UI language: Chinese or English; anything else falls back to
// English.
Language SystemLanguage();

// Accepts "en", "english", "zh", "cn", "chinese" (case-insensitive).
bool ParseLanguage(const std::wstring& text, Language& language);

// Returns the string that matches the current language.
const wchar_t* Tr(const wchar_t* english, const wchar_t* chinese);

// Same as Tr() for UTF-8 string literals, for console output.
std::string Tr8(const char* english, const char* chinese);

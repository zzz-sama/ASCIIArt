#include "Localization.h"

#include "Util.h"

#include <windows.h>

#include <atomic>

namespace {

std::atomic<Language> g_language{Language::English};

}  // namespace

Language CurrentLanguage() { return g_language.load(); }

void SetLanguage(Language language) { g_language.store(language); }

Language SystemLanguage() {
    switch (PRIMARYLANGID(GetUserDefaultUILanguage())) {
        case LANG_CHINESE:
            return Language::Chinese;
        case LANG_ENGLISH:
            return Language::English;
        default:
            return Language::English;
    }
}

bool ParseLanguage(const std::wstring& text, Language& language) {
    const std::wstring value = util::ToLower(util::Trim(text));
    if (value == L"en" || value == L"eng" || value == L"english") {
        language = Language::English;
        return true;
    }
    if (value == L"zh" || value == L"cn" || value == L"chi" ||
        value == L"chinese") {
        language = Language::Chinese;
        return true;
    }
    return false;
}

const wchar_t* Tr(const wchar_t* english, const wchar_t* chinese) {
    return CurrentLanguage() == Language::Chinese ? chinese : english;
}

std::string Tr8(const char* english, const char* chinese) {
    return CurrentLanguage() == Language::Chinese ? chinese : english;
}

#include "FileDialog.h"

#include "Localization.h"

#include <windows.h>
#include <commdlg.h>

#include <string>
#include <vector>

namespace {

constexpr DWORD kBufferChars = 32768;

void AppendFilterPart(std::wstring& filter, const wchar_t* label,
                      const wchar_t* pattern) {
    filter += label;
    filter.push_back(L'\0');
    filter += pattern;
    filter.push_back(L'\0');
}

}  // namespace

bool PickMediaFile(std::wstring& path) {
    std::wstring filter;
    AppendFilterPart(filter, Tr(L"Images and videos", L"图片和视频"),
                     L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;"
                     L"*.webp;*.mp4;*.m4v;*.mov;*.avi;*.wmv;*.mkv;*.webm;"
                     L"*.mpg;*.mpeg;*.ts;*.m2ts;*.3gp");
    AppendFilterPart(filter, Tr(L"Image files", L"图片文件"),
                     L"*.png;*.jpg;*.jpeg;*.bmp;*.gif;*.tif;*.tiff;*.ico;"
                     L"*.webp");
    AppendFilterPart(filter, Tr(L"Video files", L"视频文件"),
                     L"*.mp4;*.m4v;*.mov;*.avi;*.wmv;*.mkv;*.webm;*.mpg;"
                     L"*.mpeg;*.ts;*.m2ts;*.3gp");
    AppendFilterPart(filter, Tr(L"All files", L"所有文件"), L"*.*");
    filter.push_back(L'\0');

    std::vector<wchar_t> buffer(kBufferChars, L'\0');
    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = kBufferChars;
    dialog.lpstrTitle = Tr(L"Choose an image or video to convert",
                           L"选择要转换的图片或视频");
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                   OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&dialog) == FALSE) {
        return false;
    }
    path.assign(buffer.data());
    return !path.empty();
}

bool PickSaveFile(const std::wstring& defaultName,
                  const std::wstring& defaultExt, std::wstring& path) {
    const bool html = _wcsicmp(defaultExt.c_str(), L"html") == 0 ||
                      _wcsicmp(defaultExt.c_str(), L"htm") == 0;

    std::wstring txtFirstFilter;
    AppendFilterPart(txtFirstFilter, Tr(L"Text files (*.txt)", L"文本文件 (*.txt)"),
                     L"*.txt");
    AppendFilterPart(txtFirstFilter,
                     Tr(L"Web pages (*.html)", L"网页文件 (*.html)"),
                     L"*.html;*.htm");
    AppendFilterPart(txtFirstFilter, Tr(L"All files", L"所有文件"), L"*.*");
    txtFirstFilter.push_back(L'\0');

    std::wstring htmlFirstFilter;
    AppendFilterPart(htmlFirstFilter,
                     Tr(L"Web pages (*.html)", L"网页文件 (*.html)"),
                     L"*.html;*.htm");
    AppendFilterPart(htmlFirstFilter,
                     Tr(L"Text files (*.txt)", L"文本文件 (*.txt)"), L"*.txt");
    AppendFilterPart(htmlFirstFilter, Tr(L"All files", L"所有文件"), L"*.*");
    htmlFirstFilter.push_back(L'\0');

    std::vector<wchar_t> buffer(kBufferChars, L'\0');
    const std::wstring initial = defaultName.empty() ? L"ascii_art.txt"
                                                     : defaultName;
    wcsncpy_s(buffer.data(), kBufferChars, initial.c_str(), _TRUNCATE);

    OPENFILENAMEW dialog{};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = GetConsoleWindow();
    dialog.lpstrFilter = html ? htmlFirstFilter.c_str() : txtFirstFilter.c_str();
    dialog.nFilterIndex = 1;
    dialog.lpstrFile = buffer.data();
    dialog.nMaxFile = kBufferChars;
    dialog.lpstrDefExt = html ? L"html" : L"txt";
    dialog.lpstrTitle = Tr(L"Save character art", L"保存字符画");
    dialog.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_EXPLORER |
                   OFN_NOCHANGEDIR;

    if (GetSaveFileNameW(&dialog) == FALSE) {
        return false;
    }
    path.assign(buffer.data());
    return !path.empty();
}

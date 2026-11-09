#include "Util.h"

#include <algorithm>
#include <cwctype>
#include <filesystem>
#include <iomanip>
#include <limits>
#include <sstream>

namespace util {

namespace {

// Win32 conversion APIs take an int length. Clamp instead of truncating to a
// negative value, which used to make the conversion fail silently.
int ClampedLength(std::size_t size) {
    return static_cast<int>(
        std::min<std::size_t>(size,
                              static_cast<std::size_t>(
                                  std::numeric_limits<int>::max())));
}

}  // namespace

std::string ToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int length = ClampedLength(text.size());
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), length,
                                         nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), length, result.data(), size,
                        nullptr, nullptr);
    return result;
}

std::wstring ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int length = ClampedLength(text.size());
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), length,
                                         nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), length, result.data(), size);
    return result;
}

std::wstring FormatHResult(HRESULT hr) {
    wchar_t* buffer = nullptr;
    const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                        FORMAT_MESSAGE_FROM_SYSTEM |
                        FORMAT_MESSAGE_IGNORE_INSERTS;
    const DWORD length = FormatMessageW(
        flags, nullptr, static_cast<DWORD>(hr), 0,
        reinterpret_cast<wchar_t*>(&buffer), 0, nullptr);

    std::wostringstream stream;
    stream << L"0x" << std::hex << std::uppercase << std::setw(8)
           << std::setfill(L'0') << static_cast<unsigned long>(hr);
    if (length != 0 && buffer != nullptr) {
        std::wstring message(buffer, length);
        while (!message.empty() &&
               (message.back() == L'\r' || message.back() == L'\n' ||
                message.back() == L' ')) {
            message.pop_back();
        }
        if (!message.empty()) {
            stream << L" (" << message << L")";
        }
    }
    if (buffer != nullptr) {
        LocalFree(buffer);
    }
    return stream.str();
}

std::string FormatHResultUtf8(HRESULT hr) {
    return ToUtf8(FormatHResult(hr));
}

std::wstring Trim(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(L" \t\r\n");
    return text.substr(first, last - first + 1);
}

std::wstring ToLower(std::wstring text) {
    std::transform(text.begin(), text.end(), text.begin(), [](wchar_t ch) {
        return static_cast<wchar_t>(std::towlower(ch));
    });
    return text;
}

bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b) {
    return ToLower(a) == ToLower(b);
}

std::wstring FileNameOf(const std::wstring& path) {
    return std::filesystem::path(path).filename().wstring();
}

std::wstring StemOf(const std::wstring& path) {
    return std::filesystem::path(path).stem().wstring();
}

std::wstring ExtensionOf(const std::wstring& path) {
    return ToLower(std::filesystem::path(path).extension().wstring());
}

std::string Number(double value, int decimals) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(decimals) << value;
    return stream.str();
}

std::string PadRight(const std::string& text, std::size_t width) {
    std::string result = text;
    while (result.size() < width) {
        result.push_back(' ');
    }
    return result;
}

}  // namespace util

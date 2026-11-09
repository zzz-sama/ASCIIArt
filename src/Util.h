#pragma once

#include "Common.h"

#include <string>

namespace util {

std::string ToUtf8(const std::wstring& text);
std::wstring ToWide(const std::string& text);

std::wstring FormatHResult(HRESULT hr);
std::string FormatHResultUtf8(HRESULT hr);

std::wstring Trim(const std::wstring& text);
std::wstring ToLower(std::wstring text);
bool EqualsIgnoreCase(const std::wstring& a, const std::wstring& b);

std::wstring FileNameOf(const std::wstring& path);
std::wstring StemOf(const std::wstring& path);
std::wstring ExtensionOf(const std::wstring& path);  // lower-case, including dot

std::string Number(double value, int decimals = 0);
std::string PadRight(const std::string& text, std::size_t width);

}  // namespace util

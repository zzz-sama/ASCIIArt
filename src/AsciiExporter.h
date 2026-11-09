#pragma once

#include "AsciiConverter.h"

#include <string>

// Saves plain UTF-8 text (.txt) or a standalone colored HTML page (.html).
// If path has no extension, ".txt" is appended.
bool SaveAsciiFile(const std::wstring& path, const AsciiFrame& frame,
                   const Charset& charset, bool color,
                   const std::wstring& sourceName, std::wstring& error);

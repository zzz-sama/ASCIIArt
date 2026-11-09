#pragma once

#include "Common.h"

#include <string>

// Decodes the first frame of an image with Windows Imaging Component (WIC).
// Supports the formats installed with Windows: PNG, JPEG, BMP, GIF, TIFF, ICO...
bool LoadImageFile(const std::wstring& path, RgbaImage& image,
                   std::wstring& error);

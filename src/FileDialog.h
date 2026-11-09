#pragma once

#include <string>

bool PickMediaFile(std::wstring& path);
bool PickSaveFile(const std::wstring& defaultName, const std::wstring& defaultExt,
                  std::wstring& path);

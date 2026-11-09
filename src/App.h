#pragma once

#include "AsciiConverter.h"

#include <string>

struct AppOptions {
    RenderOptions render;
    bool noDisplay = false;
    bool fitConsole = true;  // temporarily shrink font / enlarge window if needed
    std::wstring savePath;

    AppOptions();
};

void PrintCommandLineHelp();

class App {
public:
    explicit App(AppOptions options);

    int RunFile(const std::wstring& path);
    void RunInteractive();

private:
    enum class MediaKind { Image, Video, Unknown };

    MediaKind DetectKind(const std::wstring& path) const;
    int ProcessFile(const std::wstring& path, bool interactive);

    bool ProcessImage(const std::wstring& path, bool interactive);
    bool ProcessVideo(const std::wstring& path, bool interactive);

    void DrawMainMenu() const;
    void ShowSettings();
    void ShowHelp();
    void ShowCharsetMenu();

    void ReportError(const std::wstring& message, bool pause) const;
    bool PromptSaveImage(const AsciiFrame& frame, const Charset& charset,
                         const std::wstring& sourcePath) const;

    AppOptions options_;
};

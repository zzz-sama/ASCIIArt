#include "App.h"

#include "Charset.h"
#include "Console.h"
#include "Localization.h"
#include "Util.h"

#include <mfapi.h>
#include <objbase.h>

#include <cerrno>
#include <cmath>
#include <cwchar>
#include <exception>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace {

struct CommandLine {
    AppOptions app;
    std::wstring input;
    bool help = false;
    bool valid = true;
    std::wstring error;
    int colorMode = -1;  // -1 = automatic, 0 = off, 1 = on
};

int TakeValue(const std::wstring& arg, const std::wstring& name, int argc,
              wchar_t** argv, int& index, std::wstring& value) {
    // 0 = no match, 1 = value captured, -1 = missing value
    if (arg == name) {
        if (index + 1 >= argc) {
            return -1;
        }
        value = argv[++index];
        return value.empty() ? -1 : 1;
    }
    const std::wstring withEquals = name + L"=";
    if (arg.size() > withEquals.size() &&
        arg.compare(0, withEquals.size(), withEquals) == 0) {
        value = arg.substr(withEquals.size());
        return value.empty() ? -1 : 1;
    }
    return 0;
}

bool ParseIntValue(const std::wstring& text, int& value) {
    wchar_t* end = nullptr;
    errno = 0;
    const long parsed = std::wcstol(text.c_str(), &end, 10);
    if (errno != 0 || end == text.c_str() || *end != L'\0') {
        return false;
    }
    value = static_cast<int>(parsed);
    return true;
}

bool ParseDoubleValue(const std::wstring& text, double& value) {
    wchar_t* end = nullptr;
    errno = 0;
    const double parsed = std::wcstod(text.c_str(), &end);
    if (errno != 0 || end == text.c_str() || *end != L'\0' ||
        !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

bool ParseResolutionValue(const std::wstring& text, int& width, int& height) {
    const std::wstring value = util::Trim(text);
    if (value.empty()) {
        return false;
    }

    width = 0;
    height = 0;
    const std::size_t separator = value.find_first_of(L"xX*");
    if (separator == std::wstring::npos) {
        if (!ParseIntValue(value, width)) {
            return false;
        }
    } else {
        const std::wstring left = util::Trim(value.substr(0, separator));
        const std::wstring right = util::Trim(value.substr(separator + 1));
        if (!left.empty() && !ParseIntValue(left, width)) {
            return false;
        }
        if (!right.empty() && !ParseIntValue(right, height)) {
            return false;
        }
    }

    return width >= 0 && height >= 0;
}

// Sets the language from --lang before anything else is parsed, so that even
// option errors are reported in the language the user asked for.
void ApplyLanguagePrescan(int argc, wchar_t** argv) {
    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];
        std::wstring value;
        if (arg == L"--lang") {
            if (i + 1 < argc) {
                value = argv[++i];
            }
        } else if (arg.size() > 7 && arg.compare(0, 7, L"--lang=") == 0) {
            value = arg.substr(7);
        } else {
            continue;
        }
        Language language{};
        if (ParseLanguage(value, language)) {
            SetLanguage(language);
        }
    }
}

CommandLine ParseCommandLine(int argc, wchar_t** argv) {
    CommandLine result;

    for (int i = 1; i < argc; ++i) {
        std::wstring arg = argv[i];

        if (arg == L"-w") {
            arg = L"--width";
        } else if (arg == L"-c") {
            arg = L"--color";
        } else if (arg == L"-r") {
            arg = L"--resolution";
        } else if (arg == L"-i") {
            arg = L"--invert";
        } else if (arg == L"-a") {
            arg = L"--aspect";
        } else if (arg == L"-g") {
            arg = L"--gamma";
        } else if (arg == L"-f") {
            arg = L"--fps";
        } else if (arg == L"-l") {
            arg = L"--loop";
        } else if (arg == L"-s") {
            arg = L"--save";
        } else if (arg == L"-h" || arg == L"-?" || arg == L"/?") {
            arg = L"--help";
        }

        if (arg == L"--help") {
            result.help = true;
            return result;
        }
        if (arg == L"--") {
            for (int j = i + 1; j < argc; ++j) {
                if (!result.input.empty()) {
                    result.valid = false;
                    result.error = Tr(L"Only one input file may be specified.",
                                      L"只能指定一个输入文件。");
                    return result;
                }
                result.input = argv[j];
            }
            break;
        }

        if (arg == L"--color") {
            result.colorMode = 1;
            continue;
        }
        if (arg == L"--no-color") {
            result.colorMode = 0;
            continue;
        }
        if (arg == L"--invert") {
            result.app.render.invert = true;
            continue;
        }
        if (arg == L"--loop") {
            result.app.render.loop = true;
            continue;
        }
        if (arg == L"--no-display") {
            result.app.noDisplay = true;
            continue;
        }
        if (arg == L"--fit") {
            result.app.fitConsole = true;
            continue;
        }
        if (arg == L"--no-fit") {
            result.app.fitConsole = false;
            continue;
        }

        std::wstring value;
        int take = TakeValue(arg, L"--lang", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--lang needs a value (en or zh).",
                                  L"--lang 需要一个值（en 或 zh）。");
                return result;
            }
            Language language{};
            if (!ParseLanguage(value, language)) {
                result.valid = false;
                result.error = Tr(L"Unsupported language. Use en or zh.",
                                  L"不支持的语言。请使用 en 或 zh。");
                return result;
            }
            SetLanguage(language);
            continue;
        }

        take = TakeValue(arg, L"--width", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--width needs a number.",
                                  L"--width 需要一个数值。");
                return result;
            }
            int width = 0;
            if (!ParseIntValue(value, width) || width < 0) {
                result.valid = false;
                result.error = Tr(
                    L"Width must be a non-negative integer.",
                    L"宽度必须是非负整数。");
                return result;
            }
            result.app.render.width = width;
            continue;
        }

        take = TakeValue(arg, L"--height", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--height needs a number.",
                                  L"--height 需要一个数值。");
                return result;
            }
            int height = 0;
            if (!ParseIntValue(value, height) || height < 0) {
                result.valid = false;
                result.error = Tr(
                    L"Height must be a non-negative integer.",
                    L"高度必须是非负整数。");
                return result;
            }
            result.app.render.height = height;
            continue;
        }

        take = TakeValue(arg, L"--resolution", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(
                    L"--resolution needs a value in the form WxH.",
                    L"--resolution 需要 宽x高 形式的数值。");
                return result;
            }
            int width = 0;
            int height = 0;
            if (!ParseResolutionValue(value, width, height)) {
                result.valid = false;
                result.error = Tr(
                    L"Invalid resolution format. Example: --resolution 120x40.\n"
                    L"Width and height must be non-negative integers.",
                    L"分辨率格式无效，示例：--resolution 120x40。\n"
                    L"宽度和高度必须是非负整数。");
                return result;
            }
            result.app.render.width = width;
            result.app.render.height = height;
            continue;
        }

        take = TakeValue(arg, L"--fps", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--fps needs a number.",
                                  L"--fps 需要一个数值。");
                return result;
            }
            int fps = 0;
            if (!ParseIntValue(value, fps) || fps < 1 || fps > 120) {
                result.valid = false;
                result.error = Tr(
                    L"Frame rate must be an integer between 1 and 120.",
                    L"帧率必须是 1-120 之间的整数。");
                return result;
            }
            result.app.render.fps = fps;
            continue;
        }

        take = TakeValue(arg, L"--aspect", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--aspect needs a number.",
                                  L"--aspect 需要一个数值。");
                return result;
            }
            double aspect = 0.0;
            if (!ParseDoubleValue(value, aspect) || aspect < 0.2 ||
                aspect > 1.0) {
                result.valid = false;
                result.error = Tr(
                    L"Aspect ratio must be between 0.20 and 1.00.",
                    L"宽高比必须在 0.20-1.00 之间。");
                return result;
            }
            result.app.render.aspect = aspect;
            continue;
        }

        take = TakeValue(arg, L"--gamma", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--gamma needs a number.",
                                  L"--gamma 需要一个数值。");
                return result;
            }
            double gamma = 0.0;
            if (!ParseDoubleValue(value, gamma) || gamma < 0.1 || gamma > 4.0) {
                result.valid = false;
                result.error = Tr(L"Gamma must be between 0.10 and 4.00.",
                                  L"伽马值必须在 0.10-4.00 之间。");
                return result;
            }
            result.app.render.gamma = gamma;
            continue;
        }

        take = TakeValue(arg, L"--charset", argc, argv, i, value);
        if (take == 0) {
            take = TakeValue(arg, L"--preset", argc, argv, i, value);
        }
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--charset / --preset needs a charset.",
                                  L"--charset / --preset 需要一个字符集。");
                return result;
            }
            Charset charset;
            std::wstring error;
            if (!LoadCharset(value, charset, error)) {
                result.valid = false;
                result.error = error;
                return result;
            }
            result.app.render.charset = std::move(charset);
            continue;
        }

        take = TakeValue(arg, L"--save", argc, argv, i, value);
        if (take != 0) {
            if (take < 0) {
                result.valid = false;
                result.error = Tr(L"--save needs an output file name.",
                                  L"--save 需要一个输出文件名。");
                return result;
            }
            result.app.savePath = value;
            continue;
        }

        if (!arg.empty() && arg[0] == L'-') {
            result.valid = false;
            result.error = Tr(L"Unknown option: ", L"未知选项：") + arg;
            return result;
        }

        if (!result.input.empty()) {
            result.valid = false;
            result.error = Tr(L"Only one input file may be specified.",
                              L"只能指定一个输入文件。");
            return result;
        }
        result.input = arg;
    }

    if (!result.help) {
        if (result.app.noDisplay && result.app.savePath.empty()) {
            result.valid = false;
            result.error = Tr(L"--no-display must be used together with --save.",
                              L"--no-display 必须与 --save 一起使用。");
        } else if (result.input.empty() &&
                   (!result.app.savePath.empty() || result.app.noDisplay)) {
            result.valid = false;
            result.error = Tr(
                L"An input file is required when using --save or --no-display.",
                L"使用 --save 或 --no-display 时必须指定输入文件。");
        }
    }
    return result;
}

class RuntimeGuard {
public:
    ~RuntimeGuard() {
        if (mfStarted) {
            MFShutdown();
        }
        console::Shutdown();
        if (comInitialized) {
            CoUninitialize();
        }
    }

    bool comInitialized = false;
    bool mfStarted = false;
};

}  // namespace

int wmain(int argc, wchar_t** argv) {
    RuntimeGuard runtime;

    try {
        SetLanguage(SystemLanguage());
        ApplyLanguagePrescan(argc, argv);

        HRESULT comResult = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (SUCCEEDED(comResult)) {
            runtime.comInitialized = true;
        } else if (comResult != RPC_E_CHANGED_MODE) {
            console::Initialize();
            console::Err(Tr8("  Error: cannot initialize COM: ",
                             "  错误: 无法初始化 COM：") +
                         util::FormatHResultUtf8(comResult) + "\n");
            return 1;
        }

        console::Initialize();
        console::SetTitle(L"ASCIIArt");

        const HRESULT mfResult = MFStartup(MF_VERSION);
        if (SUCCEEDED(mfResult)) {
            runtime.mfStarted = true;
        } else {
            console::Err(
                Tr8("  Warning: Media Foundation failed to initialize, video "
                    "playback will be unavailable: ",
                    "  警告: Media Foundation 初始化失败，视频功能将不可用：") +
                util::FormatHResultUtf8(mfResult) + "\n");
        }

        CommandLine commandLine = ParseCommandLine(argc, argv);
        if (commandLine.help) {
            PrintCommandLineHelp();
            return 0;
        }
        if (!commandLine.valid) {
            console::Err(Tr8("\n  Error: ", "\n  错误: ") +
                         util::ToUtf8(commandLine.error) + "\n");
            console::Err(Tr8("  Run ASCIIArt --help for help.\n\n",
                             "  使用 ASCIIArt --help 查看帮助。\n\n"));
            return 2;
        }

        // Automatic color is on by default. The terminal only receives ANSI
        // colors when the console supports them; a saved HTML file can still
        // be colored even if stdout is redirected.
        commandLine.app.render.color = commandLine.colorMode != 0;

        App app(std::move(commandLine.app));
        if (!commandLine.input.empty()) {
            return app.RunFile(commandLine.input);
        }

        app.RunInteractive();
        return 0;
    } catch (const std::bad_alloc&) {
        console::Initialize();
        console::Err(Tr8(
            "\n  Error: not enough memory for the requested size. Try a "
            "smaller output size.\n",
            "\n  错误: 内存不足，无法生成该尺寸的字符画。请尝试减小输出尺寸。\n"));
        return 1;
    } catch (const std::exception& ex) {
        // Most likely std::length_error or std::bad_alloc from an allocation
        // for an extremely large frame. Report it instead of letting the
        // process die with an unhandled-exception dialog.
        console::Initialize();
        console::Err(Tr8("\n  Error: ", "\n  错误: ") + std::string(ex.what()) +
                     "\n");
        return 1;
    } catch (...) {
        console::Initialize();
        console::Err(Tr8("\n  Error: an unknown exception occurred.\n",
                         "\n  错误: 发生未知异常。\n"));
        return 1;
    }
}

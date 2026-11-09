cmake_minimum_required(VERSION 3.20)

project(ASCIIArt LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(ASCIIArt
    src/main.cpp
    src/App.cpp
    src/AsciiConverter.cpp
    src/AudioPlayer.cpp
    src/AsciiExporter.cpp
    src/Charset.cpp
    src/Console.cpp
    src/FileDialog.cpp
    src/ImageLoader.cpp
    src/Localization.cpp
    src/VideoPlayer.cpp
    src/Util.cpp
)

target_include_directories(ASCIIArt PRIVATE src)

target_compile_definitions(ASCIIArt PRIVATE
    UNICODE
    _UNICODE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    _CRT_SECURE_NO_WARNINGS
)

if (MSVC)
    target_compile_options(ASCIIArt PRIVATE /W4 /utf-8 /permissive- /EHsc)
    target_compile_options(ASCIIArt PRIVATE $<$<CONFIG:Release>:/O2>)
endif()

target_link_libraries(ASCIIArt PRIVATE
    ole32
    windowscodecs
    mfplat
    mfreadwrite
    mfuuid
    comdlg32
    user32
    shlwapi
    xaudio2
    winmm
)

# Statically link the C++ runtime so the produced EXE does not need the
# Visual C++ redistributable installed separately.
if (MSVC)
    set_property(TARGET ASCIIArt PROPERTY MSVC_RUNTIME_LIBRARY
        "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

set_property(DIRECTORY PROPERTY VS_STARTUP_PROJECT ASCIIArt)

# Copy the READMEs next to the executable for convenience.
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    add_custom_command(TARGET ASCIIArt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
            "$<TARGET_FILE_DIR:ASCIIArt>/README.md"
    )
endif()
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/README.zh-CN.md")
    add_custom_command(TARGET ASCIIArt POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_CURRENT_SOURCE_DIR}/README.zh-CN.md"
            "$<TARGET_FILE_DIR:ASCIIArt>/README.zh-CN.md"
    )
endif()

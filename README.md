# ASCIIArt

**English** | [简体中文](README.zh-CN.md)

A Windows console program written in Visual C++ that turns an image or video into ASCII art.

- **Images** are decoded with the built-in Windows **WIC** decoder; they can be shown in the console or saved as TXT / HTML.
- **Videos** are decoded with the built-in Windows **Media Foundation**, preferring NV12 with direct downsampling, and played in the console as real-time ASCII animation.
- **Audio**: the video's audio track is played through **XAudio2** in sync with the picture; pausing also pauses the audio.
- Pure Windows APIs: no OpenCV, FFmpeg, or any third-party library.
- 24-bit true-color ANSI output, Unicode charsets, and automatic window fitting.
- Bilingual interface: follows the system UI language at startup (Chinese for Chinese, English for English, English fallback for anything else); switch from the main menu or with `--lang zh` / `--lang en`.
- Clearing the screen also clears the scrollback buffer, so scrolling up never reveals earlier ASCII output.
- Custom character-grid resolution (columns × rows) with no size cap (e.g. `10000x0` fixes only the width); very large sizes show a notice when generating/saving that they may take a long time and a lot of memory.
- Large previews automatically shrink the console font and enlarge the window; in Windows Terminal it guides you through manual scaling.

```
  ==================================================
       ASCIIArt  -  image / video  ->  ASCII art
  ==================================================

  1. Choose an image and convert
  2. Choose a video and play
  3. Conversion settings
  4. Help
  5. Switch language (中文)
  0. Exit
```

## Requirements

| Item | Requirement |
| --- | --- |
| OS | Windows 10 / 11 (64-bit) |
| Compiler | Visual Studio 2022 or 2026 with "Desktop development with C++" |
| SDK | Windows 10 / 11 SDK |
| Build tool | CMake 3.20+ (bundled with Visual Studio) |
| Terminal | Windows Terminal or the default console; ANSI escape support required |

The program links the C++ runtime statically, so the EXE does not need a separate Visual C++ runtime installation.

## Build

Run this in the project directory:

```bat
build.bat
```

The script locates the CMake bundled with Visual Studio and produces an x64 Release build:

```
build\Release\ASCIIArt.exe
```

You can also build manually:

```bat
cmake -S . -B build -A x64
cmake --build build --config Release --parallel
```

To use the Visual Studio GUI:

1. Run `build.bat` once (or the CMake commands above).
2. Open `build\ASCIIArt.slnx` (Visual Studio 2026) or `build\ASCIIArt.sln` (Visual Studio 2022).
3. Set `ASCIIArt` as the startup project and press `Ctrl+F5`.

## Quick start

Double-click `run.bat`, or run:

```bat
build\Release\ASCIIArt.exe
```

From the interactive menu:

1. Choose an image: file dialog → convert → ask whether to save as TXT / HTML.
2. Choose a video: file dialog → play in the console.
3. Conversion settings: output size, color, invert, charset, aspect ratio, video frame rate, large-resolution fitting.
4. Help: command-line help and interactive tips.
5. Switch language: toggle between English and Chinese; the UI refreshes immediately.

You can also drag an image or video onto `run.bat`; the script pauses at the end so you can inspect the result.

## Command-line usage

```text
ASCIIArt                              start the interactive menu
ASCIIArt <image or video>             convert / play directly
ASCIIArt <file> [options]             convert / play with options
```

### Options

| Option | Description |
| --- | --- |
| `-w, --width <n>` | Target width in character columns. `0` or omitted = auto (default 100); the program tries to fit oversized output automatically |
| `--height <n>` | Target height in character rows. Omitted or `0` = derived from width and aspect ratio |
| `-r, --resolution <w>x<h>` | Fixed character-grid resolution, e.g. `120x40`; also `120x0` (width only) and `0x40` (height only) |
| `-c, --color` | Use 24-bit true color |
| `--no-color` | Disable color |
| `-i, --invert` | Invert brightness |
| `-a, --aspect <x>` | Character aspect ratio, default `0.50`, range `0.20-1.00` |
| `-g, --gamma <x>` | Gamma correction, default `1.0`; `>1` darker, `<1` brighter |
| `-f, --fps <n>` | Video frame-rate cap, default 24, range `1-120` |
| `-l, --loop` | Loop the video |
| `-s, --save <file>` | Save image art; `.txt` = plain text, `.html` = colored web page |
| `--no-display` | Save only; do not show image art in the console |
| `--fit` | Shrink the font / enlarge the window for large resolutions (default) |
| `--no-fit` | Never change the font or window |
| `--charset <chars>` | Custom charset (dark to light), or a preset name |
| `--preset <name>` | Charset preset: `standard` / `detailed` / `blocks` / `minimal` |
| `--lang <en\|zh>` | Interface language; defaults to the system UI language, falling back to `en` for other languages |
| `-h, --help` | Show help (in the current language) |

### Examples

```bat
:: Interactive menu (interface language follows the system)
ASCIIArt

:: Start with the Chinese interface
ASCIIArt --lang zh

:: Convert an image, width 120, color output
ASCIIArt photo.jpg -w 120 -c

:: Fixed output of 120 columns x 50 rows
ASCIIArt photo.jpg --resolution 120x50 -c

:: 2560 columns x 1440 rows (saving is not limited by the window)
ASCIIArt photo.jpg --resolution 2560x1440 --no-display --save big.txt

:: Fix only the height at 40 rows; width follows the image aspect ratio
ASCIIArt photo.jpg --height 40

:: Save as plain text
ASCIIArt photo.png --no-color --save art.txt

:: Save as a colored web page (without preview)
ASCIIArt logo.png --preset blocks --no-display --save logo.html

:: Play a video with a width and frame-rate cap
ASCIIArt clip.mp4 -w 100 -f 30

:: Loop playback
ASCIIArt clip.mp4 -l
```

### About output resolution

"Resolution" here means the character-grid **columns × rows**, not pixel resolution. For example:

```bat
ASCIIArt photo.png --resolution 120x50 --no-display --save art.txt
```

produces exactly 120 columns and 50 rows (at most 120 characters per line, 50 lines total in the saved file).

- Specifying both `--width` and `--height` is equivalent to `--resolution WxH`.
- Fixing both columns and rows stretches/squeezes the picture to the target ratio; specify only one side to preserve the original aspect ratio.
- With only `--width`, the height is derived from the image aspect ratio and the character aspect ratio, and large resolutions are fitted automatically.
- With only `--height`, the width is derived automatically and fitting is attempted as well.
- If automatic and manual fitting both fail, the preview shrinks to the window size and the "Grid" line shows the target and actual sizes.
- When a fixed resolution is larger than the console window, the program automatically tries to:
  1. maximize the console window;
  2. temporarily shrink the console font if necessary (down to about 4×6 pixels);
  3. resize the window so the complete art fits.
- In the classic console (conhost), the window is locked to the size the grid was computed for during the preview/playback (it cannot be resized by dragging), and the original font, window size, and position are restored after the preview or playback ends.
- Windows Terminal does not allow the program to change the font. The program asks you to press `Ctrl+-` to shrink the terminal font or enlarge the window, then press Enter to retry (retries are unlimited); type `s` to skip the preview and save only, or `q` to cancel. Press `Ctrl+0` after the preview to restore the font.
- If the art exactly fills the window, the program enters **full-screen art mode**: the file header / playback status line is hidden and the title bar shows the size and key hints; press any key to return for images.
- If it still does not fit, type `s` to skip the preview and render/save at the target size, or `q` to cancel; videos offer retry or cancel only.
- Output dimensions are not capped; from 4,000,000 characters onward, a notice is shown before generating/saving that it may take a long time and a lot of memory.
- Use `--no-fit` to disable automatic font/window adjustment and keep the original error behavior.
- Clearing the screen also clears the terminal scrollback buffer (ANSI `2J` + `3J`), so scrolling up never reveals previous art; it also clears history from before the program started in the same terminal.

> 2560×1440 character grids (3,686,400 characters) are supported. Saved files are not limited by the window; console preview requires the terminal to actually provide at least 2560 columns × 1440 rows at the current font (full-screen mode).

## Charset presets

Characters are ordered from "dark" to "light":

| Name | Characters |
| --- | --- |
| `standard` (default) | ` .:-=+*#%@` |
| `blocks` | ` ░▒▓█` |
| `minimal` | ` .#@` |
| `detailed` | About 70 levels of ASCII characters, suited to detailed images |

Custom examples:

```bat
ASCIIArt photo.png --charset " .oO@"
ASCIIArt photo.png --charset " .:-=+*#%@" -w 80
ASCIIArt photo.png --preset blocks
```

Notes:

- A charset needs at least 2 visible characters and supports at most 255 Unicode code points.
- Prefer monospaced single-column characters; double-width characters such as Chinese break row-width alignment.
- Quote charsets that contain spaces.

## Save formats

### TXT

Plain character art in UTF-8 without color, suitable for code comments, logs, or text files.

### HTML

A standalone web page with a black background, monospaced font, and 24-bit colors; open it directly in a browser. For example:

```bat
ASCIIArt photo.png -w 100 --save art.html
```

Without an extension, the file is saved as `.txt`.

## Video playback keys

| Key | Function |
| --- | --- |
| `Space` | Pause / resume |
| `-` / `+` | Adjust the character aspect ratio live (0.20-1.00; useful after zooming the terminal with Ctrl+wheel) |
| `Q` / `Esc` | Quit playback |

The video's audio track plays along with the picture. Pausing pauses the audio; resuming resumes it; loop playback restarts the audio from the beginning. If there is no audio track or no audio device, the video still plays silently.

## Implementation notes

| Module | Technology |
| --- | --- |
| Image decoding | Windows Imaging Component (WIC), normalized to 32bpp RGBA |
| Video decoding | Media Foundation Source Reader, NV12 preferred, RGB32 / RGB24 fallback |
| Video scaling | Downsampled directly to the 2× character grid while reading, skipping full-resolution RGBA conversion and copies |
| Audio decoding/playback | Media Foundation streaming decode to PCM, XAudio2 playback, ~16 MB buffer only |
| Scaling/sampling | Area sampling of source pixels per character cell |
| Luminance | `0.2126R + 0.7152G + 0.0722B`, with optional gamma / invert |
| Color output | ANSI `ESC[38;2;R;G;Bm` 24-bit true color |
| Terminal control | `WriteConsoleW` + VT escape sequences; UTF-8 and newline handling |
| File dialogs | Win32 common dialogs `GetOpenFileNameW` / `GetSaveFileNameW` |

Core files:

```text
src/
  main.cpp           command-line parsing and startup
  App.cpp            interactive menu, image/video flows
  AsciiConverter.cpp grid math, pixel sampling, charset mapping, ANSI/HTML output
  Charset.cpp        preset and custom Unicode charsets
  Localization.h/.cpp English/Chinese UI strings and language switching
  ImageLoader.cpp    WIC image decoding
  VideoPlayer.cpp    Media Foundation video decoding
  AudioPlayer.cpp    Media Foundation audio decoding + XAudio2 playback
  AsciiExporter.cpp  TXT / HTML file writing
  Console.cpp        console UTF-8, VT, input/output
  FileDialog.cpp     Win32 open/save file dialogs
  Util.cpp           string, path, and error-formatting helpers
```

## Performance optimizations

The playback pipeline has several layers of optimization for high-resolution video:

1. **NV12 decoder output**: frames are no longer converted to full-resolution RGB32, avoiding the most expensive color conversion.
2. **Downsampling while reading**: video frames are sampled directly to about 2× the character grid (e.g. 100×33 characters only processes 200×66 pixels), so full 4K frames are never copied.
3. **Lightweight rendering**: color conversion runs only on the small number of sampled pixels; gamma/invert use 256-entry lookup tables instead of per-cell `pow()` calls.
4. **Fast ANSI generation**: color escape sequences use hand-written integer formatting instead of `snprintf`, and the UTF-16 screen buffer is built directly, removing the per-frame "UTF-8 → newline normalization → UTF-16" copy.
5. **Background screen writer**: terminal writes overlap with decoding/rendering of the next frame (at most one frame queued), so decoding and terminal parsing hide each other's cost.
6. **Buffer reuse**: the screen buffer and the charset's wide-character table are allocated only once per playback.
7. **1 ms timer precision**: playback pacing is no longer throttled by the system's default ~15.6 ms timer granularity.

Local measurements (CPU cost per frame, 320×120 characters, 24-bit color):

| Stage | Before | After |
| --- | ---: | ---: |
| ASCII rendering | ~1.3 ms | ~1.0 ms |
| ANSI generation | ~9.9 ms | ~1.7 ms |
| UTF-8 → UTF-16 conversion | ~1.8 ms | 0 (built as UTF-16 directly) |
| Total | ~13.0 ms | ~2.7 ms |

Terminal-write time now overlaps with processing the next frame instead of adding up serially. Larger grids still make terminal parsing the bottleneck; lower `--width` or use `--no-color` if needed.

## Limitations and notes

- Playback depends on the decoders installed on the system. MP4 / H.264 / AVI usually work; MKV / WebM may require additional decoders.
- Audio requires an available playback device. Audio is streamed and only about 16 MB of pending data is buffered, so long tracks play without loading the whole track into memory.
- If console rendering is slow, audio and video may drift slightly out of sync.
- GIF images only read the first frame; GIF animation is not supported.
- Videos cannot be exported as character-art files yet; `--save` has no effect on videos and shows a note.
- Video playback must run directly in a console window; stdout cannot be redirected to a file.
- The image preview shrinks automatically to fit the console window; `--no-display --save` is not limited by the window size.
- With a fixed resolution larger than the console window, the image preview reports an error (with `--save` it saves only); videos must fit completely inside the console.
- Console color requires ANSI/VT support on Windows 10/11. When output is redirected or the terminal lacks support, previews fall back to monochrome.
- High-resolution video already uses NV12 and 2× grid downsampling; the actual frame rate can still be limited by console/terminal rendering speed. Lower `--width`, use `--no-color`, or set `--fps`.
- EXIF orientation for images is not handled; portrait phone photos may appear rotated.
- Rotation metadata in some phone videos is not handled, so orientation may be wrong.
- Saved TXT files are UTF-8 without BOM; if an editor opens them as ANSI and shows mojibake, choose UTF-8.

## Development and testing

`test-assets/test-image.png` is a sample image you can try immediately:

```bat
build\Release\ASCIIArt.exe test-assets\test-image.png -w 100 -c
```

To test video playback, you can generate an uncompressed AVI that needs no third-party codec (about 12 MB, requires Python + numpy):

```bat
python tools\make_test_avi.py
build\Release\ASCIIArt.exe test-assets\test-video.avi -w 80
```

## FAQ

**The video says it "must run directly in a console window"**

Do not append `>` redirection, and do not run it through an IDE "Output" window. Run it directly in Windows Terminal or the system console.

**Colors show up as garbage or escape characters**

The terminal does not support VT. Use Windows Terminal or the Windows 10/11 default console, or add `--no-color` for plain character art.

**The video will not open**

First check that the system player (e.g. "Movies & TV") can play the file. If the system lacks a decoder, this program cannot decode it either. Prefer MP4/H.264 or AVI.

**Chinese text looks garbled**

The program and its output files use UTF-8. The console needs a UTF-8 code page (the program sets it automatically); open saved TXT files as UTF-8.

**The interface language is wrong / I want to switch**

The program follows the system UI language at startup: Chinese for a Chinese system, English for an English system, and English for any other language. Force a language with `--lang zh` or `--lang en`, or choose `5. Switch language` in the interactive main menu to switch immediately.

**I set `--width 300` but the preview is 85×24**

That was the old behavior: `--width` was only a target, and an insufficient preview height reduced the column count.  
The program now first tries to shrink the font / enlarge the window; only if that fails does it shrink to the window size and annotate the grid line:

```text
Grid: 85 x 24 (target 300x84, fitted to window)
```

To force exactly 300×84:

```bat
ASCIIArt image.png --resolution 300x84
```

To save the full 300×84 regardless of the window:

```bat
ASCIIArt image.png --resolution 300x84 --no-display --save art.txt
```

**Playback stutters**

Lower the width and frame rate:

```bat
ASCIIArt clip.mp4 -w 80 -f 20
```

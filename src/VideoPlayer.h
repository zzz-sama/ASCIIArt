#pragma once

#include "Common.h"

#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <wrl/client.h>

#include <string>

struct VideoInfo {
    int width = 0;
    int height = 0;
    double fps = 0.0;
    double duration = 0.0;  // seconds; 0 when unknown
};

// Decodes video frames with the Windows Media Foundation Source Reader.
// Audio is handled separately by AudioPlayer.
class VideoPlayer {
public:
    VideoPlayer() = default;
    ~VideoPlayer();
    VideoPlayer(const VideoPlayer&) = delete;
    VideoPlayer& operator=(const VideoPlayer&) = delete;

    bool Open(const std::wstring& path, std::wstring& error);
    void Close();

    // Requests a smaller RGBA output frame. When the decoder/video processor
    // can scale, the frame is reduced before it crosses into system memory, so
    // the full-resolution copy never happens. Otherwise the decoded frame is
    // downsampled in software.
    // 0,0 restores full-resolution output.
    void SetTargetSize(int width, int height);

    // Reads the next decoded sample without converting it to RGBA, so frames
    // the player will skip never pay for the conversion.
    bool ReadFrame(double& timestampSeconds, bool& eof, std::wstring& error);

    // Converts the sample returned by ReadFrame into an RGBA image.
    bool DecodeFrame(RgbaImage& image, std::wstring& error);

    // Releases the pending sample without converting it.
    void DiscardFrame();

    bool Rewind(std::wstring& error);

    const VideoInfo& Info() const { return info_; }

private:
    enum class PixelFormat { Rgb32, Rgb24, Nv12 };

    bool UpdateCurrentMediaType(std::wstring& error);
    bool ConfigureScaledOutput(int width);
    bool TryScaledOutputType(int width, int height);
    bool CopySampleToImage(IMFSample* sample, RgbaImage& image,
                           std::wstring& error);
    bool CopyPacked(const BYTE* data, LONG pitch, std::size_t length,
                    RgbaImage& image, std::wstring& error);
    bool CopyNv12(const BYTE* data, LONG pitch, std::size_t length,
                  RgbaImage& image, std::wstring& error);

    Microsoft::WRL::ComPtr<IMFSourceReader> reader_;
    Microsoft::WRL::ComPtr<IMFSample> pendingSample_;
    VideoInfo info_{};
    GUID outputSubtype_ = MFVideoFormat_RGB32;
    PixelFormat pixelFormat_ = PixelFormat::Rgb32;
    int bytesPerPixel_ = 4;
    bool hdColor_ = true;
    bool decoderScaled_ = false;
    int frameWidth_ = 0;   // size of the frame the decoder currently outputs
    int frameHeight_ = 0;
    int targetWidth_ = 0;
    int targetHeight_ = 0;
};

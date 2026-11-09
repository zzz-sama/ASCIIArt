#include "VideoPlayer.h"

#include "Localization.h"
#include "Util.h"

#include <mferror.h>

#include <algorithm>
#include <cmath>
#include <utility>
#include <cstring>
#include <limits>

using Microsoft::WRL::ComPtr;

namespace {

constexpr DWORD kFirstVideoStream =
    static_cast<DWORD>(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
constexpr DWORD kAllStreams =
    static_cast<DWORD>(MF_SOURCE_READER_ALL_STREAMS);
constexpr DWORD kMediaSource =
    static_cast<DWORD>(MF_SOURCE_READER_MEDIASOURCE);

class BufferLock {
public:
    BufferLock() = default;
    ~BufferLock() { Unlock(); }

    BufferLock(const BufferLock&) = delete;
    BufferLock& operator=(const BufferLock&) = delete;

    bool Lock(IMFMediaBuffer* buffer, std::wstring& error) {
        Unlock();
        buffer_ = buffer;
        if (buffer_ == nullptr) {
            error = Tr(L"The video frame buffer is empty.",
                       L"视频帧缓冲区为空。");
            return false;
        }

        ComPtr<IMF2DBuffer> buffer2D;
        if (SUCCEEDED(buffer_.As(&buffer2D))) {
            BYTE* scanline = nullptr;
            LONG pitch = 0;
            if (SUCCEEDED(buffer2D->Lock2D(&scanline, &pitch))) {
                scanline_ = scanline;
                pitch_ = pitch;
                locked2D_ = true;
                buffer2D_ = buffer2D;
                return true;
            }
        }

        BYTE* data = nullptr;
        DWORD maxLength = 0;
        DWORD currentLength = 0;
        const HRESULT hr = buffer_->Lock(&data, &maxLength, &currentLength);
        if (FAILED(hr)) {
            error = std::wstring(Tr(L"Cannot lock the video frame buffer: ",
                                    L"无法锁定视频帧缓冲区：")) +
                    util::FormatHResult(hr);
            return false;
        }
        scanline_ = data;
        pitch_ = 0;  // computed by the caller
        currentLength_ = currentLength;
        locked_ = true;
        return true;
    }

    void Unlock() {
        if (locked2D_ && buffer2D_) {
            buffer2D_->Unlock2D();
        } else if (locked_ && buffer_) {
            buffer_->Unlock();
        }
        locked2D_ = false;
        locked_ = false;
        buffer2D_.Reset();
        buffer_.Reset();
        scanline_ = nullptr;
        pitch_ = 0;
        currentLength_ = 0;
    }

    BYTE* Data() const { return scanline_; }
    LONG Pitch() const { return pitch_; }
    DWORD Length() const { return currentLength_; }
    bool Is2D() const { return locked2D_; }

private:
    ComPtr<IMFMediaBuffer> buffer_;
    ComPtr<IMF2DBuffer> buffer2D_;
    BYTE* scanline_ = nullptr;
    LONG pitch_ = 0;
    DWORD currentLength_ = 0;
    bool locked2D_ = false;
    bool locked_ = false;
};

bool TrySetOutputFormat(IMFSourceReader* reader, const GUID& subtype,
                        std::wstring& error) {
    ComPtr<IMFMediaType> type;
    HRESULT hr = MFCreateMediaType(&type);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot create the video media type: ",
                                L"无法创建视频媒体类型：")) +
                util::FormatHResult(hr);
        return false;
    }
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, subtype);
    hr = reader->SetCurrentMediaType(kFirstVideoStream, nullptr, type.Get());
    if (FAILED(hr)) {
        return false;
    }
    return true;
}

std::uint8_t ClampByte(int value) {
    return static_cast<std::uint8_t>(std::max(0, std::min(255, value)));
}

void YuvToRgb(int y, int u, int v, bool hdColor, std::uint8_t& r,
              std::uint8_t& g, std::uint8_t& b) {
    const int c = std::max(0, y - 16);
    const int d = u - 128;
    const int e = v - 128;
    if (hdColor) {
        // Limited-range BT.709, used by most HD/4K video.
        r = ClampByte((298 * c + 459 * e + 128) >> 8);
        g = ClampByte((298 * c - 55 * d - 136 * e + 128) >> 8);
        b = ClampByte((298 * c + 541 * d + 128) >> 8);
    } else {
        // Limited-range BT.601 for SD video.
        r = ClampByte((298 * c + 409 * e + 128) >> 8);
        g = ClampByte((298 * c - 100 * d - 208 * e + 128) >> 8);
        b = ClampByte((298 * c + 516 * d + 128) >> 8);
    }
}

// Two sample positions inside [start, end), used for cheap area sampling of
// large source blocks.
void SamplePair(int start, int end, int& first, int& second) {
    if (end <= start) {
        first = second = start;
        return;
    }
    const int length = end - start;
    if (length <= 2) {
        first = second = start + length / 2;
        return;
    }
    first = start + length / 4;
    second = start + (3 * length) / 4;
    if (second >= end) {
        second = end - 1;
    }
}

}  // namespace

VideoPlayer::~VideoPlayer() { Close(); }

void VideoPlayer::Close() {
    reader_.Reset();
    pendingSample_.Reset();
    info_ = {};
    outputSubtype_ = MFVideoFormat_RGB32;
    pixelFormat_ = PixelFormat::Rgb32;
    bytesPerPixel_ = 4;
    hdColor_ = true;
    decoderScaled_ = false;
    frameWidth_ = 0;
    frameHeight_ = 0;
    targetWidth_ = 0;
    targetHeight_ = 0;
}

void VideoPlayer::SetTargetSize(int width, int height) {
    if (width <= 0 || height <= 0 || info_.width <= 0 || info_.height <= 0) {
        targetWidth_ = 0;
        targetHeight_ = 0;
        return;
    }
    const int targetW = std::min(width, info_.width);
    const int targetH = std::min(height, info_.height);
    targetWidth_ = targetW;
    targetHeight_ = targetH;
    if (targetW >= info_.width && targetH >= info_.height) {
        return;
    }
    // Ask Media Foundation to scale the frames, so the full-resolution frame
    // stays on the GPU and is never copied to the CPU. The software
    // downsampling in CopyNv12/CopyPacked remains the fallback when the
    // decoder cannot do it.
    if (ConfigureScaledOutput(targetW)) {
        decoderScaled_ = true;
    }
}

bool VideoPlayer::ConfigureScaledOutput(int width) {
    // The video processor keeps the source aspect ratio and pads the rest of
    // the frame with black bars. Requesting a source-aspect frame avoids that
    // padding; the small software step in CopyNv12 then maps it onto the
    // character grid, which intentionally squashes the picture to match the
    // terminal's character cells.
    const int limitWidth = info_.width & ~1;
    const int limitHeight = info_.height & ~1;
    int scaledWidth = std::min(std::max(16, width), limitWidth);
    scaledWidth &= ~1;
    if (scaledWidth < 16) {
        return false;
    }
    int scaledHeight = static_cast<int>(std::lround(
        static_cast<double>(scaledWidth) * info_.height /
        static_cast<double>(info_.width)));
    scaledHeight &= ~1;
    if (scaledHeight < 2) {
        scaledHeight = 2;
    }
    if (scaledHeight > limitHeight) {
        scaledHeight = limitHeight;
    }
    if (scaledWidth >= limitWidth && scaledHeight >= limitHeight) {
        return false;  // nothing to gain: keep the full-resolution output
    }
    return TryScaledOutputType(scaledWidth, scaledHeight);
}

bool VideoPlayer::TryScaledOutputType(int width, int height) {
    ComPtr<IMFMediaType> previous;
    const bool havePrevious =
        SUCCEEDED(reader_->GetCurrentMediaType(kFirstVideoStream, &previous));

    ComPtr<IMFMediaType> type;
    if (FAILED(MFCreateMediaType(&type))) {
        return false;
    }
    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    type->SetGUID(MF_MT_SUBTYPE, outputSubtype_);
    type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    if (FAILED(MFSetAttributeSize(type.Get(), MF_MT_FRAME_SIZE,
                                  static_cast<UINT32>(width),
                                  static_cast<UINT32>(height)))) {
        return false;
    }
    if (FAILED(reader_->SetCurrentMediaType(kFirstVideoStream, nullptr,
                                            type.Get()))) {
        return false;
    }

    // The media type now describes the scaled frame, which must not overwrite
    // the native size reported through Info().
    decoderScaled_ = true;
    std::wstring ignored;
    if (!UpdateCurrentMediaType(ignored)) {
        decoderScaled_ = false;
        if (havePrevious) {
            reader_->SetCurrentMediaType(kFirstVideoStream, nullptr,
                                         previous.Get());
        }
        return false;
    }
    return true;
}

bool VideoPlayer::Open(const std::wstring& path, std::wstring& error) {
    Close();

    ComPtr<IMFAttributes> attributes;
    HRESULT hr = MFCreateAttributes(&attributes, 4);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot create Media Foundation attributes: ",
                                L"无法创建 Media Foundation 属性：")) +
                util::FormatHResult(hr);
        return false;
    }
    // Advanced video processing lets the source reader insert the video
    // processor MFT, which can scale frames before they reach the CPU. It is
    // mutually exclusive with MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING:
    // setting both makes MFCreateSourceReaderFromURL fail with E_INVALIDARG.
    attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, TRUE);
    attributes->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,
                          TRUE);

    hr = MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(), &reader_);
    if (FAILED(hr)) {
        // Older systems may reject advanced processing, so fall back to the
        // basic video processing pipeline (software downsampling remains).
        attributes->DeleteItem(
            MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING);
        attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        reader_.Reset();
        hr = MFCreateSourceReaderFromURL(path.c_str(), attributes.Get(),
                                         &reader_);
    }
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot open the video file:\n  ",
                                L"无法打开视频文件：\n  ")) +
                path + L"\n  " + util::FormatHResult(hr) +
                Tr(L"\nMake sure the file exists and the required decoder is "
                   L"installed.",
                   L"\n请确认文件存在，且系统安装了对应的解码器。");
        Close();
        return false;
    }

    hr = reader_->SetStreamSelection(kAllStreams, FALSE);
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"Cannot deselect the other streams in the video file: ",
                       L"无法关闭视频文件中的其他流：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }
    hr = reader_->SetStreamSelection(kFirstVideoStream, TRUE);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"The file has no video track: ",
                                L"该文件没有可用的视频轨道：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }

    ComPtr<IMFMediaType> nativeType;
    hr = reader_->GetNativeMediaType(kFirstVideoStream, 0, &nativeType);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"The file has no video track: ",
                                L"该文件没有可用的视频轨道：")) +
                util::FormatHResult(hr);
        Close();
        return false;
    }

    // NV12 avoids a full-resolution RGB conversion in Media Foundation and is
    // dramatically faster for high-resolution video. Fall back to packed RGB
    // when the decoder cannot provide NV12.
    bool configured = TrySetOutputFormat(reader_.Get(), MFVideoFormat_NV12,
                                         error);
    outputSubtype_ = MFVideoFormat_NV12;
    if (!configured) {
        outputSubtype_ = MFVideoFormat_RGB32;
        configured = TrySetOutputFormat(reader_.Get(), MFVideoFormat_RGB32,
                                        error);
    }
    if (!configured) {
        outputSubtype_ = MFVideoFormat_RGB24;
        configured = TrySetOutputFormat(reader_.Get(), MFVideoFormat_RGB24,
                                        error);
    }
    if (!configured) {
        error = Tr(L"Cannot convert the video frames to NV12/RGB.\n"
                   L"Try installing the matching video decoder, or use a "
                   L"different MP4/H.264 file.",
                   L"无法把视频帧转换为 NV12/RGB 格式。\n"
                   L"请尝试安装对应的视频解码器，或换一个 MP4/H.264 文件。");
        Close();
        return false;
    }

    if (!UpdateCurrentMediaType(error)) {
        Close();
        return false;
    }

    UINT32 numerator = 0;
    UINT32 denominator = 0;
    hr = MFGetAttributeRatio(nativeType.Get(), MF_MT_FRAME_RATE, &numerator,
                             &denominator);
    if (SUCCEEDED(hr) && denominator != 0 && numerator != 0) {
        info_.fps = static_cast<double>(numerator) / denominator;
    } else if (SUCCEEDED(MFGetAttributeRatio(
                   nativeType.Get(), MF_MT_FRAME_RATE_RANGE_MAX, &numerator,
                   &denominator)) &&
               denominator != 0 && numerator != 0) {
        info_.fps = static_cast<double>(numerator) / denominator;
    }
    if (info_.fps <= 0.0 || info_.fps > 240.0) {
        info_.fps = 30.0;
    }

    // Duration can live on the media source rather than the video stream.
    double durationSeconds = 0.0;
    const DWORD durationSources[] = {kMediaSource, kFirstVideoStream};
    for (const DWORD stream : durationSources) {
        PROPVARIANT duration;
        PropVariantInit(&duration);
        hr = reader_->GetPresentationAttribute(stream, MF_PD_DURATION,
                                               &duration);
        if (SUCCEEDED(hr)) {
            if (duration.vt == VT_UI8) {
                durationSeconds =
                    static_cast<double>(duration.uhVal.QuadPart) / 10000000.0;
            } else if (duration.vt == VT_I8) {
                durationSeconds =
                    static_cast<double>(duration.hVal.QuadPart) / 10000000.0;
            }
        }
        PropVariantClear(&duration);
        if (durationSeconds > 0.0) {
            break;
        }
    }
    if (durationSeconds > 0.0) {
        info_.duration = durationSeconds;
    }

    return true;
}

bool VideoPlayer::UpdateCurrentMediaType(std::wstring& error) {
    ComPtr<IMFMediaType> type;
    HRESULT hr = reader_->GetCurrentMediaType(kFirstVideoStream, &type);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot read the video frame format: ",
                                L"无法读取视频帧格式：")) +
                util::FormatHResult(hr);
        return false;
    }

    GUID subtype{};
    hr = type->GetGUID(MF_MT_SUBTYPE, &subtype);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot read the video pixel format: ",
                                L"无法读取视频像素格式：")) +
                util::FormatHResult(hr);
        return false;
    }
    if (subtype == MFVideoFormat_NV12) {
        pixelFormat_ = PixelFormat::Nv12;
        bytesPerPixel_ = 0;
    } else if (subtype == MFVideoFormat_RGB32) {
        pixelFormat_ = PixelFormat::Rgb32;
        bytesPerPixel_ = 4;
    } else if (subtype == MFVideoFormat_RGB24) {
        pixelFormat_ = PixelFormat::Rgb24;
        bytesPerPixel_ = 3;
    } else {
        error = Tr(L"The video decoder produced an unsupported pixel format.",
                   L"视频解码器输出了不支持的像素格式。");
        return false;
    }

    UINT32 width = 0;
    UINT32 height = 0;
    hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        error = Tr(L"The video frame size is invalid.", L"视频帧尺寸无效。");
        return false;
    }
    if (width > 16384 || height > 16384) {
        error = Tr(L"The video resolution is too large.", L"视频分辨率过大。");
        return false;
    }

    frameWidth_ = static_cast<int>(width);
    frameHeight_ = static_cast<int>(height);
    if (!decoderScaled_) {
        // Only the native media type defines the video's real resolution; a
        // scaled output type describes the small preview frame instead.
        info_.width = frameWidth_;
        info_.height = frameHeight_;
        hdColor_ = width >= 1280 || height >= 720;
    }
    return true;
}

bool VideoPlayer::CopySampleToImage(IMFSample* sample, RgbaImage& image,
                                    std::wstring& error) {
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot read the video frame: ",
                                L"无法读取视频帧：")) +
                util::FormatHResult(hr);
        return false;
    }

    BufferLock lock;
    if (!lock.Lock(buffer.Get(), error)) {
        return false;
    }

    if (pixelFormat_ == PixelFormat::Nv12) {
        return CopyNv12(lock.Data(), lock.Pitch(),
                        lock.Is2D() ? 0 : lock.Length(), image, error);
    }
    return CopyPacked(lock.Data(), lock.Pitch(),
                      lock.Is2D() ? 0 : lock.Length(), image, error);
}

bool VideoPlayer::CopyPacked(const BYTE* data, LONG pitch,
                             std::size_t length, RgbaImage& image,
                             std::wstring& error) {
    const int width = frameWidth_;
    const int height = frameHeight_;
    const std::size_t rowBytes =
        static_cast<std::size_t>(width) * bytesPerPixel_;
    LONG effectivePitch = pitch;
    if (effectivePitch == 0) {
        effectivePitch = static_cast<LONG>(rowBytes);
    }
    const std::size_t pitchBytes =
        static_cast<std::size_t>(effectivePitch >= 0 ? effectivePitch
                                                     : -effectivePitch);
    if (pitchBytes < rowBytes) {
        error = Tr(L"The video frame stride is invalid.", L"视频帧的跨距无效。");
        return false;
    }
    if (length != 0 &&
        static_cast<std::uint64_t>(length) <
            static_cast<std::uint64_t>(rowBytes) * height) {
        error = Tr(L"The video frame buffer is too small.",
                   L"视频帧缓冲区大小不足。");
        return false;
    }

    const int outWidth =
        targetWidth_ > 0 ? std::min(targetWidth_, width) : width;
    const int outHeight =
        targetHeight_ > 0 ? std::min(targetHeight_, height) : height;
    const bool downscale = outWidth < width || outHeight < height;

    const std::size_t pixelCount =
        static_cast<std::size_t>(outWidth) * outHeight;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        error = Tr(L"The video frame is too large.", L"视频帧过大。");
        return false;
    }
    image.width = outWidth;
    image.height = outHeight;
    // Write straight into the caller's buffer: it is reused for every frame,
    // so this avoids allocating and freeing a full frame each time.
    image.pixels.resize(pixelCount * 4);

    if (!downscale) {
        for (int y = 0; y < height; ++y) {
            const int sourceY = effectivePitch >= 0 ? y : (height - 1 - y);
            const BYTE* source =
                data + static_cast<std::size_t>(sourceY) * pitchBytes;
            std::uint8_t* target =
                image.pixels.data() + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                target[0] = source[x * bytesPerPixel_ + 2];
                target[1] = source[x * bytesPerPixel_ + 1];
                target[2] = source[x * bytesPerPixel_ + 0];
                target[3] = 255;
                target += 4;
            }
        }
    } else {
        for (int ty = 0; ty < outHeight; ++ty) {
            const int sy0 = static_cast<int>(
                static_cast<long long>(ty) * height / outHeight);
            int sy1 = static_cast<int>(
                static_cast<long long>(ty + 1) * height / outHeight);
            sy1 = std::max(sy0 + 1, std::min(sy1, height));
            int sampleY0 = 0;
            int sampleY1 = 0;
            SamplePair(sy0, sy1, sampleY0, sampleY1);

            for (int tx = 0; tx < outWidth; ++tx) {
                const int sx0 = static_cast<int>(
                    static_cast<long long>(tx) * width / outWidth);
                int sx1 = static_cast<int>(
                    static_cast<long long>(tx + 1) * width / outWidth);
                sx1 = std::max(sx0 + 1, std::min(sx1, width));
                int sampleX0 = 0;
                int sampleX1 = 0;
                SamplePair(sx0, sx1, sampleX0, sampleX1);

                int sumB = 0;
                int sumG = 0;
                int sumR = 0;
                for (int sy : {sampleY0, sampleY1}) {
                    const int sourceY =
                        effectivePitch >= 0 ? sy : (height - 1 - sy);
                    const BYTE* source =
                        data + static_cast<std::size_t>(sourceY) * pitchBytes;
                    for (int sx : {sampleX0, sampleX1}) {
                        const BYTE* pixel = source + sx * bytesPerPixel_;
                        sumB += pixel[0];
                        sumG += pixel[1];
                        sumR += pixel[2];
                    }
                }
                std::uint8_t* target =
                    image.pixels.data() +
                    (static_cast<std::size_t>(ty) * outWidth + tx) * 4;
                target[0] = static_cast<std::uint8_t>(sumR / 4);
                target[1] = static_cast<std::uint8_t>(sumG / 4);
                target[2] = static_cast<std::uint8_t>(sumB / 4);
                target[3] = 255;
            }
        }
    }

    return true;
}

bool VideoPlayer::CopyNv12(const BYTE* data, LONG pitch,
                           std::size_t length, RgbaImage& image,
                           std::wstring& error) {
    const int width = frameWidth_;
    const int height = frameHeight_;
    if ((height & 1) != 0) {
        error = Tr(L"NV12 video frames with an odd height are not supported.",
                   L"不支持奇数高度的 NV12 视频帧。");
        return false;
    }
    if (pitch < 0) {
        error = Tr(L"Bottom-up NV12 video frames are not supported.",
                   L"不支持底朝上的 NV12 视频帧。");
        return false;
    }

    const std::size_t yStride =
        pitch == 0 ? static_cast<std::size_t>(width)
                   : static_cast<std::size_t>(pitch);
    if (yStride < static_cast<std::size_t>(width) || (yStride & 1) != 0) {
        error = Tr(L"The NV12 video frame stride is invalid.",
                   L"NV12 视频帧跨距无效。");
        return false;
    }
    const std::uint64_t required =
        static_cast<std::uint64_t>(yStride) * height +
        static_cast<std::uint64_t>(yStride) * (height / 2);
    if (length != 0 && static_cast<std::uint64_t>(length) < required) {
        error = Tr(L"The NV12 video frame buffer is too small.",
                   L"NV12 视频帧缓冲区大小不足。");
        return false;
    }

    const BYTE* yPlane = data;
    const BYTE* uvPlane = data + yStride * static_cast<std::size_t>(height);

    const int outWidth =
        targetWidth_ > 0 ? std::min(targetWidth_, width) : width;
    const int outHeight =
        targetHeight_ > 0 ? std::min(targetHeight_, height) : height;
    const bool downscale = outWidth < width || outHeight < height;

    const std::size_t pixelCount =
        static_cast<std::size_t>(outWidth) * outHeight;
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4) {
        error = Tr(L"The video frame is too large.", L"视频帧过大。");
        return false;
    }
    image.width = outWidth;
    image.height = outHeight;
    image.pixels.resize(pixelCount * 4);

    if (!downscale) {
        for (int y = 0; y < height; ++y) {
            const BYTE* yRow = yPlane + yStride * y;
            const BYTE* uvRow =
                uvPlane + yStride * static_cast<std::size_t>(y / 2);
            std::uint8_t* target =
                image.pixels.data() + static_cast<std::size_t>(y) * width * 4;
            for (int x = 0; x < width; ++x) {
                const BYTE* uv = uvRow + (x / 2) * 2;
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                YuvToRgb(yRow[x], uv[0], uv[1], hdColor_, r, g, b);
                target[0] = r;
                target[1] = g;
                target[2] = b;
                target[3] = 255;
                target += 4;
            }
        }
    } else {
        for (int ty = 0; ty < outHeight; ++ty) {
            const int sy0 = static_cast<int>(
                static_cast<long long>(ty) * height / outHeight);
            int sy1 = static_cast<int>(
                static_cast<long long>(ty + 1) * height / outHeight);
            sy1 = std::max(sy0 + 1, std::min(sy1, height));
            int sampleY0 = 0;
            int sampleY1 = 0;
            SamplePair(sy0, sy1, sampleY0, sampleY1);

            for (int tx = 0; tx < outWidth; ++tx) {
                const int sx0 = static_cast<int>(
                    static_cast<long long>(tx) * width / outWidth);
                int sx1 = static_cast<int>(
                    static_cast<long long>(tx + 1) * width / outWidth);
                sx1 = std::max(sx0 + 1, std::min(sx1, width));
                int sampleX0 = 0;
                int sampleX1 = 0;
                SamplePair(sx0, sx1, sampleX0, sampleX1);

                int sumY = 0;
                int sumU = 0;
                int sumV = 0;
                for (int sy : {sampleY0, sampleY1}) {
                    const BYTE* yRow = yPlane + yStride * sy;
                    const BYTE* uvRow =
                        uvPlane + yStride * static_cast<std::size_t>(sy / 2);
                    for (int sx : {sampleX0, sampleX1}) {
                        sumY += yRow[sx];
                        const BYTE* uv = uvRow + (sx / 2) * 2;
                        sumU += uv[0];
                        sumV += uv[1];
                    }
                }
                std::uint8_t r = 0;
                std::uint8_t g = 0;
                std::uint8_t b = 0;
                YuvToRgb(sumY / 4, sumU / 4, sumV / 4, hdColor_, r, g, b);
                std::uint8_t* target =
                    image.pixels.data() +
                    (static_cast<std::size_t>(ty) * outWidth + tx) * 4;
                target[0] = r;
                target[1] = g;
                target[2] = b;
                target[3] = 255;
            }
        }
    }

    return true;
}

bool VideoPlayer::ReadFrame(double& timestampSeconds, bool& eof,
                            std::wstring& error) {
    pendingSample_.Reset();
    timestampSeconds = 0.0;
    eof = false;

    if (!reader_) {
        error = Tr(L"The video is not open.", L"视频尚未打开。");
        return false;
    }

    for (int attempt = 0; attempt < 1000; ++attempt) {
        DWORD flags = 0;
        LONGLONG timestamp = 0;
        ComPtr<IMFSample> sample;
        const HRESULT hr = reader_->ReadSample(
            kFirstVideoStream, 0, nullptr, &flags, &timestamp, &sample);
        if (FAILED(hr)) {
            error = std::wstring(Tr(L"Failed to read a video frame: ",
                                    L"读取视频帧失败：")) +
                    util::FormatHResult(hr);
            return false;
        }

        if ((flags & MF_SOURCE_READERF_ERROR) != 0) {
            error = Tr(L"The video decoder reported an error.",
                       L"视频解码器报告了一个错误。");
            return false;
        }
        if ((flags & MF_SOURCE_READERF_ENDOFSTREAM) != 0) {
            eof = true;
            return true;
        }
        if ((flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) != 0) {
            if (!UpdateCurrentMediaType(error)) {
                return false;
            }
        }
        if (!sample) {
            continue;  // stream tick or a gap between frames
        }

        timestampSeconds = static_cast<double>(timestamp) / 10000000.0;
        pendingSample_ = sample;
        return true;
    }

    error = Tr(L"The video decoder produced no frames for a long time.",
               L"视频解码长时间没有输出帧。");
    return false;
}

bool VideoPlayer::DecodeFrame(RgbaImage& image, std::wstring& error) {
    image.Clear();
    if (!pendingSample_) {
        return true;
    }
    const ComPtr<IMFSample> sample = pendingSample_;
    pendingSample_.Reset();
    return CopySampleToImage(sample.Get(), image, error);
}

void VideoPlayer::DiscardFrame() { pendingSample_.Reset(); }

bool VideoPlayer::Rewind(std::wstring& error) {
    if (!reader_) {
        error = Tr(L"The video is not open.", L"视频尚未打开。");
        return false;
    }

    PROPVARIANT position;
    PropVariantInit(&position);
    position.vt = VT_I8;
    position.hVal.QuadPart = 0;

    HRESULT hr = reader_->SetCurrentPosition(GUID_NULL, position);
    PropVariantClear(&position);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot rewind the video: ",
                                L"无法回到视频开头：")) +
                util::FormatHResult(hr);
        return false;
    }
    return true;
}

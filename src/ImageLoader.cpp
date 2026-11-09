#include "ImageLoader.h"

#include "Localization.h"
#include "Util.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <limits>
#include <utility>

using Microsoft::WRL::ComPtr;

bool LoadImageFile(const std::wstring& path, RgbaImage& image,
                   std::wstring& error) {
    image.Clear();

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr,
                                  CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"Cannot create the Windows Imaging Component (WIC): ",
                       L"无法创建 Windows 图像组件（WIC）：")) +
                util::FormatHResult(hr);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromFilename(
        path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
        &decoder);
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Cannot open the image file:\n  ",
                                L"无法打开图片文件：\n  ")) +
                path + L"\n  " + util::FormatHResult(hr);
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"Cannot read the first frame of the image: ",
                       L"无法读取图片的第一帧：")) +
                util::FormatHResult(hr);
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    hr = frame->GetSize(&width, &height);
    if (FAILED(hr) || width == 0 || height == 0) {
        error = Tr(L"The image size is invalid.", L"图片尺寸无效。");
        return false;
    }

    const std::uint64_t byteCount =
        static_cast<std::uint64_t>(width) * height * 4ull;
    if (byteCount > static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max()) ||
        byteCount > (2ull * 1024ull * 1024ull * 1024ull)) {
        error = Tr(L"The image is too large to process.",
                   L"图片过大，无法处理。");
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"Cannot create the image format converter: ",
                       L"无法创建图像格式转换器：")) +
                util::FormatHResult(hr);
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        error = std::wstring(
                    Tr(L"The image format is not supported: ",
                       L"图片格式不受支持：")) +
                util::FormatHResult(hr);
        return false;
    }

    RgbaImage result;
    result.width = static_cast<int>(width);
    result.height = static_cast<int>(height);
    result.pixels.resize(static_cast<std::size_t>(byteCount));

    hr = converter->CopyPixels(
        nullptr, width * 4, static_cast<UINT>(result.pixels.size()),
        result.pixels.data());
    if (FAILED(hr)) {
        error = std::wstring(Tr(L"Failed to read image pixels: ",
                                L"读取图片像素失败：")) +
                util::FormatHResult(hr);
        return false;
    }

    image = std::move(result);
    return true;
}

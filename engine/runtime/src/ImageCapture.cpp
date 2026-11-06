#include "ImageCapture.h"

#include "ProfileReport.h"

#include <tinyme/core/RenderPipeline.h>

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>
#include <wincodec.h>
#include <wrl/client.h>

namespace tinyme
{
namespace
{
void CheckWic(HRESULT result, const char* operation)
{
    if (FAILED(result))
    {
        throw std::runtime_error(std::string(operation) + " failed; HRESULT=" +
                                 std::to_string(static_cast<unsigned long>(result)));
    }
}
class ComApartment
{
  public:
    ComApartment()
    {
        const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (result == RPC_E_CHANGED_MODE)
        {
            return;
        }
        CheckWic(result, "CoInitializeEx");
        initialized_ = true;
    }
    ~ComApartment()
    {
        if (initialized_)
        {
            CoUninitialize();
        }
    }
    ComApartment(const ComApartment&) = delete;
    ComApartment& operator=(const ComApartment&) = delete;

  private:
    bool initialized_ = false;
};
} // namespace
std::vector<std::uint8_t> EncodePng(WindowSize size, std::span<const std::uint8_t> rgba)
{
    if (size.width == 0 || size.height == 0 || size.width > kMaxRenderDimension ||
        size.height > kMaxRenderDimension ||
        rgba.size() != static_cast<std::size_t>(size.width) * size.height * 4)
    {
        throw std::invalid_argument("PNG requires dimensions [1, 2048] and packed RGBA8 bytes.");
    }
    ComApartment apartment;
    Microsoft::WRL::ComPtr<IWICImagingFactory> factory;
    CheckWic(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                              IID_PPV_ARGS(factory.GetAddressOf())),
             "Create WIC factory");
    Microsoft::WRL::ComPtr<IStream> stream;
    CheckWic(CreateStreamOnHGlobal(nullptr, TRUE, stream.GetAddressOf()),
             "Create PNG memory stream");
    Microsoft::WRL::ComPtr<IWICBitmapEncoder> encoder;
    CheckWic(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, encoder.GetAddressOf()),
             "Create PNG encoder");
    CheckWic(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Initialize PNG encoder");
    Microsoft::WRL::ComPtr<IWICBitmapFrameEncode> frame;
    Microsoft::WRL::ComPtr<IPropertyBag2> properties;
    CheckWic(encoder->CreateNewFrame(frame.GetAddressOf(), properties.GetAddressOf()),
             "Create PNG frame");
    CheckWic(frame->Initialize(properties.Get()), "Initialize PNG frame");
    CheckWic(frame->SetSize(size.width, size.height), "Set PNG dimensions");
    auto format = GUID_WICPixelFormat32bppRGBA;
    CheckWic(frame->SetPixelFormat(&format), "Set PNG RGBA format");
    std::vector<std::uint8_t> pixels(rgba.begin(), rgba.end());
    if (IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA))
    {
        for (std::size_t index = 0; index < pixels.size(); index += 4)
        {
            std::swap(pixels[index], pixels[index + 2]);
        }
    }
    else if (!IsEqualGUID(format, GUID_WICPixelFormat32bppRGBA))
    {
        throw std::runtime_error("WIC PNG encoder rejected RGBA8/BGRA8; refusing lossy format.");
    }
    CheckWic(frame->WritePixels(size.height, size.width * 4, static_cast<UINT>(pixels.size()),
                                pixels.data()),
             "Write PNG rows");
    CheckWic(frame->Commit(), "Commit PNG frame");
    CheckWic(encoder->Commit(), "Commit PNG encoder");
    STATSTG statistics{};
    CheckWic(stream->Stat(&statistics, STATFLAG_NONAME), "Read PNG stream size");
    if (statistics.cbSize.QuadPart == 0 ||
        statistics.cbSize.QuadPart > std::numeric_limits<ULONG>::max())
    {
        throw std::runtime_error("Invalid PNG encoded byte count.");
    }
    LARGE_INTEGER start{};
    CheckWic(stream->Seek(start, STREAM_SEEK_SET, nullptr), "Rewind PNG stream");
    std::vector<std::uint8_t> png(static_cast<std::size_t>(statistics.cbSize.QuadPart));
    ULONG read = 0;
    CheckWic(stream->Read(png.data(), static_cast<ULONG>(png.size()), &read),
             "Read PNG memory bytes");
    if (read != png.size())
    {
        throw std::runtime_error("Short PNG memory stream read.");
    }
    return png;
}
void SaveCapturePng(const std::filesystem::path& path, WindowSize size,
                    std::span<const std::uint8_t> rgba)
{
    const auto png = EncodePng(size, rgba);
    SaveProfileReport(path,
                      std::string_view(reinterpret_cast<const char*>(png.data()), png.size()));
}
} // namespace tinyme

#include "ImageCapture.h"

#include "ShaderFixture.h"

#include <wincodec.h>
#include <wrl/client.h>

#include <array>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
using Microsoft::WRL::ComPtr;
int checks = 0;
void Check(bool value, const char* description)
{
    ++checks;
    if (!value)
        throw std::runtime_error(description);
}
void HResult(HRESULT value, const char* description)
{
    Check(SUCCEEDED(value), description);
}
template <class Function> void Reject(Function function, const char* description)
{
    bool rejected = false;
    try
    {
        function();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, description);
}
std::string Bytes(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
std::vector<std::uint8_t> Decode(std::span<const std::uint8_t> png, tinyme::WindowSize expected)
{
    ComPtr<IStream> stream;
    HResult(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Create independent PNG decode stream");
    ULONG written = 0;
    HResult(stream->Write(png.data(), static_cast<ULONG>(png.size()), &written),
            "Write encoded PNG into decode stream");
    Check(written == png.size(), "Decode stream has full PNG bytes");
    LARGE_INTEGER beginning{};
    HResult(stream->Seek(beginning, STREAM_SEEK_SET, nullptr), "Seek decoder stream");
    ComPtr<IWICImagingFactory> factory;
    HResult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                             IID_PPV_ARGS(&factory)),
            "Create independent WIC decoder factory");
    ComPtr<IWICBitmapDecoder> decoder;
    HResult(factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                             &decoder),
            "Actual PNG decodes");
    UINT count = 0;
    HResult(decoder->GetFrameCount(&count), "PNG frame count available");
    Check(count == 1, "PNG is exactly one captured frame");
    ComPtr<IWICBitmapFrameDecode> frame;
    HResult(decoder->GetFrame(0, &frame), "Decode PNG first frame");
    UINT width = 0, height = 0;
    HResult(frame->GetSize(&width, &height), "PNG dimensions available");
    Check(width == expected.width && height == expected.height,
          "PNG dimensions match captured target");
    ComPtr<IWICFormatConverter> converter;
    HResult(factory->CreateFormatConverter(&converter), "Create PNG RGBA converter");
    HResult(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA,
                                  WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom),
            "Decode into nonpremultiplied RGBA");
    std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
    HResult(converter->CopyPixels(nullptr, width * 4, static_cast<UINT>(rgba.size()), rgba.data()),
            "Copy packed top-down decoded pixels");
    return rgba;
}
void TestImage()
{
    tinyme::tests::ShaderFixture fixture;
    const std::array<std::uint8_t, 24> pixels{255, 0,  0,  255, 0,   255, 0,  128, 0, 0, 255, 64,
                                              12,  34, 56, 255, 230, 170, 80, 0,   1, 2, 3,   17};
    const auto encoded = tinyme::EncodePng({3, 2}, pixels);
    const std::array<std::uint8_t, 8> signature{137, 80, 78, 71, 13, 10, 26, 10};
    Check(encoded.size() > signature.size() &&
              std::equal(signature.begin(), signature.end(), encoded.begin()),
          "PNG signature and nonempty payload correct");
    const auto decoded = Decode(encoded, {3, 2});
    Check(std::equal(decoded.begin(), decoded.end(), pixels.begin(), pixels.end()),
          "Non-symmetric RGB/alpha and top-bottom orientation round trip exactly");
    const auto path = fixture.Path(L"捕获.PNG");
    tinyme::SaveCapturePng(path, {3, 2}, pixels);
    Check(std::filesystem::exists(path), "Unicode output PNG saved");
    const auto original = Bytes(path);
    Check(original.size() == encoded.size() &&
              std::equal(original.begin(), original.end(), encoded.begin(), encoded.end(),
                         [](char a, std::uint8_t b) { return static_cast<std::uint8_t>(a) == b; }),
          "File is complete original memory PNG, not partial WIC path output");
    Reject([&] { tinyme::SaveCapturePng(path, {3, 2}, pixels); }, "Existing PNG output refused");
    Check(Bytes(path) == original, "Existing PNG original bytes unchanged");
    const auto absent = fixture.Path(L"missing/out.png");
    Reject([&] { tinyme::SaveCapturePng(absent, {3, 2}, pixels); },
           "Missing output parent not auto-created");
    Check(!std::filesystem::exists(absent), "Failed output leaves no PNG");
    for (const auto size :
         {tinyme::WindowSize{0, 2}, tinyme::WindowSize{3, 0}, tinyme::WindowSize{2049, 2},
          tinyme::WindowSize{3, 2049}, tinyme::WindowSize{2, 2}})
        Reject([&] { static_cast<void>(tinyme::EncodePng(size, pixels)); },
               "Invalid dimensions or pixel count rejected before WIC");
    Reject([&] { static_cast<void>(tinyme::EncodePng({3, 2}, std::span<const std::uint8_t>{})); },
           "Empty capture bytes rejected");
    const std::array<std::uint8_t, 4> single{29, 57, 83, 127};
    const auto one = Decode(tinyme::EncodePng({1, 1}, single), {1, 1});
    Check(std::equal(one.begin(), one.end(), single.begin(), single.end()),
          "Minimum dimension PNG and alpha exact");
    const auto bad = fixture.Path(L"bad.png");
    Reject([&] { tinyme::SaveCapturePng(bad, {3, 2}, single); },
           "Invalid capture does not open output file");
    Check(!std::filesystem::exists(bad), "Failed encoding leaves no partial file");
}
} // namespace
int main()
{
    const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized))
        return 1;
    int result = 0;
    try
    {
        TestImage();
        std::cout << "image_contracts checks=" << checks << " failures=0 no_gpu=1\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << "error: " << error.what() << '\n';
        result = 1;
    }
    CoUninitialize();
    return result;
}

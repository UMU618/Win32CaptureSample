#include "pch.h"
#include "SimpleCapture.h"

// UMU Add {
#include <DirectXTex.h>
#include <ShlObj.h>

using namespace std::literals::chrono_literals;
// UMU Add }

namespace winrt
{
    using namespace Windows::Foundation;
    using namespace Windows::Foundation::Numerics;
    using namespace Windows::Graphics;
    using namespace Windows::Graphics::Capture;
    using namespace Windows::Graphics::DirectX;
    using namespace Windows::Graphics::DirectX::Direct3D11;
    using namespace Windows::System;
    using namespace Windows::UI;
    using namespace Windows::UI::Composition;
}

namespace util
{
    using namespace robmikh::common::uwp;
}

// UMU Add {
namespace
{
std::wstring GetKnownFolderPath(REFKNOWNFOLDERID folder_id) noexcept
{
    std::wstring dir;
    PWSTR path = nullptr;
    HRESULT hr = SHGetKnownFolderPath(folder_id, KF_FLAG_CREATE, nullptr, &path);
    if (SUCCEEDED(hr))
    {
        dir = path;
    }
    if (nullptr != path)
    {
        CoTaskMemFree(path);
    }
    return dir;
}
} // namespace
// UMU Add }

SimpleCapture::SimpleCapture(winrt::IDirect3DDevice const& device, winrt::GraphicsCaptureItem const& item, winrt::DirectXPixelFormat pixelFormat)
{
    // UMU Add
    save_directory_ = std::filesystem::path{GetKnownFolderPath(FOLDERID_Pictures)} / L"Win32CaptureSample";

    m_item = item;
    m_device = device;
    m_pixelFormat = pixelFormat;

    auto d3dDevice = GetDXGIInterfaceFromObject<ID3D11Device>(m_device);
    d3dDevice->GetImmediateContext(m_d3dContext.put());

    m_swapChain = util::CreateDXGISwapChain(d3dDevice, static_cast<uint32_t>(m_item.Size().Width), static_cast<uint32_t>(m_item.Size().Height),
        static_cast<DXGI_FORMAT>(m_pixelFormat), 2);

    // Creating our frame pool with 'Create' instead of 'CreateFreeThreaded'
    // means that the frame pool's FrameArrived event is called on the thread
    // the frame pool was created on. This also means that the creating thread
    // must have a DispatcherQueue. If you use this method, it's best not to do
    // it on the UI thread. 
    m_framePool = winrt::Direct3D11CaptureFramePool::Create(m_device, m_pixelFormat, 2, m_item.Size());
    m_session = m_framePool.CreateCaptureSession(m_item);
    m_lastSize = m_item.Size();
    m_framePool.FrameArrived({ this, &SimpleCapture::OnFrameArrived });
}

void SimpleCapture::StartCapture()
{
    CheckClosed();
    m_session.StartCapture();
}

winrt::ICompositionSurface SimpleCapture::CreateSurface(winrt::Compositor const& compositor)
{
    CheckClosed();
    return util::CreateCompositionSurfaceForSwapChain(compositor, m_swapChain.get());
}

void SimpleCapture::Close()
{
    auto expected = false;
    if (m_closed.compare_exchange_strong(expected, true))
    {
        m_session.Close();
        m_framePool.Close();

        m_swapChain = nullptr;
        m_framePool = nullptr;
        m_session = nullptr;
        m_item = nullptr;
    }
}

void SimpleCapture::ResizeSwapChain()
{
    winrt::check_hresult(m_swapChain->ResizeBuffers(2, static_cast<uint32_t>(m_lastSize.Width), static_cast<uint32_t>(m_lastSize.Height),
        static_cast<DXGI_FORMAT>(m_pixelFormat), 0));
}

bool SimpleCapture::TryResizeSwapChain(winrt::Direct3D11CaptureFrame const& frame)
{
    auto const contentSize = frame.ContentSize();
    if ((contentSize.Width != m_lastSize.Width) ||
        (contentSize.Height != m_lastSize.Height))
    {
        // The thing we have been capturing has changed size, resize the swap chain to match.
        m_lastSize = contentSize;
        ResizeSwapChain();
        return true;
    }
    return false;
}

bool SimpleCapture::TryUpdatePixelFormat()
{
    auto newFormat = m_pixelFormatUpdate.exchange(std::nullopt);
    if (newFormat.has_value())
    {
        auto pixelFormat = newFormat.value();
        if (pixelFormat != m_pixelFormat)
        {
            m_pixelFormat = pixelFormat;
            ResizeSwapChain();
            return true;
        }
    }
    return false;
}

// UMU Add
void SimpleCapture::EnsureSaveDirectory()
{
    std::filesystem::create_directory(save_directory_);
}

HRESULT SimpleCapture::AutoSaveToDDSFile(const winrt::com_ptr<ID3D11Texture2D>& texture)
{
    const auto now = std::chrono::steady_clock::now();
    if (now - started_ < 1s)
    {
        return S_FALSE;
    }
    started_ = now;

    winrt::com_ptr<ID3D11Device> device;
    texture->GetDevice(device.put());
    winrt::com_ptr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.put());
    auto stagingTexture = util::PrepareStagingTexture(device, texture);
    D3D11_TEXTURE2D_DESC desc = {};
    stagingTexture->GetDesc(&desc);
    auto bytesPerPixel = util::GetBytesPerPixel(desc.Format);
    // Copy the bits
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    winrt::check_hresult(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));
    auto bytesStride = static_cast<size_t>(desc.Width) * bytesPerPixel;
    std::vector<byte> bytes(bytesStride * static_cast<size_t>(desc.Height), 0);
    auto source = reinterpret_cast<byte*>(mapped.pData);
    auto dest = bytes.data();
    for (auto i = 0; i < (int)desc.Height; i++)
    {
        memcpy(dest, source, bytesStride);
        source += mapped.RowPitch;
        dest += bytesStride;
    }
    context->Unmap(stagingTexture.get(), 0);
    DirectX::Image image{.width = desc.Width,
                         .height = desc.Height,
                         .format = desc.Format,
                         .rowPitch = mapped.RowPitch,
                         .slicePitch = bytes.size(),
                         .pixels = bytes.data()};
    auto filename = std::format(L"{}.dds", now.time_since_epoch().count());
    return DirectX::SaveToDDSFile(image, DirectX::DDS_FLAGS::DDS_FLAGS_NONE, (save_directory_ / filename).c_str());
}

HRESULT SimpleCapture::SaveToRgbaFile(const winrt::com_ptr<ID3D11Texture2D>& texture)
{
    if (!rgba_file_)
    {
        auto filename = std::format(L"{}.rgba", started_.time_since_epoch().count());
        rgba_file_.reset(CreateFile((save_directory_ / filename).c_str(), FILE_GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                    CREATE_NEW, 0, nullptr));
        if (!rgba_file_)
        {
            save_rgba_ = false;
            return HRESULT_FROM_WIN32(GetLastError());
        }
    }

    const auto now = std::chrono::steady_clock::now();
    if (now - started_ >= 2min)
    {
        save_rgba_ = false;
        rgba_file_.reset();
        return S_OK;
    }

    winrt::com_ptr<ID3D11Device> device;
    texture->GetDevice(device.put());
    winrt::com_ptr<ID3D11DeviceContext> context;
    device->GetImmediateContext(context.put());
    auto stagingTexture = util::PrepareStagingTexture(device, texture);
    D3D11_TEXTURE2D_DESC desc = {};
    stagingTexture->GetDesc(&desc);
    auto bytesPerPixel = util::GetBytesPerPixel(desc.Format);
    // Copy the bits
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    winrt::check_hresult(context->Map(stagingTexture.get(), 0, D3D11_MAP_READ, 0, &mapped));
    auto bytesStride = static_cast<size_t>(desc.Width) * bytesPerPixel;
    std::vector<byte> bytes(bytesStride * static_cast<size_t>(desc.Height), 0);
    auto source = reinterpret_cast<byte*>(mapped.pData);
    auto dest = bytes.data();
    for (auto i = 0; i < (int)desc.Height; i++)
    {
        memcpy(dest, source, bytesStride);
        source += mapped.RowPitch;
        dest += bytesStride;
    }
    context->Unmap(stagingTexture.get(), 0);
    DWORD written = 0;
    if (!WriteFile(rgba_file_.get(), bytes.data(), bytes.size(), &written, nullptr))
    {
        save_rgba_ = false;
        rgba_file_.reset();
        return HRESULT_FROM_WIN32(GetLastError());
    }
    return S_OK;
}

void SimpleCapture::OnFrameArrived(winrt::Direct3D11CaptureFramePool const& sender, winrt::IInspectable const&)
{
    auto swapChainResizedToFrame = false;

    {
        auto frame = sender.TryGetNextFrame();
        swapChainResizedToFrame = TryResizeSwapChain(frame);

        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        winrt::check_hresult(m_swapChain->GetBuffer(0, winrt::guid_of<ID3D11Texture2D>(), backBuffer.put_void()));
        auto surfaceTexture = GetDXGIInterfaceFromObject<ID3D11Texture2D>(frame.Surface());
        // copy surfaceTexture to backBuffer
        m_d3dContext->CopyResource(backBuffer.get(), surfaceTexture.get());

        // UMU Add
        if (auto_save_dds_)
        {
            AutoSaveToDDSFile(surfaceTexture);
        }
        else if (save_rgba_)
        {
            SaveToRgbaFile(surfaceTexture);
        }
    }

    DXGI_PRESENT_PARAMETERS presentParameters{};
    m_swapChain->Present1(1, 0, &presentParameters);

    swapChainResizedToFrame = swapChainResizedToFrame || TryUpdatePixelFormat();

    if (swapChainResizedToFrame)
    {
        m_framePool.Recreate(m_device, m_pixelFormat, 2, m_lastSize);
    }
}

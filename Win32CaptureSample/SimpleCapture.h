#pragma once

// UMU Add
#include <filesystem>

class SimpleCapture
{
public:
    SimpleCapture(
        winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice const& device,
        winrt::Windows::Graphics::Capture::GraphicsCaptureItem const& item,
        winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat);
    ~SimpleCapture() { Close(); }

    void StartCapture();
    winrt::Windows::UI::Composition::ICompositionSurface CreateSurface(
        winrt::Windows::UI::Composition::Compositor const& compositor);

    bool IsCursorEnabled() { CheckClosed(); return m_session.IsCursorCaptureEnabled(); }
    void IsCursorEnabled(bool value) { CheckClosed(); m_session.IsCursorCaptureEnabled(value); }
    bool IsBorderRequired() { CheckClosed(); return m_session.IsBorderRequired(); }
    void IsBorderRequired(bool value) { CheckClosed(); m_session.IsBorderRequired(value); }
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem CaptureItem() { return m_item; }

    void SetPixelFormat(winrt::Windows::Graphics::DirectX::DirectXPixelFormat pixelFormat)
    {
        CheckClosed();
        auto newFormat = std::optional(pixelFormat);
        m_pixelFormatUpdate.exchange(newFormat);
    }

    void Close();

    // UMU Add
    bool IsAutoSaveDds()
    {
        CheckClosed();
        return auto_save_dds_;
    }
    void IsAutoSaveDds(bool auto_save)
    {
        CheckClosed();
        auto_save_dds_ = auto_save;
        if (auto_save)
        {
            save_rgba_ = false;
            EnsureSaveDirectory();
            started_ = std::chrono::steady_clock::now();
        }
    }
    bool IsSaveRgba()
    {
        CheckClosed();
        return save_rgba_;
    }
    void IsSaveRgba(bool save)
    {
        CheckClosed();
        save_rgba_ = save;
        if (save)
        {
            auto_save_dds_ = false;
            EnsureSaveDirectory();
            started_ = std::chrono::steady_clock::now();
        }
    }

private:
    void OnFrameArrived(
        winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool const& sender,
        winrt::Windows::Foundation::IInspectable const& args);

    inline void CheckClosed()
    {
        if (m_closed.load() == true)
        {
            throw winrt::hresult_error(RO_E_CLOSED);
        }
    }

    void ResizeSwapChain();
    bool TryResizeSwapChain(winrt::Windows::Graphics::Capture::Direct3D11CaptureFrame const& frame);
    bool TryUpdatePixelFormat();

    // UMU Add
    void EnsureSaveDirectory();
    HRESULT AutoSaveToDDSFile(const winrt::com_ptr<ID3D11Texture2D>& texture);
    HRESULT SaveToRgbaFile(const winrt::com_ptr<ID3D11Texture2D>& texture);

private:
    winrt::Windows::Graphics::Capture::GraphicsCaptureItem m_item{ nullptr };
    winrt::Windows::Graphics::Capture::Direct3D11CaptureFramePool m_framePool{ nullptr };
    winrt::Windows::Graphics::Capture::GraphicsCaptureSession m_session{ nullptr };
    winrt::Windows::Graphics::SizeInt32 m_lastSize;

    winrt::Windows::Graphics::DirectX::Direct3D11::IDirect3DDevice m_device{ nullptr };
    winrt::com_ptr<IDXGISwapChain1> m_swapChain{ nullptr };
    winrt::com_ptr<ID3D11DeviceContext> m_d3dContext{ nullptr };
    winrt::Windows::Graphics::DirectX::DirectXPixelFormat m_pixelFormat;

    std::atomic<std::optional<winrt::Windows::Graphics::DirectX::DirectXPixelFormat>> m_pixelFormatUpdate = std::nullopt;

    std::atomic<bool> m_closed = false;
    std::atomic<bool> m_captureNextImage = false;

    // UMU Add
    bool auto_save_dds_{false};
    bool save_rgba_{false};
    std::filesystem::path save_directory_;
    std::chrono::steady_clock::time_point started_;
    wil::unique_hfile rgba_file_;
};
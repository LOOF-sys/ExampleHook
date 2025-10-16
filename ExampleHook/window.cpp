#include <iostream>
#include <dxgi.h>
#include <d3d11.h>
#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"
#include "ini.hpp"

// this can be set to a directory instead of a default file name from Discord.exe path
#define CONFIGURATION_FILE "config.ini"

#define C6031(value) C6031_resolve = (void*)value

static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static bool                     g_SwapChainOccluded = false;
static UINT                     g_ResizeWidth = 0, g_ResizeHeight = 0;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// Forward declarations of helper functions
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


/*
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
MOST OF THIS IS PASTED FROM AETHERCORD
*/

float amplification = 0;
extern "C" float getamplification()
{
    if (amplification <= 0.1) return 1;
    return pow(10, amplification / 10);
}

int noiseinjection = 0;
extern "C" short getnoiseinjection()
{
    return (short)noiseinjection;
}

int floatinjection = 0;
extern "C" int getfloatinjection()
{
    return floatinjection;
}

int32_t PacketBitrate = 248000;
int32_t GetPacketBitrate()
{
    return PacketBitrate;
}

int32_t EncodeBitrate = 512000;
extern "C" int32_t GetEncodeBitrate()
{
    return EncodeBitrate;
}

float PacketLossRate = 1;
float GetPacketLossRate()
{
    return PacketLossRate;
}

int PacketSkipRate = 300;
bool PacketSkippingDisabled = false;
int GetPacketSkipRate()
{
    return PacketSkipRate;
}

void LoadIniConfig()
{
    IniParser IniFile = CONFIGURATION_FILE;
    if (!IniFile.IsInitialized()) return;

    PTEXTLINE TextLine = IniFile.GetLineByName("float"); // does not support double/floats, only integers
    if (TextLine->Value)
    {
        auto value = (uint64_t)TextLine->Value;
        if (value > 0 && value < 120) amplification = (float)(uint64_t)TextLine->Value;
    }

    // float injection starting value
    TextLine = IniFile.GetLineByName("float2");
    if (TextLine->Value) floatinjection = (int)TextLine->Value;

    // noiseinjection starting value
    TextLine = IniFile.GetLineByName("noise");
    if (TextLine->Value)
    {
        auto value = (uint64_t)TextLine->Value;
        if (value > 0 && value < 32768) noiseinjection = value;
    }

    // encoder bitrate
    TextLine = IniFile.GetLineByName("enc_bitrate");
    if (TextLine->Value)
    {
        auto value = (uint64_t)TextLine->Value;
        if (value > 0 && value <= 5120000) EncodeBitrate = value;
    }

    // packet bitrate
    TextLine = IniFile.GetLineByName("pac_bitrate");
    if (TextLine->Value)
    {
        auto value = (uint64_t)TextLine->Value;
        if (value > 0 && value <= 248000) PacketBitrate = value;
    }

    // packet bitrate
    TextLine = IniFile.GetLineByName("pac_skipping_disabled");
    if (!TextLine->Value)
    {
        PTEXTLINE PacketSkipping = IniFile.GetLineByName("pac_skipping_rate");
        if (PacketSkipping->Value)
        {
            auto value = (uint64_t)PacketSkipping->Value;
            if (value > 0 && value <= 300) PacketSkipRate = value;
        }
    }
    else
    {
        PacketSkipRate = -1;
        PacketSkippingDisabled = true;
    }

    // packet loss rate (1-1000)
    TextLine = IniFile.GetLineByName("pac_loss");
    if (TextLine->Value)
    {
        auto value = (uint64_t)TextLine->Value;
        if (value > 0 && value <= 1000) PacketLossRate = (float)value / 1000;
    }

    IniFile.discard();
}

void RenderWindow()
{
    WNDCLASSEXW wc = { sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L, GetModuleHandleA(nullptr), nullptr, nullptr, nullptr, nullptr, (L"Example Hook"), nullptr };
    RegisterClassExW(&wc);
    HWND hwnd = CreateWindowExW(WS_EX_TOPMOST, wc.lpszClassName, (L"Example Hook"), WS_OVERLAPPEDWINDOW, 100, 100, 1, 1, nullptr, nullptr, wc.hInstance, nullptr);
    ULONG WindowStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
    WindowStyle = WindowStyle | WS_EX_TOOLWINDOW;
    WindowStyle = WindowStyle & ~(WS_EX_APPWINDOW);
    ShowWindow(hwnd, SW_HIDE);
    SetWindowLongA(hwnd, GWL_EXSTYLE, WindowStyle);
    ShowWindow(hwnd, SW_SHOW);
    SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
    LoadIniConfig();

    void* C6031_resolve;

    // Initialize Direct3D
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return;
    }

    // Show the window
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;      // Enable Gamepad Controls

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    //ImGui::StyleColorsLight();

    // Setup Platform/Renderer backends
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Our state
    bool show_demo_window = true;
    bool show_another_window = false;
    ImVec4 clear_color = ImVec4(0.45f, 0.55f, 0.60f, 1.00f);

    bool HiddenWindow = true;
    bool ToggleConsole = true;
    bool SetupWindow = false;
    bool Closed = false;
    while (!Closed)
    {
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT) Closed = true;
        }

        // Handle window being minimized or screen locked
        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED)
        {
            ::Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Handle window resize (we don't resize directly in the WM_SIZE handler)
        if (g_ResizeWidth != 0 && g_ResizeHeight != 0)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, g_ResizeWidth, g_ResizeHeight, DXGI_FORMAT_UNKNOWN, 0);
            g_ResizeWidth = g_ResizeHeight = 0;
            CreateRenderTarget();
        }

        // Start the Dear ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // change these strings and add/remove features
        if (ImGui::Begin("Example Hook | https://github.com/LOOF-sys | wcypher on discord", 0, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysVerticalScrollbar))
        {
            ImGui::StyleColorsCustom(0);
            RECT WindowRectangle = {};
            if (GetWindowRect(hwnd, &WindowRectangle))
            {
                ImGui::SetWindowSize(ImVec2(WindowRectangle.right - WindowRectangle.left, WindowRectangle.bottom - WindowRectangle.top));
                ImGui::SetWindowPos(ImVec2(0, 0));
                if (!SetupWindow)
                {
                    MoveWindow(hwnd, WindowRectangle.left, WindowRectangle.top, 800, 600, false);
                    SetupWindow = true;
                }
            }
            ImGui::Text("for more content like this, visit https://discord.gg/xjrrth8wap");
            ImGui::SliderFloat("Float dB", &amplification, 0, 96.2, "%.2f", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("Noise Injection", &noiseinjection, 0, 32767, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderInt("Encode Bitrate", &EncodeBitrate, 8000, 512000, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::NewLine();
            ImGui::Text("Audio Packet");
            ImGui::SliderInt("Packet Bitrate", &PacketBitrate, 8000, 248000, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::SliderFloat("Packet Loss Rate", (float*)&PacketLossRate, 0, 1, "%.3f", ImGuiSliderFlags_AlwaysClamp);
            if (!PacketSkippingDisabled) ImGui::SliderInt("Packet Acceptance Rate", &PacketSkipRate, 2, 300, "%d", ImGuiSliderFlags_AlwaysClamp);
            ImGui::Checkbox("Disable Packet Skipping", &PacketSkippingDisabled);
            ImGui::Text("For \"Packet Bitrate\" to apply, mute and then unmute your mic on discord");
            ImGui::NewLine();
            ImGui::Text("Generic");
            if (ImGui::Checkbox("Toggle Console", &ToggleConsole))
            {
                if (ToggleConsole) // console enabled
                {
                    DWORD ProcessId = GetCurrentProcessId();
                    if (!AttachConsole(ProcessId) && GetLastError() != ERROR_ACCESS_DENIED)
                    {
                        if (!AllocConsole())
                        {
                            //MessageBoxA(NULL, xorstr("Failed to allocate console"), xorstr("Discord"), MB_ICONERROR);
                            continue;
                        }
                    }
                    C6031(freopen("conin$", "r", stdin));
                    C6031(freopen("conout$", "w", stdout));
                    C6031(freopen("conout$", "w", stderr));
                    printf("console enabled\n");
                }
                else // console disabled
                {
                    FreeConsole();
                }
            }
            ImGui::Checkbox("Hide Window", &HiddenWindow);
            ImGui::NewLine();
            ImGui::Text("Experimental Features");
            ImGui::SliderInt("Float Injection", &floatinjection, 0, 16777216);
            if (HiddenWindow) SetWindowDisplayAffinity(hwnd, WDA_EXCLUDEFROMCAPTURE);
            else SetWindowDisplayAffinity(hwnd, WDA_NONE);
            ImGui::End();
        }

        if (PacketSkippingDisabled) PacketSkipRate = UINT64_MAX;

        // Rendering
        ImGui::Render();
        const float clear_color_with_alpha[4] = { clear_color.x * clear_color.w, clear_color.y * clear_color.w, clear_color.z * clear_color.w, clear_color.w };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color_with_alpha);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present
        HRESULT hr = g_pSwapChain->Present(1, 0);   // Present with vsync
        //HRESULT hr = g_pSwapChain->Present(0, 0); // Present without vsync
        g_SwapChainOccluded = (hr == DXGI_STATUS_OCCLUDED);
    }


    // Cleanup
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
}


bool CreateDeviceD3D(HWND hWnd)
{
    // Setup swap chain
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    //createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED) // Try high-performance WARP software driver if hardware is not available.
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// Win32 message handler
// You can read the io.WantCaptureMouse, io.WantCaptureKeyboard flags to tell if dear imgui wants to use your inputs.
// - When io.WantCaptureMouse is true, do not dispatch mouse input data to your main application, or clear/overwrite your copy of the mouse data.
// - When io.WantCaptureKeyboard is true, do not dispatch keyboard input data to your main application, or clear/overwrite your copy of the keyboard data.
// Generally you may always pass all inputs to dear imgui, and hide them from your application based on those two flags.
WPARAM LastKey = 0;
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (wParam == SIZE_MINIMIZED)
            return 0;
        g_ResizeWidth = (UINT)LOWORD(lParam); // Queue resize
        g_ResizeHeight = (UINT)HIWORD(lParam);
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}
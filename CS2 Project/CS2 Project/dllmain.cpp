#include "pch.h"
#include <windows.h>
#include <d3d11.h>
#include <dxgi.h>
#include <psapi.h>
#include <cstdint>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "psapi.lib")

typedef void(*tSetupHook)(void* targetFn, void* hookFn, void** originalFn, int a4, const char* name);
typedef HRESULT(WINAPI* tPresent)(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags);

HMODULE  g_hModule = nullptr;
tPresent oPresent = nullptr;

ID3D11Device* g_pd3dDevice = nullptr;
ID3D11DeviceContext* g_pd3dContext = nullptr;
ID3D11RenderTargetView* g_mainRTV = nullptr;
bool                    g_bInit = false;
UINT                    g_width = 0;
UINT                    g_height = 0;

static void InitD3D(IDXGISwapChain* pSwapChain)
{
    if (g_bInit) return;

    if (FAILED(pSwapChain->GetDevice(__uuidof(ID3D11Device), (void**)&g_pd3dDevice)))
        return;

    g_pd3dDevice->GetImmediateContext(&g_pd3dContext);

    ID3D11Texture2D* pBB = nullptr;
    if (SUCCEEDED(pSwapChain->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)&pBB)))
    {
        D3D11_TEXTURE2D_DESC desc = {};
        pBB->GetDesc(&desc);
        g_width = desc.Width;
        g_height = desc.Height;
        g_pd3dDevice->CreateRenderTargetView(pBB, nullptr, &g_mainRTV);
        pBB->Release();
    }

    g_bInit = true;
}

static void CleanupD3D()
{
    g_bInit = false;
    if (g_mainRTV) { g_mainRTV->Release();     g_mainRTV = nullptr; }
    if (g_pd3dContext) { g_pd3dContext->Release();  g_pd3dContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release();   g_pd3dDevice = nullptr; }
}

static void RenderOverlay(IDXGISwapChain* pSwapChain)
{
    if (!g_bInit)
        InitD3D(pSwapChain);

    if (!g_pd3dContext || !g_mainRTV)
        return;

    ID3D11RenderTargetView* pOldRTV = nullptr;
    ID3D11DepthStencilView* pOldDSV = nullptr;
    UINT numVP = 1;
    D3D11_VIEWPORT oldVP = {};
    g_pd3dContext->OMGetRenderTargets(1, &pOldRTV, &pOldDSV);
    g_pd3dContext->RSGetViewports(&numVP, &oldVP);
    g_pd3dContext->OMSetRenderTargets(1, &g_mainRTV, nullptr);

    const float sz = 50.0f;
    D3D11_VIEWPORT vp = {};
    vp.TopLeftX = (static_cast<float>(g_width) - sz) / 2.0f;
    vp.TopLeftY = (static_cast<float>(g_height) - sz) / 2.0f;
    vp.Width = sz;
    vp.Height = sz;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    g_pd3dContext->RSSetViewports(1, &vp);

    const float green[4] = { 0.0f, 0.2f, 1.0f, 1.0f };
    g_pd3dContext->ClearRenderTargetView(g_mainRTV, green);

    g_pd3dContext->OMSetRenderTargets(1, &pOldRTV, pOldDSV);
    g_pd3dContext->RSSetViewports(1, &oldVP);
    if (pOldRTV) pOldRTV->Release();
    if (pOldDSV) pOldDSV->Release();
}

HRESULT WINAPI HKPresent(IDXGISwapChain* pSwapChain, UINT SyncInterval, UINT Flags)
{
    RenderOverlay(pSwapChain);
    return oPresent(pSwapChain, SyncInterval, Flags);
}

DWORD WINAPI MainThread(LPVOID)
{

    HMODULE hOverlay = nullptr;
    while (!(hOverlay = GetModuleHandleA("GameOverlayRenderer64.dll")))
        Sleep(100);

    uint64_t base = reinterpret_cast<uint64_t>(hOverlay);

    tSetupHook HookLikeSteam = reinterpret_cast<tSetupHook>(base + 0x8DAB0); //48 89 5C 24 ? 57 48 83 EC ? 33 C0

    //__int64 __fastcall sub_8DAB0(const WCHAR * a1, __int64 a2, _QWORD * a3, int a4, __int64 a5)
    //{
    //    __int64 result; // rax
    //    __int64 v6; // rbx
    //    unsigned __int8 v7; // di
    //    __int64 v8; // [rsp+50h] [rbp+18h] BYREF

    //    v8 = 0;
    //    if (a3)
    //        *a3 = 0;
    //    result = sub_8CF00(a1, a4, a5);
    //    v6 = v8;
    //    v7 = result;
    //    if (v8)
    //    {
    //        if (WaitForSingleObject(hHandle, 0x3E8u))
    //            sub_AB180("Couldn't get trampoline region lock, will continue possibly unsafely.\n");
    //        v8 = v6;
    //        if (*((_QWORD*)&xmmword_167228 + 1) == qword_167238)
    //        {
    //            sub_6DCE0(&xmmword_167228, *((_QWORD*)&xmmword_167228 + 1), &v8);
    //        }
    //        else
    //        {
    //            **((_QWORD**)&xmmword_167228 + 1) = v6;
    //            *((_QWORD*)&xmmword_167228 + 1) += 8LL;
    //        }
    //        ReleaseMutex(hHandle);
    //        return v7;
    //    }
    //    return result;
    //}

    void* steamPresent = reinterpret_cast<void*>    (base + 0x93E50); //48 89 5C 24 ? 48 89 6C 24 ? 56 57 41 54 41 56 41 57 48 83 EC ? 41 8B F0

    HookLikeSteam(
        steamPresent,
        reinterpret_cast<void*>(&HKPresent),
        reinterpret_cast<void**>(&oPresent),
        1,
        "DXGISwapChain_Present"
    );

    while (!(GetAsyncKeyState(VK_END) & 0x8000))
        Sleep(100);

    if (oPresent)
    {
        HookLikeSteam(
            reinterpret_cast<void*>(&HKPresent),
            reinterpret_cast<void*>(oPresent),
            reinterpret_cast<void**>(&oPresent),
            1,
            "DXGISwapChain_Present_Restore"
        );
    }

    CleanupD3D();
    FreeLibraryAndExitThread(g_hModule, 0);
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID)
{
    if (ul_reason_for_call == DLL_PROCESS_ATTACH)
    {
        g_hModule = hModule;
        DisableThreadLibraryCalls(hModule);
        CreateThread(nullptr, 0, MainThread, nullptr, 0, nullptr);
    }
    return TRUE;
}
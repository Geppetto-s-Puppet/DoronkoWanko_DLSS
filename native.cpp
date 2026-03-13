#include <wrl.h>
#include <thread>
#include <string>
#include <vector>
#include <windows.h>

#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;

// ===== グローバル =====
HWND g_hWnd = nullptr;

ComPtr<ID3D12Device>              g_Device;
ComPtr<IDXGISwapChain3>           g_SwapChain;
ComPtr<ID3D12CommandQueue>        g_CommandQueue;
ComPtr<ID3D12CommandAllocator>    g_CommandAllocator;
ComPtr<ID3D12GraphicsCommandList> g_CommandList;
ComPtr<ID3D12DescriptorHeap>      g_RTVHeap;
UINT                              g_RTVDescriptorSize = 0;
ComPtr<ID3D12DescriptorHeap>      g_SRVHeap;

ComPtr<ID3D12Resource>            g_RenderTargets[2];
UINT                              g_FrameIndex = 0;

ComPtr<ID3D12Fence>               g_Fence;
UINT64                            g_FenceValue = 0;
HANDLE                            g_FenceEvent = nullptr;

ComPtr<ID3D12RootSignature>       g_RootSignature;
ComPtr<ID3D12PipelineState>       g_PSO;

ComPtr<ID3D12Resource>            g_UnityTexture;

// ===== シェーダ =====
const char* g_VS = R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
    VSOut o;

    float2 pos = float2(
        (id == 2) ? 3.0 : -1.0,
        (id == 1) ? 3.0 : -1.0
    );

    o.pos = float4(pos, 0.0, 1.0);

    // Unity の RT は上下反転しているので V を反転
    float2 uv = (pos + 1.0) * 0.5;
    uv.y = 1.0 - uv.y;
    o.uv = uv;

    return o;
}
)";

const char* g_PS = R"(
Texture2D tex0 : register(t0);
SamplerState samp : register(s0);

float4 main(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_TARGET
{
    return tex0.Sample(samp, uv);
}
)";

// ===== ユーティリティ =====
void CreateOverlayWindow(UINT w, UINT h)
{
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"DWOverlayClass";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);

    RegisterClassEx(&wc);

    DWORD style = WS_POPUP;
    DWORD exStyle = WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;

    g_hWnd = CreateWindowEx(
        exStyle,
        wc.lpszClassName,
        L"DWOverlay",
        style,
        0, 0, w, h,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
}

void UpdateSRV(ID3D12Resource* tex)
{
    if (!tex || !g_Device || !g_SRVHeap) return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = tex->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        g_SRVHeap->GetCPUDescriptorHandleForHeapStart(),
        0,
        g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    );

    g_Device->CreateShaderResourceView(tex, &srvDesc, handle);
}

void RenderOverlay()
{
    if (!g_SwapChain || !g_CommandAllocator || !g_CommandList) return;

    g_CommandAllocator->Reset();
    g_CommandList->Reset(g_CommandAllocator.Get(), g_PSO.Get());

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        g_RTVHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += g_FrameIndex * g_RTVDescriptorSize;

    g_CommandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

    DXGI_SWAP_CHAIN_DESC scDesc = {};
    g_SwapChain->GetDesc(&scDesc);

    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = static_cast<float>(scDesc.BufferDesc.Width);
    vp.Height = static_cast<float>(scDesc.BufferDesc.Height);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = scDesc.BufferDesc.Width;
    scissor.bottom = scDesc.BufferDesc.Height;

    g_CommandList->RSSetViewports(1, &vp);
    g_CommandList->RSSetScissorRects(1, &scissor);

    g_CommandList->SetGraphicsRootSignature(g_RootSignature.Get());

    ID3D12DescriptorHeap* heaps[] = { g_SRVHeap.Get() };
    g_CommandList->SetDescriptorHeaps(1, heaps);

    g_CommandList->SetGraphicsRootDescriptorTable(
        0,
        g_SRVHeap->GetGPUDescriptorHandleForHeapStart()
    );

    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->DrawInstanced(3, 1, 0, 0);

    g_CommandList->Close();
    ID3D12CommandList* lists[] = { g_CommandList.Get() };
    g_CommandQueue->ExecuteCommandLists(1, lists);

    g_SwapChain->Present(1, 0);

    // ★ フェンス同期はここではしない
    g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
}


// ===== エクスポート =====
extern "C"
{

    __declspec(dllexport)
        void DW_Init(void* unityTexturePtr)

    {

        return;

        if (!unityTexturePtr) return;

        g_UnityTexture = reinterpret_cast<ID3D12Resource*>(unityTexturePtr);

        if (FAILED(g_UnityTexture->GetDevice(IID_PPV_ARGS(&g_Device))))
            return;

        D3D12_RESOURCE_DESC uDesc = g_UnityTexture->GetDesc();
        UINT sw = static_cast<UINT>(uDesc.Width);
        UINT sh = uDesc.Height;

        CreateOverlayWindow(sw, sh);

        // CommandQueue
        {
            D3D12_COMMAND_QUEUE_DESC qDesc = {};
            qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
            if (FAILED(g_Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_CommandQueue))))
                return;
        }

        // SwapChain
        {
            ComPtr<IDXGIFactory6> factory;
            if (FAILED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory))))
                return;

            DXGI_SWAP_CHAIN_DESC1 sc = {};
            sc.BufferCount = 2;
            sc.Width = sw;
            sc.Height = sh;
            sc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            sc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
            sc.SampleDesc.Count = 1;

            ComPtr<IDXGISwapChain1> sc1;
            if (FAILED(factory->CreateSwapChainForHwnd(
                g_CommandQueue.Get(),
                g_hWnd,
                &sc,
                nullptr,
                nullptr,
                &sc1)))
                return;

            sc1->QueryInterface(IID_PPV_ARGS(&g_SwapChain));
            g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
        }

        // RTV Heap + BackBuffers
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.NumDescriptors = 2;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
            if (FAILED(g_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_RTVHeap))))
                return;

            g_RTVDescriptorSize =
                g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

            CD3DX12_CPU_DESCRIPTOR_HANDLE h(g_RTVHeap->GetCPUDescriptorHandleForHeapStart());
            for (int i = 0; i < 2; ++i)
            {
                g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&g_RenderTargets[i]));
                g_Device->CreateRenderTargetView(g_RenderTargets[i].Get(), nullptr, h);
                h.Offset(1, g_RTVDescriptorSize);
            }
        }

        // SRV Heap (t0)
        {
            D3D12_DESCRIPTOR_HEAP_DESC desc = {};
            desc.NumDescriptors = 1;
            desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
            if (FAILED(g_Device->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&g_SRVHeap))))
                return;
        }

        // CommandAllocator / CommandList
        {
            if (FAILED(g_Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_CommandAllocator))))
                return;

            if (FAILED(g_Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_CommandAllocator.Get(),
                nullptr,
                IID_PPV_ARGS(&g_CommandList))))
                return;

            g_CommandList->Close();
        }

        // Fence
        {
            if (FAILED(g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence))))
                return;
            g_FenceValue = 0;
            g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        }

        // RootSignature + PSO
        {
            // SRV1 + Sampler1 の最小構成
            D3D12_DESCRIPTOR_RANGE range = {};
            range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            range.NumDescriptors = 1;
            range.BaseShaderRegister = 0;
            range.RegisterSpace = 0;
            range.OffsetInDescriptorsFromTableStart = 0;

            D3D12_ROOT_PARAMETER param = {};
            param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            param.DescriptorTable.NumDescriptorRanges = 1;
            param.DescriptorTable.pDescriptorRanges = &range;
            param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_STATIC_SAMPLER_DESC samp = {};
            samp.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            samp.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            samp.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            samp.ShaderRegister = 0; // s0
            samp.RegisterSpace = 0;
            samp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rs = {};
            rs.NumParameters = 1;
            rs.pParameters = &param;
            rs.NumStaticSamplers = 1;
            rs.pStaticSamplers = &samp;
            rs.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ComPtr<ID3DBlob> sig, err;
            if (FAILED(D3D12SerializeRootSignature(&rs, D3D_ROOT_SIGNATURE_VERSION_1, &sig, &err)))
                return;

            if (FAILED(g_Device->CreateRootSignature(
                0,
                sig->GetBufferPointer(),
                sig->GetBufferSize(),
                IID_PPV_ARGS(&g_RootSignature))))
                return;

            ComPtr<ID3DBlob> vs, ps;
            if (FAILED(D3DCompile(g_VS, strlen(g_VS), nullptr, nullptr, nullptr,
                "main", "vs_5_0", 0, 0, &vs, &err)))
                return;

            if (FAILED(D3DCompile(g_PS, strlen(g_PS), nullptr, nullptr, nullptr,
                "main", "ps_5_0", 0, 0, &ps, &err)))
                return;

            D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {};
            pso.pRootSignature = g_RootSignature.Get();
            pso.VS = { vs->GetBufferPointer(), vs->GetBufferSize() };
            pso.PS = { ps->GetBufferPointer(), ps->GetBufferSize() };
            pso.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            pso.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            pso.DepthStencilState.DepthEnable = FALSE;
            pso.DepthStencilState.StencilEnable = FALSE;
            pso.SampleMask = UINT_MAX;
            pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            pso.NumRenderTargets = 1;
            pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            pso.SampleDesc.Count = 1;

            if (FAILED(g_Device->CreateGraphicsPipelineState(&pso, IID_PPV_ARGS(&g_PSO))))
                return;
        }

        // 初回 SRV
        UpdateSRV(g_UnityTexture.Get());
    }

    __declspec(dllexport)
        void DW_Release()
    {
        if (g_CommandQueue && g_Fence)
        {
            const UINT64 fenceToWait = ++g_FenceValue;
            g_CommandQueue->Signal(g_Fence.Get(), fenceToWait);
            if (g_Fence->GetCompletedValue() < fenceToWait)
            {
                g_Fence->SetEventOnCompletion(fenceToWait, g_FenceEvent);
                WaitForSingleObject(g_FenceEvent, INFINITE);
            }
        }

        for (int i = 0; i < 2; ++i)
            g_RenderTargets[i].Reset();

        g_PSO.Reset();
        g_RootSignature.Reset();
        g_SRVHeap.Reset();
        g_RTVHeap.Reset();
        g_CommandList.Reset();
        g_CommandAllocator.Reset();
        g_CommandQueue.Reset();
        g_SwapChain.Reset();
        g_Device.Reset();
        g_UnityTexture.Reset();
        g_Fence.Reset();

        if (g_FenceEvent)
        {
            CloseHandle(g_FenceEvent);
            g_FenceEvent = nullptr;
        }

        if (g_hWnd)
        {
            DestroyWindow(g_hWnd);
            g_hWnd = nullptr;
        }
    }

    extern "C" __declspec(dllexport)
        void DW_Update(void* unityTexturePtr)
    {
        // 一旦、何もしない
        // g_UnityTexture = reinterpret_cast<ID3D12Resource*>(unityTexturePtr);
        // if (!g_UnityTexture || !g_Device || !g_SRVHeap || !g_SwapChain)
        //     return;
        //
        // UpdateSRV(g_UnityTexture.Get());
        // RenderOverlay();
    }


} // extern "C"

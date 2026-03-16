//#include <wrl.h>
//#include <thread>
//#include <vector>
#include <string>
#include <fstream>
#include "d3dx12.h"
#include <dxgi1_6.h>
#include <windows.h>
#include <d3dcompiler.h>
using Microsoft::WRL::ComPtr;
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")

// グローバル変数
static HWND g_hWnd = nullptr;
static ID3D12Resource* g_UnityTexture = nullptr;
ComPtr<ID3D12Device>       g_Device;
ComPtr<ID3D12CommandQueue> g_CommandQueue;
ComPtr<IDXGISwapChain3>    g_SwapChain;
UINT g_FrameIndex;
ComPtr<ID3D12DescriptorHeap> g_RtvHeap;
ComPtr<ID3D12DescriptorHeap> g_CbvSrvHeap;
ComPtr<ID3D12Resource>       g_BackBuffers[2];
UINT g_RtvDescSize;
ComPtr<ID3D12CommandAllocator>    g_CmdAlloc[2];
ComPtr<ID3D12GraphicsCommandList> g_CmdList;
ComPtr<ID3D12Fence>               g_Fence;
UINT64 g_FenceValue[2] = {};
HANDLE g_FenceEvent = nullptr;
ComPtr<ID3D12RootSignature> g_RootSignature;
ComPtr<ID3D12Resource>      g_DefaultZeroNum;
ComPtr<ID3D12Resource>      g_DefaultBlackTex;

ComPtr<ID3D12PipelineState> g_PSO;
static std::wstring g_ShaderPath;
static const char* g_ShaderHelper = R"(
cbuffer CB0 : register(b0)
{
    float TIME;
    float DELTA;
    float2 RES;
    float4x4 VIEW;
    float4x4 PROJ;
}
SamplerState smp : register(s0);
Texture2D tColor   : register(t0);
Texture2D tDepth   : register(t1);
Texture2D tOpaque  : register(t2);
Texture2D tNormal  : register(t3);
Texture2D tSSAO    : register(t4);
Texture2D tMotion  : register(t5);
Texture2D tShadow  : register(t6);
Texture2D tShadow2 : register(t7);

struct VSOut { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VSOut VS(uint id : SV_VertexID)
{
    VSOut o;
    o.uv  = float2((id & 1) * 2.0, (id >> 1) * 2.0);
    o.pos = float4(o.uv * float2(2, -2) + float2(-1, 1), 0, 1);
    return o;
}

float4 Main(float2 uv, float4 pos);
float4 PS(VSOut i) : SV_Target
{
    return Main(i.uv, i.pos);
}
)";
ComPtr<ID3D12Resource> g_CBuffer;
void* g_CBufferPtr = nullptr;

// エクスポート
extern "C"
{
    __declspec(dllexport) void DW_Init(void* probeTexture)
    {
        // コンソール
        AllocConsole();
        freopen("CONOUT$", "w", stdout);
        HWND hConsole = GetConsoleWindow();
        SetWindowLong(hConsole, GWL_EXSTYLE, WS_EX_LAYERED);
        SetLayeredWindowAttributes(hConsole, 0, 200, LWA_ALPHA);
        // デバイス取得
        g_UnityTexture = reinterpret_cast<ID3D12Resource*>(probeTexture);
        g_UnityTexture->GetDevice(IID_PPV_ARGS(&g_Device));
        // ウィンドウ作成
        HINSTANCE hInst = GetModuleHandle(nullptr);
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"DWOverlayClass";
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        RegisterClassEx(&wc);
        g_hWnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            wc.lpszClassName,
            L"DWOverlay",
            WS_POPUP,
            0, 0, 1920, 1080,
            nullptr, nullptr, hInst, nullptr
        );
        ShowWindow(g_hWnd, SW_SHOW);
        // コマンドキュー
        D3D12_COMMAND_QUEUE_DESC qDesc = {};
        qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        g_Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_CommandQueue));
        // スワップチェーン
        ComPtr<IDXGIFactory6> factory;
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
        DXGI_SWAP_CHAIN_DESC1 sc = {};
        sc.BufferCount = 2;
        sc.Width       = 1920;
        sc.Height      = 1080;
        sc.Format      = DXGI_FORMAT_R16G16B16A16_FLOAT;
        sc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        sc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        sc.SampleDesc  = { 1, 0 };
        ComPtr<IDXGISwapChain1> sc1;
        factory->CreateSwapChainForHwnd(
            g_CommandQueue.Get(),
            g_hWnd,
            &sc,
            nullptr,
            nullptr,
            &sc1);
        sc1->QueryInterface(IID_PPV_ARGS(&g_SwapChain));
        g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
        // RTVヒープとバックバッファ
        D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
        rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        rtvDesc.NumDescriptors = 2;
        g_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_RtvHeap));
        g_RtvDescSize = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE h(g_RtvHeap->GetCPUDescriptorHandleForHeapStart());
        for (int i = 0; i < 2; i++)
        {
            g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&g_BackBuffers[i]));
            g_Device->CreateRenderTargetView(g_BackBuffers[i].Get(), nullptr, h);
            h.Offset(1, g_RtvDescSize);
        }
        // CBVヒープとSRVヒープ（それぞれ8個ずつ）
        D3D12_DESCRIPTOR_HEAP_DESC csDesc = {};
        csDesc.NumDescriptors = 16;
        csDesc.Type           = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        csDesc.Flags          = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        g_Device->CreateDescriptorHeap(&csDesc, IID_PPV_ARGS(&g_CbvSrvHeap));
        // コマンドアロケーター
        for (int i = 0; i < 2; i++)
            g_Device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_CmdAlloc[i]));
        // コマンドリスト
        g_Device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_CmdAlloc[0].Get(), nullptr, IID_PPV_ARGS(&g_CmdList));
        g_CmdList->Close();
        // フェンス
        g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence));
        g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        // ルートシグネチャ（未使用スロットは零定数または黒画像で埋めておく）
        CD3DX12_DESCRIPTOR_RANGE1 ranges[2];
        ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 8, 0); // CBV b0〜b7
        ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 8, 0); // SRV t0〜t7
        CD3DX12_ROOT_PARAMETER1 param;
        param.InitAsDescriptorTable(2, ranges);
        CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rsDesc;
        rsDesc.Init_1_1(1, &param, 0, nullptr, D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);
        ComPtr<ID3DBlob> blob, err;
        D3DX12SerializeVersionedRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1_1, &blob, &err);
        g_Device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_RootSignature));
        auto cbHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
        auto cbDesc = CD3DX12_RESOURCE_DESC::Buffer(256);
        g_Device->CreateCommittedResource(&cbHeapProp, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_DefaultZeroNum));
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_DefaultZeroNum->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = 256;
        auto texHeapProp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
        auto texDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, 1, 1);
        g_Device->CreateCommittedResource(&texHeapProp, D3D12_HEAP_FLAG_NONE, &texDesc,
            D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, nullptr, IID_PPV_ARGS(&g_DefaultBlackTex));
        UINT step = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        CD3DX12_CPU_DESCRIPTOR_HANDLE cs(g_CbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
        for (int i = 0; i < 8; i++)
        {
            CD3DX12_CPU_DESCRIPTOR_HANDLE cbv = cs; cbv.Offset(i, step);
            g_Device->CreateConstantBufferView(&cbvDesc, cbv);
            CD3DX12_CPU_DESCRIPTOR_HANDLE srv = cs; srv.Offset(i + 8, step);
            g_Device->CreateShaderResourceView(g_DefaultBlackTex.Get(), nullptr, srv);
        }
    }

    __declspec(dllexport) void DW_Update(void** texPtrs, const wchar_t* shaderPath, float* constants, int constantCount)
    {
        ShowWindow(g_hWnd, shaderPath ? SW_SHOW : SW_HIDE); // shaderPathがnullならオーバレイ非表示
        
        if (shaderPath && g_ShaderPath != shaderPath) { g_ShaderPath = shaderPath;
            // 動的結合式シェーダーコンパイル
            std::ifstream f(shaderPath, std::ios::binary);
            std::string userCode(std::istreambuf_iterator<char>(f), {});
            std::string fullSource = std::string(g_ShaderHelper) + "\n" + userCode;
            ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
            HRESULT hrVS = D3DCompile( fullSource.c_str(),fullSource.size(),
                nullptr, nullptr, nullptr, "VS", "vs_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vsBlob, &errBlob);
            if (FAILED(hrVS)) { if (errBlob) printf("VS error: %s\n", (char*)errBlob->GetBufferPointer()); return; }
            HRESULT hrPS = D3DCompile( fullSource.c_str(), fullSource.size(),
                nullptr, nullptr, nullptr, "PS", "ps_5_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &psBlob, &errBlob);
            if (FAILED(hrPS)) { if (errBlob) printf("PS error: %s\n", (char*)errBlob->GetBufferPointer()); return; }
            // パイプラインステートオブジェクト再生成
            g_CommandQueue->Signal(g_Fence.Get(), ++g_FenceValue[g_FrameIndex]);
            g_Fence->SetEventOnCompletion(g_FenceValue[g_FrameIndex], g_FenceEvent);
            WaitForSingleObject(g_FenceEvent, INFINITE);
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = g_RootSignature.Get();
            psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
            psoDesc.SampleDesc = { 1, 0 };
            ComPtr<ID3D12PipelineState> newPSO; printf("%s\n", fullSource.c_str());
            if (SUCCEEDED(g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&newPSO)))) { g_PSO = newPSO; }
        }

        // CBV上書き
        if (!g_CBuffer) {
            auto hp = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
            auto rd = CD3DX12_RESOURCE_DESC::Buffer(256);
            g_Device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
                D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_CBuffer));
            g_CBuffer->Map(0, nullptr, &g_CBufferPtr);
            D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
            cbvDesc.BufferLocation = g_CBuffer->GetGPUVirtualAddress();
            cbvDesc.SizeInBytes = 256;
            g_Device->CreateConstantBufferView(&cbvDesc,
                g_CbvSrvHeap->GetCPUDescriptorHandleForHeapStart()); // slot 0 = b0
        }
        memcpy(g_CBufferPtr, constants, sizeof(float) * constantCount);

        // SRV上書き
        // 送られてきたtexPtrsが有効であればコピー
        // コピー後リソースやconstantsをSRV&CBV登録
        //一度コピーしておかないと、チラつく(直メモリなため)
        if (texPtrs) {
            UINT step = g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            for (int i = 0; i < 8; i++) {
                auto* tex = reinterpret_cast<ID3D12Resource*>(texPtrs[i]);
                if (!tex) continue;

                D3D12_RESOURCE_DESC desc = tex->GetDesc();
                D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format = desc.Format;
                srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                srvDesc.Texture2D.MipLevels = desc.MipLevels;

                CD3DX12_CPU_DESCRIPTOR_HANDLE h(
                    g_CbvSrvHeap->GetCPUDescriptorHandleForHeapStart());
                h.Offset(8 + i, step); // slot 8〜15 = t0〜t7

                g_Device->CreateShaderResourceView(tex, &srvDesc, h);
            }
        }

        // ── ここから Draw テスト ──────────────────────────

        g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
        g_CmdAlloc[g_FrameIndex]->Reset();
        g_CmdList->Reset(g_CmdAlloc[g_FrameIndex].Get(), g_PSO.Get());
        // バックバッファをRTに遷移
        auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(
            g_BackBuffers[g_FrameIndex].Get(),
            D3D12_RESOURCE_STATE_PRESENT,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        g_CmdList->ResourceBarrier(1, &barrier);
        // RTV セット
        CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(
            g_RtvHeap->GetCPUDescriptorHandleForHeapStart(),
            g_FrameIndex, g_RtvDescSize);
        g_CmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        // クリア（確認用に赤くしておく）
        float clearColor[] = { 0, 0, 0, 1 };
        g_CmdList->ClearRenderTargetView(rtv, clearColor, 0, nullptr);
        // ビューポート
        D3D12_VIEWPORT vp = { 0, 0, 1920, 1080, 0, 1 };
        D3D12_RECT sc = { 0, 0, 1920, 1080 };
        g_CmdList->RSSetViewports(1, &vp);
        g_CmdList->RSSetScissorRects(1, &sc);
        // 描画
        g_CmdList->SetGraphicsRootSignature(g_RootSignature.Get());
        ID3D12DescriptorHeap* heaps[] = { g_CbvSrvHeap.Get() };
        g_CmdList->SetDescriptorHeaps(1, heaps);
        g_CmdList->SetGraphicsRootDescriptorTable(0, g_CbvSrvHeap->GetGPUDescriptorHandleForHeapStart());
        g_CmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        g_CmdList->DrawInstanced(3, 1, 0, 0);
        // Presentに遷移
        auto barrier2 = CD3DX12_RESOURCE_BARRIER::Transition(
            g_BackBuffers[g_FrameIndex].Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_PRESENT);
        g_CmdList->ResourceBarrier(1, &barrier2);
        g_CmdList->Close();
        ID3D12CommandList* lists[] = { g_CmdList.Get() };
        g_CommandQueue->ExecuteCommandLists(1, lists);
        g_SwapChain->Present(1, 0);
        // フェンス待ち
        g_FenceValue[g_FrameIndex]++;
        g_CommandQueue->Signal(g_Fence.Get(), g_FenceValue[g_FrameIndex]);
        g_Fence->SetEventOnCompletion(g_FenceValue[g_FrameIndex], g_FenceEvent);
        WaitForSingleObject(g_FenceEvent, INFINITE);
    }

    __declspec(dllexport) void DW_Release()
    {
        if (g_hWnd) { DestroyWindow(g_hWnd); g_hWnd = nullptr; }
    }
}
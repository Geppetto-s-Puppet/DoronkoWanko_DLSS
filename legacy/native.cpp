#include <Windows.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <string>

// ── グローバル ──────────────────────────────────────────────────────────────

static HWND                       g_hWnd = nullptr;
static IDXGIFactory6* g_factory = nullptr;
static ID3D12Device* g_device = nullptr;
static ID3D12CommandQueue* g_commandQueue = nullptr;
static IDXGISwapChain3* g_swapChain = nullptr;
static ID3D12DescriptorHeap* g_rtvHeap = nullptr;
static ID3D12Resource* g_renderTargets[2] = {};
static UINT                       g_rtvDescSize = 0;
static ID3D12CommandAllocator* g_cmdAlloc = nullptr;
static ID3D12GraphicsCommandList* g_cmdList = nullptr;
static ID3D12Fence* g_fence = nullptr;
static UINT64                     g_fenceValue = 0;
static HANDLE                     g_fenceEvent = nullptr;
static ID3D12RootSignature* g_rootSig = nullptr;
static ID3D12PipelineState* g_pso = nullptr;
static ID3D12PipelineState* g_pendingPso = nullptr;  // CompileThreadが書き、RenderThreadが拾う
static std::wstring               g_loadedPath = L"";

// シェーダーに渡す定数バッファ
struct SceneParams
{
    float TIME, DELTA_TIME, FRAME_COUNT, SCRW;
    float SCRH, TEXEL_W, TEXEL_H, CAM_NEAR;
    float CAM_FAR, FOV, RENDER_W, RENDER_H;
    float JITTER_X, JITTER_Y, _pad0, _pad1;
};
static ID3D12Resource* g_cbuffer = nullptr;
static void* g_cbufferMapped = nullptr;
static float                      g_time = 0.0f;
static ID3D12DescriptorHeap* g_cbvHeap = nullptr;
static UINT                       g_srvDescSize = 0;

static UINT g_scrW = 0, g_scrH = 0;  // オーバーレイ解像度（Init時に確定）
static UINT g_renderW = 0, g_renderH = 0;  // Unityのレンダー解像度（Update時に更新）

static ID3D12Resource* g_textures[8] = {};  // Unityからもらった生ポインタ
static ID3D12Resource* g_ownTextures[8] = {};  // 自前コピー先リソース（SRVとして使う）

static HANDLE g_drawEvent = nullptr;  // DW_Draw -> RenderThread への合図

// ── ヘルパー ────────────────────────────────────────────────────────────────

// DepthフォーマットをTypelessに変換
// COPY_DESTとSHADER_RESOURCEの両方で使えるTypelessフォーマットにするため
static DXGI_FORMAT ToTypeless(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_D32_FLOAT:             return DXGI_FORMAT_R32_TYPELESS;
    case DXGI_FORMAT_D16_UNORM:             return DXGI_FORMAT_R16_TYPELESS;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:     return DXGI_FORMAT_R24G8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:  return DXGI_FORMAT_R32G8X24_TYPELESS;
    default:                                return fmt;
    }
}

// TypelessフォーマットをSRV用に変換
static DXGI_FORMAT ToSRVFormat(DXGI_FORMAT fmt)
{
    switch (fmt)
    {
    case DXGI_FORMAT_D32_FLOAT:
    case DXGI_FORMAT_R32_TYPELESS:           return DXGI_FORMAT_R32_FLOAT;
    case DXGI_FORMAT_D16_UNORM:
    case DXGI_FORMAT_R16_TYPELESS:           return DXGI_FORMAT_R16_UNORM;
    case DXGI_FORMAT_D24_UNORM_S8_UINT:
    case DXGI_FORMAT_R24G8_TYPELESS:         return DXGI_FORMAT_R24_UNORM_X8_TYPELESS;
    case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:
    case DXGI_FORMAT_R32G8X24_TYPELESS:      return DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:      return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:      return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:  return DXGI_FORMAT_R16G16B16A16_FLOAT;
    default:                                 return fmt;
    }
}

// srcに合わせた自前リソースを作成する（サイズが変わったときだけ再作成）
static bool EnsureOwnResource(ID3D12Resource*& own, ID3D12Resource* src)
{
    if (!src) return false;

    D3D12_RESOURCE_DESC sd = src->GetDesc();

    if (own)
    {
        D3D12_RESOURCE_DESC od = own->GetDesc();
        if (od.Width == sd.Width && od.Height == sd.Height) return true;
        own->Release();
        own = nullptr;
    }

    D3D12_RESOURCE_DESC d = sd;
    d.Format = ToTypeless(sd.Format);
    d.Flags = D3D12_RESOURCE_FLAG_NONE;  // DENY_SHADER_RESOURCEなどを除去

    D3D12_HEAP_PROPERTIES hp = {};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;

    return SUCCEEDED(g_device->CreateCommittedResource(
        &hp, D3D12_HEAP_FLAG_NONE, &d, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&own)));
}

// UnityテクスチャをCopy -> 自前リソースへ、SRVをヒープに登録
// ※RenderThread内でのみ呼ぶ（Unityスレッドとの競合を避けるため）
static void CopyAndUpdateSRVs()
{
    for (int i = 0; i < 8; i++)
    {
        if (!g_textures[i] || !EnsureOwnResource(g_ownTextures[i], g_textures[i])) continue;

        // Unityテクスチャへのバリアを一切発行しない
        g_cmdList->CopyResource(g_ownTextures[i], g_textures[i]);

        D3D12_RESOURCE_BARRIER toSRV = {};
        toSRV.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toSRV.Transition.pResource = g_ownTextures[i];
        toSRV.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        toSRV.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toSRV.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_cmdList->ResourceBarrier(1, &toSRV);
    }

    // SRVをヒープのスロット1〜8に登録
    D3D12_CPU_DESCRIPTOR_HANDLE handle = g_cbvHeap->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += g_srvDescSize;  // スロット0はCBV

    for (int i = 0; i < 8; i++, handle.ptr += g_srvDescSize)
    {
        if (!g_ownTextures[i]) goto write_null;
        {
            D3D12_RESOURCE_DESC rd = g_ownTextures[i]->GetDesc();
            DXGI_FORMAT         srvFmt = ToSRVFormat(rd.Format);
            if (srvFmt == DXGI_FORMAT_UNKNOWN) goto write_null;

            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = srvFmt;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = rd.MipLevels;
            g_device->CreateShaderResourceView(g_ownTextures[i], &srvDesc, handle);
            continue;
        }
    write_null:
        // 使えないスロットはnull SRV（黒テクスチャ）で埋める
        D3D12_SHADER_RESOURCE_VIEW_DESC nullDesc = {};
        nullDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        nullDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        nullDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        nullDesc.Texture2D.MipLevels = 1;
        g_device->CreateShaderResourceView(nullptr, &nullDesc, handle);
    }

    // 自前リソースをCOPY_DESTに戻す（次フレームのコピーのため）
    for (int i = 0; i < 8; i++)
    {
        if (!g_ownTextures[i]) continue;
        D3D12_RESOURCE_BARRIER toDest = {};
        toDest.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        toDest.Transition.pResource = g_ownTextures[i];
        toDest.Transition.StateBefore = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        toDest.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_DEST;
        toDest.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_cmdList->ResourceBarrier(1, &toDest);
    }
}

// ── シェーダーコンパイルスレッド ────────────────────────────────────────────
// D3DCompile + CreateGraphicsPipelineState は数百ms単位でブロックするため
// メインスレッド（Unityコルーチン）から切り離して非同期で実行する

static DWORD WINAPI CompileThread(LPVOID param)
{
    // パスはheapに確保されて渡ってくる、このスレッドの責任で解放する
    auto* path = reinterpret_cast<std::wstring*>(param);

    // フルスクリーントライアングル用VS（頂点バッファ不要）
    const char* vsCode = R"(
        float4 main(uint id : SV_VertexID) : SV_Position {
            float2 uv = float2((id << 1) & 2, id & 2);
            return float4(uv * 2.0 - 1.0, 0, 1);
        }
    )";
    ID3DBlob* vsBlob = nullptr;
    if (FAILED(D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, nullptr)))
    {
        delete path;
        return 0;
    }

    // cbuffer宣言とSRV宣言をユーザーシェーダーの先頭に結合して渡す
    const char* preamble = R"(
cbuffer SceneParams : register(b0)
{
    float TIME, DELTA_TIME, FRAME_COUNT, SCRW;
    float SCRH, TEXEL_W, TEXEL_H, CAM_NEAR;
    float CAM_FAR, FOV, RENDER_W, RENDER_H;
    float JITTER_X, JITTER_Y, _pad0, _pad1;
};
Texture2D t0 : register(t0); // color
Texture2D t1 : register(t1); // depth
Texture2D t2 : register(t2); // normal
Texture2D t3 : register(t3); // motion
Texture2D t4 : register(t4); // shadow
Texture2D t5 : register(t5); // shadowAdd
Texture2D t6 : register(t6); // opaque
Texture2D t7 : register(t7); // ssao
SamplerState s0 : register(s0);
)";

    HANDLE hFile = CreateFileW(path->c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) { vsBlob->Release(); delete path; return 0; }
    DWORD       fileSize = GetFileSize(hFile, nullptr);
    std::string fileContents(fileSize, '\0');
    ReadFile(hFile, (LPVOID)fileContents.data(), fileSize, nullptr, nullptr);
    CloseHandle(hFile);

    std::string psSource = preamble + fileContents;
    ID3DBlob* psBlob = nullptr;
    if (FAILED(D3DCompile(psSource.c_str(), psSource.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, nullptr)))
    {
        vsBlob->Release();
        delete path;
        return 0;
    }

    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.pRootSignature = g_rootSig;
    psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
    psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
    psoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    psoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    psoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R16G16B16A16_FLOAT;
    psoDesc.SampleDesc.Count = 1;
    psoDesc.SampleMask = UINT_MAX;

    ID3D12PipelineState* newPso = nullptr;
    g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&newPso));

    // pendingに積む（RenderThreadが次フレームのGPUアイドル後に安全に差し替える）
    auto* old = (ID3D12PipelineState*)InterlockedExchangePointer((void**)&g_pendingPso, newPso);
    if (old) old->Release();

    vsBlob->Release();
    psBlob->Release();
    delete path;
    return 0;
}

// ── 描画専用スレッド ────────────────────────────────────────────────────────
// UnityのメインスレッドをブロックせずGPU待機できるように別スレッドで動かす

static DWORD WINAPI RenderThread(LPVOID)
{
    while (true)
    {
        WaitForSingleObject(g_drawEvent, INFINITE);

        // GPU待機後のタイミングでPSOを安全に差し替え
        auto* pending = (ID3D12PipelineState*)InterlockedExchangePointer((void**)&g_pendingPso, nullptr);
        if (pending)
        {
            if (g_pso) g_pso->Release();
            g_pso = pending;
        }

        if (!g_swapChain || !g_cmdAlloc || !g_cmdList) continue;

        UINT frameIndex = g_swapChain->GetCurrentBackBufferIndex();

        g_cmdAlloc->Reset();
        g_cmdList->Reset(g_cmdAlloc, nullptr);

        // バックバッファ: PRESENT -> RENDER_TARGET
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = g_renderTargets[frameIndex];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_cmdList->ResourceBarrier(1, &barrier);

        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += frameIndex * g_rtvDescSize;

        g_cmdList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        float clear[] = { 0, 0, 0, 0 };
        g_cmdList->ClearRenderTargetView(rtv, clear, 0, nullptr);

        if (g_pso)
        {
            CopyAndUpdateSRVs();

            g_time += 1.0f / 60.0f;
            SceneParams params = {};
            params.TIME = g_time;
            params.SCRW = (float)g_scrW;
            params.SCRH = (float)g_scrH;
            params.TEXEL_W = 1.0f / g_scrW;
            params.TEXEL_H = 1.0f / g_scrH;
            params.RENDER_W = (float)g_renderW;
            params.RENDER_H = (float)g_renderH;
            memcpy(g_cbufferMapped, &params, sizeof(SceneParams));

            D3D12_VIEWPORT vp = { 0, 0, (float)g_scrW, (float)g_scrH, 0.0f, 1.0f };
            D3D12_RECT     scissor = { 0, 0, (LONG)g_scrW, (LONG)g_scrH };

            ID3D12DescriptorHeap* heaps[] = { g_cbvHeap };
            g_cmdList->SetDescriptorHeaps(1, heaps);
            g_cmdList->SetGraphicsRootSignature(g_rootSig);
            g_cmdList->SetPipelineState(g_pso);
            g_cmdList->SetGraphicsRootDescriptorTable(0, g_cbvHeap->GetGPUDescriptorHandleForHeapStart());
            g_cmdList->RSSetViewports(1, &vp);
            g_cmdList->RSSetScissorRects(1, &scissor);
            g_cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_cmdList->DrawInstanced(3, 1, 0, 0);
        }

        // RENDER_TARGET -> PRESENT
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_cmdList->ResourceBarrier(1, &barrier);

        g_cmdList->Close();
        ID3D12CommandList* lists[] = { g_cmdList };
        g_commandQueue->ExecuteCommandLists(1, lists);
        g_swapChain->Present(0, 0);

        // CPU/GPU同期（別スレッドなのでINFINITE待機しても問題ない）
        g_commandQueue->Signal(g_fence, g_fenceValue);
        if (g_fence->GetCompletedValue() < g_fenceValue)
        {
            g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, INFINITE);
        }
        g_fenceValue++;
    }
    return 0;
}

// ── エクスポート関数 ────────────────────────────────────────────────────────
extern "C" {

    __declspec(dllexport) void DW_Init(void* sampleResourcePtr)
    {
        if (!sampleResourcePtr) return;

        HINSTANCE hInst = GetModuleHandle(nullptr);
        g_scrW = GetSystemMetrics(SM_CXSCREEN);
        g_scrH = GetSystemMetrics(SM_CYSCREEN);

        // WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE:
        //   最前面・透過可能・マウス無視・フォーカス無効
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"DW_DLSS";
        RegisterClassEx(&wc);

        HWND gameHwnd = FindWindowW(nullptr, L"DoronkoWanko");
        g_hWnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,
            L"DW_DLSS", L"DW_DLSS Overlay", WS_POPUP,
            0, 0, g_scrW, g_scrH,
            gameHwnd, nullptr, hInst, nullptr);

        ShowWindow(g_hWnd, SW_SHOW);

        // UnityのリソースからD3D12Deviceを取得（デバイスを共有することでテクスチャを共有できる）
        auto* resource = reinterpret_cast<ID3D12Resource*>(sampleResourcePtr);
        resource->GetDevice(IID_PPV_ARGS(&g_device));

        CreateDXGIFactory2(0, IID_PPV_ARGS(&g_factory));

        // コマンドキュー（Unityとは別キュー、テクスチャ共有はCOMMON状態で行う）
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
        g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));

        // スワップチェーン（R16G16B16A16_FLOAT: HDR・透過対応）
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.BufferCount = 2;
        scDesc.Width = g_scrW;
        scDesc.Height = g_scrH;
        scDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        IDXGISwapChain1* sc1 = nullptr;
        g_factory->CreateSwapChainForHwnd(g_commandQueue, g_hWnd, &scDesc, nullptr, nullptr, &sc1);
        sc1->QueryInterface(IID_PPV_ARGS(&g_swapChain));
        sc1->Release();

        // RTVヒープ（バックバッファ2枚分）
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 2;
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
        g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap));
        g_rtvDescSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        for (UINT i = 0; i < 2; i++)
        {
            g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i]));
            g_device->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle);
            rtvHandle.ptr += g_rtvDescSize;
        }

        // コマンドアロケータ・コマンドリスト
        g_device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&g_cmdAlloc));
        g_device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, g_cmdAlloc, nullptr, IID_PPV_ARGS(&g_cmdList));
        g_cmdList->Close();

        // フェンス（CPU/GPU同期）
        g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        g_fenceValue = 1;
        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);

        // ルートシグネチャ（スロット0=CBV b0, スロット1〜8=SRV t0〜t7, s0=サンプラー）
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0;
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].NumDescriptors = 8;
        ranges[1].BaseShaderRegister = 0;
        ranges[1].OffsetInDescriptorsFromTableStart = 1;

        D3D12_ROOT_PARAMETER rootParam = {};
        rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
        rootParam.DescriptorTable.NumDescriptorRanges = 2;
        rootParam.DescriptorTable.pDescriptorRanges = ranges;
        rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_STATIC_SAMPLER_DESC sampler = {};
        sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
        sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
        sampler.ShaderRegister = 0;
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters = &rootParam;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &sampler;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

        ID3DBlob* blob = nullptr;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, nullptr);
        g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_rootSig));
        blob->Release();

        // 定数バッファ（256バイト境界にアライメント、MapしっぱなしがDX12の定石）
        UINT cbSize = (sizeof(SceneParams) + 255) & ~255;
        D3D12_HEAP_PROPERTIES hp = {};
        hp.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = cbSize;
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        g_device->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&g_cbuffer));
        g_cbuffer->Map(0, nullptr, &g_cbufferMapped);

        // CBV/SRVヒープ（スロット0=CBV, スロット1〜8=SRV、GPU可視）
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = 9;
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        g_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&g_cbvHeap));
        g_srvDescSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_cbuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = cbSize;
        g_device->CreateConstantBufferView(&cbvDesc, g_cbvHeap->GetCPUDescriptorHandleForHeapStart());

        // 描画専用スレッドを起動（UnityスレッドをブロックせずにGPU待機するため）
        g_drawEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        CreateThread(nullptr, 0, RenderThread, nullptr, 0, nullptr);
    }


    __declspec(dllexport) void DW_Update(const wchar_t* shaderPath, void** textures, BOOL visible, int w, int h)
    {
        ShowWindow(g_hWnd, visible ? SW_SHOWNOACTIVATE : SW_HIDE);

        for (int i = 0; i < 8; i++)
            g_textures[i] = textures ? reinterpret_cast<ID3D12Resource*>(textures[i]) : nullptr;

        g_renderW = w;
        g_renderH = h;

        // シェーダーが変わったときだけ非同期コンパイルを起動
        // D3DCompile + CreateGraphicsPipelineState は数百ms単位でブロックするため
        // メインスレッド（Unityコルーチン）では絶対に呼ばない
        if (!shaderPath || g_loadedPath == shaderPath) return;
        g_loadedPath = shaderPath;

        // パスをheapにコピーしてスレッドに渡す（CompileThread側で解放）
        CreateThread(nullptr, 0, CompileThread, new std::wstring(shaderPath), 0, nullptr);
    }


    __declspec(dllexport) void DW_Draw()
    {
        // 合図を送るだけ、実際の描画はRenderThreadが行う
        if (g_drawEvent) SetEvent(g_drawEvent);
    }


    __declspec(dllexport) void DW_Release()
    {
        if (g_hWnd) DestroyWindow(g_hWnd);
        g_hWnd = nullptr;
    }

} // extern "C"
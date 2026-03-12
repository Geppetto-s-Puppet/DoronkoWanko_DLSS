#include <wrl.h>
#include <thread>
#include <string>
#include <windows.h>

#include "d3dx12.h"
#include <dxgi1_6.h>
#include <d3dcompiler.h>

#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")




// ===== グローバル変数 =====
HWND g_hWnd = nullptr;

ID3D12Resource* g_UnityTexture = nullptr;
ID3D12Device* g_Device = nullptr;

IDXGISwapChain3* g_SwapChain = nullptr;
ID3D12CommandQueue* g_CommandQueue = nullptr;

ID3D12CommandAllocator* g_CommandAllocator = nullptr;
ID3D12GraphicsCommandList* g_CommandList = nullptr;

ID3D12DescriptorHeap* g_RTVHeap = nullptr;
UINT g_RTVDescriptorSize = 0;

ID3D12DescriptorHeap* g_CBVSRVHeap = nullptr;
// 定数バッファ8個
// [0]  b0  
// [1]  b1
// [2]  b2
// [3]  b3
// [4]  b4
// [5]  b5
// [6]  b6
// [7]  b7
// Unityテクスチャ8個
// [8]  t0
// [9]  t1
// [10] t2
// [11] t3
// [12] t4
// [13] t5
// [14] t6
// [15] t7

ID3D12Resource* g_RenderTargets[2] = {};
UINT g_FrameIndex = 0;

ID3D12Fence* g_Fence = nullptr;
UINT64       g_FenceValue = 0;
HANDLE       g_FenceEvent = nullptr;

ID3D12RootSignature* g_RootSignature = nullptr;
ID3D12PipelineState* g_PSO = nullptr;

ID3D12Resource* g_TestTexture = nullptr;

// ===== シェーダー勘でここ =====

const char* g_VS = R"(
struct VSOut {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
    VSOut o;

    // フルスクリーン三角形
    float2 pos = float2(
        (id == 2) ? 3.0 : -1.0,
        (id == 1) ? 3.0 : -1.0
    );

    o.pos = float4(pos, 0.0, 1.0);
    o.uv  = (pos + 1.0) * 0.5;

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




// ===== 関数プロトタイプ =====
void CreateOverlayWindow()
{
    HINSTANCE hInstance = GetModuleHandle(nullptr);

    WNDCLASSEX wc = {};
    wc.cbSize = sizeof(WNDCLASSEX);
    wc.lpfnWndProc = DefWindowProc; // ★これでOK
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
        0, 0, 1280, 720,
        nullptr, nullptr, hInstance, nullptr
    );

    ShowWindow(g_hWnd, SW_SHOW);
    UpdateWindow(g_hWnd);
}


void RenderOverlay()
{
    // ===============================
    // 1. CommandAllocator をリセット
    // ===============================
    // ・前フレームのコマンドを全部破棄して再利用する
    g_CommandAllocator->Reset();

    // ===============================
    // 2. CommandList をリセット
    // ===============================
    g_CommandList->Reset(g_CommandAllocator, g_PSO);


    // ===============================
    // 3. バックバッファを取得
    // ===============================
    ID3D12Resource* backBuffer = g_RenderTargets[g_FrameIndex];

    // ===============================
    // 4. RTV をセット
    // ===============================
    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
        g_RTVHeap->GetCPUDescriptorHandleForHeapStart();
    rtvHandle.ptr += g_FrameIndex * g_RTVDescriptorSize;

    g_CommandList->OMSetRenderTargets(
        1, &rtvHandle,
        FALSE, nullptr
    );

    // ===============================
    // 5. 画面クリア
    // ===============================
    // ・とりあえず赤でクリア（デバッグ用）
    const float clearColor[4] = { 1.0f, 0.2f, 0.2f, 1.0f };
    g_CommandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);


    D3D12_VIEWPORT vp = {};
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = 1280.0f;
    vp.Height = 720.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    D3D12_RECT scissor = {};
    scissor.left = 0;
    scissor.top = 0;
    scissor.right = 1280;
    scissor.bottom = 720;

    g_CommandList->RSSetViewports(1, &vp);
    g_CommandList->RSSetScissorRects(1, &scissor);




    // あとからさしこんだここ
    g_CommandList->SetGraphicsRootSignature(g_RootSignature);

    ID3D12DescriptorHeap* heaps[] = { g_CBVSRVHeap };
    g_CommandList->SetDescriptorHeaps(1, heaps);

    // DescriptorTable をセット（b0〜b7, t0〜t7）
    g_CommandList->SetGraphicsRootDescriptorTable(0,
        g_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart());



    // フルスクリーン三角形
    g_CommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    g_CommandList->DrawInstanced(3, 1, 0, 0);


    // ===============================
    // 6. CommandList を Close
    // ===============================
    g_CommandList->Close();

    // ===============================
    // 7. GPU に実行させる
    // ===============================
    ID3D12CommandList* lists[] = { g_CommandList };
    g_CommandQueue->ExecuteCommandLists(1, lists);

    // ===============================
    // 8. Present（画面に出す）
    // ===============================
    g_SwapChain->Present(1, 0);

    // ===============================
    // 9. フェンスで同期
    // ===============================
    const UINT64 fenceToWait = g_FenceValue;
    g_CommandQueue->Signal(g_Fence, fenceToWait);
    g_FenceValue++;

    if (g_Fence->GetCompletedValue() < fenceToWait)
    {
        g_Fence->SetEventOnCompletion(fenceToWait, g_FenceEvent);
        WaitForSingleObject(g_FenceEvent, INFINITE);
    }

    // ===============================
    // 10. 次のフレームへ
    // ===============================
    g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
}


void RunOverlayLoop()
{
    MSG msg = {};
    while (true)
    {
        while (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            if (msg.message == WM_QUIT)
                return;

            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }

        // ★ DX12 描画
        RenderOverlay();
    }
}


void RenderThread()
{
    RunOverlayLoop();
}













// ===== DW_Init / DW_Release =====

extern "C" {

    __declspec(dllexport)
        void DW_Init(void* unityTexturePtr)
    {
        g_UnityTexture = reinterpret_cast<ID3D12Resource*>(unityTexturePtr); // Unity のテクスチャを保存
        g_UnityTexture->GetDevice(IID_PPV_ARGS(&g_Device)); // Unity と同じ GPU デバイスを取得
        CreateOverlayWindow(); // オーバーレイウィンドウを作成
        // DX12 初期化（SwapChain など）は後で追加する

        // DXGI Factory
        IDXGIFactory6* factory = nullptr;
        CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));

        // CommandQueue
        D3D12_COMMAND_QUEUE_DESC qDesc = {};
        qDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        g_Device->CreateCommandQueue(&qDesc, IID_PPV_ARGS(&g_CommandQueue));

        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.BufferCount = 2;
        scDesc.Width = 1280;
        scDesc.Height = 720;
        scDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
        scDesc.SampleDesc.Count = 1;

        IDXGISwapChain1* swap1 = nullptr;
        factory->CreateSwapChainForHwnd(
            g_CommandQueue,
            g_hWnd,
            &scDesc,
            nullptr,
            nullptr,
            &swap1
        );

        swap1->QueryInterface(IID_PPV_ARGS(&g_SwapChain));
        swap1->Release();
        factory->Release();

        // ===============================================
        // RTV Heap（Render Target View）
        // ===============================================
        // ・SwapChain のバックバッファに対して「描画先」を割り当てるためのヒープ
        // ・画面に描画するために必須
        // ・RTV は Shader から見えないので SHADER_VISIBLE は不要
        {
            D3D12_DESCRIPTOR_HEAP_DESC rtvDesc = {};
            rtvDesc.NumDescriptors = 2; // ダブルバッファ
            rtvDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
            rtvDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

            g_Device->CreateDescriptorHeap(&rtvDesc, IID_PPV_ARGS(&g_RTVHeap));

            // RTV のハンドルサイズ（次のスロットに進むために必要）
            g_RTVDescriptorSize =
                g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
        }


        // ===============================================
        // CBV/SRV Heap（ConstantBuffer + Texture SRV）
        // ===============================================
        // ・HLSL の b0〜b7（定数バッファ）
        // ・HLSL の t0〜t7（Unity から来るテクスチャ）
        // をまとめて管理する「GPU のインベントリ」
        //
        // DX12 は Descriptor を連続領域に置くと高速なので
        // 16 スロット（CBV 8 + SRV 8）を確保する。
        // Shader から見える必要があるので SHADER_VISIBLE を付ける。
        {
            D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
            heapDesc.NumDescriptors = 16; // CBV 8 + SRV 8
            heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
            heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;

            g_Device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&g_CBVSRVHeap));
        }

        D3D12_CPU_DESCRIPTOR_HANDLE cbvSrvCPU =
            g_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart();

        D3D12_GPU_DESCRIPTOR_HANDLE cbvSrvGPU =
            g_CBVSRVHeap->GetGPUDescriptorHandleForHeapStart();

        // SwapChain 作成と RTV Heap 作成の「後」に入れる

        // ===============================
        // バックバッファ → RTV 作成
        // ===============================
        {
            D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle =
                g_RTVHeap->GetCPUDescriptorHandleForHeapStart();

            for (UINT i = 0; i < 2; ++i)
            {
                // SwapChain から i 番目のバックバッファを取得
                g_SwapChain->GetBuffer(i, IID_PPV_ARGS(&g_RenderTargets[i]));

                // そのバッファに対する RTV を作成
                g_Device->CreateRenderTargetView(
                    g_RenderTargets[i],
                    nullptr,        // デフォルト（フォーマットは SwapChain に従う）
                    rtvHandle
                );

                // 次のスロットへ
                rtvHandle.ptr += g_RTVDescriptorSize;
            }

            g_FrameIndex = g_SwapChain->GetCurrentBackBufferIndex();
        }



        // ===============================
        // CommandAllocator / CommandList
        // ===============================
        // ・CommandAllocator：コマンドのメモリプール
        // ・CommandList：実際に描画コマンドを書き込むオブジェクト
        {
            g_Device->CreateCommandAllocator(
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                IID_PPV_ARGS(&g_CommandAllocator)
            );

            g_Device->CreateCommandList(
                0,
                D3D12_COMMAND_LIST_TYPE_DIRECT,
                g_CommandAllocator,
                nullptr, // PSO は後で設定
                IID_PPV_ARGS(&g_CommandList)
            );

            // 最初は Close しておく（毎フレーム Reset → Record → Close）
            g_CommandList->Close();
        }
        // フェンス作成
        g_Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_Fence));
        g_FenceValue = 1;
        g_FenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
        // ★ 別スレッドでレンダリングループ開始
        std::thread(RenderThread).detach();



        // ===============================
        // RootSignature（CBV8 + SRV8）
        // ===============================
// RootSignature（CBV8 + SRV8 + Sampler s0）
        {
            D3D12_DESCRIPTOR_RANGE ranges[2] = {};

            // CBV 8個 → b0〜b7
            ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
            ranges[0].NumDescriptors = 8;
            ranges[0].BaseShaderRegister = 0;
            ranges[0].RegisterSpace = 0;
            ranges[0].OffsetInDescriptorsFromTableStart = 0;

            // SRV 8個 → t0〜t7
            ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
            ranges[1].NumDescriptors = 8;
            ranges[1].BaseShaderRegister = 0;
            ranges[1].RegisterSpace = 0;
            ranges[1].OffsetInDescriptorsFromTableStart = 8;

            D3D12_ROOT_PARAMETER rootParam = {};
            rootParam.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
            rootParam.DescriptorTable.NumDescriptorRanges = 2;
            rootParam.DescriptorTable.pDescriptorRanges = ranges;
            rootParam.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

            // ★ StaticSampler s0 を追加
            D3D12_STATIC_SAMPLER_DESC sampler = {};
            sampler.Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
            sampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
            sampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
            sampler.ShaderRegister = 0;          // s0
            sampler.RegisterSpace = 0;
            sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

            D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
            rsDesc.NumParameters = 1;
            rsDesc.pParameters = &rootParam;
            rsDesc.NumStaticSamplers = 1;        // ★ ここ
            rsDesc.pStaticSamplers = &sampler;   // ★ ここ
            rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

            ID3DBlob* sigBlob = nullptr;
            ID3DBlob* errBlob = nullptr;
            D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1,
                &sigBlob, &errBlob);

            g_Device->CreateRootSignature(
                0,
                sigBlob->GetBufferPointer(),
                sigBlob->GetBufferSize(),
                IID_PPV_ARGS(&g_RootSignature)
            );

            if (sigBlob) sigBlob->Release();
            if (errBlob) errBlob->Release();
        }


        // ===============================
// PSO（フルスクリーン三角形）
// ===============================
        {
            // シェーダをコンパイル
            ID3DBlob* vsBlob = nullptr;
            ID3DBlob* psBlob = nullptr;
            ID3DBlob* errBlob = nullptr;

            D3DCompile(g_VS, strlen(g_VS), nullptr, nullptr, nullptr,
                "main", "vs_5_0", 0, 0, &vsBlob, &errBlob);

            D3DCompile(g_PS, strlen(g_PS), nullptr, nullptr, nullptr,
                "main", "ps_5_0", 0, 0, &psBlob, &errBlob);

            // PSO 設定
            D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
            psoDesc.pRootSignature = g_RootSignature;
            psoDesc.VS = { vsBlob->GetBufferPointer(), vsBlob->GetBufferSize() };
            psoDesc.PS = { psBlob->GetBufferPointer(), psBlob->GetBufferSize() };
            psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
            psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
            psoDesc.DepthStencilState.DepthEnable = FALSE;
            psoDesc.DepthStencilState.StencilEnable = FALSE;
            psoDesc.SampleMask = UINT_MAX;
            psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
            psoDesc.NumRenderTargets = 1;
            psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
            psoDesc.SampleDesc.Count = 1;

            g_Device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_PSO));

            vsBlob->Release();
            psBlob->Release();
        }

        //D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        //srvDesc.Format = g_UnityTexture->GetDesc().Format;
        //srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        //srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
        //srvDesc.Texture2D.MipLevels = 1;

        //CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        //    g_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart(),
        //    8, // ★ t0 は 8番目（CBV8個の後ろ）
        //    g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
        //);

        //g_Device->CreateShaderResourceView(g_UnityTexture, &srvDesc, handle);



        // ===============================
// 自前テクスチャ（チェッカーボード）
// ===============================
        {
            const UINT texWidth = 256;
            const UINT texHeight = 256;
            const UINT pixelSize = 4; // RGBA8

            std::vector<UINT8> data(texWidth * texHeight * pixelSize);

            for (UINT y = 0; y < texHeight; y++)
            {
                for (UINT x = 0; x < texWidth; x++)
                {
                    bool checker = ((x / 32) % 2) ^ ((y / 32) % 2);
                    UINT8 color = checker ? 255 : 0;

                    UINT idx = (y * texWidth + x) * 4;
                    data[idx + 0] = color;
                    data[idx + 1] = 0;
                    data[idx + 2] = 255 - color;
                    data[idx + 3] = 255;
                }
            }

            D3D12_RESOURCE_DESC texDesc = {};
            texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            texDesc.Width = texWidth;
            texDesc.Height = texHeight;
            texDesc.DepthOrArraySize = 1;
            texDesc.MipLevels = 1;
            texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            texDesc.SampleDesc.Count = 1;
            texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;

            CD3DX12_HEAP_PROPERTIES heapProps(D3D12_HEAP_TYPE_DEFAULT);

            g_Device->CreateCommittedResource(
                &heapProps,
                D3D12_HEAP_FLAG_NONE,
                &texDesc,
                D3D12_RESOURCE_STATE_COPY_DEST,
                nullptr,
                IID_PPV_ARGS(&g_TestTexture)
            );

            UINT64 uploadSize = 0;
            g_Device->GetCopyableFootprints(&texDesc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

            ID3D12Resource* uploadBuffer = nullptr;
            CD3DX12_HEAP_PROPERTIES uploadHeap(D3D12_HEAP_TYPE_UPLOAD);

            g_Device->CreateCommittedResource(
                &uploadHeap,
                D3D12_HEAP_FLAG_NONE,
                &CD3DX12_RESOURCE_DESC::Buffer(uploadSize),
                D3D12_RESOURCE_STATE_GENERIC_READ,
                nullptr,
                IID_PPV_ARGS(&uploadBuffer)
            );

            D3D12_SUBRESOURCE_DATA sub = {};
            sub.pData = data.data();
            sub.RowPitch = texWidth * pixelSize;
            sub.SlicePitch = sub.RowPitch * texHeight;

            g_CommandAllocator->Reset();
            g_CommandList->Reset(g_CommandAllocator, nullptr);

            UpdateSubresources(g_CommandList, g_TestTexture, uploadBuffer, 0, 0, 1, &sub);

            CD3DX12_RESOURCE_BARRIER barrier =
                CD3DX12_RESOURCE_BARRIER::Transition(
                    g_TestTexture,
                    D3D12_RESOURCE_STATE_COPY_DEST,
                    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
                );

            g_CommandList->ResourceBarrier(1, &barrier);
            g_CommandList->Close();

            ID3D12CommandList* lists[] = { g_CommandList };
            g_CommandQueue->ExecuteCommandLists(1, lists);

            g_CommandQueue->Signal(g_Fence, g_FenceValue);
            g_Fence->SetEventOnCompletion(g_FenceValue, g_FenceEvent);
            WaitForSingleObject(g_FenceEvent, INFINITE);
            g_FenceValue++;

            uploadBuffer->Release();

            // SRV を t0 (8番目) に置く
            D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
            srvDesc.Texture2D.MipLevels = 1;

            UINT srvOffset = 8;

            CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
                g_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart(),
                srvOffset,
                g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
            );

            g_Device->CreateShaderResourceView(g_TestTexture, &srvDesc, handle);
        }






    }
    __declspec(dllexport)
        void DW_Release()
    {
        // 後でリソース解放を書く
    }
}








extern "C" __declspec(dllexport)
void DW_OnUnityRenderEvent(void* unityTexturePtr)
{
    g_UnityTexture = reinterpret_cast<ID3D12Resource*>(unityTexturePtr);



    if (!g_UnityTexture || !g_Device) return;

    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = g_UnityTexture->GetDesc().Format;
    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvDesc.Texture2D.MipLevels = 1;

    UINT srvOffset = 8;

    CD3DX12_CPU_DESCRIPTOR_HANDLE handle(
        g_CBVSRVHeap->GetCPUDescriptorHandleForHeapStart(),
        srvOffset,
        g_Device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    );

    g_Device->CreateShaderResourceView(g_UnityTexture, &srvDesc, handle);
}

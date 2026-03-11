#include <Windows.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")
#include <d3dcompiler.h>
#pragma comment(lib, "d3dcompiler.lib")
#include <string>


HWND g_hWnd = nullptr;
IDXGIFactory6* g_factory           = nullptr; // DX12用のバージョン
//IDXGIAdapter1* g_adapter           = nullptr; // アダプターはGPUのこと
ID3D12Device* g_device             = nullptr;
ID3D12CommandQueue* g_commandQueue = nullptr;
IDXGISwapChain3* g_swapChain       = nullptr; // ダブルバッファリング
ID3D12DescriptorHeap* g_rtvHeap    = nullptr; // 
ID3D12Resource* g_renderTargets[2] = {};      // 
UINT g_rtvDescriptorSize           = 0;       // 
ID3D12CommandAllocator* g_commandAllocator = nullptr; // 命令を書き込むメモリ領域
ID3D12GraphicsCommandList* g_commandList   = nullptr; // 命令を書き込む道具
ID3D12Fence* g_fence      = nullptr; // CPU/GPU同期用の信号機
UINT64       g_fenceValue = 0;       // 現在のフェンス値
HANDLE       g_fenceEvent = nullptr; // 待機用のWindowsイベント
ID3D12RootSignature* g_rootSignature = nullptr;
ID3D12PipelineState* g_pso = nullptr;
std::wstring         g_loadedPath = L"";
struct SceneParams // 定数バッファ
{
    float TIME;
    float DELTA_TIME;
    float FRAME_COUNT;
    float SCRW;

    float SCRH;
    float TEXEL_W;
    float TEXEL_H;
    float CAM_NEAR;
    float CAM_FAR;
    float FOV;
    float RENDER_W;
    float RENDER_H;

    float JITTER_X;
    float JITTER_Y;
    float _pad0;
    float _pad1;
};
ID3D12Resource* g_cbuffer = nullptr;
void* g_cbufferMapped = nullptr; // CPUから書き込む用のポインタ
float           g_time = 0.0f;
ID3D12DescriptorHeap* g_cbvHeap = nullptr;

UINT g_scrW = 0;
UINT g_scrH = 0;
ID3D12Resource* g_textures[8] = {}; // t0〜t7
UINT g_srvDescriptorSize = 0;

extern "C" {
    __declspec(dllexport) void DW_Init(void* sampleResourcePtr)
    {
        HINSTANCE hInst = GetModuleHandle(nullptr);
        g_scrW = GetSystemMetrics(SM_CXSCREEN);
        g_scrH = GetSystemMetrics(SM_CYSCREEN);
        WNDCLASSEX wc = {};
        wc.cbSize = sizeof(WNDCLASSEX);
        wc.lpfnWndProc = DefWindowProc;
        wc.hInstance = hInst;
        wc.lpszClassName = L"DW_DLSS";
        RegisterClassEx(&wc);
        HWND gameHwnd = FindWindowW(nullptr, L"DoronkoWanko");
        g_hWnd = CreateWindowEx(
            WS_EX_TOPMOST | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE,  // 最前面・透過可能・マウス無視・フォーカス無効
            L"DW_DLSS",                                                            // 上でさっきで登録したウィンドウクラスの名前
            L"DW_DLSS Overlay",                                                    // タイトルバー（WS_POPUPだから常に非表示）
            WS_POPUP,                                                              // 枠なし・タイトルバーなし
            0, 0,                                                                  // 表示位置のXとY、つまり真ん中
            g_scrW, g_scrH,          // ウィンドウサイズ（念のため取得しておく）
            gameHwnd,                                                              // ダイアログボックスみたいに親ウィンドウ設定
            nullptr,                                                               // ファイル・編集・表示みたいなメニューバー不要
            hInst,                                                                 // このDLLのハンドル
            nullptr                                                                // 追加パラメータなし
        );
        // まずウィンドウを生成・表示
        ShowWindow(g_hWnd, SW_SHOW); // MessageBoxW(nullptr, L"ウィンドウ作成完了", L"DW_DLSS", MB_OK);
        // つぎにGPUを探してくる
        //CreateDXGIFactory2(0, IID_PPV_ARGS(&g_factory)); // これ、DX12のAPIの慣習？というか惰性でこんな関数名、でもこれもUnityに任せる
        // 
        CreateDXGIFactory2(0, IID_PPV_ARGS(&g_factory));
        //g_factory->EnumAdapterByGpuPreference(
        //    0, // 一番高性能なGPUを選ぶためにこれ
        //    DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
        //    IID_PPV_ARGS(&g_adapter)
        //);
        // つぎにGPUとの通信窓口、でもこれはUnityと同期するので無効化
        //D3D12CreateDevice(
        //    g_adapter,              // さっき選んだGPU
        //    D3D_FEATURE_LEVEL_12_0, // DX12の機能使用
        //    IID_PPV_ARGS(&g_device)
        //);
        // UnityのID3D12Resourceポインタからデバイスを取得
        auto* resource = reinterpret_cast<ID3D12Resource*>(sampleResourcePtr);
        resource->GetDevice(IID_PPV_ARGS(&g_device));
        // つぎにGPUに命令するベルトコンベア 
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 描画・計算・コピーなど全種類の命令対応
        g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
        // つぎに画面に表示するバッファの管理、昔は裏画面を表画面にコピーしてたが、今はFLIP方式で効率化
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.BufferCount = 2; // 表画面と裏画面
        scDesc.Width = g_scrW;
        scDesc.Height = g_scrH;
        scDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // 親ウィンドウのR11G11B10_FLOATより高精度
        scDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // 描画の出力先としてバッファを使う
        scDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // 表示したバッファの中身を捨てる
        scDesc.SampleDesc.Count = 1; // アップスケールするのに邪魔なMSAAをオフにしてる
        IDXGISwapChain1* swapChain1 = nullptr;
        g_factory->CreateSwapChainForHwnd(
            g_commandQueue,
            g_hWnd,
            &scDesc,
            nullptr,
            nullptr,
            &swapChain1
        );
        swapChain1->QueryInterface(IID_PPV_ARGS(&g_swapChain));
        swapChain1->Release();
        // レンダーターゲット関連
        //ディスクリプタヒープ(GPUに「このテクスチャがどのメモリ、どんなフォーマット、どんな使途か」を事前に教えるのディスクリプタのまとめ)
        D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
        rtvHeapDesc.NumDescriptors = 2;                        // スロット2つ（バッファ0と1）
        rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;     // RTV用の棚
        g_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&g_rtvHeap));
        g_rtvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV); // GPUメーカーによって異なる
        D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = g_rtvHeap->GetCPUDescriptorHandleForHeapStart(); // RTVはCPU側からセットするのでこれ
        for (UINT i = 0; i < 2; i++) // ④ バッファ0とバッファ1それぞれにRTV（バッファに描く許可証）を作る、HLSLがアクセスするため
        {
            g_swapChain->GetBuffer(i, IID_PPV_ARGS(&g_renderTargets[i])); // スワップチェーンからGetBuffer()関数を使うことで、バッファを取得
            g_device->CreateRenderTargetView(g_renderTargets[i], nullptr, rtvHandle); // RTVを作ってスロットに、第二引数はフォーマットの上書き指定
            rtvHandle.ptr += g_rtvDescriptorSize; // 次のスロットに移動
        }
        // コマンドアロケータ
        g_device->CreateCommandAllocator(
            D3D12_COMMAND_LIST_TYPE_DIRECT,        // キューと同じタイプ
            IID_PPV_ARGS(&g_commandAllocator)
        );
        // コマンドリスト
        g_device->CreateCommandList(
            0,                                     // GPUが1つなので0
            D3D12_COMMAND_LIST_TYPE_DIRECT,        // キューと同じタイプ
            g_commandAllocator,                    // このアロケータにメモリを借りる
            nullptr,                               // 初期パイプラインステートなし
            IID_PPV_ARGS(&g_commandList)
        );
        g_commandList->Close(); // 作成直後は開いた状態なので閉じる
        // フェンス（CPU/GPU同期）これがないと、描いてる最中にバッファが上書きされる
        g_device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&g_fence));
        g_fenceValue = 1;
        g_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr); // 待機用イベント作成
        // ルートシグネチャ（今は空：シェーダーに何も渡さないver）
        //D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        //rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        //ID3DBlob* blob = nullptr;
        //ID3DBlob* errBlob = nullptr;
        //D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errBlob); // これで一度バイナリ形式に変換
        //g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature));
        //blob->Release();
        // ルートシグネチャ（b0に定数バッファを渡すver）
// ルートシグネチャ（b0=CBV, t0〜t7=SRV）
        D3D12_DESCRIPTOR_RANGE ranges[2] = {};
        ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_CBV;
        ranges[0].NumDescriptors = 1;
        ranges[0].BaseShaderRegister = 0; // b0
        ranges[0].OffsetInDescriptorsFromTableStart = 0;
        ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
        ranges[1].NumDescriptors = 8; // t0〜t7
        ranges[1].BaseShaderRegister = 0; // t0
        ranges[1].OffsetInDescriptorsFromTableStart = 1; // CBVの次から
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
        sampler.ShaderRegister = 0; // s0
        sampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
        D3D12_ROOT_SIGNATURE_DESC rsDesc = {};
        rsDesc.NumParameters = 1;
        rsDesc.pParameters = &rootParam;
        rsDesc.NumStaticSamplers = 1;
        rsDesc.pStaticSamplers = &sampler;
        rsDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
        ID3DBlob* blob = nullptr;
        ID3DBlob* errBlob = nullptr;
        D3D12SerializeRootSignature(&rsDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &errBlob);
        g_device->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&g_rootSignature));
        blob->Release();
        // 定数バッファ作成
        // GPUメモリは256バイト単位なのでサイズを切り上げる
        UINT cbSize = (sizeof(SceneParams) + 255) & ~255;
        D3D12_HEAP_PROPERTIES heapProps = {};
        heapProps.Type = D3D12_HEAP_TYPE_UPLOAD; // CPUから書き込めるヒープ
        D3D12_RESOURCE_DESC cbDesc = {};
        cbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        cbDesc.Width = cbSize;
        cbDesc.Height = 1;
        cbDesc.DepthOrArraySize = 1;
        cbDesc.MipLevels = 1;
        cbDesc.SampleDesc.Count = 1;
        cbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        g_device->CreateCommittedResource(
            &heapProps,
            D3D12_HEAP_FLAG_NONE,
            &cbDesc,
            D3D12_RESOURCE_STATE_GENERIC_READ, // UPLOADヒープはこれ固定
            nullptr,
            IID_PPV_ARGS(&g_cbuffer)
        );
        // 一度MapしたままにしておくのがDX12の定石（毎フレームMap/Unmapは遅い）
        g_cbuffer->Map(0, nullptr, &g_cbufferMapped);
        // CBV用のディスクリプタヒープ（RTVとは別に必要）
        // ※後でDraw時にセットするために必要
        // ヒープ作成
        D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc = {};
        cbvHeapDesc.NumDescriptors = 9; // 1(CBV) + 8(SRV)
        g_srvDescriptorSize = g_device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
        cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; // CBV/SRV/UAV共用の棚
        cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE; // GPU可視
        g_device->CreateDescriptorHeap(&cbvHeapDesc, IID_PPV_ARGS(&g_cbvHeap));

        // CBVをヒープに登録
        D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
        cbvDesc.BufferLocation = g_cbuffer->GetGPUVirtualAddress();
        cbvDesc.SizeInBytes = cbSize;
        g_device->CreateConstantBufferView(&cbvDesc, g_cbvHeap->GetCPUDescriptorHandleForHeapStart());


    }

    __declspec(dllexport) void DW_Show()
    {
        if (g_hWnd) ShowWindow(g_hWnd, SW_SHOWNOACTIVATE);
    }
    __declspec(dllexport) void DW_Hide()
    {
        if (g_hWnd) ShowWindow(g_hWnd, SW_HIDE);
    }


    __declspec(dllexport) void DW_Update(const wchar_t* shaderPath)
    {
        // 同じパスなら何もしない
        if (!shaderPath || g_loadedPath == shaderPath) return;
        g_loadedPath = shaderPath;

        // VS（フルスクリーントライアングル、頂点バッファ不要）
        const char* vsCode = R"(
            float4 main(uint id : SV_VertexID) : SV_Position {
                float2 uv = float2((id << 1) & 2, id & 2);
                return float4(uv * 2.0 - 1.0, 0, 1);
            }
        )";
        ID3DBlob* vsBlob = nullptr;
        ID3DBlob* vsError = nullptr;
        HRESULT vsHr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, &vsBlob, &vsError);
        if (FAILED(vsHr))
        {
            if (vsError) vsError->Release();
            return;
        }

        // cbuffer宣言をユーザーのhlslの先頭に結合
        const char* cbufferDecl = R"(
cbuffer SceneParams : register(b0)
{
    float TIME;
    float DELTA_TIME;
    float FRAME_COUNT;
    float SCRW;
    float SCRH;
    float TEXEL_W;
    float TEXEL_H;
    float CAM_NEAR;
    float CAM_FAR;
    float FOV;
    float RENDER_W;
    float RENDER_H;
    float JITTER_X;
    float JITTER_Y;
    float _pad0;
    float _pad1;
};
Texture2D t0 : register(t0); // color
Texture2D t1 : register(t1); // depth
Texture2D t2 : register(t2); // normal
Texture2D t3 : register(t3); // motion
Texture2D t4 : register(t4); // opaque
Texture2D t5 : register(t5); // (reserved)
Texture2D t6 : register(t6); // (reserved)
Texture2D t7 : register(t7); // (reserved)
SamplerState s0 : register(s0);
)";
        HANDLE hFile = CreateFileW(shaderPath, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        DWORD fileSize = GetFileSize(hFile, nullptr);
        std::string fileContents(fileSize, '\0');
        ReadFile(hFile, (LPVOID)fileContents.data(), fileSize, nullptr, nullptr);
        CloseHandle(hFile);
        std::string psSource = cbufferDecl + fileContents;

        ID3DBlob* psBlob = nullptr;
        ID3DBlob* psError = nullptr;
        HRESULT psHr = D3DCompile(psSource.c_str(), psSource.size(), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, &psBlob, &psError);
        if (FAILED(psHr))
        {
            vsBlob->Release();
            if (psError) psError->Release();
            return;
        }

        // 古いPSOを破棄
        if (g_pso) { g_pso->Release(); g_pso = nullptr; }

        D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
        psoDesc.pRootSignature = g_rootSignature;
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
        g_device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&g_pso));

        vsBlob->Release();
        psBlob->Release();
    }



    __declspec(dllexport) void DW_Draw()
    {
        // ① 今どちらのバッファが裏か
        UINT frameIndex = g_swapChain->GetCurrentBackBufferIndex();
        // ② コマンドリストをリセット（ノートを白紙に戻す）
        g_commandAllocator->Reset();
        g_commandList->Reset(g_commandAllocator, nullptr);
        // ③ リソースバリア：表示中 → 描画中
        D3D12_RESOURCE_BARRIER barrier = {};
        barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        barrier.Transition.pResource = g_renderTargets[frameIndex];
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;  // 表示中
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET; // 描画中
        barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        g_commandList->ResourceBarrier(1, &barrier);
        // ④ レンダーターゲットをセット
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = g_rtvHeap->GetCPUDescriptorHandleForHeapStart();
        rtv.ptr += frameIndex * g_rtvDescriptorSize; // 今のバッファのスロットに移動
        g_commandList->OMSetRenderTargets(1, &rtv, FALSE, nullptr);
        // ⑤ 赤でクリア
        float clear[] = { 0.0f, 0.0f, 0.0f, 0.0f }; // RGBAだけど透明度は関係ない
        g_commandList->ClearRenderTargetView(rtv, clear, 0, nullptr);
        // 間にシェーダー描画
        if (g_pso)
        {
            // 定数バッファを更新
            g_time += 1.0f / 60.0f; // 仮、後でDELTA_TIMEに置き換える
            SceneParams params = {};
            params.TIME = g_time;
            params.SCRW = (float)g_scrW;
            params.SCRH = (float)g_scrH;
            params.TEXEL_W = 1.0f / g_scrW;
            params.TEXEL_H = 1.0f / g_scrH;
            memcpy(g_cbufferMapped, &params, sizeof(SceneParams));

            // ビューポートとシザー矩形（描画範囲）
            D3D12_VIEWPORT vp = { 0, 0, (float)g_scrW, (float)g_scrH, 0.0f, 1.0f };
            D3D12_RECT scissor = { 0, 0, (LONG)g_scrW, (LONG)g_scrH };

            g_commandList->SetGraphicsRootSignature(g_rootSignature);
            g_commandList->SetPipelineState(g_pso);

            // CBVヒープをセットしてb0に紐付け
            ID3D12DescriptorHeap* heaps[] = { g_cbvHeap };
            g_commandList->SetDescriptorHeaps(1, heaps);
            g_commandList->SetGraphicsRootDescriptorTable(0, g_cbvHeap->GetGPUDescriptorHandleForHeapStart());

            g_commandList->RSSetViewports(1, &vp);
            g_commandList->RSSetScissorRects(1, &scissor);
            g_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            g_commandList->DrawInstanced(3, 1, 0, 0);
        }
        // ⑥ リソースバリア：描画中 → 表示中
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);
        // ⑦ コマンドリストを閉じてGPUに送る
        g_commandList->Close();
        ID3D12CommandList* lists[] = { g_commandList };
        g_commandQueue->ExecuteCommandLists(1, lists);
        // ⑧ スワップ
        g_swapChain->Present(0, 0); // VSyncは無効
        // ⑨ フェンスで同期
        g_commandQueue->Signal(g_fence, g_fenceValue);
        if (g_fence->GetCompletedValue() < g_fenceValue)
        {
            g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, INFINITE); // GPUが終わるまで待つ
        }
        g_fenceValue++;
    }
    __declspec(dllexport) void DW_SetTextures(void** textures, int count)
    {
        for (int i = 0; i < count && i < 8; i++)
            g_textures[i] = reinterpret_cast<ID3D12Resource*>(textures[i]);

        D3D12_CPU_DESCRIPTOR_HANDLE handle = g_cbvHeap->GetCPUDescriptorHandleForHeapStart();
        handle.ptr += g_srvDescriptorSize;

        for (int i = 0; i < 8; i++)
        {
            if (g_textures[i])
            {
                D3D12_RESOURCE_DESC resDesc = g_textures[i]->GetDesc();

                DXGI_FORMAT srvFormat = resDesc.Format;
                switch (resDesc.Format)
                {
                    // --- 既存のDepthフォーマット ---
                case DXGI_FORMAT_D32_FLOAT:              srvFormat = DXGI_FORMAT_R32_FLOAT; break;
                case DXGI_FORMAT_D16_UNORM:              srvFormat = DXGI_FORMAT_R16_UNORM; break;
                case DXGI_FORMAT_D24_UNORM_S8_UINT:      srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
                case DXGI_FORMAT_D32_FLOAT_S8X24_UINT:   srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;

                    // --- ★追加: UnityがTypelessで渡してくるパターン ---
                case DXGI_FORMAT_R32_TYPELESS:           srvFormat = DXGI_FORMAT_R32_FLOAT; break;
                case DXGI_FORMAT_R16_TYPELESS:           srvFormat = DXGI_FORMAT_R16_UNORM; break;
                case DXGI_FORMAT_R24G8_TYPELESS:         srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS; break;
                case DXGI_FORMAT_R32G8X24_TYPELESS:      srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
                case DXGI_FORMAT_R8G8B8A8_TYPELESS:      srvFormat = DXGI_FORMAT_R8G8B8A8_UNORM; break;
                case DXGI_FORMAT_B8G8R8A8_TYPELESS:      srvFormat = DXGI_FORMAT_B8G8R8A8_UNORM; break;
                case DXGI_FORMAT_R16G16B16A16_TYPELESS:  srvFormat = DXGI_FORMAT_R16G16B16A16_FLOAT; break;
                case DXGI_FORMAT_R11G11B10_FLOAT:        srvFormat = DXGI_FORMAT_R11G11B10_FLOAT; break; // そのままOK

                    // --- ★追加: UNKNOWNは作成スキップ ---
                case DXGI_FORMAT_UNKNOWN:
                    // nullSRVを書いてスロットを埋める（GPU不正アクセス防止）
                    goto write_null_srv;

                default: break;
                }

                {
                    D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                    srvDesc.Format = srvFormat;
                    srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                    srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                    srvDesc.Texture2D.MipLevels = resDesc.MipLevels; // ★ 1固定じゃなくGetDescの値を使う
                    g_device->CreateShaderResourceView(g_textures[i], &srvDesc, handle);
                    handle.ptr += g_srvDescriptorSize;
                    continue;
                }
            }

        write_null_srv:
            {
                // ★ nullスロットにはnull SRV（黒テクスチャ相当）を書く
                D3D12_SHADER_RESOURCE_VIEW_DESC nullSrvDesc = {};
                nullSrvDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
                nullSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
                nullSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
                nullSrvDesc.Texture2D.MipLevels = 1;
                g_device->CreateShaderResourceView(nullptr, &nullSrvDesc, handle);
                handle.ptr += g_srvDescriptorSize;
            }
        }
    }
    

    __declspec(dllexport) void DW_Release()
    {
        if (g_hWnd) DestroyWindow(g_hWnd);
        g_hWnd = nullptr;
    }
}


//
//1フレームの描画の流れ
//① 今どちらのバッファが裏画面か確認
//② コマンドリストをリセット（ノートを白紙に戻す）
//③ バッファの状態を「表示中」→「描画中」に変える（リソースバリア）
//④ レンダーターゲットをセット
//⑤ クリア（透明で塗りつぶす）
//⑥ バッファの状態を「描画中」→「表示中」に変える（リソースバリア）
//⑦ コマンドリストを閉じてGPUに送る
//⑧ スワップ（表と裏を入れ替える）
//⑨ フェンスで同期（GPUが終わるまで待つ）
//ルートシグネチャ → シェーダーコンパイル → PSO作成 → 描画
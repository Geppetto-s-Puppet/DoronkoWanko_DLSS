#include <Windows.h>
#include <dxgi1_6.h>
#pragma comment(lib, "dxgi.lib")
#include <d3d12.h>
#pragma comment(lib, "d3d12.lib")


HWND g_hWnd = nullptr;
IDXGIFactory6* g_factory           = nullptr; // DX12用のバージョン
IDXGIAdapter1* g_adapter           = nullptr; // アダプターはGPUのこと
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

extern "C" {
    __declspec(dllexport) void DW_Init()
    {
        HINSTANCE hInst = GetModuleHandle(nullptr);
        int scrW = GetSystemMetrics(SM_CXSCREEN);
        int scrH = GetSystemMetrics(SM_CYSCREEN);
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
            scrW, scrH,          // ウィンドウサイズ（念のため取得しておく）
            gameHwnd,                                                              // ダイアログボックスみたいに親ウィンドウ設定
            nullptr,                                                               // ファイル・編集・表示みたいなメニューバー不要
            hInst,                                                                 // このDLLのハンドル
            nullptr                                                                // 追加パラメータなし
        );
        // まずウィンドウを生成・表示
        ShowWindow(g_hWnd, SW_SHOW); // MessageBoxW(nullptr, L"ウィンドウ作成完了", L"DW_DLSS", MB_OK);
        // つぎにGPUを探してくる
        CreateDXGIFactory2(0, IID_PPV_ARGS(&g_factory));
        g_factory->EnumAdapterByGpuPreference(
            0, // 一番高性能なGPUを選ぶためにこれ
            DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE,
            IID_PPV_ARGS(&g_adapter)
        );
        // つぎにGPUとの通信窓口
        D3D12CreateDevice(
            g_adapter,              // さっき選んだGPU
            D3D_FEATURE_LEVEL_12_0, // DX12の機能使用
            IID_PPV_ARGS(&g_device)
        );
        // つぎにGPUに命令するベルトコンベア 
        D3D12_COMMAND_QUEUE_DESC queueDesc = {};
        queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT; // 描画・計算・コピーなど全種類の命令対応
        g_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&g_commandQueue));
        // つぎに画面に表示するバッファの管理、昔は裏画面を表画面にコピーしてたが、今はFLIP方式で効率化
        DXGI_SWAP_CHAIN_DESC1 scDesc = {};
        scDesc.BufferCount = 2; // 表画面と裏画面
        scDesc.Width = scrW;
        scDesc.Height = scrH;
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
    }

    __declspec(dllexport) void DW_Show()
    {
        if (g_hWnd) ShowWindow(g_hWnd, SW_SHOWNOACTIVATE);
    }
    __declspec(dllexport) void DW_Hide()
    {
        if (g_hWnd) ShowWindow(g_hWnd, SW_HIDE);
    }


    __declspec(dllexport) void DW_Update() {}
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
        float red[] = { 1.0f, 0.0f, 0.0f, 1.0f }; // RGBA
        g_commandList->ClearRenderTargetView(rtv, red, 0, nullptr);
        // ⑥ リソースバリア：描画中 → 表示中
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        g_commandList->ResourceBarrier(1, &barrier);
        // ⑦ コマンドリストを閉じてGPUに送る
        g_commandList->Close();
        ID3D12CommandList* lists[] = { g_commandList };
        g_commandQueue->ExecuteCommandLists(1, lists);
        // ⑧ スワップ
        g_swapChain->Present(1, 0); // 1=VSync有効
        // ⑨ フェンスで同期
        g_commandQueue->Signal(g_fence, g_fenceValue);
        if (g_fence->GetCompletedValue() < g_fenceValue)
        {
            g_fence->SetEventOnCompletion(g_fenceValue, g_fenceEvent);
            WaitForSingleObject(g_fenceEvent, INFINITE); // GPUが終わるまで待つ
        }
        g_fenceValue++;
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
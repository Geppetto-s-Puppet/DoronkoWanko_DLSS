# 進捗
**DORONKO WANKO™**というUnity URP Forward製ゲームに、カスタムシェーダーとDLSSを実装しGitHubに公開したい
- [x] C# : 他のURP製ゲームでの成功体験, BepInEx注入, Harmonyパッチ, P/Invoke呼出, GameObject追加
- [x] C# : メインカメラとUIカメラ個別取得, GetAsyncKeyState入力, ファイルパス選択ダイアログ
- [x] C++: Win32APIでゲーム上に自前オーバレイ表示, DX12で定数バッファをセットしPS描画
- [x] C++: DW_Init()で渡されたダミーテクスチャを逆引きしてUnityのデバイスと同期
- [ ] C# : dnSpyによるRT解析, リフレクションすべきprivateフィールド特定



- [ ] C++: DW_Update()で渡された
- [ ] C++: DW_Release()
- [ ] C++: プラットフォーム及びグラフィックスAPIの互換性, 自動検証や情報収集などのフォールバック, パフォーマンス最適化




直接的な RenderTexture 取得  
RenderTargetHandle から内部の RenderTexture を直接取り出せていない。現在は RenderTargetIdentifier → Blit のフォールバックに頼っている。

ネイティブに渡すタイミングの保証  
GetNativeTexturePtr() を取得しても、DW_Update に渡す順序とタイミングが確実に守られていない（特に Release 前後の順序）。

一時RTのライフタイム管理  
一時RT（GetTemporary）をネイティブが使い終わるまで保持する仕組みがない。ReleaseTemporary を誤ったタイミングで呼ぶリスクが残っている。

ネイティブの同期モデル未確認  
ネイティブ側がポインタを即時使用するのか非同期で使用するのかを把握していないため、安全なメモリ管理方針が決まっていない。

最初のフレームのタイミング問題  
フレーム1で ptr == 0 が出ている原因（GPU割当の遅延や取得タイミング）を吸収するリトライ／待機ロジックがない。



スロットとターゲットのマッピング確認不足  
どの rtFields が最終出力（ネイティブに渡すべきスロット）かを確定していないため、誤ったスロットにポインタを渡す可能性がある。




    [HarmonyPatch(typeof(ScriptableRenderer), "Execute")]
    class ScriptableRenderer_Execute_Patch
    {
        private static readonly string[] rtFields = {
            "m_CameraColorTarget", // 最終出力
            };

        [DllImport("DW_DLSS_N.dll")] static extern void DW_Update(IntPtr[] texPtrs, int count);
        static void Postfix(ScriptableRenderContext context, ref RenderingData renderingData)
        {
            if (renderingData.cameraData.camera != Plugin.dlss.mainCamera) return;

            var renderer = renderingData.cameraData.renderer; // RT情報はカメラじゃなくScriptableRenderer

            IntPtr[] texPtrs = new IntPtr[8];

            for (int i = 0; i < rtFields.Length; i++)
            {
                // --- ここに貼る（Postfix 内、ループの中） ---
                //RenderTexture rt = ResolveRenderTexture(renderer, rtFields[i], context, renderingData);
                //texPtrs[i] = rt != null ? rt.GetNativeTexturePtr() : IntPtr.Zero;

}

//
//static void Postfix(ScriptableRenderContext context, ref RenderingData renderingData)
//{
//    var cam = renderingData.cameraData.camera;
//    if (cam.name != "Main Camera") return;

//    // ★ UniversalRendererの内部フィールドをReflectionで全列挙。何が取れるか確認するため
//    var renderer = renderingData.cameraData.renderer;
//    var flags = System.Reflection.BindingFlags.NonPublic | System.Reflection.BindingFlags.Instance;
//    foreach (var field in renderer.GetType().GetFields(flags))
//    {
//        if (field.Name.ToLower().Contains("color") ||
//            field.Name.ToLower().Contains("depth") ||
//            field.Name.ToLower().Contains("attach") ||
//            field.Name.ToLower().Contains("target"))
//        {
//            var val = field.GetValue(renderer);
//            Plugin.Log.LogInfo($"[Field] {field.Name} = {val?.GetType().Name} : {val}");
//        }
//    }
//    Plugin.Log.LogInfo($" --------------------------------------------------------- ");
//}



//// shaderPath=nullのときはオーバレイ非表示、C++側でnullか判断する
//[DllImport("DW_DLSS_N.dll", CharSet = CharSet.Unicode)]
//static extern void DW_Update(string shaderPath, IntPtr[] textures, [MarshalAs(UnmanagedType.Bool)] bool visible, int w, int h);

//[DllImport("DW_DLSS_N.dll")]
//static extern void DW_Draw();


// ほしいもの: opaque、シャドウ、最終出力、ライティング前、シャドウマップ、法線、深度、モーションベクター、解像度、カメラ位置、


// ここから下は全部nullった
//"m_ActiveCameraDepthAttachment",   // rts[1] 深度バッファ
//"m_CameraDepthAttachment",         // rts[2] 深度バッファ2
//"m_DepthTexture",                  // rts[3] 深度テクスチャ
//"m_OpaqueColor",                   // rts[4] Opaque
//"_CameraDepthTexture",             // rts[5] 深度(Global)
//"_CameraDepthNormalsTexture",      // rts[6] 法線+深度
//"_CameraMotionVectorsTexture",     // rts[7] モーションベクター













# Deep Learning Super Sampling (DLSS) for DORONKO WANKO™
* Unity URP ForwardのゲームでG-Buffersを再構築して、Deferred-renderingに対応する
* C#とC++の橋渡しを行い、誰でもカスタムシェーダーを書けるための土壌を形成する
* このゲームで、わざわざBepInEx経由してDLSS作ったイカれ野郎はこの世で俺だけ

### 導入手順
- Steamライブラリから、ダウンロードしたDoronkoWankoのローカルファイルを閲覧
- そこへ`BepInEx_win_x64_5.4.23.5`をインストールし、ゲームを一度だけ起動
- フォルダ`DoronkoWanko/BepInEx/plugins/`がちゃんと生成されてるか確認
- そこへ`DW_DLSS_P.dll`と`DW_DLSS_N.dll`をD&Dすれば、MOD導入完了😎
- あとは起動オプション`-force-d3d12`を追加すれば、ワンコと遊べる

### 俺用メモ
- レポジトリの更新: コード書いたら、`Git(G)`→`コミット(C)`→メッセ書いて上矢印マーク
- ゲームのログ: `"D:\SteamLibrary\steamapps\common\DoronkoWanko\BepInEx\LogOutput.log"`
- `GraphicsSettings.currentRenderPipeline.GetType().Name`は`UniversalRenderPipelineAsset`になっている
- Win32 ウィンドウスタイル `https://learn.microsoft.com/ja-jp/windows/win32/winmsg/extended-window-styles`




### 未整理


[レイヤー1] Unity の GPU デバイスを握る（今ここ）
[レイヤー2] 自前の DX12 ウィンドウ + SwapChain を作る
[レイヤー3] PSO / RootSignature / SRV / CommandList を作る






デバイス共有：ダミーRenderTextureを1枚作ってGetNativeTexturePtr()でUnityのD3D12デバイスを特定し、DLLに渡す。DLL側は自前でデバイスを作るのをやめてUnityのものを借りる
ScriptableRenderPass注入：URPパイプラインにカスタムパスを差し込んで、カラー・深度・法線・モーションベクターのポインタをDLLに渡す
DLL側でテクスチャをSRVとしてシェーダーに渡す：ルートシグネチャを拡張してシェーダーからサンプリングできるようにする

https://raw.githubusercontent.com/BepInEx/BepInEx/refs/heads/master/Runtimes/Unity/BepInEx.Unity.Mono/Bootstrap/UnityChainloader.cs
原因が確定しました
csharpManagerObject = new GameObject("BepInEx_Manager") { hideFlags = HideFlags.HideAndDontSave };
Object.DontDestroyOnLoad(ManagerObject);
BepInExはBepInEx_ManagerというGameObjectを作ってDontDestroyOnLoadしています。それでもOnDestroyが呼ばれているということは、ゲームが起動時にBepInEx_Managerを名前かタグで検索して明示的にDestroyしている可能性が高いです。
そして：
csharppublic override BaseUnityPlugin LoadPlugin(...) =>
    ManagerObject.AddComponent(...);
プラグインはBepInEx_ManagerにAddComponentされているので、そのGameObjectが破壊されれば当然プラグインも死にます。
つまりPlayerLoop注入が唯一の正解
GameObjectに依存しない以上、PlayerLoopへの直接注入が現状で取れる唯一の確実な手段です。すでに動作確認済みなので、あとは重複登録フラグを追加するだけです。
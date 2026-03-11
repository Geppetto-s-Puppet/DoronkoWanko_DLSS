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
デバイス共有：ダミーRenderTextureを1枚作ってGetNativeTexturePtr()でUnityのD3D12デバイスを特定し、DLLに渡す。DLL側は自前でデバイスを作るのをやめてUnityのものを借りる
ScriptableRenderPass注入：URPパイプラインにカスタムパスを差し込んで、カラー・深度・法線・モーションベクターのポインタをDLLに渡す
DLL側でテクスチャをSRVとしてシェーダーに渡す：ルートシグネチャを拡張してシェーダーからサンプリングできるようにする
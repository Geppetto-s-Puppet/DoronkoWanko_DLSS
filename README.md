# 進捗
**DORONKO WANKO™**というUnity URP Forward製ゲームに、カスタムシェーダーとDLSSを実装しGitHubに公開したい
- [x] C# : 他のURP製ゲームでの成功体験, BepInEx注入, Harmonyパッチ, P/Invoke呼出, GameObject追加
- [x] C# : メインカメラとUIカメラ個別取得, GetAsyncKeyState入力, ファイルパス選択ダイアログ
- [x] C++: Win32APIでゲーム上に自前オーバレイ表示, DX12で定数バッファをセットしPS描画
- [x] C++: DW_Init()で渡されたダミーテクスチャを逆引きしてUnityのデバイスと同期
- [x] C# : dnSpyで解析したprivateフィールドをリフレクションで叩く
- [ ] C++: ダブルバッファリングに注意しながらDW_Update()でSRV一括送信、DW_Release()まわりの整備
 `Unity2021/URP12`では、せいぜい最終カラーと深度バッファ取得が関の山。Gバッファは再構成したほうが賢明か。
 パフォーマンスは最適化できそうだが、プラットフォームやグラフィックスAPIの互換性を保つのは諦める。

### 導入手順
- Steamでゲームをダウンロードし、起動オプション`-force-d3d12`を追加
- ローカルファイルを閲覧し、`BepInEx_win_x64_5.4.23.5`をインストール(D&D)
- 一度だけゲームを起動し、`DoronkoWanko/BepInEx/plugins/`が生成されてるか確認
- このリリースから`DW_DLSS_P.dll`と`DW_DLSS_N.dll`をダウンロードし、プラグインフォルダに移動
- MOD導入完了😎　あとはゲーム内で`Uキー`を押せばUI切替、`Rキー`を押せばシェーダー読み込み&描画

### 俺用メモ
- レポジトリの更新: コード書いたら、`Git(G)`→`コミット(C)`→メッセ書いて上矢印マーク
- ゲームのログ: `"D:\SteamLibrary\steamapps\common\DoronkoWanko\BepInEx\LogOutput.log"`
- `GraphicsSettings.currentRenderPipeline.GetType().Name`は`UniversalRenderPipelineAsset`になっている
- Win32 ウィンドウスタイル `https://learn.microsoft.com/ja-jp/windows/win32/winmsg/extended-window-styles`
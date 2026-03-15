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

どろんこワンコのDX12リソースを解析する図
<img width="1518" height="811" alt="Image" src="https://github.com/user-attachments/assets/9db8d10b-b7dc-4a0a-bc7b-270203fdd6dc" />

どろんこワンコをdnSpyにかけてデコンパイルする図
<img width="1511" height="811" alt="Image" src="https://github.com/user-attachments/assets/65a288ea-7e2a-409f-8816-bbb07573b541" />
<img width="1512" height="810" alt="Image" src="https://github.com/user-attachments/assets/b07cfbc2-5172-437e-a9f2-5b897b922f58" />

どろんこワンコのライティング処理前が怖すぎる図
<img width="1918" height="1079" alt="Image" src="https://github.com/user-attachments/assets/16d983f1-643a-46d2-bf9b-74ecdba921e3" />
<img width="964" height="561" alt="Image" src="https://github.com/user-attachments/assets/ad2a4b35-1585-4669-93b6-d2a2553b4751" />
<img width="1919" height="1079" alt="Image" src="https://github.com/user-attachments/assets/8759c091-d5bd-47d1-bd7f-8563a13eaea6" />
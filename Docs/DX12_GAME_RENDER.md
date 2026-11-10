# ImGuiから独立したゲーム描画

スレッドの共通基盤と描画移行の条件は [スレッド設計](THREADING.md) を参照してください。現時点のDX12実行はゲームスレッド所有で、共通基盤の追加だけで任意スレッドから呼べるようにはなりません。

2026-11-10（日本時間）。DX12の通常画面と撮影を、ゲームの2D・基本3D・文字・画像からGPUへ直接記録する経路へ切り替えました。ゲーム描画はImGuiの文脈・描画リスト・フォント画像・コールバック・終了順を必要としません。ImGuiは同じGPUを借りる、後段のUI利用者です。

## 責務と所有

|配置・型|担当|
|---|---|
|`Core/Interfaces/Render/IRenderTextRasterizer`|UTF-8の指定範囲をCPU上の白いRGBAと文字の被覆率へ変換する窓口。GPUとUIを知らない|
|`Core/Render/FRenderClipRect`|描画先の画素による、右・下を含まない切り取り範囲|
|`Platform/Render/Canvas/CGeometryCanvas2D`|矩形・線・円・三角形・凸四角形・画像・文字の共通頂点生成。GPUの型とWindows・ImGuiを知らない|
|`Platform/Windows/Text/CWindowsTextRasterizer`|OSの文字をCPU画像へ変換する。GPU資源とUIフォントを所有しない|
|`Platform/DX12/Render/CDX12GeometryRenderer`|頂点の所有コピー、画像・補間・深度・描画状態、GPUへの直接記録。UIの描画順を知らない|
|`Platform/DX12/Render/CDX12GameRenderer`|共通CanvasとGPUの描画実装を接続する。2Dと3Dで切り取り範囲を共有し、呼出順に記録する。提出・待機の所有者ではない|
|`Platform/DX12/Render/CDX12GameCapture`|同じゲーム描画をFDX12CaptureTargetへ描き直し、完了後にRGBA8を返す。ImGui実装を借りない|
|`Platform/DX12/ImGui/CDX12ImGuiFrameRenderer`|ゲーム描画の後にUIだけを記録する。GPUの所有・作成・フレーム提出を担当しない|

操作は既存の描画スレッドで行います。既存のCDX12PlatformSubsystemがGPUと利用者を別々に所有し、フレーム開始・ゲーム描画・UI描画・提出・終了をつなぎます。新しいSubsystemは追加していません。内部のFDX12GameRendererには、所有しない提出・待機の窓口を無理に継承させていません。

FDX12ImGuiGeometryRenderer・FDX12ImGuiCaptureRenderer・FImGuiCaptureDrawListは削除しました。FImGuiCanvas2DはUI側の論理座標の補助として残りますが、DX12のゲーム描画からは参照しません。

## 描画の順序

2Dと3Dは同じGPU命令列へ、その場で記録します。描画→色の消去→描画、クリップの追加・復元、基本3Dの深度消去の順序を保ちます。2Dは3Dの深度を読み書きしません。色の消去はクリップを無視してRGBA全体を置き換え、深度を変えません。画像付き3Dでは、完全に透明な画素の色・深度を書きません。

ゲームの切り取りは、固定の描画先の画素で扱います。主画面がデスクトップ上を移動・拡大しても、ゲーム頂点の変換や文字の切り取りをImGuiの表示原点へ依存させません。撮影は独立した色・深度・命令列を使い、2Dの頂点へ指定倍率を適用します。通常画面の記録と深度を変更しません。

ゲーム段階のコピーはゲーム描画後・UI描画前、UI込みのコピーは主画面のUI記録後です。外部窓は従来どおり別の表示先で、撮影画像へデスクトップ合成しません。UIなしの場合、両段階は同じゲーム画像になります。

各描画利用者は必要なGPU状態を設定します。特に連続する画像付き描画と色だけの描画で補間用heapを欠落させず、UIもゲーム側が残した画像heapに依存せず自分の参照先を設定します。

## 文字と上限

既定のゲーム文字はUI設定から独立した13画素のConsolasです。OSの文字置換・補間を使うため、従来のImGui既定フォントとは字形・幅・境界画素が変わります。ImGuiFontPath・ImGuiFontSize・PushFont等をゲームの既定文字へ適用しません。個別のFontアセット、TextFont／TextStyledの対応を完成させた変更ではありません。

Windows側は[DrawTextW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-drawtextw)とCPU上の[CreateDIBSection](https://learn.microsoft.com/en-us/windows/win32/api/wingdi/nf-wingdi-createdibsection)を使い、GdiFlush後に被覆率をRGBAへ取り出します。UTF-8の指定範囲、&、改行を保持し、タブはWindowsの既定の展開を使います。改行だけの文字列は透明な一列の画像として扱います。不正UTF-8・空・64KiB超・各辺4096超・4M画素超は、全出力を保ってfalseです。

文字画像のGPU所有は既存IRenderTextureResourcesへ渡します。一フレームにつき最大128画像・合計32MiBで、終了時に番号を解除し、実体は既存のGPU完了確認後の回収へ残します。文字画像は描画ごとに作り、永続atlasや非同期アップロード・性能最適化は追加していません。文字画像・頂点・消去の記録失敗は終了へ伝え、撮影を成功扱いせず出力を保ちます。

基本3Dの一描画の頂点数65536、一フレームの頂点64MiB、描画状態128個の既存の上限は維持します。円は3〜512分割、未指定時は半径で決めます。図形の端の補間は共通の頂点生成で行い、従来ImGui／DxLibとの境界画素の完全一致を保証しません。HDR・MSAA・PBR・GPU変換・永続メッシュ・一般のCRenderTarget資産は今回の範囲外です。

## UIを作らない起動

```powershell
.\x64\Debug\Samples.exe --backend=dx12 --sample=Canvas2D --no-imgui --capture=game.png --capture-frame=10 --capture-exit
.\x64\Debug\Samples.exe --backend=dx12 --dx12-warp=true --sample=Geometry3D --no-imgui --capture=large.png --capture-scale=2 --capture-frame=10 --capture-exit
```

SamplesのConfig・AssetsとEngineContentを利用できる作業場所から起動します。共通の--no-imguiは起動設定のbEnableImGuiをfalseにします。DX12はその場合、ImGui文脈とWin32／DX12のUI接続を作りません。ゲーム文字・図形・入力・撮影のためにUIを再有効化しません。DebugOverlayも文脈がないときはUIを記録しません。

## 検査と残る範囲

対応する検査はframework-testのcodex/dx12-windows-validation、[0456903](https://github.com/mynameisGaku/framework-test/commit/045690330c92d20a409e9bdccccd35e276579854)に置きます。RunCaptureTests.ps1 -FrameworkRoot <作業コピー> -Configuration Debug|Release -Platformで、GPU撮影先、UIなしのNativeGameContract、UI合成ありのCaptureContract、UI有効／無効の実アプリを確認します。RunAllChecksの撮影検査にも含まれます。

NativeGameContractはImGuiをinclude・初期化・リンクしません。同じゲーム契約を、UI合成ありの検査にも適用します。矩形・太線・円・輪郭・凸四角形・三角形・AAの内外画素、2Dと3Dの共通クリップと復元、深度消去、文字画像のUTF-8と入力拒否、画像の色と向き、撮影倍率、通常画面への復帰、文字作成失敗時の撮影拒否を調べます。リンクmapにImGuiの関数・データ・実装が含まれないことも確認します。

Tools/CheckGameRenderDependencies.ps1は、GPUの撮影・直接描画・共通Canvas・Windows文字・UI補助の参照境界を検査します。RunGameRenderDependencyCheckTest.ps1はGPUからUIと、共通CanvasからDX12の混入を拒否させます。既存のCheckDX12DeviceDependencies.ps1はGPU所有者からの間接参照も調べます。

Debug／Releaseの4 projectは警告・エラー0でビルドされ、RunAllChecks.ps1 -SkipBuild -Quickの18項目が成功しました。その後の改行だけの文字・空クリップ・命令列の種類の補完と書式整理後も、両構成のビルド、窓・終了・撮影の検査を再実行して成功しています。

|検査|Debug（既定GPU／WARP）|Release（既定GPU／WARP）|
|---|---|---|
|GPU撮影先|各28条件・失敗0|同じ結果|
|UIなしのゲーム描画・保存|各84条件・失敗0|同じ結果|
|UI合成ありのゲーム描画・保存|各91条件・失敗0|同じ結果|
|ImGuiを作らない実アプリ|両GPUで段階通知、PNG、高解像度、透明撮影、三枚のGIFまで成功|同じ結果|
|UIありの実アプリ|両GPUで同じ撮影契約が成功|同じ結果|
|SDKデバッグ層|意図しない警告・エラー0|同じ結果|

UIなしの実行ファイルのmap、依存とその負例、全1723ファイルの書式が成功しています。Canvas2D／Geometry3D／StaticModelも両構成・既定GPU／WARPで、UIありの通常撮影1280×720とUIなしの二倍撮影2560×1440の計24起動が保存まで成功しました。Canvas2D、Geometry3D、StaticModelの画像を目視でも確認しています。起動ごとの保存先はSaved/NativeGameDraw/sample-captures.csvに記録しました。

既知のReleaseのLNK1103を避けるため、検証用コピーの既存iobj／ipdbだけを退避して同じ最適化設定でリンクしました。VendorModulesや最適化設定による回避は追加していません。

検証のログ・画像はSaved/NativeGameDrawとSaved/CaptureTests/<構成>へ保存します。互換台帳は458項目で、Verifiedは増やしていません。ゲームの既存全描画命令とFontアセット、一般描画先、非同期転送、複数ベンダーの一致や性能評価、R1／R2全体を完成させた意味ではありません。

制御文のAllman形式と括弧内を一行に保つ書式はTools/GameRendering.clang-formatで固定しています。今回のC++だけへ明示適用する設定で、通常ビルドの依存は増やしません。Visual Studio同梱clang-format 22.1.3の--dry-run --Werror --style=file:Tools/GameRendering.clang-formatを対象ファイルへ適用して確認しています。既存のSource/SelfTest/CheckSourceFormat.ps1でもUTF-8 BOM・CRLF・括弧を確認します。

## GPUバッファからの後続描画

computeが書いたバッファを既存の深度・画像・合成で描く経路を追加しました。通常画面・撮影先で同じバッファ所有先とキューを渡し、UIの依存は追加しません。固定の28バイト頂点、index、寿命と提出順は [GPUバッファ描画](R1_BUFFER_GEOMETRY.md) を参照してください。

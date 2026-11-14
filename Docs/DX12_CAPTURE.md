# DX12の画素読み戻しと撮影

2026-11-10（日本時間）。DX12の主画面から通常の撮影サービスへ画素を渡し、PNG・BMP・JPEG・GIF、高解像度の描き直しと被写体だけの透明撮影を接続しました。新しいSubsystemは追加していません。

## 責務と依存

通常画面と描き直しは[ImGuiから独立したゲーム描画](DX12_GAME_RENDER.md)を使います。FDX12CaptureTargetはGPUの色・深度・命令列・読み戻し、FDX12GameCaptureはゲーム描画との接続、FGeometryCanvas2DはAPI非依存の形状、WindowsTextRasterizerはCPU文字画像、WicImageEncoderは保存形式を担当します。既存のCScreenCaptureSubsystemが要求・切り抜き・縮小・被写体選択・GIFを管理します。

旧FDX12ImGuiCaptureRenderer・FDX12ImGuiGeometryRenderer・FImGuiCaptureDrawListは削除しました。撮影はImGui文脈・フォント・描画リストを使わず、GPUとCPU文字の窓口だけを借ります。CheckGameRenderDependencies.ps1と、ImGuiをリンクしないNativeGameContractで依存を確認します。

## 通常の撮影

`IPlatform::SetFrameCaptureSink`へ既存の撮影サービスを接続します。必要なフレームと段階だけをコピーし、GPU完了後にゲーム→UI込みの順で通知します。通知番号は描き終えるフレームの`GetFrameCount() + 1`、寸法はゲームの論理解像度、透明度は255です。主画面の移動・拡大後も、固定の描画先から同じ論理寸法で受け取ります。UI込みの画像には主画面内のUIを含めます。切り離した外部窓をデスクトップ画像へ合成する処理は追加していません。

ゲームの画素はゲーム描画後・UI記録前、UI込みの画素は主画面のUI記録後にコピーします。ゲームの記録順へImGuiのコールバックを挿入しません。撮影が不要なフレームにはコピーと撮影用のGPU待機を追加しません。通知中に受け取り先を解除・差し替えた場合、残る段階を古い相手へ通知しません。画素列の借用はOnFrameの呼出中だけです。

既存の撮影APIと起動引数を使えます。既定の保存先は`Saved/Screenshots`です。

```powershell
.\x64\Debug\Samples.exe --backend=dx12 --sample=Canvas2D --capture=Canvas.png --capture-frame=10 --capture-exit
.\x64\Debug\Samples.exe --backend=dx12 --sample=Geometry3D --capture=Large.png --capture-scale=2 --capture-frame=10 --capture-exit
.\x64\Debug\Samples.exe --backend=dx12 --sample=Geometry3D --record=Motion.gif --record-start=10 --record-frames=12 --record-step=2 --record-exit
```

WARPでは`--dx12-warp=true`を追加します。`--capture-ui`・`--record-ui`、切り抜きと縮小も既存の撮影サービスで処理します。

## 別の大きさへの描き直し

`IDrawExecutor::BeginOffscreenCapture`／`EndOffscreenCapture`を、既存のIRenderSurfaceの省略可能な操作へ接続します。DX12は通常の命令列と別に、撮影用の色・深度・命令列・頂点を持ちます。2Dの頂点・文字生成時のクリップ・画像へ撮影倍率を適用し、基本3Dには撮影先の寸法と専用深度を使います。通常画面の論理座標と記録済みの命令は変更しません。

文字はWindows側でCPU画像へ変換して、通常のゲーム画像と同じ経路で描きます。描き直しにImGuiのEndFrameや文字画像更新を必要としません。VendorModulesは変更していません。

GPUの完了を待ってから、コピー先の行の余白を除き、左上から行順のRGBA8で返します。二重開始・未開始の終了・不正な寸法や倍率は拒否します。UI文脈の有無や切り替えは撮影へ影響しません。失敗時に呼出側の画素列を上書きしません。提出後の待機失敗では資源と命令列を保持して再利用を止め、終了を再試行できます。最終破棄はGPU完了かデバイス喪失まで待ちます。

撮影先は各辺1〜16384、色の画素列は256MiBまで、2D倍率は有限の0より大きく16384以下です。色と深度、コピー先の割り当ては別に必要です。メモリ不足では作成に失敗します。操作は既存の描画APIと同じスレッドで行い、並行呼出しには対応しません。

## 保存形式

PNGはRGBAを保持し、BMPとJPEGは透明度を持たない画像にします。JPEG品質は0〜100へ収めます。Unicode名と既存のファイル窓口を使い、画像全体をメモリで符号化してから保存先へ書きます。不正な入力・未対応の形式・符号化失敗で、保存前にファイルを作ったり上書きしたりしません。ファイルへの書き込みそのものが失敗した場合の原子的な置換は追加していません。

WICのPNG設定は圧縮率0〜9を直接指定する窓口を持たないため、初期実装は`PngCompression == -1`だけを受け付けます。個別指定は既存ファイルを保ってfalseを返します。撮影サービスのPNG保存は既定設定を使います。PNGの設定とJPEG品質は[Microsoftの符号化設定](https://learn.microsoft.com/en-us/windows/win32/wic/-wic-creating-encoder)を参照してください。GIFは既存のFGifEncoderをそのまま使います。

## 検査

対応する検査は別リポジトリの`codex/dx12-windows-validation`、[0456903](https://github.com/mynameisGaku/framework-test/commit/045690330c92d20a409e9bdccccd35e276579854)に置きます。対応する製品の静的ライブラリとproject.exeをビルドしてから実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunCaptureTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunCaptureTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

|検査|Debug|Release|
|---|---|---|
|ImGui・OS窓なしのGPU撮影先（既定GPU／WARP）|各28条件、失敗0|各28条件、失敗0|
|UIなし／ありのゲーム描画と保存|最新の結果は[独立描画の記録](DX12_GAME_RENDER.md)を参照|同左|
|SDKデバッグ層|意図しない警告・エラー0|同じ結果|
|実アプリの撮影サービス（DX12／WARP）|通常PNG、UI込みPNG、別寸法、被写体の透明度、三枚のGIFまで成功|同じ結果|
|依存の確認|GPU専用実行ファイルにImGuiなし。両方向の不正includeを拒否|同じ結果|

GPU専用テストは奇数の寸法・繰り返すresize・RGBA全数比較・初期消去色と異なる消去・提出後のタイムアウト保持・終了再試行を含みます。接続テストは2Dの拡大とクリップ、独立したゲーム文字の実画素、共有画像の向き、基本3Dの深度、通常描画への復帰、移動・拡大後の主画面の3Dクリップ、ゲームとUIの違い、無効入力とUI文脈からの独立、PNG透明度とUnicode名を確認します。GIFは別の読み込み器でも32×32・三枚を確認します。

Releaseの最初のリンクはLNK1103で停止しました。検証用コピーのproject／Samples／Editorのiobj・ipdbだけを退避し、同じ最適化設定で作り直して成功しました。ソースや最適化設定での回避は追加していません。途中と最終のログは`Saved/DX12Capture`、各起動の画像と診断は`Saved/CaptureTests/<構成>`へ保存します。

最終のDebug／Releaseはソリューションの4 projectが警告・エラー0でビルドされ、RunAllChecks.ps1 -SkipBuild -Quickの18項目が成功しました。既存のecs／assets／world、バッファ・compute、窓・入力・終了の回帰も含みます。Canvas2D／Geometry3D／StaticModelの通常撮影1280×720と二倍の描き直し2560×1440も、両構成・既定GPU／WARPの計24起動で保存まで成功しました。画像を目視でも確認し、起動ごとの場所は`Saved/DX12Capture/sample-captures.csv`に残しています。sample／template検査一式や配布物のクリーンビルド・性能測定の完了とは区別します。

## 残る範囲

一般のCRenderTarget資産・PushTarget、HDR／MSAA、GPU上の非同期撮影、すべての描画命令の互換、外部窓の合成、旧IPlatform::RequestScreenshotは残っています。GPU喪失中のImGui内部のアップロードや描画実装の回復も未完了です。今回の対応をR1全体・R6全体・R8全体の完了とは扱いません。互換台帳は457項目で、Verifiedへの昇格はしていません。

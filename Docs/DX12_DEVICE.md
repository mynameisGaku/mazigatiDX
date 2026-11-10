# GPU所有者とImGui利用者の分離

2026-11-10（日本時間）。DX12はゲームとUIが利用するグラフィックスの実装です。ImGuiのためにGPUを作る構成へ固定せず、UIは初期化済みのGPUを借ります。

## 所有と依存

|型|担当と所有|
|---|---|
|`FDX12Device`|GPU・交換用画面・画像／バッファ／compute・フレームの記録と提出・完了待ち。ImGuiの型・文脈・実装に依存しない|
|`FDX12ImGuiFrameRenderer`|GPU所有者を借り、通常画面のImGui資源と描画記録を管理。GPUの作成・開始・提出・破棄は行わない|
|`FDX12ImGuiRenderer`|公式ImGuiのDX12実装と、UIが借りた記述子。ゲーム画像の所有者ではない|
|`CDX12PlatformSubsystem`|GPUと利用者を別々に所有し、窓・ゲーム描画・UI記録・提出・終了の順序をつなぐ|
|`FDX12GeometryRenderer`|ゲームの共通頂点を直接GPUへ記録する。UIと記録順の借用を必要としない|

FDX12Deviceの公開操作からImDrawData・ImDrawList・ImU32を外しました。初期化にImGui文脈は不要です。外側の描画実装は、記録中の命令列・描画先と共有する画像heapを借ります。API固有の型をCore／Frameworkへ追加しません。

## フレームと終了

GPU所有者がBeginFrameを行い、各利用者がGPUへ描画を記録し、GPU所有者がEndFrameで提出・表示します。ImGuiのRenderは描画データを記録するだけで、フレームを提出しません。UIを外してもGPUを作り直さず、ゲーム用の画像・バッファ・computeを使えます。

UIだけの終了はGPUの完了を確認してUIの借用だけを戻します。通常フレームを記録中の解除は拒否します。GPU全体を終了するときは、撮影側の終了後、所有側がPrepareShutdownで新規記録を止めて完了を確認します。その後、基本3D・UIの利用者を外してからGPUの資源を解放します。期限切れでは参照を保持し、再試行できます。最終破棄ではGPU完了か喪失まで保持します。

操作は従来と同じ描画スレッドで行い、終了中に借用したキューへ新しい命令を提出しません。利用者がGPU所有者より長く生きる使い方は認めません。外部窓のGPU障害回復全体や、複数キューの新しい実行方式を完成させた変更ではありません。

## 検査

対応する検査はframework-testのcodex/dx12-windows-validation、[0cf93ea](https://github.com/mynameisGaku/framework-test/commit/0cf93ea1197365da54b046567f29591f6ddaf9a5)に置きます。製品のDebug／Releaseをビルドしてから、RunDX12WindowTests.ps1 -FrameworkRoot <作業コピー> -Configuration Debug|Release -Platformを実行します。RunAllChecksの窓・終了検査にも含まれます。

DeviceLifecycleはImGuiを含めず、通常フレームの開始・色の消去・提出・表示・RGBA全画素の読み戻し、待機の期限切れと古い通知、終了保持・再試行・再初期化・喪失を調べます。リンク結果のmapにもImGuiのシンボルがないことを確認します。ImGuiFrameLifecycleは同じGPU終了契約に加え、UIを解除した後の表示とゲームバッファ保持、同じGPUへのUI再接続、記録中のUI解除拒否を調べます。

Tools/CheckDX12DeviceDependencies.ps1はFDX12Deviceから67個の自作ファイルをたどり、直接・間接includeのUI／DxLib／Framework参照とUI型を拒否します。RunDX12DeviceDependencyCheckTest.ps1は正常例、直接・間接include、UI型の前方宣言の負例を製品のソースを変えずに確認します。

検証結果はSaved/DX12DeviceIsolationへ保存します。Releaseの既存リンクキャッシュの破損時は検証用コピーのiobj／ipdbだけを退避して同じ最適化設定で作り直します。

Debug／Releaseとも4 projectのビルドが警告・エラー0で完了し、RunAllChecks.ps1 -SkipBuild -Quickの18項目が成功しました。既定GPU／WARPで次の結果を確認しています。

|検査|Debug|Release|
|---|---|---|
|UIなしのGPUデバイス|通常28条件・喪失17条件、失敗0|同じ結果|
|GPUを借りるUI|通常31条件・喪失18条件、失敗0|同じ結果|
|リンクと間接依存の検査|ImGuiの関数・データ・実装をリンクしない。67ファイルの参照と負例を確認|同じ結果|
|外部窓|有効180条件・無効114条件・実行中切り替え228条件、失敗0|同じ結果|
|撮影|GPU専用28条件・描画と保存61条件、実アプリの撮影とGIF、失敗0|同じ結果|

SDKデバッグ層は有効で、意図した専用デバイスの喪失通知以外の警告・エラーは0でした。実アプリの主画面の最小化・復帰・外部窓の終了順も通っています。既存ecs／assets／world・バッファ・computeの回帰も18項目に含めます。sample／template検査一式・配布物のクリーンビルド・性能測定や複数ベンダーの検証を完了した意味ではありません。互換台帳は457項目で、Verifiedは増やしていません。

## 後続のゲーム描画

[ゲーム描画の分離](DX12_GAME_RENDER.md)で、既存の2D・基本3D・文字・撮影もImGuiの記録順とフォントから外しました。GPU所有者・ゲーム描画・UIは、それぞれの窓口で同じGPUへ接続します。上の検証表はGPU所有者を分離した時点の記録で、最新のゲーム描画と撮影の条件数・制限は後続記録を参照してください。

# 共通バッファからのcompute提出

2026-11-10（日本時間）。R1の一部として、共通窓口による「buffer作成→compute書込→状態遷移→copy→完了確認→readback」をWindowsで接続しました。後続で [GPUバッファ描画](R1_BUFFER_GEOMETRY.md) を接続しました。交互の提出管理、DXC／SM6、R1全体の完了は引き続き残っています。

## 契約と所有者

|窓口・値|役割|
|---|---|
|IRenderBufferResources|初期転送・サイズ照会・readback・解放。CreateBufferへ用途None／Read／ReadWriteを追加。既定Noneは従来と同じ転送専用|
|IComputePipelineResources|コンパイル済みバイナリとバッファ配置からプログラムを作成・解放。ソースの生成・コンパイルやファイル読み込みは担当しない|
|IComputeQueue|命令列の提出、完了照会、期限付き待機。作成と資源所有から分離|
|FComputePipelineHandle|登録先と照合するプログラム番号。GPUアドレスを公開しない|
|FComputeBufferSlot|読み取り／読み書きと場所の番号。配置配列の順とdispatchのバッファ配列の順が対応|
|FComputeDispatch|プログラム番号、バッファ番号の借用配列、XYZのグループ数|
|FComputeSubmission|提出先の識別番号と、その提出の完了番号。再初期化後の番号や別の提出先と混同しない|

共通の値はCore/Render、3つの窓口はCore/Interfaces/Renderに置きます。FDX12Deviceが既存のデバイスとDIRECTキューを使い、FDX12BufferStore・FDX12ComputePipelineStore・FDX12ComputeQueueを所有します。既存のCDX12PlatformSubsystemが窓口を提供し、新しいSubsystemは追加しません。操作はゲームスレッドから行い、並行呼出しには対応しません。

バッファ・プログラム・提出先の番号は、既存のTAtomicを使うRenderResourceSerialから発行します。同じプロセス内で再利用せず、番号枯渇では失敗します。バッファの公開型は従来のFBufferResourceHandleを保ちます。DxLib／Nullは3つのGPU窓口を生成しません。

## 受け入れる入力

- 初期DX12実装はDXBCのCS5.0／5.1と、ByteAddressBuffer／RWByteAddressBufferだけです。reflection情報が必要で、段・形式・コンテナ全長・実際の資源宣言を検査します。Pixel、reflectionを取り除いたデータ、切断・末尾余剰、不一致の配置は拒否します。DXIL／SM6、構造化・型付きバッファ、画像、定数バッファ、samplerは未対応です。
- Readはt、ReadWriteはuのspace 0へ対応します。読み取りの場所は0〜127、読み書きは0〜63。種類と番号が重なる配置は拒否し、shaderで実際に使う資源と一致させます。同じ数のtとuは別の場所です。
- シェーダー用途のバッファ長は4の倍数です。ReadWriteだけにGPU書き込みを許可し、ReadWriteをReadの入力にも使えます。NoneからのSRV／UAV利用、ReadからのUAV利用は拒否します。
- 命令列全体のプログラム、用途、バッファ番号、グループ数を検査してから記録・提出します。後ろに不正な命令があっても前の命令だけを実行しません。同じdispatchで同じバッファを複数箇所に置くことは、読み取り同士を含めて拒否します。
- グループ数は各軸1〜65535です。一グループのスレッド数はreflectionで検査し、合計1024以下、X／Yは1024以下、Zは64以下とします。範囲内の巨大な実行量が短時間で終わる保証はありません。

初期上限はプログラム64個（回収前の解放分を含む）、バイナリ4MiB、配置16箇所、一提出32dispatch、未完了8提出です。既存バッファの一つ64MiB・合計256MiB・256個の上限も適用します。待機は0〜60000msで、0は完了確認だけです。

## 状態と寿命

SubmitComputeはGPU完了を待たずに戻ります。提出ごとに専用の記録領域、命令列、サイズ付きのSRV／UAV記述子、プログラムとバッファへの実体参照を持ちます。呼出側の配列変更・バイナリ解放・番号の登録解除は提出済みの処理へ影響しません。解除済み番号で新しい命令を提出することはできません。

一dispatchの入口でCOMMONからSRV／UAV用途へ移し、書き込みの後にUAV barrierを記録し、出口でCOMMONへ戻します。次のdispatchは同じバッファを入力として読めます。同期readbackも同じDIRECTキューに順序通り提出するため、先行computeの結果を読めます。描画用の画像heap・ImGuiの状態へ依存せず、computeは専用heapを使います。バッファの範囲を持つ記述子を選んだ理由は、root descriptorにサイズ情報がないためです（[Microsoftのroot signature説明](https://learn.microsoft.com/en-us/windows/win32/direct3d12/root-signatures-overview)）。

完了照会で回収済みの提出番号も、同じ提出先が生きている間は照会できます。不正・未来・別の提出先の番号では出力を保って失敗します。タイムアウトは資源回収や番号無効化を伴わず、後から再待機できます。以前の短い待機のイベント通知を受けても、今回の完了番号を確認するまで成功にはしません。同じ未完了番号への通知の重複登録も抑止します。

Signal失敗またはデバイス喪失では再提出を禁止し、未完了参照を保持します。GPUへ渡した後の失敗で、実行済みの書き込みを巻き戻すことはできません。CollectAfterGPUIdle／ResetAfterGPUIdleと破棄は、所有者が同じキューの完了またはデバイス喪失を保証する前提です。共有デバイスの待機と終了保持は後続の [ImGui対応](DX12_IMGUI_VIEWPORTS.md) で修正しました。外部窓のresizeやデバイス喪失を含む障害回復全体はR1の残件です。

## 検査

検査は別リポジトリframework-dx12-selftestsのcodex/dx12-windows-validationに置きます。CPU参照は検査用kernelの独立した式を計算するだけで、製品用のHLSL実行器や未対応GPUの代替処理ではありません。SDKコンパイラーは検査側でだけ使用し、製品の作成窓口にはコンパイル済みデータを渡します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

独立した検査の中間・出力はSaved/ComputeContract/<構成>へ分離します。開発中のx64配下やCLの追跡ファイルを共有しません。-Platformには、対応する自己検査を組み込んだproject.exeが必要です。RunAllChecks.ps1にも構成ごとの検査を追加しました。

|検査|Debug|Release|
|---|---|---|
|独立CPU参照|29条件・失敗0|29条件・失敗0|
|既定GPU（CS5.0／5.1実行）|149条件・失敗0|149条件・失敗0|
|WARP（CS5.0／5.1実行）|149条件・失敗0|149条件・失敗0|
|SDKデバッグ層|有効、警告・エラー0|有効、警告・エラー0|
|アプリの共通サービス取得|DX12／WARP／DxLib／Nullの4起動、各3フレーム成功|同じ4起動が成功|

両構成でソリューションの4 projectが警告・エラー0でビルドされました。その後のRunAllChecks.ps1 -SkipBuild -Quickは両構成合わせて14項目が成功しています。書式・書式検査自体・Clean.bat・台帳の整合に加え、既存ecs／assets／worldをワーカー数0／1／4／15で検査しました。ecsは7,867,833値でハッシュ403b39415ba4b9a6、assetsはDebug 9,956件／Release 9,953件、worldは両構成3,635件でした。既存assetsの省略項目もそのままで、全機能を実機検査した意味ではありません。

既存バッファ検査も両構成でCPU88条件・既定GPU／WARP各378条件、4バックエンドの各3フレームが成功しました。Canvas2D／Geometry3D／StaticModelは両構成の既定GPU／WARPで各120フレーム、合計12起動が成功しました。画像の比較を伴うsample検査一式・template検査・配布物のクリーンビルド・性能測定・複数ベンダー確認は今回の範囲に含めていません。

64要素の初回出力と、それを次の入力にする二段の計算をCPU式と比較しました。半分だけのdispatchでは後半が前の値を保つことも確認します。不正な命令列の全体拒否、33dispatchの拒否、用途・番号・配置の誤り、プログラム64個の上限と回収、未完了8提出の上限、提出直後の入力／プログラム解除、呼出側配列の変更、意図的なタイムアウトと遅い別通知を挟む再待機、reset後の番号拒否を検査しています。

検査側の対応コミットは46d17a3788a9b2bd438fbd8259f61d44a6091079（ローカル）です。製品の開発用作業コピーとは分けたframework-dx12-verifyでビルド・実行し、結果をSaved/R1Compute/とSaved/ComputeContract/へ保持します。

互換台帳は455項目（API311）です。新規5操作と用途を追加したCreateBufferをImplementedとして記録し、Verifiedへは昇格していません。

## 残る範囲

同じバッファを頂点・indexとして使う契約は [GPUバッファ描画](R1_BUFFER_GEOMETRY.md) で接続しました。交互の提出管理、定数／構造化バッファ・画像の用途、能力照会、DXC／SM6、非同期copy、複数キュー、フレーム別の領域再利用、timestamp、終了・resize・GPU失敗の包括的検査は残っています。今回の非同期待機はCPUを止めずに提出する意味で、専用のasync computeキューを追加した意味ではありません。通常フレーム開始時の全GPU待機も既存のままです。

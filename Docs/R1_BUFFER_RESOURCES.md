# 共通GPUバッファの初期転送

2026-11-09（日本時間）。R1の最初のバッファ資源の契約です。R0・R1全体の完了や、compute・永続GPUメッシュの完成とは区別します。

2026-11-10追記: CreateBufferへRead／ReadWrite用途を追加し、RAWのSRV／UAVとcompute提出・完了番号を接続しました。以下は初期転送を追加した時点の記録です。現在の用途と非同期処理、検査結果、残る制限は [computeの契約](R1_COMPUTE.md) を参照してください。既定Noneの転送専用呼出しはそのまま利用できます。

## 今回の窓口

`Library/Core/Interfaces/Render/IRenderBufferResources.h` の4操作を追加しました。

|操作|契約|
|---|---|
|CreateBuffer|空でない初期バイト列を呼出中に複写し、GPUへの転送と完了確認後に型付き番号を返す。失敗時は無効値|
|ReleaseBuffer|番号を直ちに無効にする。二重解放・無効値・別の所有先の番号は無視し、実体の回収をGPU完了後へ延期する|
|GetBufferSize|登録したデータのバイト数を照会。失敗時は出力を保つ|
|ReadBuffer|指定範囲をGPUから別のreadback領域へコピーし、完了後だけ出力を置き換える。負数・範囲外・待機失敗時は出力を保つ。末尾を含む空範囲は有効|

番号は `FBufferResourceHandle` です。画像番号・アセット番号・GPUアドレスを流用せず、同じプロセスの登録番号を再利用しません。番号の有無と実際の生存は別で、所有先は登録表も確認します。OSや描画APIの型は共通契約へ出しません。

`FDX12BufferStore` がGPUバッファ・upload/readback領域・コピーの記録領域・fenceを所有します。`FDX12Device` は既存のデバイスとDIRECTキューを渡して、フレーム開始時と終了時の回収を接続します。既存の `CDX12PlatformSubsystem` から共通窓口を取得できます。新しいSubsystemは追加していません。DxLib／NullはGPU機能を持つふりをせず、この窓口を生成しません。

## 状態・寿命・上限

- 初期GPUバッファはCOMMONで作り、COPY_DESTへ遷移して初期データを転送し、COMMONへ戻します。読み戻しもCOMMON→COPY_SOURCE→COMMONを同じ命令列に記録します。提出の間にバッファがCOMMONへ戻る規約を前提にし、フレームの画像・ImGui記述子へ依存しません。
- 登録番号は転送完了後だけ公開します。入力配列を解放しても内容は変わりません。入力を書き換える更新APIやSRV／UAV、描画・computeへの貸し出しはまだありません。
- 記録・提出・最大10秒の待機に失敗した場合は、その記録領域を再使用しません。提出したコピーの両側を保持し、登録解除後もGPU参照を早く破棄しません。GPU完了を確認した回収だけでは再使用せず、明示したreset／初期化で復帰します。
- `CollectAfterGPUIdle`／`ResetAfterGPUIdle` と破棄は、呼出側が同じキューの完了またはデバイス喪失を保証する契約です。デバイス喪失や、共有デバイス終了時の待機が終わらない場合の対処全体はR1の残件です。
- 一つ64MiB、合計256MiBのデータ、256個までです。登録解除済みも回収まで合計へ含めます。256MiBはデータの合計であり、GPUの確保単位による余白・一時upload/readback領域・呼出側のCPU配列は含みません。同期コピーの一時領域は一つの転送につき最大64MiBです。
- 作成と読み戻しは同期です。通常描画の毎フレームでCPUを停止するための窓口ではありません。

## 検査

自己検査は製品のSourceへ取り込まず、別リポジトリ `framework-dx12-selftests` の `codex/dx12-windows-validation` で管理します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunRenderBufferTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunRenderBufferTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

`-Platform` は、対応版の自己検査を組み込んだproject.exeのビルド後に指定します。省略すると、製品と同じ実装ソースを直接コンパイルする独立した契約検査だけを実行します。ビルド・結果の記録先は、対象の作業コピーの `Saved/RenderBufferContract/<構成>/` です。Visual Studioで開発中の作業コピーと同じ中間ファイルを使いません。`RunAllChecks.ps1` へも構成ごとの検査を接続しました。

CPUの参照実装とDX12実装に共通の検査は、1・3・255・257・4097・65539・1048579バイト、非整列の部分範囲、空範囲、負数と64bit範囲のあふれ、失敗時の出力保持、入力の解放後の内容、別の所有先・解放済みの番号です。GPU固有では、保持数とデータ量の上限、回収後の再利用と番号の非再利用、異なる種類と所有先のキューを検査します。コピーの前に検査専用の待ち合わせを入れ、意図したタイムアウト後の出力保持・資源の寿命・記録領域の再使用禁止・明示的な復帰も確認します。

異なるデバイスの検査では実機とWARPを分けます。同じアダプターに対するD3D12CreateDeviceは既存デバイスを返すためです（[Microsoftの契約](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice)）。

ゲーム側の接続検査は具体的なDX12クラスへ触れず、共通窓口から作成して三つのフレームで同じ内容を読み戻し、最後に登録を解除します。DX12既定／WARPでは窓口の存在、DxLib／Nullでは非生成を確認します。

最終結果（Windows x64、MSVC v145 14.51.36231、SDK 10.0.26100.0）:

|確認|Debug|Release|
|---|---|---|
|全体ビルド|4 project成功、警告0・エラー0|4 project成功、警告0・エラー0|
|CPU参照の共通契約|88条件成功|88条件成功|
|既定GPUのバッファ契約|378条件成功、SDK検査層有効、警告・エラー0|同じ条件で成功|
|WARPのバッファ契約|378条件成功、SDK検査層有効、警告・エラー0|同じ条件で成功|
|ゲーム側の接続|DX12／WARP／DxLib／Nullの4起動が成功|同じ4起動が成功|
|既存DX12 sample|Canvas2D／Geometry3D／StaticModelを既定GPU／WARPで各120フレーム、6起動が成功|同じ6起動が成功|
|既存の回帰検査|一括検査8項目が成功|ecs・assets・worldが全ワーカー数で成功|

既存のecsは7,867,833値、結果のハッシュは両構成とワーカー数0／1／4／15で `403b39415ba4b9a6` でした。assetsはDebug 9,956件・Release 9,953件、worldは両構成3,635件です。assetsには既存の実機検査等の省略があり、Releaseのログでは4件を省略しています。sample／template全体や配布物のクリーンビルドは今回の範囲に含めていません。

Releaseの最初のリンクと直列での再試行では、MSVCがString.cppを指すC1001とLNK1000で停止しました。検証用コピーのproject.iobj／project.ipdbを退避して同じ最適化設定で作り直すと成功し、その後の全体ビルドも成功しました。ソースや最適化設定による回避は入れていません。最初の内部エラーの原因は断定せず、失敗と再作成のログを `Saved/R1Buffers/` へ保持しています。

検査側の対応コミット: cfec69da1afb02e2c35bbad4520c0442b99f33b5（ローカル）。SDKデバッグ層が利用できる場合は、警告・エラーも不一致として数えます。互換台帳は4操作をImplementedとして450項目へ増やしましたが、Verifiedへの昇格はしていません。

## 次の区切り

次は共通の用途・SRV／UAVとcompute命令の記録・提出・完了番号を定め、今回の所有と失敗時の保持へ接続します。シェーダー形式と能力はR0のWindows確認を続けます。「buffer→compute書込→遷移→copy→完了→readback」のうち、今回完了する範囲は初期データの所有と同期コピー／readbackだけです。R1全体の完了条件である描画→compute→描画の共通経路は残っています。

## 後続の接続

compute提出は [R1_COMPUTE.md](R1_COMPUTE.md)、GPU出力を読み戻さず頂点・16／32bit indexに使う描画は [R1_BUFFER_GEOMETRY.md](R1_BUFFER_GEOMETRY.md) で接続しました。上記「次の区切り」は初期実装時点の記録です。R1全体の完了は引き続き残ります。

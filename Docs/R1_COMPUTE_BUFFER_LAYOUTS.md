# computeの定数と構造体配列

2026-11-10（日本時間）。R1の既存RAW computeへ、読み取り専用の定数と、読み取り／読み書きの構造体配列を追加しました。バッファ作成、プログラム所有、非同期提出とGPU完了待ちの所有者は増やさず、既存の三窓口を使います。ImGui・アセット・ファイル入出力の依存は追加しません。

## 共通の値と責務

FComputeBufferSlotにEComputeBufferLayoutのLayoutと、一要素の幅ElementStrideを追加しました。BindingとAccessだけの従来の二項目初期化はRaw／幅0になり、既存RAW呼出しを維持します。配置の選択は、バッファに許可する読み取り／読み書きとは別の指定です。

|Layout|Access|ElementStride|初期DX12の場所|提出するバッファ全体の条件|
|---|---|---|---|---|
|Raw|Read／ReadWrite|0|t0〜127／u0〜63|従来通り4バイトの倍数|
|Structured|Read／ReadWrite|4〜2048、4の倍数|t0〜127／u0〜63|全長が要素幅で割り切れる|
|Constant|Readだけ|0|b0〜13|256バイトの倍数、256〜65536バイト、シェーダーの宣言範囲以上|

Core/Renderの新しいenumと既存slotはAPI固有の列挙やGPUアドレスを持ちません。FDX12ComputePipelineStoreがコンパイル済みCS5.0／5.1の資源宣言を照合し、FDX12ComputeQueueが実際のバッファ範囲を検査して記録します。FDX12BufferStoreは引き続き同じバイト列と番号の所有先で、別の定数専用storeやSubsystemは作りません。

FComputeBufferSlotはfield追加でメモリ配置が変わるため、利用側も同じheaderで再ビルドします。二項目指定のソース互換を保つことと、以前のバイナリとの互換は別です。

## 作成時の照合

RAW／構造体配列のReadはt、ReadWriteはu、定数はbの別の名前空間です。同じ番号のb0／t0／u0は同居できます。RAWと構造体配列は同じtまたはuを占めるため、同じ場所へ重複指定できません。バイナリの列挙順には依存せず、dispatchの配列はslotの宣言順へ対応させます。

構造体の幅はreflectionのNumSamplesと一致させます。これは構造体配列の場合に要素の幅を返すfieldです（[Microsoftの定義](https://learn.microsoft.com/en-us/windows/win32/api/d3d12shader/ns-d3d12shader-d3d12_shader_input_bind_desc)）。StructuredBufferとRWStructuredBufferだけを受け付け、Append／Consume、隠れたcounterを使う種類、型付きBuffer・画像は拒否します。fieldごとの型・順序まで自動変換はせず、同じ幅でも内容の配置は呼出側が保証します。

定数はreflectionの宣言サイズをslot順の内部値へ保持します。最適化で実際に使われない資源を追加した宣言、サイズ0や64KiB超過、非対応の種類、幅やレジスターの不一致ではプログラムを公開しません。既存のDXBC全長・段・profile・space 0・資源数・スレッド数・バイナリ容量等の検査も保ちます。

## 提出と寿命

命令列全体の番号・用途・個数に加え、構造体の全長と定数の整列・最大／最小範囲を、記述子の作成前に確認します。後のdispatchが不正なら前のdispatchも提出しません。定数でもCreateBufferのReadまたはReadWriteが必要です。ReadWriteの資源を、後のdispatchで定数やSRVとして読むこともできます。同じdispatch内の同一資源の重複は、従来通り読み取り同士でも拒否します。

StructuredはDXGI_FORMAT_UNKNOWN、要素幅と全要素数を持つサイズ付きSRV／UAVへ変換します（[MicrosoftのSRVの定義](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ns-d3d12-d3d12_buffer_srv)）。Constantはサイズ付きCBVへ変換します。256バイトへの整列は現在のDX12経路の条件で、暗黙に範囲を切り捨てたり、呼出側のデータの外まで拡張したりしません（[Microsoftの定数記述子の例](https://learn.microsoft.com/en-us/windows/win32/direct3d12/creating-descriptors)）。

RAW／構造体の読み取りはNON_PIXEL_SHADER_RESOURCE、書き込みはUNORDERED_ACCESS、定数はVERTEX_AND_CONSTANT_BUFFERへCOMMONから遷移します。書き込み後のUAV barrierを保ち、各dispatchの出口でCOMMONへ戻します。前のdispatchのUAV出力を、次の定数や構造体入力として同じキューで利用できます。新しいCPU待機やreadbackを挟みません。

プログラム、slotの配置と最小定数サイズ、バッファと記述子、命令の領域は提出側が完了まで保持します。提出直後の登録解除と呼出側の配列解放で実行は変わりません。GPUバッファ描画へ渡す場合も、既存の [描画の順序と寿命](R1_BUFFER_GEOMETRY.md) を使います。操作はゲームスレッドから行います。

## 検査

別リポジトリframework-testのRunComputeBufferLayoutsTests.ps1を使います。独立した実行ファイルのmapでImGui未リンクを確認し、SDKデバッグ層が利用できる場合は警告以上を失敗とします。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeBufferLayoutsTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunComputeBufferLayoutsTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

RAW／構造体入力とRAW／構造体出力の四組合せ、CS5.0／5.1、CPU定数／先行GPU更新の定数を使う計16経路で、uint／float／uintの64要素を独立したCPU式と全バイト比較します。提出直後にプログラム・入力・定数の番号を解除しても実行できることを確認します。半分だけ実行した後半の番兵、宣言不一致、配置の未知値、定数の不正整列・過大・宣言未満、構造体の割り切れない範囲、後半の誤りによる提出全体の拒否、counterの拒否も検査します。

b13、64KiB定数の最後の要素、2048バイト構造体の最後のfieldを実際に読み書きします。定数行列とStructuredBufferの位置から28バイト頂点を作り、RAWのindexと一緒にCPUへ戻さず描画し、完成画像を独立したCPU座標の描画と全画素比較します。compute完了後に所有先の登録を回収し、描画器の保持参照だけでも提出できることを確かめます。

通常アプリは前段のフレーム・解除・画素通知の検査シーンを再利用します。RAW検査とは別の起動指定で、定数／構造体／RAWを同じdispatchへ渡し、初回のindexなし、その後のindex付き、提出前の登録解除と別世代の再作成を六フレーム確認します。CPUバッファreadbackは呼ばず、既定GPU／WARP×ImGuiあり／なしで実行します。

Debug／Releaseは各4 projectが警告・エラー0でビルドでき、既定GPU／WARPはそれぞれ90条件・失敗0、SDKデバッグ層の警告・エラー0でした。通常アプリは両構成で各4起動・各6フレームが成功しました。移動・Cへの改名・描画PSOの分離後にも再実行し、RunAllChecks.ps1 -SkipBuild -Quickは26項目が成功しています。検査側の対応コミットは [8a9291180c4a9dd8c7250e6e374bc47e9ca64869](https://github.com/mynameisGaku/framework-test/commit/8a9291180c4a9dd8c7250e6e374bc47e9ca64869) です。出力と中間ファイルは検証用コピーのSaved/ComputeBufferLayoutsとSaved/ComputeBufferLayoutsContract/<構成>へ分離し、開発用のCL追跡ファイルを使いません。

## 残る範囲

今回はcomputeのバッファ配置です。graphics shaderの定数・材質の共通API、画像のcompute利用、部分範囲のview、型付きBuffer、counter、DXC／SM6、複数キュー、能力照会、一般のGPU変換メッシュやPBR、障害回復全体は残ります。行列をcomputeで適用する検査を、R2のシーン描画の完成とは扱いません。

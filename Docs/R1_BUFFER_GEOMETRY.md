# computeのGPUバッファを直接描く

2026-11-10（日本時間）。R1のバッファとcomputeを既存のゲーム描画へ接続しました。CPUへ頂点を読み戻さず、同じDIRECTキュー上のcompute出力を頂点／indexとして描きます。ImGuiの文脈・型・描画リストは使いません。R1全体と、永続メッシュを使うR2のシーン描画の完成ではありません。

## 窓口と責務

|型・配置|役割|
|---|---|
|Core/Render/FGeometryBufferDraw|型付きの頂点／indexバッファ番号、バイト位置、個数、indexの整数幅。GPUアドレスを公開しない|
|Core/Render/EGeometryIndexFormat|UInt16／UInt32の頂点番号|
|IRenderGeometry3D::DrawBufferGeometry|フレーム中の既存描画先でGPU上の共通頂点を読む。未対応の実装はfalse|
|FDX12BufferStore|バッファの作成・登録解除・転送・compute用参照と、描画用の実体参照の取得|
|FDX12GeometryRenderer|描画範囲の検査、GPU参照の保持、VB／IBへの遷移、既存の深度・画像・合成での記録|
|FDX12GameRenderer／FDX12GameCapture|通常画面と撮影先へ同じバッファ所有先とキューを渡す|
|FDX12Device／CDX12PlatformSubsystem|既存のGPU所有・提出・完了待ちと、共通サービスへの接続|

新しいSubsystem・別のGPUバッファ所有者・ImGui経由の実行先は追加しません。既存のIRenderSurface::GetGeometry3D()から利用します。ネイティブ描画器の初期化へ所有先とキューを省略した場合、従来のCPU頂点描画だけが使えます。所有先とキューは両方指定し、同じデバイスとキューを使います。借用する所有先は描画器より長く生存させます。

## 頂点と入力の契約

FGeometryVertexと同じ28バイト固定の配置です。Positionは射影後・透視除算前のfloat4（先頭0）、ColorはRGBA8（16）、U／Vはfloat2（20）。WをCPUで除算しません。バッファ上の有限な座標・UV、indexがVertexCount未満であることは呼出側が保証します。GPUの内容をCPUへ読んで検査する処理は入れません。固定配置はsizeofと各fieldのoffsetofをコンパイル時に確認します。

- VertexOffsetはバイト単位で4の倍数、VertexCountは1〜65536です。
- IndexBufferが無効値なら順番に頂点を描き、IndexOffset／IndexCountは0です。有効ならUInt16／UInt32で1〜196608個を読み、位置は整数幅の倍数です。
- 点・線・三角形の個数はそれぞれ1／2／3の倍数です。index使用時のVertexCountは番号が参照できる頂点数なので、四頂点・六indexの四角形も使えます。
- 範囲の加算による桁あふれを避け、資源サイズからの減算で全範囲を確認します。解除済み番号・別所有先・未知の整数幅や描画状態・不正な画像は記録前にfalseです。
- CreateBufferのShaderAccessはシェーダーでの用途です。None／Read／ReadWriteをいずれも頂点・indexの読み取りに使えます。computeで書く場合は従来通りReadWriteが必要です。
- Textureを指定すると既存の画像・頂点色の積を描きます。画像のviewは従来通りGPU完了まで所有側が生存させます。

一フレームで1024回、重複を除く256実体、合計256MiBまで保持します。部分範囲でも保持する資源全体を数え、同じ実体に頂点とindexが同居しても二重には数えません。CPU頂点の一時転送64MiBとは別の上限です。

## 順序と寿命

描画の入口でCOMMONから頂点読み取り／index読み取りへ移し、出口でCOMMONへ戻します。同じ実体をVBとIBで読む場合は二つの読み取り状態をまとめ、一回だけ遷移します（[Microsoftの資源状態の定義](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_resource_states)）。computeは従来通りUAVの書き込みを終えてCOMMONへ戻します。

DrawBufferGeometryは命令を記録するだけで、GPU完了待ち・CPU readback・再転送を行いません。バッファ番号、位置、個数、描画状態は呼出時に固定します。バッファ内容はGPU実行時の値です。順序は同じキューへの**実際の提出順**で決まり、CPUで描画を記録した後にcomputeを提出しても、主画面の描画提出より先ならそのcomputeが先に実行されます。未提出の描画とcomputeをCPU呼出順だけで交互に実行できる契約ではありません。別キューの共有には対応しません。

記録が成功した資源の実体参照は、登録解除と所有先の回収後も描画器が保持します。解除した番号による新しい描画は失敗します。EndFrameでは参照を残し、GPU完了後の次のBeginFrameで回収します。破棄もGPU完了後です。既存の通常フレーム開始時の完了待ちを使い、新しい待機主体は作りません。

```cpp
// Programsで作ったプログラムとBuffersの出力を、同じキューへ先に提出する。
Queue->SubmitCompute( Dispatches );
// フレーム中だけ有効な実行先。OutputはFGeometryVertexの固定配置を持つ。
FGeometryBufferDraw Draw;
Draw.VertexBuffer = Output;
Draw.VertexCount = 3;
Surface->GetGeometry3D()->DrawBufferGeometry( Draw, State );
```

## 検査

別リポジトリframework-dx12-selftestsのRunBufferGeometryTests.ps1で、Debug／Releaseそれぞれの独立した検査と共通サービス経由の起動を行います。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunBufferGeometryTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Debug -Platform
powershell -NoProfile -ExecutionPolicy Bypass -File Source\SelfTest\RunBufferGeometryTests.ps1 -FrameworkRoot (Get-Location).Path -Configuration Release -Platform
```

独立したGPU検査は、頂点だけ・別実体の16／32bit index・同居する16／32bit indexを画像なし／ありで描き、独立したCPU式から作った頂点描画と全画素を比較します。GPUを検査用の待ち合わせで止めたまま描画の記録が戻ること、先にcomputeと入力の登録を解除できること、描画提出前に出力の登録と所有先の参照を回収しても描けることを確認します。GPUバッファのバイト比較は描画記録が済んだ後の検証だけに使い、描画へ戻しません。

不正な範囲・配置・個数・状態・別所有先とキュー、1024回／256実体／256MiBの境界と次フレームでの回収も検査します。検査用kernelのコンパイルは自己検査側だけで行い、製品の作成窓口へはバイナリを渡します。独立した実行ファイルのmapでImGui未リンクも確認します。

通常アプリは共通窓口だけで色と移動量をcomputeへ渡し、CPUバッファreadbackを一度も呼ばずに六フレームを描きます。初回のindexなし、以降のindex付き、提出前の登録解除と別世代の再作成、完成画面の画素と先行compute完了を確認します。既定GPU／WARPとImGuiあり／なしの四起動です。

|検査|Debug|Release|
|---|---|---|
|ソリューション|4 project、警告・エラー0|同じ結果|
|独立したGPUバッファ描画|既定GPU／WARP各188条件、失敗0|同じ結果|
|SDKデバッグ層|両GPUで有効、警告・エラー0|同じ結果|
|共通サービスからの通常アプリ|既定GPU／WARP×UIあり／なしの4起動、各6フレーム成功|同じ結果|
|既存回帰|ECS・assets・world、転送・compute・窓・撮影が成功|同じ結果|

Tools/RunAllChecks.ps1 -SkipBuild -Quickは両構成合わせて20項目が成功しました。ECSは全ワーカー数0／1／4／15で7,867,833値、ハッシュ403b39415ba4b9a6。assetsはDebug 9,956件／Release 9,953件、worldは両構成3,635件で既存の省略条件を保っています。既存のUIなしゲーム描画は両GPUで各84条件、UI合成ありは各91条件が成功し、通常PNG・高解像度・被写体の透明撮影・GIFもUIあり／なしで通りました。sample／template全体、配布物のクリーンビルド、複数ベンダーと性能測定は今回の対象に含めていません。

GPU・ゲーム描画・共通Canvas・OS文字とUIの依存検査、依存検査の負例、1730ファイルの書式と対象C++のclang-format確認も成功しました。互換台帳は459項目（API315）で、新しい操作はImplementedです。Verifiedは0のままです。

既知のReleaseリンク用キャッシュの不整合を避けるため、検証用コピーの既存iobj／ipdbの六ファイルだけを退避し、同じ最適化設定でビルドしました。製品のソース・VendorModules・ビルド設定での回避は追加していません。

検査側の対応コミットは [56b0d3f4131e0c8e34653f57d45569d9fd745a7d](https://github.com/mynameisGaku/framework-test/commit/56b0d3f4131e0c8e34653f57d45569d9fd745a7d) です。出力は検証用コピーのSaved/BufferGeometryとSaved/BufferGeometryContract/<構成>へ分離します。一括検査のログは実行時の一時フォルダーに加え、Saved/BufferGeometry/all-checks.logにも集約しました。開発用のCL追跡ファイルは共有せず、元の作業コピーにあるOptional.hとproject.slnの未コミット変更も保持しています。

## 残る範囲

GPUバッファの直接描画は、固定の同次座標と画像・頂点色を扱う基礎です。ローカル座標の永続mesh、GPU変換、instancing、indirect draw、スキン・モーフ、PBR、定数／構造化バッファ・画像をcomputeで使う経路、DXC／SM6、複数キューと能力照会、包括的な障害回復・性能評価は引き続き残ります。R1／R2全体の完了や他バックエンドのGPUバッファ描画対応は宣言しません。

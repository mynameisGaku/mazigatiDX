# Surface材質の固定と並列記録

`CMaterial::CreateSurfaceSnapshot` は、ゲームスレッドで既存のSurface材質のシェーダーを準備し、場面とパラメーターの定数、画像の番号、開始・終了命令と、それらの資源の共有所有を `FMaterialSurfaceSnapshot` へ固定する。

Unlit、DefaultLit、Substrateは既存の `FMaterialProgram`、コンパイラ、`EngineContent/Shaders/Material` のHLSLを再利用する。DX12側へ材質の定義やSubstrateの計算を複製しない。新しいsubsystem・常駐スレッド・GPU資源Storeは追加しない。

```cpp
// ゲームスレッドで固定する。
const FMaterialSurfaceSnapshot MaterialFrame = Material->CreateSurfaceSnapshot( SceneState );
// 各ジョブは別の一覧へ、固定した値だけを使って記録する。
CSceneDrawList Batch( Camera );
const bool bAccepted = MaterialFrame.AddMesh( Batch, ESceneDrawClass::Translucent, Center, Vertices, NumVertices, Indices, NumIndices, World );
```

準備失敗、Surface以外、ゲームスレッド以外では未作成の値を返す。別スレッドで呼ばれた場合は、元の材質の可変状態やLastErrorへ触れる前に戻る。初期化の前にゲームスレッドが未登録の場合は、既存のIsInGameThreadの契約に従う。

## 公開後に参照するもの

- 固定後のメッシュ記録は、元のCMaterial、親、パラメーターコレクション、シェーダーの準備処理へアクセスしない。不変値のコピーを別々のジョブで読める。
- 元の材質でパラメーターや画像を差し替えたり、Reloadしたり、材質自体を解放したりしても、固定値は記録時の定数とシェーダー・画像を保持する。インスタンスの画像解決は既存のFindEffectiveTextureを使う。
- 環境マップと影を指定した場合も、実際に束縛する画像を保持する。通常の `CreateSurfaceSnapshot` は共有参照を保持し、同じ画像内の描画・フィルター・更新は再生時の内容に反映される。

## CPUから登録した画像の版を固定する

動的画像を `CDynamicTexture::Update` で管理している材質では、ゲームスレッドで `CMaterial::CreateSurfaceSnapshotWithCpuImageVersions( Scene )` を使える。直接束縛する動的テクスチャ（環境マップを含む）を、最後にCPUから正常に登録した画素の版へ差し替える。作成した版は確定値が所有する。元の画像に `Update` しても、元を解放しても、記録済みの版は変わらない。画像の版を作れなければ無効な材質スナップショットを返す。

CPU画素は動的画像ごとに一組保持する。版を初めて取得すると既存の画像登録を使って独立画像を作り、その版が生存中は複数の材質で共有する。通常の画像番号は `Update` で変わらない。GPU資源の解放は既存の所有者と完了管理が担当する。[MicrosoftのGPU資源の寿命管理](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)

描画先、影マップ、派生画像、GPUのフィルターやcomputeで書いた画素は、このCPU版固定の対象外。
- BuildMeshCommandsの結果は命令の所有コピー。資源の所有は固定値が保持するので、再生まで固定値も保持する。AddMeshを使えば、その所有もCSceneDrawListへ一緒に渡す。

BuildMeshCommandsは既存のcbObjectと88バイトのシェーダー頂点を使い、World・Tintと頂点・添字を所有する。非有限な行列、不正な頂点・添字、記録上限ではfalseを返し、出力を変更しない。記録時は既存の命令形式で頂点・添字をコピーする。DX12でのGPUバッファ共有は再生時に行う。

## GPUで書いた描画先の色を固定する

ゲームスレッドで描画先への書き込みを実行し、描画パスを閉じてから `CRenderTarget::CaptureGpuColorVersion()` を呼ぶ。描画命令を記録するだけの場面では、記録の再生後に呼ぶ。取得した `CTexture` は通常の画像として既存の材質へ渡せる。元の描画先を上書き・解放した後も、材質スナップショットがコピー先を保持する。

```cpp
const TSharedPtr<CTexture> ColorVersion = Target->CaptureGpuColorVersion();
if ( ColorVersion && Material->SetTexture( "Albedo", ColorVersion ) )
{
	const FMaterialSurfaceSnapshot FrameMaterial = Material->CreateSurfaceSnapshot( SceneState );
	// FrameMaterial を使って描画一覧を記録する。
}
```

DX12ではRGBA8、RGBA16F、対応GPUのR32Floatカラーを、同じ形式と寸法の独立した画像へGPUで複写する。コピーは既存の描画キューに順序付け、完了を待ってから返る。GPU上で色だけを複写する。描画パスや撮影中、マルチサンプル指定、未対応バックエンド、確保・コピー失敗時は空の参照を返す。素材側で同じ版を共有する場合は、取得した参照を使い回す。[MicrosoftのCopyTextureRegion](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-copytextureregion)

`RunRenderTargetTests.ps1 -Platform` で色を二度書き、元を解放した後に各版の色とアルファを確認する。HDRの1を超える値と負値、対応GPUの単一成分の値も検査する。

## 材質定義による分類

Surfaceの `.gxmaterial` で `"SurfaceBlend": "Opaque"`、`"Masked"`、`"Translucent"` を指定できる。省略または `"Manual"` は従来どおり呼び出し側が不透明・半透明を指定する。明示した分類は固定値に保存され、元の材質を再読み込みしても変わらない。`TryGetDrawClass` で確認できる。

分類を省略する `AddMesh( List, Center, Shape, World )` は固定材質の分類から描画順と状態を選ぶ。頂点・添字を直接渡す形式でも使える。明示指定と定義の分類が一致しない場合や、手動材質で分類を省略した場合は受付を拒否する。不透明と切り抜きは上書きと深度書き込み、半透明は通常アルファ合成と読み取り深度になる。直接 `BeginSurface` と `DrawSurfaceMesh` を使う経路では、呼び出し側が深度と合成を設定する。

`Masked` はSurfaceグラフの出力不透明度が `OpacityCutoff` を超えた画素だけ色と深度を書く。しきい値は0〜1、既定は0.5。残る画素の出力アルファは1。HLSLの直指定は対象外。影生成への適用は [Surfaceの影](MATERIAL.md) を参照する。参考 [Epicの材質プロパティ](https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-engine-material-properties)、[MicrosoftのHLSL clip](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-clip)。

同じ形状を複数の材質や個体で使う場合は、`FShaderMeshSnapshot::Create( Vertices, NumVertices, Indices, NumIndices )` で添字付き三角形を一度検査して固定し、その値を受け取る `AddMesh( List, Class, Center, Shape, World )` を使う。各項目の形状命令は1 wordで、頂点・添字は不変の領域を共有する。ジョブへ公開した後は形状を読み取り専用で扱い、各ジョブが別の一覧へ記録する。受付済みの一覧は形状と固定材質を保持するため、呼び出し元の値や元の配列は再生前に手放せる。

再生時は一回のExecuteが一つの形状を借り、既存のシェーダー描画命令へ解決する。Nullは参照と命令長を検査する。GPUバッファの再利用はDX12の既存キャッシュが担当し、再生時の頂点変換・照合は引き続き行う。[Epicのメッシュ描画パイプライン](https://dev.epicgames.com/documentation/en-us/unreal-engine/mesh-drawing-pipeline-in-unreal-engine)

`FShaderMeshSnapshot::Create( VertexView, Index32View )` は最大262,144頂点と786,432添字の三角形を受け取る。従来の16bit入口も、頂点番号の範囲内で一命令を超える形状に使える。作成時に元の三角形順を保って分割し、各部分が使う頂点を16bit添字へ置き直す。各部分は既存の命令上限内に収まり、一つの材質・深度・合成の状態で連続して実行する。項目のソートや所有の単位は形状全体で、分割境界で頂点を重複して保持する場合がある。後半に不正な頂点・添字があっても作成済みの部分は公開しない。頂点を直接埋め込む `BuildMeshCommands` と `AddMesh` の旧形式には従来の命令上限を適用する。

## 所有と命令の検査

CSceneDrawListに、命令と既存共有所有を一緒に受け付けるAddのoverloadを追加した。分類・命令・空き容量・所有参照数のどれかが不正なら、項目も参照も追加しない。固定材質のAddMeshはこの窓口を使う。

項目には、UseShader、ShaderConstants、ShaderTexture、PrimitiveShader3Dを追加で受け付ける。両シェーダーを項目内で明示し、定数の段・スロット・16バイト整合・正確な長さ、画像の段、頂点と添字の個数・正確な長さ・予約値、全浮動小数点成分の有限値、全添字の範囲を所有コピー前に検査する。定数の中身は任意の整数やビット列もあるため、floatとして解釈しない。

描画先の変更、ShaderOutputTarget、Clear、コピー、影生成、2DやShaderRectは引き続き拒否する。R3のパスや資源依存を暗黙に作る機能にはしない。[描画一覧の所有と合流](R2_SCENE_DRAW_SNAPSHOT.md)と[スレッドの契約](THREADING.md)も参照する。

## 検証

`Materials --material-snapshot=reference|sorted|reverse|mutated|live-mutated` が比較用の入口。既存コンパイラで三種類の陰影を作り、球を不透明・半透明で重ねる。固定後に元の色・画像を差し替え、Reloadし、場面の光も変更する。固定経路の画像が手動基準と一致し、固定しない変更後の画像は異なることを調べる。

`RunMaterialSurfaceSnapshotTests.ps1 -Platform` は、実GPU / WARP / UIなし / CPU変換指定 / DxLibで、ワーカー0 / 1 / 4 / 15も変えて比較する。ここでのCPU変換指定は既存Forwardの診断フラグであり、材質の陰影をCPUで評価する機能ではない。

共通のSceneDrawSnapshot契約には、可変長シェーダー命令の不正入力、20成分のNaN、整数定数の保持、所有付き受付の成功と拒否、ワーカーからの材質固定の拒否を追加した。RunSceneShaderValidationMutationTestsは、定数長・頂点列長・有限値・添字の検査をコピー上で一つずつ壊して検出を確認する。製品ソースは変更しない。

## DX12での形状の再利用

32頂点以上のシェーダー三角形では、初回はフレーム転送で描き、比較用に頂点・添字を所有コピーする。同じバイト列がもう一度現れると既存のGPUバッファ資源へ登録し、その後は材質や個体定数が異なっても同じバッファを使う。部分的なハッシュで候補を絞り、長さと全バイトを比較する。変更された形状や異なる添字範囲は別の項目となる。登録が失敗した場合や共有描画が拒否された場合は、その一回をフレーム転送で描く。

キャッシュはDX12シェーダーサービスが所有し、既存の資源所有先と同じ実行スレッドで操作する。比較用コピーは32 MiB・32件を上限とし、長く使われていない項目から解放する。GPUが使用中の資源は既存の完了管理が保持する。二度目の登録時は同期転送が発生する。再生時の比較は必要で、CPU時間の改善は未計測。従来経路との比較には `--no-shader-mesh-cache` を使う。参考 [MicrosoftのGPU資源の寿命管理](https://learn.microsoft.com/en-us/windows/win32/direct3d12/fence-based-resource-management)。

`RunShaderMeshCacheTests.ps1` は製品のシェーダーサービスを通し、使い回す登録番号、材質定数の再送、部分登録の取り消し、従来経路への復帰、容量と解放を確認する。材質画像の基準はキャッシュ無効で撮影する。

残件はより広いPBR材質の互換確認と、深度・描画先以外のGPU更新資源の版管理。描画グラフでの完了待ちなしのコピーは、`AddTextureCopyPass` で予約できる（コピー先は材質へ固定せず、後続のパスで読む）。DX12の既存モデル姿勢の捕捉は[描画一覧](R2_SCENE_DRAW_SNAPSHOT.md)を、パスの組み立ては [R3の描画グラフ](R3_RENDER_GRAPH.md) を参照する。

参照: [Epicの描画スレッドと描画用データの所有](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)、[HLSLの定数バッファ](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-constants)、[定数の配置規則](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx-graphics-hlsl-packing-rules)。

- **確認結果**: Debug / Releaseの契約93項目と、実GPU / WARP / UIなし / CPU変換指定 / DxLibの材質画像20項目が合格。固定経路は手動基準に対し各成分1を超える差のある画素0、固定しない変更後は12,628点の差を検出。ワーカー数0 / 1 / 4 / 15、元のパラメーター・画像の差し替えとReload、元の材質の解放を含む。長さ・頂点有限値・添字の4変異、所有・合流の6変異を、無変更の対照が合格する状態で検査の失敗として検出した。既存サンプル画像126項目も両構成で合格。最終ビルドは警告・エラー0、互換台帳525項目・役割54型・書式2023ファイルが整合した。全体検査の一括実行は行っていない。

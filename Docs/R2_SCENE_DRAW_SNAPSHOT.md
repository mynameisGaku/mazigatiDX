# 独立した描画項目の収集と確定

`CSceneDrawList` は、記録元ごとにカメラと3D描画項目を所有し、`Freeze` で不変の `FSceneDrawSnapshot` へ渡す。既存の `BeginScene3D` / `EndScene3D` が同じ状態の連続した命令だけを並べるのに対し、この経路は項目ごとに独立したSpanを保持する。異なる合成・深度・ライト・材質状態を持つ項目も、その状態ごと移動できる。

```cpp
CSceneDrawList Batch( Camera );
Batch.KeepAlive( Texture );
Batch.Add( Commands, ESceneDrawClass::Translucent, WorldCenter );
FSceneDrawSnapshot Snapshot = Batch.Freeze();
Snapshot.Replay( Executor );
```

追加・合流・所有参照の保持はboolで結果を返す。実際の呼出し側では結果を確認し、拒否された項目を描けた扱いにしない。

## 固定する値と並べ方

- カメラの値と命令を記録時にコピーする。項目の命令も所有コピーするので、元のバッファを空にしたり、元のカメラを変更したりしても確定値は変わらない。射影に使う画面の大きさは既存のカメラ命令どおり再生先で決まるため、画面サイズごとに射影行列を固定する仕組みとは区別する。
- 各項目は、カメラ開始、項目固有の状態と描画、カメラ終了を含む一つのSpan。既存Executeの状態保存・復元を使い、別の項目へ状態を漏らさない。項目内の命令順は変更しない。
- 不透明、切り抜き、半透明の順に並べる。不透明と切り抜きはそれぞれの受付順、半透明は固定したカメラの前方向へ中心点を射影した奥行きの大きい順。同深度は受付順を保つ。分類は呼出し側が明示し、合成や切り抜きの設定そのものを自動で変更しない。
- 一つの項目に複数の独立した3D描画を含められる。その項目全体を一つの中心点と分類で扱うので、異なる透明物は別項目として登録する。

固定長3D描画は [既存の明示的シーン順序](R2_SCENE_DRAW_ORDER.md) と同じ共有判定 `NSceneDrawOrder::IsIndependent` を使う。加えて、項目内で両シェーダーと定数・画像を明示したPrimitiveShader3Dにも対応する。可変長の長さ・値・添字は別の共通検査で確認する。状態命令は合成・色・World行列・深度・カリング・画像座標・フォグ・ライト・材質設定等に限る。World行列とライトのPush/Popは項目内で閉じる。

2D、カメラ変更、描画先変更、Clear、コピー、影生成、シーン範囲、アニメーション時刻の変更、Primitive3Dや帯など未対応の可変長描画は受け付けない。これらを含む場合はAddがfalseを返し、それ以前の記録は変更しない。既存Spanは引き続き同じ意味で使える。

## 合流と所有

各ジョブは別の `CSceneDrawList` と記録バッファを持つ。確定した結果を、所有者が決めた順で `Append` する。ジョブが終わった順をそのまま描画順にはしない。同じ深度の順序は、明示的な合流順に従う。カメラ命令が一致しない結果、未作成の結果、上限を越える結果は拒否し、合流元も合流先も変更しない。

上限は合計65,536項目、カメラを含めて4,194,304 word、所有参照4,096個。一覧は可変なのでコピーできず、移動で渡す。Freeze後はResetまで受付終了。Resetで次の記録を始めても、以前の確定値は変えない。不正なカメラでのResetは以前の記録を保持する。

`KeepAlive` は既存の `TSharedPtr` の制御部分を別名参照して保持する。独自のアセット管理やGPU資源Storeは作らない。確定値のコピーは不変データと所有参照を共有し、最後の参照で解放する。Replayは実行中にも参照を保持するので、実行先の処理が呼出し元の公開値を手放しても途中で命令を失わない。

KeepAliveは**資源の内容やモデルの姿勢をコピーする機能ではない**。画像・メッシュ・モデル等の内容や番号は、記録から再生完了まで変更しない。資源を使うすべての所有者をKeepAliveするか、既存の借用契約に従って別途保持する。アニメーションするモデルや更新可能なGPUバッファを、後のフレームから独立した版として固定する経路は残件。

確定値の別々のコピーは、別の実行先へ同時に再生できる。同じ可変一覧への追加、同じ公開値の代入・Resetと別スレッドからの照会は競合させない。所有者の破棄スレッドの契約も維持する。ゲームスレッドで破棄すべき資源の最後の参照を、ワーカーで手放してよいという意味ではない。実行先のスレッド・フレームの管理とGPU完了までの保持は、[既存の描画スレッド](THREADING.md)とGPU所有者が担当する。

Replayのtrueは有効な一覧をExecuteへ渡したことを表し、GPUの完了や全描画の成功を表さない。描画先のパスを開始・終了する場所は利用者が決め、通常の描画と同じ位置で再生する。任意のSpanの途中へ暗黙に挿入する仕組みにはしない。

## 検査

`RunSceneDrawSnapshotTests.ps1` は、命令とカメラのコピー、状態の分離、資源所有、確定後の再利用、ビュー不一致・不正入力・所有数上限の拒否、合流順、複数の記録元と確定値の同時再生を検査する。`-Platform` は手動で正しく並べた独立Spanを基準とし、通常順、逆順、元のカメラとバッファを変更した後の確定値を画像で比較する。

`Geometry3D --scene-snapshot=reference|sorted|reverse|mutated` で再現できる。記録は既存IJobSystem::ParallelForを使い、比較時のワーカー数を0・1・4・15へ変更する。対象は実GPU、WARP、UIなし、CPU変換、DxLib。画像検査の実行環境は一組だけコピーし、`-ImageOutputRoot` と `-SamplesDirectory` で保存先と実行ファイルの所在を指定できる。

`RunSceneDrawSnapshotMutationTests.ps1` は製品ソースを変更せず、検査用のコピーから命令のコピー、ビューのコピー、所有参照、ビュー不一致の拒否、深度順、同深度の合流順を一つずつ壊し、契約検査が失敗することを調べる。

FMaterialProgramと結ぶ共通PBRと更新資源の版の固定は引き続きR2の残件。パスの登録には [R3の描画グラフ](R3_RENDER_GRAPH.md) を使う。

設計時の参照: [Epicの描画スレッドと不変の描画用データ](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)、[並列化と提出順](https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine)、[材質の共通入力](https://dev.epicgames.com/documentation/en-us/unreal-engine/physically-based-materials-in-unreal-engine)。既存のジョブ・共有所有・実行先を再利用し、新しいsubsystemや常駐スレッドは追加しない。

- **最終確認**: Debug / ReleaseのライブラリとSamplesの最終ビルドは警告・エラー0。契約50項目、実GPU / WARP / UIなし / CPU変換 / DxLibの画像15項目が両構成で合格し、手動基準との差は0。ワーカー数は0 / 1 / 4 / 15で確認した。無変更の対照が合格し、6種類の所有・コピー・ビュー・順序の変異を検査の失敗としてすべて検出。従来のシーン順序50、Forward105、既存サンプル画像126項目も両構成で合格。互換台帳525項目、DX12の役割54型、ソース書式2014ファイルが整合した。全体検査の一括実行は行っていない。Releaseの出力と画像検査の一時領域は、容量を確保したD:/CodexVerification/framework-scene-snapshotへ分けた。

Surface材質は [固定材質の記録](R2_MATERIAL_SURFACE_SNAPSHOT.md) に対応した。UseShader・ShaderConstants・ShaderTextureと可変長のPrimitiveShader3Dを項目内で検査し、両シェーダーを明示した描画を受け付ける。描画先・消去・ShaderOutputTarget等は引き続き境界として拒否する。

`FShaderMeshSnapshot` を渡すSurface描画では、各項目が不変形状を共有所有する。上限と `GetNumWords()` は、カメラ・状態・1 wordの形状参照を含む項目の命令を数える。参照先で共有する頂点・添字は含めない。直接 `FDrawSpan` を渡す場合、添付する形状は1個までで、Executeの呼び出し中だけ借りる。C++のバイナリ配置が変わるため、既存の呼び出し側も合わせて再ビルドする。

## モデルの姿勢と配置を固定する

ゲームスレッドで個体のアニメーションと骨の上書きを更新してから `FModelPoseSnapshot::Capture( *Instance )` を呼ぶ。評価済みの頂点・添字・材質範囲、所有画像、個体の不透明度とメッシュの版を共有する。`AddModel( Pose, Class, Center, World )` は、その姿勢と呼び出し側が指定する配置・分類を一つの描画項目へ固定する。通常の照明は最後の引数で切り替えられる。未対応の実行先や無効な個体では捕捉に失敗する。

```cpp
const FModelPoseSnapshot Pose = FModelPoseSnapshot::Capture( *Instance );
CSceneDrawList Batch( Camera );
const bool bAdded = Batch.AddModel( Pose, ESceneDrawClass::Opaque, Center, World );
```

捕捉後は別々の一覧へ並列に記録できる。合流・確定済みの一覧が姿勢を保持するため、元の個体を更新・解放してから再生できる。DX12は既存のモデル描画へ固定値を渡し、再生時に個体の状態を読み直さない。GPUとCPUの変換経路は従来と同じものを使う。資源を発行したバックエンドを再生まで保持し、最後の参照の解放はゲームスレッドで行う。描画パスの構成は呼び出し側が選べる。[Epicの描画スレッドとデータ所有](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)

捕捉の対象は呼び出し時に個体へ反映済みの状態。まだ実行していないアニメーション命令や、一時的な描画命令の骨上書きは取り込まない。画像の画素・GPU資源の内容は版として固定しない。対応はDX12の所有付きモデルビューで、Nullは参照を検査できる。DxLibのネイティブモデルはこの入口では未対応。材質の切り抜きはSurface経路を使う。

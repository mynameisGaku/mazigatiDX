# 明示的な3Dシーンと描画順

`FDrawContext::BeginScene3D` / `EndScene3D` で、同じ `FDrawSpan` 内の並べ替え可能な範囲を宣言する。範囲外の描画順は従来どおり。シーンの入れ子、Spanをまたぐ開始と終了、影生成中の開始には対応しない。

`SceneDrawKey3D( ESceneDrawClass, float32 Depth )` は直後の独立した3D描画にだけ付く値で、描画先や材質の状態を変更しない。Depthは同じビューで比較できる有限な奥行きとし、大きいほど奥とする。視点からの距離やカメラ方向への射影など、呼出し側が一つの規約を選ぶ。ここではカメラやGPU資源を参照してキーを推測しない。

```cpp
Draw.PushCamera( Camera );
Draw.Depth( FDepthState::ReadOnly() );
Draw.Blend( EDrawBlend::Alpha );
Draw.BeginScene3D();
Draw.SceneDrawKey3D( ESceneDrawClass::Translucent, FarDepth );
Draw.Quad3D( FarQuad );
Draw.SceneDrawKey3D( ESceneDrawClass::Translucent, NearDepth );
Draw.Quad3D( NearQuad );
Draw.EndScene3D();
Draw.PopCamera();
```

同じ状態の連続した項目を、不透明、切り抜き、半透明の順へ並べる。不透明と切り抜きは各分類の記録順を維持し、半透明は奥から手前へ並べる。同じ深度では元の記録順を保つ。分類は順序だけを指定するので、合成・深度の書き込み・アルファテストは利用者が既存APIで設定する。透明物の深度書き込みを自動で有効にしたり、切り抜きのしきい値を決めたりはしない。

## 境界と対応範囲

- カメラ・描画先・ライト・材質・合成・深度・World行列などの状態変更、Clear、2D描画、キーのない描画はすべて境界。並べ替えはその境界を越えない。キーと描画の間にこれらの命令を置くと、そのキーは失効する。
- 現在の対象は、固定長のTriangle3D、Sphere3D、Capsule3D、Cube3D、Cone3D、Quad3D、Mesh3D、およびアニメーション時刻を書き換えないModel3D。輪郭も独立した描画として扱う。モデルの個体状態は既存の借用契約に従い、実行中に変更しない。
- アニメーション時刻を指定したModel3D、ModelAnim、骨の上書き、材質効果、独自シェーダー、可変長の頂点列や帯は境界。姿勢の変更が後続の同じモデルに作用しうるため、単純な描画キーだけでは動かさない。
- キーは命令とともに移動する。キーや境界はバックエンドにとって描画操作ではなく、DX12 / DxLibでは読み飛ばし、Nullでは命令数と範囲の検査に含める。既存の命令番号と形式は変更せず、3命令を末尾に追加した。

入れ子、閉じ忘れ、余分な終了、シーン外のキー、不正な分類・非有限の奥行き・予約フラグ・長さが見つかると、Span全体の並べ替えを行わず元の順序で実行する。不正な描画命令そのものの扱いは既存実行先の検査に従う。記録APIは不正な分類・奥行きを受け付けずfalseを返す。並べ替え用の複製はSpanあたり4,194,304 word、キーは65,536個までとし、超過時も元の順序を保つ。

## 所有とスレッド

順序の実装は `Framework/Render/Scene/SceneDrawOrder` に置き、GPUやImGuiの型・大域状態・subsystemを持たない。`NSceneDrawOrder::Prepare` の入力は不変の借用値で、出力配列は呼出しごとに所有する。別の入力や読み取り専用の同じ入力を、別の出力配列へ同時に処理できる。出力配列を入力の所有領域と兼用してはいけない。

各実行先はExecuteのローカル領域へ並べ、既存の状態保存・復元を含む一回のExecuteとして再生する。項目ごとにExecuteを再帰呼出しして状態を失う実装にはしない。DX12の既存描画スレッドへ渡す際のコピー・同期とGPU完了までの資源保持は、既存の所有者が継続して担当する。記録用のFDrawContext / FDrawBufferは各ジョブが別々に持ち、共有バッファへの書き込みを並行させない。[スレッドの契約](THREADING.md)も参照する。

## 検証と残件

`RunSceneDrawOrderTests.ps1` は、分類順、同じ深度の安定順、状態・2D・未分類の境界、入力保持、不正範囲と上限、モデル姿勢の境界、Nullの診断、4スレッドからの同時実行を検査する。`-Platform` はGeometry3Dの固定シーンを使い、手動の正しい記録順、通常順と逆順のシーン、および意図的な誤順を画像で比較する。対象はDX12の実GPU / WARP / UIなし / CPU変換、およびDxLib。画像は検査先へ保存する。

`Geometry3D --scene-order=reference|sorted|reverse|unsorted` で再現できる。unsortedが基準と異なることも検査し、空画像同士の一致で成功させない。

これはR2の明示的な順序制御。モデル・ビュー・材質・ライトを一括してフレームへ固定するシーン収集、材質からの自動分類、異なる状態をまたぐ収集、透明な面どうしの交差の解決、共通PBRは残る。R3のフレームグラフや `GraphBuilder.AddPass` は未実装。

設計時の参照: [Epicの透明描画](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-transparency-in-unreal-engine-materials)、[透明の並べ替え方針](https://dev.epicgames.com/documentation/unreal-engine/rendering-settings-in-the-unreal-engine-project-settings)、[描画スレッドの所有](https://dev.epicgames.com/documentation/unreal-engine/threaded-rendering-in-unreal-engine)。既存Spanの状態と順序の契約を優先し、Unreal EngineのAPIと完全互換にはしない。

検証結果: Debug / Releaseとも契約50項目、実GPU / WARP / UIなし / CPU変換 / DxLibの画像比較15項目が合格。通常順・逆順は各実行先の手動基準との差0、意図的な誤順は15,625点が異なる。`RunSceneDrawOrderMutationTests.ps1` では、深度順・分類順・同深度の順序の反転、境界越え、閉じ忘れの無視、姿勢変更モデルの移動の6変異を、コンパイルエラーではなく検査の失敗としてすべて検出した。対照の無変更ソースも合格した。

検査の実行環境は一組だけコピーして全比較で使い回す。`-ImageOutputRoot` で画像検査先、`-SamplesDirectory` で別出力先にビルドしたSamplesの所在を指定できる。今回のRelease通常リンクはCドライブの容量不足でLNK1180となり、既存の完全リンク指定を維持したままDドライブへの出力で成功した。プロジェクトの恒久設定は変更していない。

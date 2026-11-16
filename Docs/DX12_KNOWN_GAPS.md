# DX12 の未対応・未確認の事項

DX12（`--backend=dx12`）で、まだ動かないもの・動くが確認できていないものの一覧です。作業のたびに更新します（最終更新: 2026-11-16）。
項目ごとの細かい状態は [BACKEND_COMPATIBILITY.md](BACKEND_COMPATIBILITY.md)（台帳 `BackendCompatibility.csv`）にあります。ここは「次に何をするか」を見つけやすくするための要約です。
経緯と詳細は [DX12_HANDOFF.md](DX12_HANDOFF.md)。

DX12 で動いているかどうかを、コードで分けたいときは `NBackendInfo`（`Library/Framework/Engine/BackendInfo.h`）、プリプロセッサで分けたいときは `Library/Core/Types/BackendMacros.h` のマクロを使います。

## 動かない（未対応）

| 領域 | 内容 | 影響 | 備考 |
|---|---|---|---|
| アニメーションつきモデル（残り） | 実素材での描画の確認（アニメーションつき FBX を DX12 で描いて、DxLib と比べる）。`ModelAnim` / `ModelAnimFrame` / `Model3D` のアニメーション時間 / `ModelFramePose3D`（描くときだけ骨を上書き）は、実行側（`CGeometryCommandExecutor`）まで接続して、合成した骨組みで、命令から描かれた頂点までを検査してある（GPU は通していない）。`MV1` ファイルの読み込みは、DX12 では FBX / OBJ だけ | アニメーションつきの FBX は、読み込める（CPU でスキニング）。見た目は、実素材で未確認 | サンプルには、アニメーションつきの素材が無い。割合の混ぜ方（付けたアニメーションごとの割合の重み。足りないぶんは元の姿へ）は、DxLib の `MV1SetAttachAnimBlendRate` の挙動を、フレームワークの割合の計算（`NAnimationBlend`。骨ごとに合計 1）に合わせて仮定したもの。DxLib のモデルで比べて確かめること |
| 材質効果の 3D 描画 | `MaterialQuad3D` / `DistortedMaterialQuad3D` / `MaterialRibbon3D` / `DistortedMaterialRibbon3D` / `MaterialMesh3D` / `ModelMaterial3D`、`IDrawExecutor::SupportsEffect*` / `SupportsModelMaterialEffects`（すべて false） | 材質を使うエフェクトのリボン・メッシュ、モデルへの材質効果は出ない（その命令だけ飛ばす）。パーティクルの材質（`ShaderQuad3D` 経由）は出る | どのサンプルも使わない |
| モデルのメッシュデータ・モーフ | `GetModelMeshData`（現在の姿勢の頂点データ）/ モーフ（`ShapeRate`）/ `SaveModelToMV1` / 材質の環境光・鏡面・自己発光の描画への反映（拡散色だけ反映） | 失敗を返す（材質の色は、拡散色だけ描画に出る） | 当たり判定（`RaycastModel` / `OverlapModelSphere` / `PrepareModelCollision`）と材質色の上書きは、ある（DxLib との辺・頂点ぎりぎりの比較は未確認） |
| 合成モード | 画像・文字・3D の `Invert`、全部の `MultiplyAlpha` / `PmaInvert`（平らな図形の `Invert` だけ対応）。通常の合成で描き、初回に報告する | 色が違う（形は出る） | どのサンプルも使わない |
| シャドウマップの確認表示 | `PreviewShadowMap` | 出ない | |
| 排他的フルスクリーン | `EWindowMode::Fullscreen` | `Borderless` で代替を報告 | |
| 画像の mip | 縮小の段が無いので、異方性（`Anisotropy`）は状態だけで、見た目は線形補間と同じ | 斜めから見た遠くの地面の鮮明さ | |
| `IPlatform` 以外の保存 | `SaveModelToMV1`（MV1 の保存）| 失敗を返す | MV1 の読み込み自体も DX12 は FBX / OBJ だけ |

## 動くが、確認できていない

| 領域 | 未確認の内容 |
|---|---|
| 音 | 耳での聞き比べ（パンの法則・ピッチの音質）。ogg は読めない（OS のデコーダーに無い）。音声ファイルの実物（サンプルの素材に無い）の読み込みは WAV の検査まで |
| 動画 | 音声と映像の同期のずれ（時計は経過時間で進める）。1080p など大きな映像の重さ（主スレッドで読む） |
| 文字入力（IME） | Microsoft IME で、変換中の見た目・候補の位置。モダンな IME が `ISC_SHOWUI*` を守るか。マウスで入力欄を操作する経路（DX12 の入力は OS の実カーソルを読むため、自動操作できない） |
| タッチ | タッチ機器での `WM_TOUCH` の読み取り（指の取り込みの部分だけ検査） |
| ディスプレイ・窓 | 拡大率の違う複数ディスプレイの間の移動 |
| ビデオメモリ | 複数 GPU の構成 |
| 文字 | 字形の細かい差（ストロークの太さ・字間は、DxLib と数%以内） |

## 進行中

- なし（アニメーションつきモデルの骨組みは `Library/Platform/Render/Model/ModelRig.*` と `StaticModelStore.*` に入った。残りは上の表）。

## 作業の取り決め

- **push は、ChatGPT（Codex）の `framework-dx12` の作業が行う**（この作業では push しない）。`Dx12` ブランチの本体と、検査用リポジトリ（`Source/SelfTest` の接続先）の 2 つ。

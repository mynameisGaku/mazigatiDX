# バックエンド互換性の初期調査

[ロードマップ](ROADMAP.md)のR0で確認した、現在の実装・互換上の不足・次に検証する契約です。これは実装済み機能の拡大や、R0全体の完了を宣言する文書ではありません。機能ごとの判定は[互換表](BACKEND_COMPATIBILITY.md)、編集する台帳は[BackendCompatibility.csv](BackendCompatibility.csv)です。

## 台帳と判定

初期の抽出対象は、Core/Interfacesの非破棄virtual関数298件、EDrawCommandの100件、登録サンプル33件、Toolsメニューの入口11件の計442件です。APIの継承は重複させず、CPU側の共通実装とcallback契約、DX12追加時の内部描画契約も含めます。Frameworkの公開ヘッダーの補助関数や、各編集画面の全操作まで自動抽出した数ではありません。これらは文書と呼出側を追加で照合し、各項目の検査計画へ展開します。

台帳は実装状況と互換検査を区別します。現在はUnreviewed 45件、Missing 177件、Partial 69件、Implemented 151件、Verified 0件です。Implementedは処理または共通経路の存在をソースで確認した判定であり、Windows実機での互換を意味しません。Unreviewedの大半はサンプル・Editorの操作確認、残りはAIアニメーション制御の接続確認です。個々の機能が等しい重さではないため、件数の割合を開発の進捗率には使いません。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\UpdateBackendCompatibility.ps1
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\UpdateBackendCompatibility.ps1 -Check
```

通常実行は、追加された項目をUnreviewedとして登録し、既存の計画と注記を保って文書を生成します。関数宣言または命令の並びが変わった項目はUnreviewedへ戻し、過去のEvidenceを外します。削除・改名・重複は明示的な確認まで失敗にします。`-Check`はファイルを変更せず、項目の欠落・削除・宣言の変更・重複・不正な判定・証拠のないVerified・古い文書を検出します。C++の完全な構文解析・型別名の展開・ABI検査ではありません。

既存RunAllChecks.ps1へこの確認を加えます。別置きの検査では、コメントと文字列内の偽の宣言を数えないこと、追加・削除・重複、宣言変更、判定と証拠、文書の古さ、注記の保持、BOM／CRLF、確認と拒否時の非変更を検査します。Windows PowerShell上での既存検査一式の実行は別途必要です。

## MV1とモデルの互換

[ModelExporter](../Library/Framework/Asset/Model/Import/ModelExporter.cpp)は、取り込んだFImportedSceneを.xへ書き、IModelBackendのLoadModelとSaveModelToMV1へ渡す経路を持ちます。MV1の独立した読み書き実装ではありません。[DxLibの保存実装](../Library/Platform/DxLib/Asset/DxLibAssetBackendSubsystem.cpp)はMV1SaveModelToMV1Fileを呼びます。現在のDX12は、静的FBX／OBJを直接読めますが、MV1読み込みと保存には対応しません。

既存の読み取り窓口を、完全なMV1変換器として流用するだけでは不足します。

- [FModelMeshData](../Library/Framework/Asset/Model/ModelMeshTypes.h)の頂点にはDominantFrameがありますが、全ボーンのウェイト・逆バインド行列はありません。[DxLibのメッシュ取得](../Library/Platform/DxLib/Asset/DxLibAssetBackendModelMesh.cpp)も、参照メッシュから最も強いフレームだけを写します。これからスキン全体を復元できるとは扱いません。
- [静的面の取得](../Library/Platform/DxLib/Asset/DxLibAssetBackendModelSurface.cpp)は、アニメーション・shape・非剛体のtriangle listを拒否します。静的な変換・描画には使えますが、変形モデルの変換経路にはなりません。
- [FImportedScene](../Library/Framework/Asset/Model/Import/ImportedScene.h)は階層・スキンclusterとウェイト・アニメーションtrackを保持します。一方、モーフ・頂点キャッシュは検出するflagが中心で、変形データをすべて保持する契約はまだありません。中間形式の不足も確認してから、変換経路を作る必要があります。

次の実証では、静的・複数ボーンのスキン・アニメーション混合・モーフ・材質画像を持つ既存MV1を用意し、元FBX／OBJなしの取り込み、描画用データ、既存窓口の読み取り、保存と再読込を比較します。独立した読込／保存器か、DxLib形式を扱う変換サービスを選ぶのは、その実証と配布条件の確認後です。描画バックエンド間でハンドルを渡したり、変換のために別の描画実装へ無断で切り替えたりしません。

この項目は未解決です。静的面だけの抽出や、元FBX／OBJを同梱する方法で、完全互換を達成したとはしません。

## 既存シェーダーと新しいGPU経路

[CShader](../Library/Framework/Asset/Gpu/Shader.h)の既定はShader Model 4.0で、既存の[DxLibコンパイル実装](../Library/Platform/DxLib/Asset/DxLibAssetBackendSubsystem.cpp)は4.0／4.1／5.0を受け付けます。EShaderStageはVertexとPixelの二つです。[既存マテリアルのHLSL生成](../Library/Framework/Material/MaterialHlsl.h)はDxLib向けの頂点形式と定数配置を持ちます。新しいDX12のシェーダーを追加するときに、これらを一律にSM6へ読み替えることはできません。

初期検証では、既存ShadersサンプルのObjectVertexとToonPixelをそのまま取り出し、次を確認しました。

|試験|結果|限界|
|---|---|---|
|SM4.0／4.1／5.0の頂点・ピクセルHLSLをコンパイル|両段とも成功。reflectionでb4／b5と頂点入力を確認|このサンプルの確認。全独自シェーダーや全bindingを証明しない|
|既存の入力semanticと定数b4／b5を使うDX12 PSO作成|三つのprofileで成功|Wine／vkd3d上。Windows実機とSDKデバッグ層での受け入れ・描画は未確認|
|Wine側コンパイラーでCS5.0のRWStructuredBuffer書込|未実装の診断で失敗|コンパイラーの制限。製品のDX12実装の不具合やGPUの非対応とは判定しない|
|公式SDKコンパイラーで同じCS5.0をコンパイルし、DX12 PSO作成|成功|SM6／DXIL・DXCの動作確認ではない|
|CS5.0をDispatchし、UAV→CopySourceへ遷移してreadback|64要素すべてが期待した1〜64。fence完了後にCPUで確認|Wine／vkd3d／lavapipe上の実行。性能評価やWindows実機の代わりにはしない|

検査用コンパイラーは既存の対象SDKと同系統の10.0.26100を使い、製品やリポジトリへ同梱しません。この環境のShader Model照会結果は5.1で、SM6.0の必須能力を検証できる環境ではありません。新しい経路のDXC／SM6.0という基準案は、ロードマップどおりWindows実機・WARPで確定する必要があります。

この結果から、既存用のコンパイル／binding互換と、新しい描画・compute用のシェーダー生成を分けて検証する方針にします。DX12側のroot signature・descriptorへの変換はPlatformに閉じ、既存のb4／b5等をゲームや材質へ書き換えさせません。FMaterialProgramは再利用し、出力先ごとの入口・binding・標準頂点の違いを吸収します。

コンパイル済みデータの受け入れでは、profileとstage、入力配置、必要なbindingを調べ、不正・途中で切れたデータ・異なるstageを拒否する契約が必要です。ソースの再コンパイルが成功しただけでは、既存バイナリ互換を完了としません。Compute等の段を増やす場合は、Vertex=0／Pixel=1の意味を保ち、既存の「Pixel以外はVertex」とする実装も同時に見直します。

## 音声・PCM・動画と画像の接続

[DxLib音声](../Library/Platform/DxLib/Audio/DxLibAudioBackendSubsystem.cpp)・[PCM](../Library/Platform/DxLib/Audio/DxLibPcmStreamBackendSubsystem.cpp)・[動画](../Library/Platform/DxLib/Video/DxLibMovieBackendSubsystem.cpp)は、DxLibが選ばれたときだけ作られます。Null実装もDX12向けには作られません。DX12のアセット実装の音声関数は未対応の戻り値であり、現在は独立した実音声・動画サービスがありません。

既存のIAssetBackendは役割別に分かれていますが、音声データの読み込みと、IAudioBackendへ渡すvoiceの番号の所有者は一致させる必要があります。別の実装の整数ハンドルを流用せず、デコード済み資源・voice・PCMキューの所有を分け、その接続を必要最小限の窓口で表します。コーデックの対応表、複製と同時再生、pitch・pan・loop・seek、非同期読み込み、終了とデバイス切断を検証します。出力実装の選定を、グラフィックスAPIの選択へ埋め込みません。

DxLibのGetMovieImageは動画の番号をそのまま画像番号として使います。DX12では、デコーダーの番号を画像として描けません。IMovieBackendから返す画像をITextureBackendの所有・更新経路へ接続し、動画を閉じてもGPUが参照中の画像を早く破棄しない契約が必要です。画像サイズ変更・音声との時刻同期・seek後の古いframe・終端・複数動画も比較します。デコーダーと音声出力の選定・対応形式の実証は未完了です。

[SDLゲームパッド](../Library/Platform/SDL/SdlGamepadInputSubsystem.cpp)は非Nullで選択され、DX12でも共通経路を使えます。キーボード・マウスはWindows実装へ接続済みです。文字入力／IME、clipboard、cursor操作、touch、window controlとdisplay情報は、各窓口への接続が残っています。ImGuiへの文字入力があることを、ゲーム側のIME対応とは数えません。

## R1へ持ち込む最小の責務

|役割|必要な契約|R0で残っている確認|
|---|---|---|
|能力の照会|shaderの段と形式、compute、SRV／UAV、MRT、深度参照、indirect、timestamp、資源とbindingの上限|Windows実機・WARPでの最低能力、DXCの配布と診断、形式ごとの読み書き能力|
|資源の作成・所有|用途と寸法、型付きhandle、更新・copy・readback、作成失敗と解放|従来の資源handleとの接続、無効・古いhandle、画像と動画の寿命|
|命令の記録|graphics／computeのbindingと実行、資源の読み書き、順序と遷移|既存Spanとの境界、将来のフレームグラフからの接続、使用中資源の禁止操作|
|提出と完了|フレーム別の記録・転送領域、完了番号、待機、GPU使用中の資源と記述子の遅延解放|終了・resize・GPU失敗、待機後の再利用、通常frameごとの全停止を避ける構成|

インターフェースはCore/Interfaces/Render、APIに依存しない値はCore/Renderに置きます。作成・所有・借用、記録・提出・照会を一つの巨大な窓口へまとめません。D3D12の列挙やGPUアドレスをそのまま公開値にしません。新しい段・資源用途・handleは、既存の意味を変えずに追加する方法を選びます。

最初のGPU検証経路は、既存サンプルの互換確認とは別に、共通契約を通した「buffer作成→compute書込→資源遷移→描画／copy→完了待ち→readback」とします。今回の直接DX12呼び出しによる小さな実証は、そのための実行環境を確認したもので、共通GPU基盤が実装できたことにはしません。

## R0の残り

- MV1の完全な入出力、アニメーション・全ウェイト・モーフを保持する中間データと、独立した実装／変換経路の実証。
- 既存シェーダーのバイナリ・定数配置・頂点形式と、新しいDXC出力のWindows実機／WARP確認。
- 音声／動画の形式・所有・同期の実証と、独立した出力／デコーダー実装の選定。
- Frameworkの文書化された補助API、サンプルの操作、Editor内の操作を、台帳の検査計画へ展開すること。
- 最低GPU能力と、性能測定に使うGPU・CPU・解像度・参照シーンの確定。今回のsoftware GPUの結果から、実機の性能予算は決めないこと。

R0を進める間も、確認前の機能をVerifiedへ進めず、未解決項目をロードマップの必須範囲から外しません。

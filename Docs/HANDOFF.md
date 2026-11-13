# DX12バックエンド開発の引き継ぎ

最終整理: 2026-11-09（日本時間）。作業ブランチは `backend-dx12`、取り込み元devの基点は `c1d2143262d246c73bffd55c60ef2d84ac4773ba`。実装の直近コミットは `3344e8f` です。最新版のHEADはGitHubのbackend-dx12または `git rev-parse HEAD` で確認してください。別途作成したオフライン配布物では、同梱の `DELIVERY.md` に配布時のHEADを記録します。

## Phase B `RGBA16F`と画像コピー・readback（2026-11-13 追記）

`IRenderTextureResources`へ`CreateComputeTextureRGBA16F`・`CreateRenderTargetRGBA16F`・`CopyTexture`・`ReadTextureRGBA8`・`ReadTextureRGBA16F`を追加し、RGBA16Fのcompute/描画先、RGBA16F+RGBA8混合MRT、主画面の未提出コマンド提出（`SubmitPendingMainCommands`）を伴う同形式同寸法の画像コピーとCPU readback、撮影/パス/未閉鎖timestamp時の拒否と出力非破壊を`CDX12PlatformSubsystem`・`CDX12TextureStore`等で接続しました。検証は`Source/SelfTest/RunHdrTextureCopyTests.ps1`で行います。互換台帳は486項目（`UpdateBackendCompatibility.ps1 -Check`通過）です。

## Windows での再開記録（2026-11-09 追記）

Windows の全体ビルドで見つかった画面消去の定義漏れと警告を修正しました。Debug／Release の4 project と、DX12／WARP／DxLib／Null の起動確認は [Windows 検証記録](DX12_WINDOWS_VALIDATION.md) を参照してください。自己検査の対応版と残る制限も同記録へ分けています。以下の未確認一覧は引き継ぎ時点の記録です。

## バッファ資源の作業（2026-11-09 追記）

R1の最小のバッファ所有・同期転送・readbackを追加しました。窓口、失敗時と回収の契約、自己検査の対応版、まだ未接続のcompute等は [バッファ資源の記録](R1_BUFFER_RESOURCES.md) を参照してください。互換台帳は450件で、Verifiedは増やしていません。

## compute提出の作業（2026-11-10 追記）

共通窓口からRAWバッファのcompute提出・完了照会・期限付き待機・readbackを接続しました。CS5.0／5.1、提出後の番号解除とGPU参照の保持、CPU参照／既定GPU／WARPとDebug／Releaseの検査は [computeの記録](R1_COMPUTE.md) を参照してください。互換台帳は455項目、Verifiedは0です。描画との資源共有、DXC／SM6、障害時の終了を含むR1全体は未完了です。

## ImGuiの複数ウィンドウ（2026-11-10 追記）

DX12でImGuiのウィンドウを主画面から切り離せます。公式のWin32／DX12実装を接続し、DxLibの更新順・頻度設定を共通化しました。無効起動からの有効化と、実行中の繰り返し切り替えにも対応しています。座標補正、GPU待機・終了保持の修正、Windows実GPU／WARPの窓と画素の検査は [マルチビューポートの記録](DX12_IMGUI_VIEWPORTS.md) を参照してください。複数DPI・複数ベンダーや外部窓を含むGPU障害回復全体は残っています。

## 撮影と責務の分離（2026-11-10 追記）

DX12の画素読み戻しを既存の撮影サービスへ接続し、通常PNG・GIF、高解像度の描き直しと被写体だけの透明撮影を追加しました。GPU撮影先のFDX12CaptureTarget、共通ImGui記録のFImGuiCaptureDrawList、接続層のFDX12ImGuiCaptureRendererへ分けています。UIなしのGPU検査、依存の拒否、実GPU／WARPの画素と保存物の結果は [撮影の記録](DX12_CAPTURE.md) を参照してください。互換台帳は457項目、Verifiedは0です。

## GPU所有者とImGui利用者（2026-11-10 追記）

FDX12DeviceからImGuiの型・初期化条件・所有・描画呼出しを外しました。GPUの所有とフレーム提出はプラットフォーム側に残し、FDX12ImGuiFrameRendererはGPUを借りてUIの記録だけを行います。UIなしの通常表示・画素読み戻し、UI解除後のゲーム資源保持と再接続は [デバイスの分離](DX12_DEVICE.md) を参照してください。既存ゲーム2D／基本3DのImGui依存は互換の接続側に残り、独立したゲーム描画経路の完成とは扱いません。

## ゲーム描画と文字の独立（2026-11-10 追記）

通常画面と撮影のゲーム2D・基本3D・文字・画像をImGuiの描画リストから外しました。FGeometryCanvas2Dで共通頂点を作り、FDX12GeometryRendererが直接記録します。WindowsTextRasterizerはCPU文字だけ、ImGuiは後段のUIだけを担当します。旧UI経由の3D・撮影接続と撮影描画リストを削除しました。--no-imguiでは文脈そのものを作らず、通常描画・PNG・高解像度・透明撮影・GIFが使えます。検査と上限・既定文字の差は [ゲーム描画の分離](DX12_GAME_RENDER.md) を参照してください。互換台帳は458項目、Verifiedは0です。

## 最初に読むもの

1. [ROADMAP.md](ROADMAP.md): v1.0.0の必須範囲、R0〜R10の順序と完了条件。
2. [BACKEND_AUDIT.md](BACKEND_AUDIT.md): 互換性の調査結果、GPU基盤・形式・非描画サービスの不足。
3. [DEVELOPMENT.md](DEVELOPMENT.md): 実装上の契約、対応範囲、画像資源の寿命とコーディング規約。
4. [BACKEND_COMPATIBILITY.md](BACKEND_COMPATIBILITY.md): 生成された対応一覧。編集元は [BackendCompatibility.csv](BackendCompatibility.csv)。

## 確定した要件

- v1.0.0ではDX12でForward／Deferred／Forward+／Deferred+を完成させる。Vulkan／OpenGLの実装は次版以降。API追加に使い回せるデータと責務を今から整える。
- このプロジェクトが公開する既存DxLib機能は、描画・音声・動画・入力・Editor等を含め、原則DX12構成でも対応させる。DxLibそのものの全関数を公開する要件ではない。
- **例外: DX12でのMV1形式の読み込み・保存互換はv1.0.0の必須範囲から除外済み。** MV1しかない資産への対応も必須ではない。スキン・モーフ・アニメーション混合・骨の上書きは引き続き対象。既存DxLib側のMV1機能は維持する。MV1変換器の追加調査を再開しない。
- 高品質描画としてPBR・IBL・影・HDR内部描画・TAA等を計画済み。詳細と次版へ回す範囲はロードマップを正本とする。
- 静的meshlet生成、computeでのfrustum／cone／Hi-Zカリング、通常のindex付きindirect drawをv1.0.0に含む。Mesh Shaderは次版以降の能力別経路であり、meshletの必須条件にしない。
- 新しいインターフェースは `Library/Core/Interfaces` に置く。既存の窓口を優先し、今必要な分だけを追加する。内部クラスを分けるたびにインターフェースを増やさない。
- GPU資源の所有、デコード、アセット登録、UI描画、命令実行を別の責務として保つ。巨大なDevice／Subsystemへ機能を集めない。
- コミットメッセージはプロジェクトの変更内容だけを記す。作業依頼者の情報や外部記事の情報を入れない。

## 現在の到達点

DX12の初期描画経路を実装した段階です。Forward／Deferred／Forward+／Deferred+の完成実装はまだありません。R0は進行中で、R1は画像資源の責務分離など一部だけを先行実装しています。R2〜R10も未完了です。

|領域|実装したもの|残る制限|
|---|---|---|
|起動・窓|DX12選択、窓・swapchain・フレーム・同期、終了とresizeの基本経路|Windows実機・全体ビルドの検証が残る|
|画像|WICによるCPUデコード、RGBA8作成・同寸法更新、共通のアセット登録、DX12での転送・遅延解放|同期アップロード。非同期指定もその場で完了。mipmap等は未実装|
|2D|基本図形・文字・画像・反転・クリップ・Opaque／Alphaの初期対応|形状生成と描画順の接続には引き続きImGuiを使用。全描画命令対応ではない|
|3D|頂点色・UV付き三角形／線／点、深度・表裏・基本の補間、球・カプセル・画像付き四角形等|非照明の基本経路。永続GPUメッシュとGPU変換の本格的なシーン経路は未実装|
|静的モデル|FBX／OBJ直接取り込み、階層変換、複製と形状共有、不透明度、静的面と構造の取得|スキン・モーフ・動的アニメーション・高度な材質は拒否する。モデル読み込みは同期|
|入力|Windowsのキーボード／マウス、共通SDLゲームパッドの接続|ゲーム側IME・文字入力等は未接続。ImGui入力の存在をゲーム側対応と数えない|
|非描画サービス|既存の窓口とDxLib実装はある|DX12構成の実音声・PCM・動画は未実装|

互換台帳は446項目（API 302、Draw 100、Sample 33、Editor 11）。状態はUnreviewed 45、Missing 176、Partial 69、Implemented 155、Verified 0、Excluded 1です。Implementedはソース上の実装確認であり、Windowsでの互換確認ではありません。ExcludedはMV1保存の対象外判定で、実装済みとは数えません。

## 責務と主要ファイル

|配置・型|責務|
|---|---|
|`Core/Interfaces/Render/IRenderTextureResources.h`|RGBA8資源の作成・更新・解放・view照会の4操作。今回の画像分離で追加したインターフェースはこれ一つ|
|`Core/Render/TextureResourceHandle.h`|APIに依存しない型付き画像資源番号。アセット番号・GPU bindingと区別する|
|`Platform/Render/Texture/TextureAssetStore.*`|GPU・UI・codecを知らないアセット番号と資源番号の登録・所有管理|
|`Platform/Windows/Image/WicImageDecoder.*`|CPUでの画像展開。ImGuiとD3D12へ依存しない|
|`Platform/DX12/Resource/Texture/DX12TextureStore.*`|D3D12画像、転送領域、完了待ち、更新前の版と解放済み画像の遅延回収|
|`Platform/DX12/Resource/Descriptor/DX12DescriptorPool.*`|共有SRV heapの貸し出しと返却、所有先と世代の検査|
|`Platform/DX12/ImGui/Render/DX12ImGuiRenderer.*`|公式ImGui描画実装の初期化・フレーム・描画・終了とUI記述子の仲介|
|`Platform/DX12/Device/DX12Device.*`|device・swapchain・queue・フレーム記録と提出の所有と、上記の接続|
|`Platform/Render/Canvas/CanvasDrawExecutor.*`|共通2D命令の検査と展開先への接続|
|`Platform/Render/Geometry/GeometryCommandExecutor.*`|共通3D命令と描画状態の検査・実行|
|`Platform/Render/Geometry/GeometryBufferStore.*`|現在のCPU側の不変バッファ。最終的な永続GPUメッシュの代替とはしない|
|`Platform/Render/Geometry/GeometryShapeBuilder.*`、`GeometryQuadBuilder.*`|GPUに依存しない形状の計算|
|`Framework/Asset/Model/Import/StaticModelBuilder.*`、`Platform/Render/Model/StaticModelStore.*`|静的面の組み立てと共有資源・モデル個体の管理|
|`Framework/Asset/Model/Import/FbxImporter.*`、`ImportedScene.*`、`XFileWriter.*`、`ModelExporter.*`|取り込み、共通中間データ、既存X書き出し、既存MV1出力の各責務|

上表のパスは、特記がなければ `Library/` からの相対です。実際のファイル名にFプレフィックスがないものもあります。

## 壊してはいけない契約・注意点

- エンジン画像をImTextureDataやImGui_ImplDX12_UpdateTextureで作成・所有しない。ImGuiはUI接続だけで、ゲーム画像はUIの終了・再初期化後も利用できる。
- 更新は新しい画像・SRVを完成させてから現在の版へ切り替える。失敗時は既存の内容とbindingを保ち、以前に記録した描画が借りたviewはGPU完了まで維持する。
- 登録解除は番号を直ちに無効にするが、画像・転送元・記述子の実体はGPUの完了確認前に回収しない。CollectAfterGPUIdle／ResetAfterGPUIdleは呼出側が完了を保証する契約。
- 現在の画像上限は遅延解放分込みで1024。最大辺16K、画素数64M、1 mip。上限変更は寿命・メモリ検査と合わせる。
- CoreコンテナにComPtrを直接格納すると、ComPtrのoperator&とコンテナの処理が衝突した事例がある。現在のコードどおり通常の構造体へ包んで格納する。
- DxLib／DX12／Nullで別実装のサービスが混在しないよう、生成条件を保つ。ITextureDrawSourceへIRenderTextureResourcesを継承させる変更は、依存解決の候補を曖昧にするため行わない。
- FGeometryVertexは同次クリップ座標の初期描画入力。CPUで先にw除算しない。本格的なシーンのPBR・GPUスキニング入力として流用しない。
- 既存Spanの2D・3D・Clear・クリップ等の順序を保持する。将来のソートは明示したシーン範囲内に限定する。
- FBX取り込みの影響数はFImportOptions::MaxSkinInfluencesで指定し、既定4、0は全保持。既存MV1変換だけ8を指定する。正の影響の欠落と未対応スキン方式を診断情報として保持する。
- X経由で保持できない変形はModelExporterで既定拒否。明示的なbAllowUnsupportedDeformationsだけ警告付きで続行する。過去の出力を再診断するにはbForceが必要。モーフの実データ保持や変形描画を実装した変更ではない。

## 検査済みの範囲と未検証の範囲

補助環境はLinux上のMinGW C++17とWine 10、DX12はvkd3d／lavapipe。CPU検査は描画命令の不正入力・状態復元、形状、静的モデルの階層・逆転置法線・共有・寿命・失敗時の保持、画像番号と所有、取り込みの影響数・削減・未対応変形、exporterの副作用抑止、互換台帳の追加・削除・宣言変更・Excluded等を確認しました。

GPU補助検査では、基本3Dと画像付き描画、深度・表裏・クリップ・2Dへの復帰、画像の行padding・更新前後の版・記録後の解放・記述子不足と回収、UI再初期化との独立性をreadbackしました。既存SM4／4.1／5シェーダーのコンパイル・reflection・PSO、CS5の64要素書き込みとreadbackも実証済みです。これは製品の共通compute基盤が完成した意味ではありません。

**MSVCによるソリューション全体のビルド、Windows実機、WARP、SDKデバッグ層、複数ベンダー、性能評価、既存自己検査一式は未確認です。** 補助GPUの能力照会はSM5.1で、DXC／SM6は未検証。Wine側のHLSLコンパイラーではRWStructuredBuffer書き込みが未実装だったため、compute実証だけ公式SDKのコンパイラーで確認しました。SDKバイナリは配布に含めません。

補助検査ソースと自作fixtureは作業環境の `/workspace/scratch/` に別置きし、オフライン配布物の `verification/` にも控えを作成しました。GitHubのソースZIPにはverificationを含みません。製品のSource/SelfTestへ勝手に取り込まないでください。既存のframework-testリポジトリはこの作業環境に取り付けておらず、GitHubのソースZIPにも含みません。補助検査には元環境の固定パスがあり、移設時は調整が必要です。検査結果と限界は上記およびDEVELOPMENTに記録しています。

## 次に進める作業

1. Windows環境で現時点のx64 Debug／Releaseをビルドし、DX12のCanvas2D／Geometry3D／StaticModel、DxLib／Nullの選択、起動・resize・終了を確認する。未確認をVerifiedに変更しない。
2. R0の残りとして、MV1以外の対象モデル形式と変形データ保持、既存シェーダーのバイナリとbinding、音声・動画のcodecと独立サービス、最低能力・性能参照環境を詰める。詳細はBACKEND_AUDITのR0の残りを参照する。
3. R1では、最初に必要なbuffer・SRV／UAV・graphics／computeの記録・資源遷移・提出と完了・copy／readbackの小さな共通契約を作る。既存画像資源の寿命と接続し、必要のないインターフェースは先に増やさない。
4. 最初の共通GPU経路を「buffer作成→compute書込→資源遷移→描画／copy→完了待ち→readback」で通し、CPUの偽物とDX12実装で契約を比較する。D3D12直呼びの検査だけを共通基盤の完成としない。
5. その後R2の永続GPUメッシュ・シーン・Forward、R3のフレームグラフ・Deferred、R4の共通クラスタリングと両Plusへ進む。基本図形の追加だけを続けてこの順序を後回しにしない。

## Windowsでの再開手順

必要なものはWindows 10／11 x64、Visual StudioのMSVC v145、Windows SDK 10.0.26100.0、C++17。Jolt・ImGui・SDL・json・ufbxは同梱。DxLibは既存ソリューションの標準構成が参照するため、DX12を実行する場合も標準ビルドでは取得が必要です。

```powershell
.\FetchDxLib.bat
.\GenerateProjectFiles.bat
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\UpdateBackendCompatibility.ps1 -Check
```

生成スクリプトはWindowsで実行してください。Linuxで無調整に実行するとパス区切りとフィルターの差で不要な変更が大量に発生します。自己検査リポジトリを利用できる場合は `Tools/SetupSelfTests.ps1` で取り付け、再生成後に `Tools/RunAllChecks.ps1` とクリーンビルド確認を実行します。RunAllChecksは自己検査がない状態では停止します。

起動例（作業フォルダーはSamples）。実際の出力先はビルド設定で確認してください。

```powershell
Set-Location Samples
..\x64\Debug\Samples.exe --backend=dx12 --sample=Canvas2D --max-frames=120
..\x64\Debug\Samples.exe --backend=dx12 --sample=Geometry3D --max-frames=120
..\x64\Debug\Samples.exe --backend=dx12 --sample=StaticModel --max-frames=120
```

## コーディング・ブランチ運用

規約はDEVELOPMENTの「コードとコメント」を正本とします。自作h／cpp／inl／ps1はUTF-8 BOM一つ・CRLF・NULなし。hはSPDX MITとpragma once。宣言・field・変数に日本語の役割コメント、Allman、既存Coreコンテナ・FString・NMathを優先。OS／標準ライブラリの橋渡しを低層へ閉じます。第三者ソースの書式は変えません。

作業先はbackend-dx12。GitHub側の接続設定の更新後、同ブランチへのpushが成功し、実装コミットと引き継ぎ文書をリモートへ反映しました。履歴を含めて再開する場合は次を使います。

```powershell
git clone --branch backend-dx12 https://github.com/mynameisGaku/mazigatiDX.git .\mazigatiDX-restored
```

ソース一式だけを取得する場合は [backend-dx12のZIP](https://github.com/mynameisGaku/mazigatiDX/archive/refs/heads/backend-dx12.zip) を使えます。GitHubのZIPはGit履歴を含まないため、履歴が必要な場合はcloneします。別途作成したオフライン配布物のbackend-dx12-full.bundleにも全祖先を含めています。認証設定や作業環境の.gitフォルダーは同梱しません。統合・公開はロードマップと既存ブランチ運用に従い、この引き継ぎだけでdev／mainへマージしません。

## computeからGPU描画への接続（2026-11-10追記）

共通のGPUバッファを、CPUへ読み戻さず頂点と16／32bitのindexに使えるよう接続しました。同居する頂点・index、画像付き描画、記録後の番号解除と実体保持に対応します。通常画面と撮影先は同じ所有先とキューを借り、ImGuiへ依存しません。入力と提出順・上限・検査結果は [GPUバッファ描画](R1_BUFFER_GEOMETRY.md) を参照してください。互換台帳は459項目でVerifiedは増やしません。ローカル座標のGPU meshと変換、indirect draw、R1／R2全体は未完了です。

## computeの定数と構造体配列（2026-11-10追記）

既存RAW computeへ、定数の読み取りと構造体配列の読み書きを追加しました。シェーダーの宣言と配置・幅・必要範囲を照合し、GPUが更新した定数を次の計算で読むこともできます。定数行列→構造体の頂点→直接描画とUIなし起動の検査は [バッファ配置の記録](R1_COMPUTE_BUFFER_LAYOUTS.md) を参照してください。新しい資源所有者・Subsystem・UI依存はありません。互換台帳は459項目、Verifiedは0で、R1全体とR2のシーン描画は未完了です。

## 描画パイプライン所有と命名・配置（2026-11-10追記）

GeometryRenderer内のシェーダー・root signature・PSOの作成とキャッシュをCDX12GeometryPipelineStoreへ分離しました。機能型25個をCへ揃え、データ型のFを保ち、開発文書の食い違う命名規則も統一しました。DX12の既存32ファイルを役割別フォルダーへ移し、参照・ビルド・検査も更新しています。適用範囲と描画方式のパスとの違い、検証結果は [責務・命名・配置](DX12_ARCHITECTURE.md) を参照してください。

## compute画像の能力照会と読み書き・描画連携（2026-11-11追記）

RGBA8画像のcompute能力照会（`GetComputeTextureCapabilities`）、UAV対応画像の作成（`CreateComputeTextureRGBA8`）、画像slot付きcomputeプログラム（`CreateComputePipelineWithTextures`）、compute提出での画像SRV／UAV束縛と状態遷移、2D／3D描画への直接受け渡しを接続しました。提出後の `ReleaseTexture` や `UpdateTextureRGBA8` に対する版保持、CPU参照／既定GPU／WARPおよび通常アプリ6起動の検査結果は [compute画像の記録](R1_COMPUTE_TEXTURE_RESOURCES.md) を参照してください。互換台帳は464項目（API 320、Draw 100、Sample 33、Editor 11；Implemented 178、Verified 0）です。

## オフスクリーン描画先資源と読み取れる深度・compute連携（2026-11-11追記）

単一のRGBA8色描画先（`CreateRenderTargetRGBA8`）と読み取れるD32深度（`CreateDepthTextureD32`）の能力照会・作成、共通窓口（`BeginRenderTargetPass`／`EndRenderTargetPass`）によるオフスクリーン描画と同一 `DIRECT` キューへの実GPU提出、色（`Texture2D<float4>`）とスカラー深度（`Texture2D<float>`、`EComputeTextureFormat::R32Float`）を同時に読むcompute、ならびに通常画面・撮影への受け渡しを接続しました。詳細は [オフスクリーン描画先と深度の記録](R1_RENDER_TARGET_RESOURCES.md) を参照してください。互換台帳は469項目（API 325、Draw 100、Sample 33、Editor 11；Implemented 183、Verified 0）です。

## カスタムグラフィックスパイプライン契約とMRT複数描画先（2026-11-12追記）

API非依存のカスタムグラフィックスパイプライン作成・遅延破棄契約（`IGraphicsPipelineResources`）、DXBC SM5.0／5.1 reflection照合（`CDX12GraphicsPipelineStore`）、1〜8枚の `RGBA8` MRT描画先＋任意 `D32` 深度へのパス記録（`FRenderTargetPassDesc::ColorTargets`・`CDX12RenderTargetPass`）、汎用バッファ・画像入力の `IRenderGeometry3D::DrawGraphicsPipeline`、MRT色＋`R32Float` 深度の合成・compute・撮影・主画面連携を接続しました。詳細は [カスタムグラフィックスパイプラインとMRTの記録](R1_GRAPHICS_PIPELINE_MRT.md) を参照してください。互換台帳は472項目（API 328、Draw 100、Sample 33、Editor 11；Implemented 186、Verified 0）で、`RunAllChecks.ps1 -SkipBuild -Quick` 全36ステップが通過済みです。

## GPUタイムスタンプ計測契約（2026-11-12追記）

GPUタイムスタンプの能力照会・作成・遅延解放（`IGpuTimestampResources`）、主画面および `BeginRenderTargetPass`〜`EndRenderTargetPass` MRT/D32パス内の計測（`IRenderSurface::BeginGpuTimestamp`／`EndGpuTimestamp`）、compute一括提出計測（`IComputeQueue::SubmitComputeWithTimestamp`）を同一 `DIRECT` キュー上の `CDX12TimestampStore` へ接続しました。詳細は [GPUタイムスタンプ計測契約](R1_GPU_TIMESTAMPS.md) を参照してください。`Tools/RunAllChecks.ps1 -SkipBuild -Quick` 全38ステップが合格し、互換台帳は481項目（API 337、Draw 100、Sample 33、Editor 11；Implemented 195、Verified 0）です。


## フレームの転送ページとresize（2026-11-13）

DX12デバイスが所有する共有アップロードページを主画面・描画先パス・撮影へ接続し、GPU完了前の再利用を防ぎます。窓の寸法変更は専用描画側を完了させて窓側で交換先を更新し、描画側で主画面深度を再作成します。最小化では有効寸法を保持します。最大64MiB要求の境界とresize失敗時の回収順序をレビュー・修正し、Debug／Releaseの実機・WARP各1306条件と6起動、両構成の全40項目の回帰を確認しました（-Quickでサンプル集・テンプレートの全検査は除外）。詳細は[R1の転送領域と寸法変更](R1_FRAME_UPLOAD_RESIZE.md)に記録しています。

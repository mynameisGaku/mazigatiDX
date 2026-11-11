# DX12開発の引き継ぎ

最終更新: 2026-11-11（日本時間）。この文書は作業中も更新します。実装済み、未接続、検証済みを分け、予定を完了扱いにしません。

## 目的と公開済みの基点

作業ブランチは`backend-dx12`です。[ロードマップ](ROADMAP.md)のR0／R1を整え、共有メッシュ・材質とForwardへ進む依存を先に満たします。v1.0.0のR1全体、四描画方式、完全互換は未完了です。

- 本体の公開済み基点: `d580e767a37bfb42b49684ad43832a2368480a9d`。
- 対応する検査: framework-testの`codex/dx12-windows-validation`、`1d02c862a043f364d4e5f05b988a3993699073c2`。
- この基点は、DX12主画面の専用描画、IsInRenderThread／RENDER_THREAD_ONLY、描画コンテキスト、2D操作の所有コピーまで実装済みです。Debug／Release各4プロジェクトは警告・エラー0件、全30項目を確認して公開しました。
- 現在の変更はこの基点からのR1追加です。下の作業欄と新しい検査結果を確認してから次へ進めてください。

## この端末の作業コピーと保護対象

|作業コピー|役割と注意|
|---|---|
|framework-dx12|ユーザーの`backend-dx12`。公開後は履歴を前進させるだけで反映する。ユーザーの実行中アプリを停止せず、ここではビルドしない|
|framework-dx12-verify|実装とビルドの作業コピー。明示的にこの作業ディレクトリを指定する。公開済み履歴に揃えつつ既存の未コミット設定を保持する|
|framework-dx12-selftests|別の検査リポジトリ。検証側のSource/SelfTestがこのコピーへ接続されている。検査と本体の対応コミットを文書へ残す|
|framework-dev|開発規約の参照先。ここや他の作業コピーの未コミット変更を上書きしない|

本体側の`Library/Core/Collection/Optional.h`と`project.sln`はユーザーの変更です。検証側の`Samples/Samples.vcxproj`、同filters、`project.sln`、`project.vcxproj`、同filtersも、今回より前からある変更です。検査リポジトリの未追跡`x64/`も対象外です。まとめてstageせず、自分の変更だけを明示します。

現在の保護対象のハッシュと生成物は検証側の`Saved/ComputeTextureResources/`へ保存しています。Savedは共有する実装ではなく、端末ごとの作業記録です。

## 継続する規約と責務

実装前に[開発手順](DEVELOPMENT.md)、[ラッパー規約](WRAPPER_GUIDE.md)、対象機能のローカル設計と公式オンライン文書を確認します。機能を実行・管理する型はC、値・データ・handleはF、templateはT、enumはE、窓口はIです。公開する主要型は1 header、実装は同名cppに分けます。機能別フォルダーを責務の境界とし、既存の所有と終了処理を優先します。

DX12はゲームとUIが使うグラフィックス実装です。GPUデバイス・資源・ゲーム描画へImGuiやFrameworkの依存を加えません。共通の命令記録側もAPIを知りません。[スレッド設計](THREADING.md)に従い、公開のゲーム側サービスと、バックエンドの実行側を使い分けます。

- 並列のCPU準備は既存IJobSystem／CJobPipe、同じ実行スレッドへの固定はCRenderCommandThreadを使います。
- 主画面のGPU記録・資源・compute操作は専用描画側です。Present・外部窓はCPU完了の区切りで排他的に窓側へ貸します。
- FTaskFenceはCPU完了です。GPU資源・descriptor・allocatorの回収は別のGPU fenceで確認します。
- 描画命令ではconst FRenderCommandContext&のCanvas／Geometryだけを実行中に借ります。窓・UI文脈・ゲームの可変状態へ触れません。フレーム外は両方nullptrです。
- 複数フレームの同時進行、複数native command listの並列記録、DXC／SM6は未対応です。

## 現在の作業: R1の画像能力照会とcompute用途

選定理由: R1は能力照会・画像の読み書き・barrier・寿命の確認が残り、R2の共有メッシュ／材質へ必要な基礎です。既存のbuffer→compute→描画を保ち、RGBA8画像のcompute読取／読書きと描画への受渡しを追加します。画像所有・プログラム作成・提出を別々の既存所有者へ接続し、新しいsubsystemは増やしません。詳細は [compute画像の記録](R1_COMPUTE_TEXTURE_RESOURCES.md) を参照してください。

|項目|現在の状態|
|---|---|
|共通の用途・配置・能力値|`EComputeTextureAccess`、`FComputeTextureSlot`、`FComputeTextureCapabilities` を追加・実装済み|
|既存窓口の追加操作|`IRenderTextureResources` へ能力照会とcompute画像作成、`IComputePipelineResources` へ画像を含む作成を追加・実装済み。既定実装は未対応で、他の提供元を壊さない|
|dispatchの画像|既存の5要素aggregateを保つため `FComputeDispatch::Textures` を末尾へ追加・実装済み|
|GPU実体と形式照会|`FDX12TextureBinding` と `NDX12TextureFormat` を実装し、`CDX12TextureStore`・`CDX12ComputePipelineStore`・`CDX12ComputeQueue`・`CDX12Device`・`CDX12PlatformSubsystem` へ接続済み|
|検査と互換台帳|Debug／Release各4プロジェクトのビルド（警告・エラー0件）、`RunComputeTextureTests.ps1 -Platform`（CPU 27条件・既定GPU 67条件・WARP 67条件・通常アプリ6起動）、および `RunAllChecks.ps1 -SkipBuild -Quick` 全32項目を確認済み（`Saved/ArgonCoordination/run_all_checks.log`）。互換台帳は464項目（Implemented 178、Verified 0）。検査側の公開コミットは `45c2635ce7fd854eec7de99e44f68ad0f69b1451`（tree `e1df9788f48c9abcf37eb79024f9f5a79daf5a43`、検証済みローカル `5a609de7307934626f9eb37e66eabd0016ca4e4b` と同一tree）|

実装・確認した接続は以下です。

1. `CDX12TextureStore` へ用途と実体の取得を追加し、`UpdateTextureRGBA8` 後も読み書き用途を保つ。資源番号を既存 `RenderResourceSerial` から発行し、別の所有先の番号を拒否する。
2. 転送の完了確認は既存 `CDX12QueueCompletion` を再利用する。待機失敗でも提出済み実体を手放さない。
3. pipelineのreflectionでbuffer／texture共通のt／u重複、2D・RGBA8に合う浮動小数4要素、space・個数・宣言を検査する。画像だけのプログラムも許可する。
4. compute提出へ画像の実体参照と提出専用SRV／UAVを持たせる。通常の読取状態は `PIXEL_SHADER_RESOURCE | NON_PIXEL_SHADER_RESOURCE`、読書き時はUAVへ遷移し、完了位置で元へ戻す。同じdispatch内の実体重複を拒否する。
5. 既存のDX12公開窓口から専用スレッドへ接続し、提出後の画像更新・登録解除と、compute結果の通常画面／撮影描画を確認した。
6. 正常・不正入力・能力未対応・別所有先・型の不一致・全命令の一括拒否を、CPUの参照実装と既定GPU／WARPで検査し、ロードマップと互換台帳を更新して全32項目の回帰を通した。

参照した公式文書（2026-11-11確認）:

- [Typed UAV loads](https://learn.microsoft.com/en-us/windows/win32/direct3d12/typed-unordered-access-view-loads): 追加形式群と個別形式の能力を照会する。現在の公式記載では、UNORM UAVにもfloatの要素型を利用できる。古い「unorm修飾必須」の仮定を引き継がず、float4とunorm float4を実GPUで確かめる。
- [形式のUAV能力](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_format_support2): typed loadとstoreの両方を照合する。
- [資源状態の同期](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12): 同じキューの前後で用途と資源の寿命を管理する。
- [reflectionの入力flags](https://learn.microsoft.com/en-us/windows/win32/api/d3dcommon/ne-d3dcommon-d3d_shader_input_flags): 画像の要素数も検査する。

## 検査、書式、公開

C++とPowerShellはUTF-8 BOM＋CRLF、vcxproj／filtersはBOMなしCRLFです。既存Markdownの改行を一括変換しません。Tools/GameRendering.clang-formatを明示適用し、ラムダを引数に直接書いて括弧内が複数行になる場合は、名前付きの関数値に分けます。source format、依存境界の負例、互換台帳の確認を行います。

ソースを追加・移動したらGenerateProjectFilesを実行します。検証側の既存5設定は事前のバイト列とハッシュで保全し、生成結果のうち自分の登録だけを残します。ビルドは検証側でx64 Debug／Releaseの4プロジェクトを/m:1で行い、完了した生成物を固定してからRunAllChecks -SkipBuild -Quickを実行します。同じ構成のビルドと実行検査を競合させません。

MSVC v145では、型の変更後のRelease増分リンクでLNK1103、Debugの生成物でLNK1236を観測しました。前回はソースの最適化設定を変えず、検証側の該当obj、またはproject／Samples／Editorのx64/Releaseのiobj・ipdbをSavedへ退避して再生成すると通りました。再発時も対象が検証側の配下であることを絶対パスで確認し、必要な生成物だけを扱います。ユーザーの作業コピーを再帰削除しません。

commitはfeat:／fix:／refactor:／other:に日本語の説明を続けます。検査を先に対応する専用ブランチへ公開し、その実コミットを本体の文書へ記録します。本体はbackend-dx12へ履歴を前進させて公開し、GitHub上のtreeと検証したtreeが一致すること、ユーザーの既存変更が保持されることを確認します。

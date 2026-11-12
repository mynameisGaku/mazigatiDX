# フレームの転送領域とウィンドウの寸法変更

2026-11-13（日本時間）。R1のフレーム別領域・アップロード管理と、swapchainのresize・最小化復帰を接続します。描画命令ごとの一時GPUバッファ作成を、GPU完了まで再利用しない共有ページへ置き換えます。Coreの窓口、ゲームの描画命令形式、ImGuiとの依存関係は増やしません。

## 所有と配置

|配置|責務|
|---|---|
|`Platform/DX12/Resource/Upload/CDX12UploadArena`|CPUから書き込むGPUページ、記録中の予約、提出フェンスと再利用の条件を所有する|
|`CDX12Device`|同じDIRECTキューのアリーナと主画面のGPU寿命、交換用画像とresizeを所有する|
|`CDX12GeometryRenderer`|一つの記録区間の予約を開き、頂点をコピーする。提出先の所有者へ予約番号を渡す|
|`CDX12RenderTargetPass`・`CDX12CaptureTarget`|自分の命令を提出した後で予約を確定し、提出前の失敗では予約を取り消す|
|`CDX12PlatformSubsystem`|窓から届いた寸法を記録し、フレームの境界でGPU接続を排他的に借りてresizeする|

アリーナは既存デバイスの内部資源です。新しいsubsystemやスレッドは作りません。通常の資源操作は既存の専用描画スレッドが所有し、窓の交換先を変更する間だけ、受付済みCPU命令の完了後に窓側へ排他的に貸します。アリーナ自体へ複数スレッドから同時にアクセスしません。

## 転送ページの契約

通常ページは1MiB、1回の割り当ては64MiBまで、全ページの合計は128MiBまで、同時に開ける記録予約は256件です。整列は1〜65536の2のべき乗を受け付けます。最大64MiBの要求も新規ページの先頭で受け付け、整列用の余分な容量を加算して拒否しません。

1. `BeginTicket`で記録区間を開き、`Allocate`で書き込み先とGPUアドレスを取得します。CPUはこのメモリへ書き込み、読み戻しには使いません。
2. `ExecuteCommandLists`の後で`CommitTicket`を呼び、その位置のGPUフェンスをページへ関連付けます。
3. 提出前に破棄した区間だけを`AbortTicket`で取り消します。同じページを別の記録区間や未完了提出が使っていれば、先頭へ戻しません。
4. 開いている予約がなく、ページを使う最後のGPUフェンスが完了してから先頭を再利用します。未完了ページの空き末尾へ追加することは可能です。

不正な要求と容量超過では出力を保ちます。GPU喪失・Signal失敗後は新しい割り当てを止め、未完了のページを再利用しません。破棄と`ResetAfterGPUIdle`は、所有者がGPU完了または喪失を確認してから行います。

主画面は一つの予約をフレーム全体で保持します。途中の描画先切り替えやcomputeで主画面命令を提出済みの場合、後半の描画が失敗しても、その予約を未提出として取り消しません。先行提出を含むフェンスへ関連付けます。オフスクリーン描画・撮影は、それぞれ独立した記録予約を使います。

アリーナを渡さない独立した既存描画器では、従来の一時バッファ所有を維持します。通常アプリは主画面・描画先パス・撮影へ同じデバイスのアリーナを接続します。

## 寸法変更の契約

`--dx12-resizable`または既存の`Settings.bResizable`で窓の寸法変更を有効にします。`WM_SIZE`の処理は要求寸法を保存するだけで、記録中のGPU状態を変更しません。最小化や0×0通知ではGPU画像を作り直さず、最後の有効な寸法を保ちます。

次のフレーム開始前に、以下の順で変更します。

1. 専用描画側のCPU命令を完了させ、窓側へGPU接続を排他的に貸します。
2. 窓の所有スレッドから`CDX12Device::Resize`を呼びます。GPU完了を確認し、旧バックバッファと撮影資源を戻してから`ResizeBuffers`・画像取得・RTV再作成を行います。DXGIが窓へ通知を送る場合にも、窓側が描画スレッドを待つ構成を作りません。
3. 専用描画側へ所有を戻し、完了済みの別パス参照を回収して主画面のD32深度を作り直します。PSOやSamplerは保ちます。
4. 成功後に画面の論理寸法を更新します。撮影とImGuiの倍率・入力座標もこの寸法を参照します。

記録中・表示待ち・終了中・不正寸法は拒否します。GPU待機が失敗した場合、旧画像や別パスの未完了資源を回収しません。`ResizeBuffers`や新画像取得の失敗後は安全な未初期化状態とし、通常アプリは終了へ進みます。デバイス喪失後の自動再作成を実装した意味ではありません。

`CDX12GeometryRenderer::Resize`は、所有する主画面深度だけを対象とします。外部の深度を借りる描画器、記録中の描画器には使えません。呼出し側が先行描画のGPU完了を保証します。

## 確認

検査は別リポジトリの`RunUploadResizeTests.ps1 -FrameworkRoot <実装先> -Configuration Debug|Release -Platform`と`Tools/RunAllChecks.ps1`へ接続します。

- CPU参照: 予約の重複、未完了ページの保持、取り消しと完了後の再利用。
- 既定GPU・WARP: 実フェンスでキューを止めた間の保護、整列、64MiBの最大要求と128MiB容量境界、出力保持、取り消した最大ページの再利用。
- 描画: 200回×6フレームの実画素と、初回準備後に転送ページを再生成しないこと。
- resize: 不正寸法・記録中・表示待ち・外部深度の拒否、複数の寸法での色・深度遮蔽・撮影、デバイス喪失後の拒否と終了。
- 通常アプリ: 実機・WARPのUIあり／なし、DxLib・Nullの6モード。窓の400×300への変更、最小化、321×213への復帰と画素を確認する。
- 依存: 独立した検査実行ファイルへImGuiがリンクされないことと、GPU資源層へFramework・UIが流入しないこと。

レビューで最大要求の拒否を再現した記録は`Saved/ArgonCoordination/codex-upload-boundary-before.log`、修正後は`codex-upload-resize-Debug.log`と対応するRelease／全体回帰ログへ保存します。前回のタイムスタンプ段階の38項目成功は、今回の修正後の検証とは区別します。

## 参照と残る範囲

実装前にローカルの開発規約・スレッド設計と、以下の公式資料を確認しました（2026-11-13）。

- [Microsoft: 資源のアップロードと部分割り当て](https://learn.microsoft.com/en-us/windows/win32/direct3d12/uploading-resources)
- [Microsoft: ResizeBuffers](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgiswapchain-resizebuffers)
- [Microsoft: DXGIの複数スレッド利用](https://learn.microsoft.com/en-us/windows/win32/direct3ddxgi/d3d10-graphics-programming-guide-dxgi#multithread-considerations)
- [Epic: 描画スレッドの所有と寿命](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine)

R1全体は未完了です。計画の次のまとまりであるRGBA16F資源・画像コピー／readback、R1横断の失敗・終了検査を引き続き実施します。MSAA・複数フレームの同時GPU進行・複数キュー・デバイス喪失後の自動復旧は、この変更の実装済み機能として数えません。

検証結果: Debug／Release各4プロジェクトは警告・エラー0件、CPU615・実GPU1306・WARP1306・喪失6と6起動、全40項目の回帰が成功しました。対応する検査は `725b8be50b6fc45f2e28ffb77190b13812708d4f`。`-Quick`でサンプル集とテンプレートの全検査は除外しています。

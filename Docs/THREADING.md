# スレッドの所有と実行順序

最終確認: 2026-11-10（日本時間）。実装前にこの文書、[開発規約](DEVELOPMENT.md)、利用するサービスの契約と下記の公式資料を確認します。参考にするのは所有・受付・順序・完了の考え方です。Unreal EngineのAPIやスケジューラーと完全互換ではありません。

|用途|使う仕組み|所有と制約|
|---|---|---|
|独立したCPU計算・ファイル処理|既存のIJobSystem／ParallelFor／AsyncTask Worker|可変の領域を仕事ごとに分け、完了時に結果を渡す|
|同じ資源の操作を直列化|CJobPipe（Framework/Job）|既存のワーカーを共有する。実行順は受付順だが、同じOSスレッドとは限らない|
|同じスレッドで継続して処理|CCommandThread（Framework/Thread）|所有者が1本のスレッドを保持する。作成・操作・解放をその列へ積む|
|ゲーム状態・DxLib・窓への反映|既存のAsyncTask Game／NGameThread|ゲームスレッドが所有する。ワーカーから直接変更しない|
|CPU側の区切りの確認|FTaskFence（Core/Thread）|受付済み番号までの処理と取り込んだ値の解放を表す。GPU完了とは別|
|GPU資源の回収・allocator再利用|既存のGPUフェンスと提出所有者|CPU票の完了だけで回収しない|

計算は共有ジョブを優先します。新たな永続スレッドは、同じスレッドへの固定、継続的な待機、外部APIの制約が必要な所有者だけが持ちます。局所的な実行列のためにsubsystemは増やしません。

## 受付、実行、終了

CJobPipeとCCommandThreadのEnqueue、GetFence、Close／RequestStopは複数のスレッドから呼べます。同時の受付は内部ロックで1つの順序に決めます。要求元ごとの順序は保ちますが、異なる要求元の先後は受付タイミングで決まります。ゲーム側で描画順が必要なら、記録後に順序を確定してから渡します。未実行の仕事は4096個までです。満杯、空の関数、受付終了、番号枯渇では無効な票を返し、仕事を実行しません。

関数は例外を投げず、Nameは文字列リテラルを使います。送る値はコピー・移動で所有し、借用ポインターや参照は完了まで寿命を保証します。受け付けた仕事の取り込んだ値は実行スレッドで、CPU完了を通知する前に手放します。拒否された仕事の値は呼出し元で破棄されるため、スレッドを固定して解放すべき資源を拒否経路へ渡さないでください。

CJobPipeは64件ごとに次の実行ジョブへ継続し、共有ワーカーを返します。ワーカー0本では最初の要求元で直列実行し、実行中の再受付はその仕事が戻った後に処理します。Closeは新規受付を止め、受付済み全件の票を返します。デストラクターもCloseしますが待ちません。実行列は共有所有により残ります。Jobsは全件完了まで生存させ、外部資源を破棄する前に所有者側で完了を確認します。

CCommandThreadは作成時に専用スレッドを起動します。初期化も最初の仕事として積み、最後の解放を積んでからRequestStop／StopAndJoinします。RequestStopは受付を閉じるだけです。StopAndJoinと破棄は作成スレッドが担当し、受付済み全件を実行してから結合します。作成スレッド以外のStopAndJoinはfalseで、仕事を止めません。自分自身での破棄は契約違反として終了します。公開呼出し中のオブジェクトを別スレッドから破棄しないでください。所有者はプロファイラーや関連サービスより先に終了します。

FTaskFenceの既定値は無効で、IsComplete／Waitもfalseです。有効な空列の票は完了済みです。Waitは0〜60000ミリ秒で条件変数により眠り、期限切れ・実行不能・自分の実行列の未完了票への待機はfalseです。GetFenceは取得時点の区切りなので、後から足した仕事は待ちません。票のコピーは実行列の破棄後も照会できます。同じ票の代入と照会を別スレッドで競合させないでください。

ワーカーの中で他の列を待つと、全ワーカーを使い切る可能性があります。短い仕事を依存や継続で結び、ゲーム側はIsCompleteを更新時に調べます。スケジューラーのWaitIdleは外部の所有者による終了・読み込み時の待機用で、実行中ジョブからはfalseを返します。既存のWaitは他の仕事を実行しながら待つ旧方式のままです。循環待機や任意の入れ子待機の安全を保証するものではありません。

## ジョブとエンジンの終了

IJobSystem::Scheduleは空の仕事と別のスケジューラーが発行した依存を拒否します。終了開始後は外部の新しい要求を拒否しますが、既に実行している同じスケジューラーの仕事から派生する子は受け付けます。実行待ち・依存待ち・実行中・子の全件が終わってからワーカーを停止します。再初期化では受付状態を戻します。

WaitIdleはジョブの本体と取り込んだ値、子の完了を待ちます。継続的な外部の要求元は先に停止します。Initialize／DeinitializeとParallelForの呼出しを競合させないでください。CEngineはメインループ終了後、ワーカーの結果とゲームスレッドへの返送を処理し、返送から発生したジョブも終えてからGameInstanceを終了します。ジョブがゲームスレッドの返送完了を同期的に待つ設計は禁止です。Term／Deinitialize内で新しい仕事を発行する所有者も、自分の資源を破棄する前に完了させます。ジョブを使うsubsystemはInitializeDependency<IJobSystem>で終了順序を定めます。

## DX12の専用描画スレッド

DX12ではCRenderCommandThread（Framework/Render/Thread）が既存CCommandThreadを1本保持し、開始時に描画スレッドを登録し、全CPU命令の終了後に解除して結合します。GPUデバイスへFrameworkやImGuiの依存は追加しません。CThreadedRenderSurfaceはCoreの描画窓口と所有した命令値だけを使い、DX12・Windows・ImGuiを知りません。

|段階|所有するスレッド|順序と寿命|
|---|---|---|
|窓・GPU・UIの初期作成|ゲーム／窓|作成後、専用側へ実行の所有を渡す|
|主画面のDXGI番号照会|ゲーム／窓|前の専用処理を終えた段階で番号を固定する|
|フレーム開始・GPU完了確認・allocator再利用|描画|BeginFrameForBufferへ固定した番号を渡す|
|2Dの引数記録、オブジェクト状態の参照、UIの構築|ゲーム|生きたゲーム状態を別スレッドから読ませない|
|2Dの頂点展開、文字画像化、3DのGPU記録、画像・バッファ・compute操作|描画|同じ命令列へ受付順に集約する|
|主画面のImGui GPU記録とフレーム提出|描画|ゲーム側の全命令の後。UIデータはゲーム側が変更せず、CPU完了まで借用する|
|Present・交換先resize、外部窓のOS処理と公式UI接続|ゲーム／窓|専用側を完了させ、GPU接続を排他的に借りる。終了後に専用側へ戻す|
|撮影のCPU受信者への通知|ゲーム|GPU読み戻し自体を専用側で完了させてから通知する|
|終了|両側の所有順|命令を全件実行→専用側で撮影・GPU完了・ゲーム描画を片付ける→識別解除と結合→窓側でUI・GPU・窓を解放する|

2Dは固定長の操作値と所有するUTF-8で保持し、64件ずつ配列を移動します。1フレーム65536命令、文字列は1回64KiB・合計8MiBまでです。点列・UV・色・クリップを呼出し時に写すので、元の変数を後から変更しても使いません。画像のbindingは借用値で、既存のGPU所有先の遅延解放を使います。資源更新・登録解除の前には未送信の2Dも同じ列へ渡し、記録前の解放や別スレッドのMap参照を起こしません。

3Dの成否、画像の登録、buffer readback、compute提出の検査結果など、既存APIが即座に返す値は同期で返します。同期呼出しでは入力・出力の参照を実行終了まで保持します。CPU待機が60秒を過ぎたことを理由に、借用参照を残して戻りません。GPU処理側の既存の期限・喪失処理は維持します。同期呼出しの列が満杯なら先行CPU処理を待って再受付し、空の区切りを追加できない満杯状態でもGetFenceで全件完了を確認します。

GPU提出と表示はCDX12Device::SubmitFrame／PresentFrameに分けました。従来のEndFrameはこの2つを連続して呼ぶ形で互換を保ちます。未表示の提出を残して次を開始したり、同じ提出を二度表示したりすることは拒否します。Presentと外部窓処理中は専用側に未完了CPU命令を残さず、新規の非同期描画命令は無効票で拒否します。その窓側の再入から必要な同期GPU操作は排他的な貸出中の所有側で実行します。窓側のメッセージ処理を止めたまま、別スレッドのDXGI処理を待つ構成を避けます。

通常画面から描画先パス・撮影へ切り替える前、および通常画面の途中でcomputeを提出する前には、先行するCPUバッチだけでなく主画面のGPU命令もSubmitFrameSegmentで同じDIRECTキューへ提出します。この区切りではGPUを待たず、命令列だけをResetし、allocator・色・深度・クリップとフレーム資源の寿命を保ちます。allocatorのResetと資源回収はフレーム開始のGPU完了確認後です。ゲーム側のcompute提出は描画先パス・撮影を閉じてから行い、記録中の割り込みは拒否します。詳細と画素による回帰検査は[R1描画先と深度](R1_RENDER_TARGET_RESOURCES.md)に記録します。

現段階ではフレームをまたぐCPU／GPUの複数同時進行や、複数のnative command listの並列記録は追加していません。allocatorの再利用と資源回収は、既存どおりGPU完了後です。ゲーム更新や2D命令準備と、渡したバッチの実行は並行に進められます。成否を同期で返す操作の待機点は残しています。描画順を変えずに、後から複数フレーム・複数記録先へ広げる境界を先に作っています。

## 識別と描画命令の窓口

Core/Thread/RenderThread.hのIsInRenderThread()は、登録された専用スレッドだけでtrueです。起動前・終了後・ゲームスレッド・共有ワーカー上ではfalseです。フレーム外でも、その専用スレッドならtrueです。これは処理の種類でなく、実際に動いているスレッドの識別です。専用側が自分で別のCPU関数を実行しても識別は変わりません。IsInGameThread()の未登録時の既存仕様は変更せず、エンジンの起動時にBindした後で比較します。

RENDER_THREAD_ONLY()／RENDER_THREAD_ONLY( 戻り値 )は、別のスレッドからの呼出しを記録して戻します。NRenderThread::IsBoundは専用スレッドの有無を返します。Bind／Unbindは調停する所有者専用で、利用側は呼びません。別スレッドからの上書き・解除、同時に2つの描画所有者を作ることを拒否します。

ゲーム側はGetAppSubsystem<IRenderCommandQueue>()で、値を所有する仕事を先行描画と同じ順序へ渡せます。コールバックの引数FRenderCommandContextは、実行中だけ有効なCanvas／Geometryを借りる値です。フレーム外では両方nullptrです。ゲーム側の公開サービスは描画側から使い回しません。CDX12GameSurfaceが通常画面と撮影先のバックエンド実行先を選び、窓・ゲーム側の受付状態・スケジューラーを知りません。コンテキストは窓やフレーム・撮影先の管理操作を公開しません。EnqueueRenderCommandの呼出しはゲームスレッドで、渡した仕事は描画スレッドで実行します。戻るFTaskFenceはCPU完了で、GPU完了ではありません。窓・UI文脈やゲームの可変状態へ触れず、ゲームスレッドからの返送を同期的に待ちません。共有ワーカーの計算結果は既存のAsyncTask Gameへ返してから、この窓口へ渡します。DxLib／Nullにはこの専用窓口を追加せず、存在しない場合はnullptrです。

## 検査と参照資料

Source/SelfTestのRunThreadExecutionTests.ps1は、本物のCJobSubsystemを使い、Debug／Releaseそれぞれワーカー0・1・4・15本で確認します。4要求元の各400件の順序と非同時実行、ワーカー0での再受付、自分待ち拒否、期限・受付上限・区切り、取り込んだ値と票の寿命、Pipe破棄後の実行、64件単位の譲渡、終了時の依存と子、再初期化を確認します。Tools/RunAllChecks.ps1へ組み込みます。実アプリでもNull（ワーカー0・1・4・15）／DX12／WARPで、メインループ終了後に返るワーカー結果と追加の結果返送を、シーンの解放前に全件反映することを確認します。独立した検査のリンク結果からGPU／ImGuiへの依存がないことも確認します。これはCPU側の検査で、並列GPU記録や描画互換性の検証を表しません。

参照したローカル文書はframework-dev/Docs/DEVELOPMENT.mdとWRAPPER_GUIDE.md、およびこのブランチの同文書、Core/Thread、Framework/Jobの既存契約です。オンラインは以下の公式文書です（確認日: 2026-11-10）。実装を進める際には対象分野の最新の公式文書も開いて確認し、判断と制約を対応する開発文書に残します。

- [Epic: Tasks System](https://dev.epicgames.com/documentation/en-us/unreal-engine/tasks-systems-in-unreal-engine): 共有スケジューラー、依存による非同期待機の削減、pipeによる資源操作の直列化を参考にしました。この実装は受付順を保証する有界キューを持ち、UEのFPipeの内部構造や依存解決後の並び替えを再現しません。
- [Epic: Threaded Rendering](https://dev.epicgames.com/documentation/en-us/unreal-engine/threaded-rendering-in-unreal-engine): スレッドごとの状態所有、データの複製、非同期の命令とCPU完了の区切りを参考にしました。
- [Epic: FRunnable](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/FRunnable): 専用スレッド内の初期化・実行・終了という寿命の考え方を参考にしました。独自の仮想基底型を増やさず、既存のFThreadと受付列を使います。
- [Microsoft: Command queues and command lists](https://learn.microsoft.com/en-us/windows/win32/direct3d12/design-philosophy-of-command-queues-and-command-lists): 個別のcommand listの同時操作を避けること、提出順とGPU完了をアプリが管理することを、今後のDX12移行の条件とします。

検査の固定点: framework-test `1d02c862a043f364d4e5f05b988a3993699073c2`（`codex/dx12-windows-validation`）。Debug／Releaseの各4プロジェクトは警告・エラー0件、`RunAllChecks -SkipBuild -Quick`の全30項目を確認済みです。専用描画のCPU契約は各構成32条件をワーカー0・1・4・15本で実行し、実アプリは各構成6起動×6フレームで2D／3D記録・UI記録・フレーム後の空コンテキストを確認しました。主画面の専用描画への移行まで対応し、複数フレームの同時進行は未実施です。

追加の検査: RunRenderThreadTests.ps1は、識別の登録・解除・再起動・競合拒否、誤スレッドのガード、通常ジョブとの区別、点列・文字列・UVの所有、描画順、成否の同期返却、撮影切替、受付上限と破棄時の全件回収を確認します。既定GPU／WARP、ワーカー0・1・4、UIあり／なしの実アプリでも、6フレームの実画素と主画面UIのGPU記録コールバックが専用スレッド上で動くことを確認します。共通実行側のmapにGPU／ImGuiを含めず、依存検査へAPI逆流の負例も追加しています。

追加の公式資料（2026-11-10確認）:

- [Epic: Parallel Rendering Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/parallel-rendering-overview-for-unreal-engine): APIに依存しない命令と実行側を分け、並列化してもGPU提出順を維持する考え方を参照しました。
- [Microsoft: DXGI Best Practices — Multithreading](https://learn.microsoft.com/en-us/windows/win32/direct3darticles/dxgi-best-practices): DXGIの窓メッセージ処理との相互待機を避けるため、表示と外部窓を窓側へ残す判断に使いました。


追加の転送ページとresize: 主画面・別パス・撮影はCDX12Deviceが所有するアリーナを共有し、記録予約とGPUフェンスでページの再利用を管理します。交換先resizeは、専用側のCPU命令完了後に窓のスレッドへ排他的に貸して実行します。GPU待機失敗では旧資源を回収せず、成功後に描画側で主画面の深度と完了済みの別パス参照を更新します。[実装と検査](R1_FRAME_UPLOAD_RESIZE.md)を参照してください。

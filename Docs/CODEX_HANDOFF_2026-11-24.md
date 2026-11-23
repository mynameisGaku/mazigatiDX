# Codex への引き継ぎ（2026-11-24）

Claude の作業を、Codex（ChatGPT）へ渡します。この文書だけで、「いまどこまで進んでいて、何を push し、次に何をするか」が分かるように書きました。経過の細部は [DX12_HANDOFF.md](DX12_HANDOFF.md)（最後の 4 つの節が今回の分）、未対応の一覧は [DX12_KNOWN_GAPS.md](DX12_KNOWN_GAPS.md)、順序と完了条件は [ROADMAP.md](ROADMAP.md) です。

## 1. 先に知っておくこと

- **push は Codex が行います**（Claude は push していません。取り決め）。対象は 2 つ: 本体（`mynameisGaku/mazigatiDX` の `Dx12` ブランチ）と、検査のリポジトリ（`mynameisGaku/framework-test` の `Dx12` ブランチ。本体の `Source/SelfTest` の接続先）。
- 利用者は、不在のことがあります。確認が要る判断は、「9. 利用者の方針と、未確認の判断」を見てください。
- 利用者の HidHide・Steam・Switch2Connect の設定は、触りません（権限の判定で拒否されたので、迂回もしないこと）。

## 2. いまの状態

|項目|場所|状態|
|---|---|---|
|本体の作業コピー|`C:\Users\g0190\.claude\worktrees\framework-stl`（ブランチ `Dx12`）|HEAD は、この文書を足したコミットの直前で `e3b737e`。**origin/Dx12 より 39 コミット先（未 push。この文書のコミットを加えると、1 つ増える）**。未コミットは、利用者の `project.sln` / `project.vcxproj` / `project.vcxproj.filters` だけ（コミットしない。取り決め）|
|検査の作業コピー|`C:\Users\g0190\OneDrive\Desktop\framework-test`（ブランチ `Dx12`。本体の `Source/SelfTest` が、ここへのジャンクション）|HEAD は `d52543a`。**origin/Dx12 より 16 コミット先（未 push）**。未追跡の `.vs/` と `NativeGameContract.vcxproj.user` は、コミットしない|
|Codex の作業コピー|`C:\Users\g0190\OneDrive\Desktop\framework-dx12`（`backend-dx12`、`6ebe9e0`）、検査は `framework-dx12-selftests`（`codex/dx12-windows-validation`）|**`Dx12` にまだ入っていないコミットがある**（次の節）|
|利用者の原本|`C:\Users\g0190\OneDrive\Desktop\framework`（`dev`）|ゲームを動かす場所。変更・未追跡を残さない|

検査は、直近で、**`RunAllChecks.ps1 -SkipBuild -Quick` が 77 ステップすべて通過**（約 55 分）。`-Quick` が飛ばす 3 手順（`samples` 約 17 分・`template project` 約 9 分・`package` 十数秒）も、通過しています（古くなっていた 2 つの検査を直したあと）。そのあと、第 2 段の固定姿勢・文字の上限・落としたフレームを足して、関係する 30 ステップ前後を回し直し、通っています。

## 3. 最初にやること: `backend-dx12` の取り込み

Codex の `backend-dx12`（`origin/backend-dx12`）は、`Dx12` より **1 コミットだけ**（`6ebe9e0 fix: 静的モデルの鏡映時に面の向きを保つ`）多く、`Dx12` はそれ以外をすべて含みます（`Dx12` は backend-dx12 より 149 コミット先）。検査のリポジトリも、`codex/dx12-windows-validation` に 2 コミット（`9d9c183` 鏡映モデルの面と GPU 描画を検査、`f482f34` 鏡映時の材質色を検査）が、`Dx12` に入っていません。

取り込んだときの衝突を、`git merge-tree` で先に調べました（衝突は、次の 3 つだけ）:

1. **`Library/Framework/Asset/Model/Import/StaticModelBuilder.cpp`**（本体）: `Transform` の行が、両方で書き換わっています。**両方を残します**:
   ```cpp
   const FMatrix4x4 Transform = Mesh.Node < 0 || Mesh.IsSkinned() ? FMatrix4x4() : Global[Mesh.Node]; // Dx12 の側（スキンつきは、骨の変形で位置が決まる）
   const float64 Determinant = LinearDeterminant( Transform ); // backend-dx12 の側（親を含む変換が鏡映なら面の向きを戻す）
   ```
   スキンつきのメッシュは、`Transform` が単位行列なので、反転は起きません（行列式 1）。スキンの無い小物のメッシュ（骨組みのあるモデルの中）は、基準の姿の節の行列で、鏡映を判定します。
2. **`Docs/DX12_HANDOFF.md`**（本体）: 両方が、末尾へ節を足しています。**両方を残します**（日付の順）。
3. **`GraphicsPipelineContractMain.cpp`**（検査）: `#include` が両方で変わっています。**両方の `#include` を残します**。

取り込んだあとに、**必ず確かめること**: 鏡映の親を持つ、スキンの影響が無い小物のメッシュを、**GPU スキニングで描いたとき、面の向きが CPU 変形と一致するか**。GPU スキニングの経路は、頂点を骨組みの元の位置（`FRigMesh::Positions`）から作り、添字はビルダーの出力（鏡映なら 2 頂点を入れ替えたもの）を使い、節の行列は頂点シェーダーでかけます。理屈では合うはずですが、鏡映の検査（`9d9c183`）に、GPU スキニングの経路は入っていません。`ModelRigContract` の `TestGpuSkin` の「骨の影響が無い小物のメッシュ」の検査に、節に鏡映（負の拡大）を足して、`SkinnedMatchesDeformed` が通ることを確かめるのが、最短です。

取り込み後は、`backend-dx12` が `Dx12` の祖先になるので、`backend-dx12` を `Dx12` へ進める（fast-forward）かどうかは、Codex が決めてください。

## 4. push の手順（Codex）

1. 上の取り込みを、`framework-stl` の `Dx12`（または、同じ内容の作業コピー）で行い、衝突を解く。検査のリポジトリでも同様に。
2. 本体を `Dx12` のままビルド: `Docs/AI_WORKFLOW/tools/build_cfg.ps1 -Config Release -Target Samples`、`-Config Debug -Target Samples`（利用者の私用の道具。「7. 道具」）。
3. 検査: `Tools\RunAllChecks.ps1 -SkipBuild -Quick`（約 55 分。窓が開く）。続けて、`-Quick` が飛ばす手順: `Tools\RunAllChecks.ps1 -SkipBuild -Filter '*samples*','*template*','*package*'`（約 25 分）。**`samples` の手順は、`Samples/Samples.vcxproj` と `.filters` を、並び替えだけで書き換える**ので、終わったら `git checkout -- Samples/Samples.vcxproj Samples/Samples.vcxproj.filters` で戻す（機能の差は無い）。
4. 検査のリポジトリを push、続けて本体を push。**検査と本体の対応するコミットを、`DX12_HANDOFF.md` へ残す**（これまでの習慣）。対応は、いまの HEAD で、本体 `e3b737e` ↔ 検査 `d52543a`（取り込みで変わったら、書き直す）。

## 5. 2026-11-23 〜 24 の作業（本体のコミット。古い順）

- `eea4f45` feat: 球・カプセル・円錐の頂点を、円ごと・境目ごとに1回ずつ作って共有する（Debug の Camera 5.9 → 4.8 ms）
- `78ef97d` feat: 同期呼び出しの往復を、眠る前に最大50µsだけ眠らずに待って受ける（Debug の Camera 4.8 → 3.0 ms）
- `3521420` other: 頂点の共有と眠らずに待つ往復を、ドキュメントへ書く
- `6dae137` feat: 待たずに積む描画の複写を1つの入れ物にまとめ、使い終わったら使い回す（確保と解放を繰り返さない）
- `ceb5134` feat: 登録したバッファの三角形を共有メッシュのまま描く（Island の Release 2.06 → 1.15 ms、Debug 4.6 → 2.9 ms）
- `988969c` other: 共有メッシュで描く変更と、積む描画の入れ物の使い回しを、ドキュメントへ書く
- `2052dcd` other: 互換表へ、同じ版を何度か描いたあとだけ共有メッシュの識別を返す窓口を足す
- `92a07b4` other: ロードマップへ、遮蔽カリングと遠いライトのデカール表現を、後で入れる負荷削減として書く
- `123518a` other: ゲームパッドの SDL 初期化が 0.5 秒以上かかったら、起動が止まる原因（応答しない HID 機器・DirectInput の列挙）を警告で知らせる
- `09bea24` other: ロードマップへ、VAT による大量キャラクターを、後で入れる負荷削減として書く
- `4b91cbc` other: SDL3 の x64 の DLL を、libusb 対応（Nintendo Switch 2 のコントローラーのドライバー付き）で作り直す
- `181cb43` other: Switch 2 Pro Controller（USB）を実機で確認した結果を、ドキュメントへ書く
- `a24a2da` other: Steam が動いているときの Switch 2 Pro の扱いと、Steam SDK で対応する方針を、開発手順へ書く
- `dd1c6c9` feat: PMX の頂点モーフを形状として読み、SetModelShapeRate で動かす（Faputa の表情が DxLib と一致）
- `26c74a3` feat: Character サンプルに、まばたきと、にこりの表情（形状）を足す
- `a6c21ea` feat: モデルのスキニングを GPU で行う（Forward の Draw.Model）
- `862de5f` other: GPU スキニングの記録と互換台帳を更新する
- `d39a727` feat: 固定した姿勢を、骨と重みのまま保持して GPU で描く
- `bb9dce7` fix: 文字の数の上限を超えても、フレームを失敗にしない
- `83fa34f` other: 固定した姿勢の GPU スキニングと文字の上限の記録、互換台帳を更新する
- `8f15b68` fix: 描画の内容が失敗したフレームで、アプリを終了させない
- `e3b737e` other: 落としたフレームの扱いと、故障を注入するオプションを記録する

検査のリポジトリ（古い順）:

- `0262d2e` feat: 並列の変形が順番の変形と同じ結果になることと、新規作成した画像を同じフレームで作って解放しても化けないことを確かめる
- `0301dea` feat: 眠らずに待つ処理と、往復を繰り返す検査を足す
- `0630de3` other: 積む描画の入れ物の使い回しと、バッファの三角形を共有メッシュで描く条件の検査を足す
- `4db3f50` other: モデルの形状（モーフ）と、PMX のモーフの取り込みの検査を足す
- `139e2b1` feat: GPU スキニングの検査を足す
- `af6c0ef` feat: 固定した姿勢の GPU スキニングと、文字の上限の検査を足す
- `087e07f` feat: 落としたフレームの後も続くことの検査を足す
- `d52543a` fix: サンプルの説明文の位置と、libusb の配布物の検査を、いまの実装に合わせる

要点（詳細は、`DX12_HANDOFF.md` の 2026-11-23 追記（夜、その 5）以降）:

- **性能**（11-23 の夜）: 全サンプルが、間隔の下限（約 2.94〜3.1 ms）に届いた（Release・Debug）。球・円錐の頂点の共有、スキニングの並列化、登録したバッファを共有メッシュのまま描く、積む描画の入れ物の使い回し、眠らずに待つ往復など。経過は `DX12_HANDOFF.md` の 2026-11-23 追記。
- **SDL3 と libusb の自前ビルド**: Nintendo Switch 2 Pro Controller（USB）を、プレイヤーの環境で、ドライバーの導入なしに使える。`VendorModules/SDL3/BUILD_NOTES.md`、`Docs/DEVELOPMENT.md`（Steam・HidHide・DirectInput の競合の注意）。x86 / arm64 の SDL の DLL は、公式のまま。Steam が動いていると使えない件は、**利用者が、Steam SDK を入れるときに対応する**と決めた（いまは書くだけ）。
- **モデルのモーフ**（形状。`SetModelShapeRate` / `GetModelShapeRate`）: PMX の頂点モーフ。DxLib と同じ形状の番号・名前で、割合は 0 以上（1 を超えると、ずれを延ばして足す。DxLib の実測どおり）。Character サンプルに、まばたきとにこり（`--character-expression=<0 から 1>`）。DxLib との画像の一致（IoU 0.96〜0.98）。
- **GPU スキニング（第 1 段）**: Forward の `Draw.Model` と影の深度。骨の行列は、描画ごとの定数（256 個ぶんに、0 で埋めて 12 KB）、頂点ごとの骨と重みは、構造化バッファ。頂点の版は、骨の動きでは変わらない。Character（Release）の `open` が 1.71 → 0.54 ms、Debug の間隔が 3.75〜4.14 → 3.12 ms（下限）。CPU の経路（`--no-gpu-forward`）との画像差は 0。変異テスト 12 件、すべて検出。**検査が見つけた不具合を 1 つ直した**: 骨の影響が無い頂点（FBX の小物）で、節の行列が二重にかかっていた（`RestVertices` を、骨組みの元の位置と法線から作るように）。
- **GPU スキニング（第 2 段）**: 固定した姿勢（`FModelPoseSnapshot`）が、骨と重みのまま保持され、GPU で描かれる。GPU が使えないときは、ビューから CPU で変形する（`CGeometryModelCommands::SkinViewOnCpu`）。Deferred の G-buffer には、モデルを描く経路が無い（固定メッシュだけ）ので、第 2 段は、これで完了。
- **文字の上限**: 1 フレームの新規文字が 512 個 / 32 MiB を超えても、フレームを失敗にしない（以前は、アプリが終了した）。そのフレームは描かず、次のフレームで続きを作る。
- **落としたフレーム**: 描画の内容が失敗したフレーム（`DX12 frame dropped`）で、アプリを終了しない。命令は提出して、表示だけをしない（`CDX12Device::DiscardPendingPresent`）。120 フレーム連続したときだけ、終了する。デバイスの失敗は、すぐ終了のまま。故障の注入: `--dx12-drop-frame=N` / `--dx12-drop-frames-from=N` / `--dx12-drop-frame-every=N`。
- **互換台帳**: `API.IStaticModelSource.GetStaticModelSkinnedDrawView`（Implemented, R5）を足し、`Draw.Model3D` の注記を更新。`UpdateBackendCompatibility.ps1` で `BACKEND_COMPATIBILITY.md` を作り直す。

## 6. 次にやること（優先の順）

1. **頂点数の上限の引き上げ**（調査済み・未着手）: GPU の Forward 経路は、頂点が 65,536 を超えるメッシュを受け付けず、毎フレーム CPU で変形して描く。MMD 系の PMX は 7 万頂点前後が普通なので、実際のモデルで、GPU スキニングの効果が出ない恐れがある。**登録した共有メッシュだけ**、上限を上げる（登録しない描画は、65536 / 196608 のまま）。直す場所は 4 か所を、そろえて:
   - `Library/Platform/DX12/Pipeline/Forward/DX12ForwardMeshRenderer.cpp` の `MaxVertices` / `MaxIndices`（16〜17 行目。`Draw` の入口の検査）。登録用（`1 << 20` 頂点・`6 << 20` 添字）と、登録しない描画用に分ける。
   - `Library/Platform/Render/Geometry/GeometryForwardMesh.cpp` の `CanDrawForward( Model )`（`NumVertices <= 65536`）。
   - `Library/Framework/Render/Thread/AsyncDrawGate.cpp` の `MaxVertices` / `MaxIndices`（7〜8 行目。**登録したバッファの描画の検査（64 行目あたり）にも使っている**ので、登録した描画の分だけ上げる）。
   - `Library/Core/Render/GraphicsPipelineDraw.h` と `ForwardMeshDraw.h` の、範囲のコメント。
   - 検査で、上限を確かめているのは、`ForwardMeshContractMain.cpp` の 506 行目あたり（実行側）と 912 行目あたり（描画部品）。上げたあとの値で、境目（ちょうど上限・1 つ超え）に書き直す。
   - ほかに確かめる: `CDX12BufferStore::CreateBuffer` の大きさの上限、CPU の経路の上限（`DrawModelViewFrom` の 786432、`StaticModelBuilder` の取り込みの 262144 頂点）。7 万頂点以上の PMX（実素材は、リポジトリに無い）を、`Samples/Assets/Models` に置いて、Character サンプルで、GPU と CPU の画像が一致することを実機で確かめる。
2. **群衆の描画の下ごしらえ**（利用者の希望は、R9 のインスタンシングの後の VAT。いまは未着手）: 骨の行列の定数が、骨の数によらず 12 KB（256 個ぶん）で、描画ごとに送られる。個体が多いと、転送が重い。骨の数の段階（64 / 128 / 256）を作るだけで減らせる。インスタンシングは、まだ無い（同じ共有メッシュの個体は、1 つずつ描く）。
3. **R5 の残り**: FBX のブレンドシェイプ（**保留**: DxLib の形状の番号の付け方が分からず、手がかりになる実素材も無い。`bHasBlendShapes` の FBX は、骨組みとして断る）。実素材のアニメーションつき FBX のクリップの確認（素材が無い）。非同期の読み込み（R9）。
4. **ロードマップの順**: R6 の残り（互換台帳の `Partial` の確認。多くは、実素材での DxLib との画素比較）→ R7（高品質描画）→ R7b（ルックデブ。**利用者は「いまは始めない」**）→ R8 → R9（遮蔽カリング・距離のライトのデカール・VAT。「8. 後で入れるもの」）→ R10。
5. **検査の宿題**: GPU スキニングを、DxLib の画像と比べた検査は、まだ無い（DX12 の CPU の経路との比較だけ。CPU の経路は、Character で DxLib と一致を確認済み）。デバッグレイヤーを有効にした確認は、Character と主要なサンプルで通した（落としたフレームの検査に含む）。

## 7. 道具と検査の回し方

- **私用の道具**（利用者の原本の `Docs/AI_WORKFLOW/tools/`。git の管理外）: `build_cfg.ps1 -Config Debug|Release -Target Build|Samples|mazigatiDX`（ビルドと、警告・エラーの数。`LNK1103` などは、1 回だけ、自分でやり直す）、`move_windows_off_main.ps1`（窓を、利用者の主モニターから外す。窓の出る検査の前に、バックグラウンドで）。今回の道具（`skin_compare.ps1` GPU と CPU の画像・時間の比べ、`drop_try.ps1` 落としたフレームの実機確認、`mutate_skin.py` / `mutate_stage2.py` / `mutate_drop.py` 変異テストの枠）も、同じ場所へ写しました（パスは、`framework-stl` 固定）。
- **全体の検査**: `Tools\RunAllChecks.ps1 -SkipBuild -Quick`（`-Filter '*名前*'` で絞れる。複数可）。事前に、Debug / Release のビルドを済ませる。1 つの手順だけ回すときは、`Source\SelfTest\Run*Tests.ps1 -FrameworkRoot <絶対パス> -Configuration Release`（**相対パスは、cwd が戻ると壊れる**）。
- **契約検査（`*Contract.vcxproj`）は、ソースではなく、ビルド済みの `x64\<構成>\mazigatiDX.lib` にリンクする**: ライブラリのソースを直したら、先に `build_cfg.ps1 -Target mazigatiDX` を回さないと、検査に反映されない（変異テストで、全部が「生き残り」に見える原因）。アプリ経由の検査（`-Platform`、`RunDX12SampleImageTests.ps1`）は、ビルド済みの `Samples.exe` を動かすので、`-Target Samples`。
- **変異テスト**: 1 件ごとに、ソースを書き換え → ライブラリ（またはサンプル）を作り直し → 検査 → **検査の出力に `FAIL:` の行があるときだけ「検出」**（`FAIL` が無いのに終了コードが 1 のものは、ビルドや権限の失敗）→ 元のバイト列へ戻す。生き残ったら、検査を足す（今回、3 件の生き残りのうち 1 つで、実際の不具合が見つかった）。最後に、ライブラリを元のソースで作り直す。
- **窓を出さない確認**: `Library/Platform/DX12` の検査は、WARP（`--dx12-warp=true`）でも回る。

## 8. 後で入れるもの（利用者の希望。ROADMAP の「後で入れる負荷削減」に記載済み）

- **遮蔽カリング（R9）**: 視野の外だけでなく、遮蔽物の陰の物も描かない。方式は、**遮蔽カリング（オクルージョンカリング）**と、利用者が決めた（R9 で行うことにも同意済み）。いまは、物ごとの自動カリングは無い（`FFrustum` は、ゲーム側が呼ぶ部品。境界の情報も無い）。R9 で、まず境界を用意して視錐台カリングを入れ、続けて遮蔽カリングを入れる（段階案は、利用者へ出してある）。
- **遠いライトのデカール表現**: デカール（投影）の描画が、まだ無いので、そちらが先。
- **VAT による大量キャラクター（R9、インスタンシングの後）**: 利用者が貼った資料を、ロードマップへ書いた。インスタンシングが先（まだ無い）。
- **Steam Input**: Steam SDK を入れるときに、対応する（利用者の決定）。

## 9. 利用者の方針と、未確認の判断

- **DX12 は、DxLib と同じ見た目にしない**: バックエンドを替えるだけで、全体が格段に綺麗になるのが価値。ただし、公開 API の互換（形・意味・順序・状態・寿命）は保つ。見た目の水準は `ERenderLook`（`--dx12-look=compat|standard`）。**既定を標準見た目へ替えるのは、R7b**（いまはしない）。
- **機能を、レビューの指摘から、勝手に足さない**: 先に提案する（GameFeel の追加を、利用者の依頼で戻した前例がある）。
- **この引き継ぎの時点で、利用者の答えが無い判断**: ①頂点数の上限を、いくつまで上げるか（案は `1 << 20`）、②FBX のブレンドシェイプ（形状の番号を、どう付けるか。DxLib に手がかりが無い）、③群衆の下ごしらえ（骨の行列の定数の段階分け）に、先に着手してよいか。**①は、案の値で進めてよい**（上限は、後から変えやすい定数）。②③は、利用者に確かめてから。

## 10. 書き方の決まり（要約。違反しない）

- **コミット**: 件名 1 行だけ。先頭は `feat:` / `fix:` / `refactor:` / `other:` のみ（`chore` / `test` / `docs` は使わない）。理由・箇条書き・検査の件数は、件名に入れない。「バージョン」と書く（「版」は使わない）。署名の行は、Codex 自身のもの（Claude の署名を、そのまま使わない）。
- **コード**: 波括弧は、常に改行（`if ( ... )` の次の行に `{`。1 行の本体を書かない）。`FORCEINLINE`（普通の `inline` は書かない）。実装は `.cpp`（ヘッダーは、宣言・テンプレート・1 文の取得関数だけ）。識別子は、略さない英語の単語（ローマ字・略語は使わない。`bool` は `b` を付ける。述語は `Can...`）。名前空間は `N` で始める。クラスの名前は、役割を言う（`Manager` は使わない。サブシステムは、役割 + `Subsystem`）。アサートは `CHECK` / `ENSURE` 系（`assert` は使わない）。`#include` は `/` 区切り。日本語は、「ウィンドウ」「ルート」「ツリー」（窓・根・木とは書かない）、「シード」「インデックス」のように、プログラミングの用語は、カタカナ。
- **ファイルの形式**: UTF-8 の BOM + CRLF（`.bat` は CRLF・BOM なし。HLSL は BOM なし。LF のまま残っているファイルは、そのまま保つ）。**改行は、バイト列で確かめる**（テキストモードで読むと、CRLF が隠れる）。`sed -i` は、CRLF を LF にするので使わない。`Source/SelfTest/CheckSourceFormat.ps1`（`source format` の手順）が、確かめる。
- **新しい `.cpp`**: `mazigatiDX.vcxproj` と `.filters`、`Tools/CheckDX12Architecture.ps1`、ライブラリのソースを直接コンパイルする検査の `*.vcxproj` へ足す。
- **層の規則**: ゲームの描画の層（`Library/Platform/Render/Canvas` など）は、`Library/Framework/` と DX12 / Windows を、インクルードしない（`Tools/CheckGameRenderDependencies.ps1` が、確かめる。ログは `LogTemp`）。DxLib の型・インクルードは、`Library/Platform/DxLib` の中だけ。新しい窓口は `Library/Core/Interfaces` に置き、DxLib / Jolt / D3D の型を出さない。
- **検査のリポジトリ**: 自己検査と結果の記録は、本体へ入れない（別のリポジトリ）。検査では、`<thread>` / `<vector>` / `<limits>` を使わない（`FThread` / `TArray`。NaN は `std::nanf("")`）。HLSL は、必ずファイル（C++ の生文字列にしない）: 製品の部品は `EngineContent/Shaders`、サンプルだけは `Samples/Assets/<サンプル>/`、検査だけは `Source/SelfTest`。
- **標準の容器より、フレームワークの型**: `std::` の代わりに、`TArray` / `TMap` / `FString` / `NMath` / `NCString` など。`-0` や NaN の端の値を確かめてから、機械的に置き換える。

## 11. 落とし穴（実際に起きたもの）

- **`powershell -File` を、別のプロセス（Python の `subprocess` など）から呼ぶときは、`-ExecutionPolicy Bypass` を付ける**。付けないと `UnauthorizedAccess` で終了コード 1 になり、変異テストが「検出した」ように見える。
- **bash のヒアドキュメントの中の `\\` は、半分になる**（`\n` が改行になって、C++ の文字列が壊れる）。C++ や HLSL を、スクリプトで直すときは、Write ツールで書いた Python のファイルから、`chr(92)` を使う。Edit ツールは、CRLF のファイルで、複数行の `old_string` が合わないことがある。
- **`RunAllChecks.ps1 -Quick` は、`samples` / `template project` / `package` を飛ばす**（今回、そこで 2 件の、以前からの検査の古さが見つかった: サンプルの説明文の位置、`libusb` のライセンスの数）。節目では、飛ばした手順も回す。
- **窓の出る検査・変異テストのあとに、`mspdbsrv` が残り、次のビルドが `C1041` で失敗することがある**（約 10 分で消える。止める操作は、拒否される）。失敗したビルドの後で検査を走らせると、古い実行ファイルで走る。ビルドの成否を、先に確かめる。
- **プロセスは、名前で止めない**（利用者の Visual Studio・実行中のアプリがある）。自分が起動した PID だけ。
- **ゲームの描画の層が、`Framework/` を読んだ**ために、`DX12 capture contracts` の依存の検査で落ちた（今回）。上の 10 を参照。
- **`D3DCompile` の引数の順**: マクロの配列が 5 番目、インクルードのハンドラーが 6 番目。
- **起動の約 10 秒の停止**: SDL のジョイスティックの初期化で、応答しない HID 機器があると、DirectInput の列挙が止まる。`SDL_JOYSTICK_DIRECTINPUT=0` で避けられる。0.5 秒以上かかると、警告が出る。
- **デバイスの状態**: 提出しないフレームを作ると、`CDX12Device` の記録中の印（`bRecording`）が残り、次のフレームが始められなくなる。今回、提出して、表示だけをやめる形にした（`DiscardPendingPresent`）。この規則を、崩さないこと。
- **テストの音・窓**: 検査の音は、環境変数で守られたものだけ（`COG_AUDIO_TEST` など）。利用者の主モニターへ、窓を出さない。自分の一時ファイル（`%TEMP%\claude_*`、`mazigati_checks_*`）は、片付ける（ディスクが埋まると、無関係な失敗が出る。空きは 80 GB 前後を保つ）。

## 12. この文書の更新

節目ごとに、この文書の「2」（状態）と「6」（次にやること）を更新し、経過は `DX12_HANDOFF.md` の末尾へ足してください。

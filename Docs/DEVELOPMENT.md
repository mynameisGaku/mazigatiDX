# 開発手順

## 構成と依存

`Library/Core`は値・コンテナ・インターフェース、`Library/Framework`はエンジンと資源の管理、`Library/Platform`はDxLibやSDLの実装、`Library/DebugUI`は編集画面、`Source`はアプリを持ちます（自己確認は`Source/SelfTest`へ置く別のリポジトリで、このリポジトリには含めません）。

FrameworkからDxLibやImGuiの実装へ依存を追加せず、`IDrawExecutor`・`IAssetBackend`・入力など既存の窓口を使います。既存の型・所有・更新・終了処理を優先し、局所状態や単純な計算のためのsubsystemは作りません。

物理の空間クエリ（レイ・重なり・形状の掃引）は、`Library/Core/Interfaces/Physics/IPhysicsQuery.h`の`IPhysicsQuery`から呼びます。形状・絞り込み・結果の型は`PhysicsQueryTypes.h`にあり、どちらも物理エンジンのヘッダーを含みません。実装は`CPhysicsSystem`（シーンでは`CPhysicsSubsystem`）で、`GetSubsystem<IPhysicsQuery>()`でも取れます。NaN・無限大・零ベクトル・0以下の距離や大きさは、何にも当たらなかった扱いで結果を空にします。メッシュの裏面は、レイと掃引では無視し、重なりでは数えます。

物理の衝突レイヤーは、`Library/Core/Interfaces/Physics/ICollisionLayers.h`の`ICollisionLayers`で決めます。剛体は`FRigidBodySettings::CollisionLayer`（または`FRigidBody::SetCollisionLayer`）で0〜31のレイヤーに入り、`SetLayerCollision( A, B, false )`のようにレイヤー同士がぶつかるかを表で決めます（初めはすべてぶつかり、表は対称です）。ぶつからない組は衝突判定も接触の通知も起きません。表やレイヤーを変えると、関係する眠っている剛体を起こします。空間クエリは`FPhysicsQueryFilter::LayerMask`（`NCollisionLayer::ToMask`を足し合わせる）でレイヤーを絞れ、衝突の表には左右されません。レイヤーの定数は`PhysicsLayerTypes.h`にあり、`IPhysicsQuery`と同じく物理エンジンのヘッダーを含みません。

物理の関節（拘束）は、`Library/Core/Interfaces/Physics/IPhysicsConstraints.h`の`IPhysicsConstraints`で作ります。`CreateConstraint( BodyA, BodyB, FConstraintSettings::Hinge( 位置, 軸, 最小, 最大 ) )`のように、固定（Fixed）・点（Point）・距離（Distance）・蝶番（Hinge）・スライダー（Slider）・円錐（Cone）をワールド座標で指定します（作った瞬間の姿勢が「ずれ 0」。`BodyB`が`nullptr`なら世界に固定）。作った`FPhysicsConstraint`（`Library/Physics/PhysicsConstraint.h`）で、範囲・ばね・摩擦・モーター（速さ・位置）・有効／無効・現在の角度や位置を操作します。どちらのヘッダーも物理エンジンのヘッダーを含みません。実装は`CPhysicsSystem`（シーンでは`CPhysicsSubsystem`）で、`GetSubsystem<IPhysicsConstraints>()`でも取れます。既定ではつないだ剛体どうしは衝突しません（`bCollideConnected`）。剛体を破棄すると、その関節は外れて`IsAttached()`が`false`になり（入れ物は`DestroyConstraint`まで残る）、設定が不正な関節は作れず`nullptr`を返します。関節は壊れるようにもできます。`FConstraintSettings::BreakForce`（N）と`BreakTorque`（N·m）を設定すると、物理のステップの終わりに、関節が点や向きを保つためにかけている力かトルクがそれを超えたとき、関節は壊れて外れます（`IsBroken()`が`true`、`IsAttached()`が`false`。つないでいた剛体は起こされ、衝突に戻ります）。既定は壊れません。力は点をつなぐ力（Distanceは棒の力）、トルクは向きを保つ力（Fixed・Hinge・Slider・Cone）で、モーター・範囲の端・摩擦の力は含みません。作った直後の数ステップは、実際の荷重より大きな力が出ることがあるので、限界には余裕を持たせてください。壊れたことは`IPhysicsConstraints::AddConstraintListener`で登録した`IPhysicsConstraintListener::OnConstraintBroken( 関節, BodyA, BodyB, FConstraintBreakInfo )`で受け取れ（通知の中で剛体・関節を壊す、受け取り側を登録・解除してもかまいません）、いまかかっている力は`FPhysicsConstraint::GetAppliedForce()` / `GetAppliedTorque()`、限界は`SetBreakLimits`で後から変えられます。眠っている剛体の関節は荷重を測らないので壊れません。

操作するキャラクター（プレイヤーや敵）は、`Library/Core/Interfaces/Physics/ICharacterControllers.h`の`ICharacterControllers`で作ります。`CreateCharacter( 足元の位置, FCharacterSettings )`で縦向きのカプセルを作り（半径・高さ・登れる坂の角度・段差の高さ・床に吸いつく距離・体重・押す力・重力の倍率・衝突レイヤー）、作った`FPhysicsCharacter`（`Library/Physics/PhysicsCharacter.h`）に、毎フレーム`SetMoveVelocity`で歩きたい水平の速さを渡し、`Jump`で跳びます。動かすのは物理の`Update`で、固定ステップごとに、重力・ジャンプ・坂（登れない角度）・段差・床への吸いつき・動く床に乗る・動く剛体を押す・他のキャラクターとぶつかる、までを行います。足元の状態（`GetGroundState`。`OnGround`・`OnSteepGround`・`NotSupported`・`InAir`）、足元の剛体、`SetHeight`（かがむ）、`SetPosition`（瞬間移動）なども使えます。キャラクターは、物理のワールドにキャラクターの形の運動学の剛体（`GetBody`）を持つので、空間クエリや接触の通知（トリガーなど）にも現れます。この剛体は`DestroyBody`では壊せず、`DestroyCharacter`で一緒に壊します。実装は`CPhysicsSystem`（シーンでは`CPhysicsSubsystem`）で、`GetSubsystem<ICharacterControllers>()`でも取れます。どちらのヘッダーも物理エンジンのヘッダーを含みません。

物理のデバッグ描画は、`Library/Core/Interfaces/Physics/IPhysicsDebug.h`の`IPhysicsDebug::DrawDebug( IPhysicsDebugDrawer&, FPhysicsDebugDrawSettings )`で、剛体の形・外接の箱・関節・キャラクターの足元の法線を線にして渡します（読むだけで、描いた線の本数を返します）。渡す先の`IPhysicsDebugDrawer`は線分1本の描画だけを持つ窓口で、画面に描くには`Library/Physics/PhysicsDebugDrawContext.h`の`FPhysicsDebugDrawContext`が`FDrawContext::Line3D`へ橋渡しします。剛体の色は種類（動かない・キネマティック・動いている動的・眠っている動的・センサー・キャラクター）で変わり、`FPhysicsDebugDrawSettings`で種類ごとに描く・描かないを選べ、範囲（中心と半径）を絞れ、円の細かさや三角形の上限、色を変えられます。箱・球・カプセルは輪郭、凸包・三角形メッシュなどは三角形の辺で描きます。実装は`CPhysicsSystem`（シーンでは`CPhysicsSubsystem`）で、`GetSubsystem<IPhysicsDebug>()`でも取れます。

物理の接触の通知（ぶつかった・離れた・センサーに入った）は、`Library/Core/Interfaces/Physics/IPhysicsContacts.h`の`IPhysicsContacts::AddContactListener( IPhysicsContactListener*, FPhysicsContactFilter )`で登録します。受け取る側は`IPhysicsContactListener`を継承し、必要な関数だけ上書きします。たいていは、剛体の組ごとに1回届く`OnBodyContactBegin`（その組の接触が無い状態から有る状態になった）と`OnBodyContactEnd`（すべての接触が無くなった）で足ります。複合形状やメッシュは、1組の剛体でも接触する部位が複数ありますが、組としては1回ずつです。センサー（トリガー）の出入りも同じです。剛体が眠っても（物理エンジンは接触の終わりを知らせますが）End にはせず、触れたまま扱います。接触点ごとの通知（`OnContactAdded` / `OnContactPersisted` / `OnContactRemoved`）も届きます。接触の情報`FContactInfo`には、接触点・法線（AからBへ）・めり込み量に加えて、ぶつかる直前の法線方向の近づく速さ`ClosingSpeed`（衝撃音の大きさやダメージに使えます）があります。`FPhysicsContactFilter`で、特定の剛体が絡む通知だけ（`Body`）、どちらかの剛体のレイヤーが含まれる通知だけ（`LayerMask`）、毎ステップ届く継続の通知は要らない（`bPersisted`）を選べます。通知は物理の`Update`の中で固定ステップごとにメインスレッドから呼ばれるので、中で剛体を壊したり、受け取り側を登録・解除したりしてかまいません（壊した剛体は以降の通知に出ず、解除したものには以降届きません）。受け取り側は、破棄する前に`RemoveContactListener`で外してください。実装は`CPhysicsSystem`（シーンでは`CPhysicsSubsystem`）で、`GetSubsystem<IPhysicsContacts>()`でも取れます。どちらのヘッダーも物理エンジンのヘッダーを含みません。（以前の`CContactListener`は`IPhysicsContactListener`に置き換わりました。関数の形は同じです。）

帯と雷のUVディストーションは、既存の`Material.Distortion`を共有します。`FDrawDistortedMaterialRibbonData`を`FDrawContext::DistortedMaterialRibbon3D`で記録し、`IDrawExecutor::SupportsEffectRibbonUVDistortion()`で対応を問い合わせます。未対応実装の既定はfalseです。可変長命令の実行側は、固定部・点数・全長を検査してから点列を読みます。画像の取得と準備状態の固定は再生開始時に行い、描画の記録中には読み込みません。

メッシュ粒子は`FEffectMeshDefinition`と、再生開始時に作る`FEffectMeshGeometry`を使います。`FDrawContext::MaterialMesh3D`が96バイトの設定、ワールド座標の`FMeshVertex`、32bitの三角形indexを命令へ複写します。上限は1024頂点・3072 indexで、全長と全要素を検査してから追記します。描画時に呼出側のWorld行列を重ねません。`IDrawExecutor::SupportsEffectMaterialMesh()`の既定はfalseです。材質無効時も同じメッシュ形状を自己発光1・消失なし・UV移動なしで描きます。現在は照明を使わず、半透明・加算合成に対応します。GPU資源は既存の材質描画実装が所有します。

編集メッシュの材質効果は`FEditMeshMaterial::Effect`の`FMeshMaterialEffectDefinition`を使い、既存の`FMeshRenderer`が材質ごとの再生時計を所有します。画像と準備状態、分割済みの形状は生成時に固定して共有し、時計はコピーごとに独立します。`Update`で時間を進め、`Record`は状態を更新せず用意した資源だけを読みます。必要な画像がない効果はその材質全体を省略します。既定の効果は従来のJSONとハッシュに含めず、停止・無効時は通常のMesh3D命令へ戻します。

`MaterialMesh3D`の末尾引数`bUseRegularCulling`は既定falseで、従来の呼出元の描画状態を保ちます。編集メッシュの材質効果だけtrueを指定し、`UseRegularCulling` flagで通常メッシュの裏面除去設定を使います。命令番号・96バイトの設定形式は維持し、両面指定はどちらの方法でも裏面を描きます。GPUの状態は描画後に復帰します。

静的モデルの面データは、`IAssetBackend::GetStaticModelSurface`から位置・法線・UV0・頂点色・三角形・連続した材質範囲を取得します。位置はフレーム階層の変換を含み、モデル個体の配置変換を含みません。材質は拡散色とモデルが所有する借用画像ハンドルです。非対応・失敗時はfalseを返し、出力を変更しません。DxLib実装はアニメーション・形状変形・スキニングのない通常の静的モデルを対象にし（法線画像付きの頂点形式も現段階では対象外）、一時的なモデルの複製から抽出します。

`CModel::CreateStaticSurfaceSnapshot()`はメインスレッドで呼び、不変の`FStaticModelSurface`を返します。取得した値と元の`CModel`を共有所有するため、呼び出し側がモデルの参照を手放しても借用画像を使えます。画像を個別に削除したり、所有する`CTexture`に包んだりしません。利用中は元モデルとbackendの寿命を保ちます。頂点色は参照APIが返す値で、モデル形式によって材質色を含みます。保存した材質の拡散色を再び掛けません。現在の取得範囲は拡散材質までで、元の照明・透過判定・画像座標の変換・変形アニメーションの描画再現は含みません。モデル個体への材質効果は、以下の実行APIで準備します。

静的モデルの選択材質には、`FModelAnimator::PrepareMaterialEffect`で材質番号、既存の`FMeshMaterialEffectDefinition`、画像の読み込み関数、`IDrawExecutor`を渡します。一つのAnimatorに一つの材質を指定でき、`GetMaterialEffect()`から再生・一時停止・停止・再開を制御します。材質の時計はアニメーションの一時停止から独立し、既存のAnimator更新で一度進みます。個体ごとに`CModel::CreateInstance()`を呼んでください。rendererのコピーは不変資源を共有し、時計を値としてコピーします。Animatorをコピーした場合のモデル個体の共有は従来どおりです。

形状は準備時に`IAssetBackend::CreateModelMaterialGeometry`で分割保存し、`ModelMaterial3D`命令へ個体の配置行列12要素、形状handle、材質値、不透明度だけを固定長で記録します。設定部分は160バイト、命令全体は164バイトです。既存の命令番号と形式は維持します。`IDrawExecutor::SupportsModelMaterialEffects()`の既定はfalseです。準備失敗・非対応・停止・元モデルの不一致・アニメーションを付けた個体は従来のモデル描画を使います。効果再生中の非有限行列は記録を抑止します。記録中には画像の読み込みや準備状態の問い合わせを行いません。形状の解放は既存のAsset serviceを使い、別スレッドからの解放はメインスレッドまで延期します。

ワールドのModelには、任意の`FWorldModelMaterialEffect`を`FWorldModel::MaterialEffect`へ保存できます。材質番号、既存の`FMeshMaterialEffectDefinition`、両面指定を持ち、JSONでは`Model.MaterialEffect`の`MaterialIndex`・`Effect`・`TwoSided`になります。未設定なら従来のModel JSONを変えず、設定済みで効果を無効にした場合も編集値を保持します。型違いや整数の範囲外は不正状態として保存・再読み込み後も残し、`ValidateWorld`が対象オブジェクトと項目を報告します。モデルの実際の材質数や静的形状の対応は、配置時の準備で確認します。

`NWorld::Spawn`は有効な設定だけを既存の同期アセット読み込みと`FModelAnimator`へ渡します。`CScene::GetDrawExecutor()`はシーンの差し替え先を優先し、未設定ならアプリの描画先を借用します。非対応の描画先、アニメーションを結び付けた個体、画像・形状の準備失敗では、材質番号と原因を警告して通常のモデル描画を残します。停止時も通常描画へ戻り、別個体の時計や表示へ影響しません。保存したワールドをゲームで使う経路に対応し、編集画面では以下の個体別プレビューを使います。モデル用Inspectorでの設定と再生操作は後述します。

ワールドのモデルプレビューは`FWorldEditorState::ModelPreviews`が所有します。`FWorldEditorModelPreviewKey::ObjectIds`は、最上位のPrefab配置から入れ子の配置を経てモデルへ至るIDの全列です。通常のモデルは自分のIDだけを持ちます。キーは列の長さと全要素で比較し、同じPrefab内の同じIDでも配置元が違えば別の個体と時計になります。共有する`FAssetEditorInfo`のモデル個体は変更せず、元モデルから作った専用の`CModelInstance`と`FModelAnimator`を各配置が保持します。

`UpdateModelMaterialPreviews`はメインスレッドで配置を巡回し、画像・形状の準備と再生時間の更新を記録前に済ませます。設定、元モデル、読み込み完了状態、描画先と対応状態が変わったときだけ再構築し、失敗も同じ入力で繰り返し準備しません。設定の比較はハッシュだけで判断せず、不正な値も安定して比較します。`FAssetEditorInfo::GetModelSource`は既存キャッシュの元モデルを読み込み途中でも返し、失敗時は空を返します。元モデルが準備できたら専用個体の準備を再開できます。

`RecordModelObject`は準備済みの個体だけを読み、読み込みや再準備を行いません。停止・効果の無効化・非対応・効果の準備失敗では同じ個体の通常描画を使い、二重には記録しません。アニメーション集の補正、クリップと速度もゲーム配置と同じ順序で設定します。アニメーションが結び付いた個体の材質効果は未対応で、通常のアニメーション描画へ戻ります。非表示・削除された配置は更新時に解放し、ワールドの切り替え・新規作成・アセットの再読込・終了でもキャッシュより先に専用個体を解放します。

World EditorのModel Inspectorでは、任意の材質効果の追加・削除、材質番号、両面指定と、Meshと共通の材質パラメータ・曲線を編集します。追加直後の効果は無効です。無効中も編集値を保ち、`Reset binding`は型不正の印も含めて設定全体を初期化します。保存値の変更は既存のWorld文書とUndo／Redoへ渡し、連続入力は配置ごとの編集キーでまとめます。

材質一覧は枠番号、拡散色、元画像の有無、使うsection数を表示します。範囲外の番号は保存値を保持して問題を示します。`FAssetEditorInfo::PrepareModelSurface(Path)`はInspectorより前にメインスレッドで呼び、読み込み完了後の静的面取得を成功・失敗とも一度だけ試します。`FindPreparedModelSurface(Path)`は用意済みの値だけを読み、読み込みや準備状態の問い合わせをしません。`Clear()`で取得状態を破棄します。snapshotは元モデルを保持し、借用画像の寿命を支えます。

`Placement preview transport`は選択した配置の専用Animatorだけを操作します。Play／Pause／Stop／Restartは保存値や編集履歴を変えません。最新のModel設定と準備済みの値が異なる間は古い個体の操作を受け付けず、次の記録前更新を待ちます。準備待ち・非対応・失敗理由は同じ欄に表示します。

DxLibでは個体の選択meshだけを一時非表示にし、残りを通常のモデルとして描きます。まとめ描画を確定してから可視状態を戻し、元のローカル形状へ配置行列を一度掛けて選択材質を描きます。元の拡散色を頂点色へ二重適用せず、不透明度は頂点alphaへ一度掛けます。効果は照明を使わないLDRのAlpha/Addで、消失・自己発光・UV移動・ゆがみを使えます。元の材質の照明・画像変換・透過判定の完全再現は含みません。通常部分の描画順を保ちますが、効果面はその後に描くため、通常の半透明面との完全な相対順序は保証しません。変形するモデル、HDRとブルームは後続の範囲です。

準備・非対応・事前検査の失敗は通常モデルへ戻します。GPU描画の途中に失敗した場合は、描けなかった分割形状を省略して警告を一度記録します。成功した形状へ重ね描きする全体の再描画は行いません。backendの終了は、利用中のモデル・画像・効果資源を片付けてから行ってください。確認用の`CAssetManager::SetBackendOverride`は、既存のモデル・画像・効果資源と遅延解放を全て片付けてから切り替えます。

シーンの`OnDraw`、ゲームインスタンスの`OnDraw`、オブジェクトの`Draw()`など、描画システムの外から描くときは、DxLibを直接呼ばず`Library/Framework/Render/ImmediateDraw.h`の`FImmediateDraw`を使います。`FImmediateDraw Draw( GetDrawExecutor() );`と書くと、`FDrawContext`と同じ関数（`Box / Circle / Line / Text / Sprite`など）で描け、`Flush()`かスコープを抜けるときに、記録した順に`IDrawExecutor`で実行します。実行はメインスレッドで、描いたものがなければ実行先を呼びません。実行先が`nullptr`なら、描いたものは捨てます。

組み込みのデバッグ表示は、`Library/DebugUI/DebugOverlay.h`の`CDebugOverlay`です。ゲームインスタンスが持ち、`Init() / Tick() / Draw( *this ) / Term()`を呼ぶと、F1の窓（FPS・入力・シーン・ログ）、F2のプロファイラー、F3のコンソール（Debugだけ）、F11のフルスクリーンが使えます。`OnDrawPanelItems`と`OnDrawWindows`に関数を渡すと、ゲーム側の項目や窓を同じ画面に並べられます。キーの処理は`HandleKeys`に切り出してあり、偽の入力・プロファイラー・プラットフォームで確かめられます。

## 粒子の力場

`FParticleEmitterDefinition::ForceFields`は、既存の粒子計算へ加えるWorld空間の力場を保持します。`FEffectForceFieldDefinition`は値の保存・検証・加速度の計算だけを持ち、描画やPhysicsへ依存しません。再生・停止・寿命は既存の`FParticleSimulation`と`IEffectPlayback`を使います。Effect Editorの`Force fields`から値を編集でき、`Force field wind`・`Gravity well`・`Whirlwind`・`Gust`を未保存のコピーとして開けます。

各要素は`Enabled`、`Kind`、`Acceleration`、`Position`、`Radius`、`Strength`を保存します。`Uniform`は`Acceleration`をそのままWorld加速度として加えます。`Radial`は`Position`を中心とし、`Strength`が正なら外へ、負なら中心へ加速します。中心から距離rの点で、加速度の強さは`Strength * (1 - r / Radius)`です。中心ちょうどと半径以上では作用しません。乱数を使わず、重力と合成して速度、抵抗、位置の順に更新します。生成停止後の残粒子にも作用し、即時停止は従来どおり粒子を取り除きます。

Local粒子は、現在のエフェクトの回転と移動からWorld位置を求めて評価し、加速度の向きだけをLocalへ戻します。生成時の拡大率を、場の半径や加速度へ再び掛けません。再生速度は従来どおり経過時間に掛かります。計算が粒子の速度や位置を有限範囲から押し出す場合、その刻みの力場の寄与を省き、従来の重力計算を使います。

1エミッターの上限は16場です。各ベクトル成分と強さの絶対値は1,000,000以下、半径は0より大きく1,000,000以下の有限値を使います。無効中や現在の種類が使わない値も検証します。型違い、未知の種類、非配列・非オブジェクトの入力は不正状態を保持し、保存して読み直しても検証エラーに残します。配列の上限を超えた値は捨てずに検証エラーとし、計算は先頭16件に限ります。空の配列はJSONとハッシュへ追加せず、従来の保存値、生成乱数、運動を保ちます。

`Vortex`は`Position`を中心に`Axis`（長さは使わず正規化）の周りを回る接線方向の加速度で、強さは`Strength * (1 - r / Radius)`（rは球状の距離）です。中心と回転軸上、半径以上では作用しません。`Turbulence`は`Frequency`（rad/m）と`ScrollSpeed`（rad/s）から、3軸が独立に変わる決定論的な波`(sin(py)+cos(pz), sin(pz)+cos(px), sin(px)+cos(py))`を求め、同じ線形減衰を掛けます。中心でも作用し、乱数は使いません。`FollowEmitter`が`true`の場は、エフェクトの移動と回転（拡大率は使わない）に中心・向きが追従し、`false`（既定）はWorld固定です。旧JSONで省略された項目は既定値で読みます。再生中の`Simulation.Time`が乱流の時間変化へ渡ります。

編集画面では追加・複製・削除・並べ替えと全体の`Reset`を使えます。無効中や種類で使わない値も編集・保持します。16件以上では追加と複製を止めますが、既存の超過分は残して削除できます。型違い・非有限値などがある場は数値入力を隠し、理由を表示します。`Reset`は不正の印を含めた場全体を既定値へ置き換えます。変更は既存の文書履歴へ入り、Undo／Redoできます。選択行と範囲線の表示切替は文書を変更しません。

`Show force field guides`は選択した場のWorld中心・半径・作用方向を表示します。開いた直後と`Reset camera`時には、全エミッターの形状・寿命中の移動・力場の範囲線を一度だけ視界へ合わせ、その後は手動視点を保持します。無効・過大な範囲を安全に収められない場合は元の視点を保ちます。`Move emitter preview`による円運動は自動範囲に含みません。力場付きの帯のPresetでは自動の発生元移動を使わず、旧来の力場なしの帯では従来の表示を保ちます。

`Kind`は`Uniform / Radial / Vortex / Turbulence`の4択で、`FollowEmitter`の有無、`Axis`（3成分の非ゼロベクトル）、`Frequency`（rad/m、下限0.001）、`ScrollSpeed`（rad/s、符号付き）を編集できます。使わない種類の値も編集・保持されます。`Show force field guides`では、`Vortex`は三円＋回転軸矢印＋周回矢印（Strengthの符号で反転）、`Turbulence`は三円＋3軸矢印を記録します。`FollowEmitter`が有効な場は、プレビューのエミッター姿勢（円運動）へ中心と向きが追従して描かれます。カメラの自動視点（`FocusCameraOnForceFields`）は、VortexとTurbulenceの球範囲と矢印長さ、およびTurbulenceの最大加速度（2*sqrt(3)*|Strength|）を正しく含めます。`Whirlwind`（Vortex）と`Gust`（Turbulence）の組み込みPresetをPresetメニューに追加し、原本保持・別名保存・World配置・通常再生まで繋げました。

範囲線は`FDrawContext`へ記録し、既存の`IDrawExecutor`で実行します。`FEffectPreviewSurface::Render`の5引数版は、合計65,535ワード以内の追加の`Line3D`／wire `Sphere3D`命令を呼出中だけ借りて格子・粒子の後へ重ねます。他の命令や壊れた並びは失敗として返し、従来の4引数版は追加命令なしの経路を使います。範囲線の記録は粒子の時計・保存値・アセット資源を変更しません。

Presetは原本を保ったまま編集し、`File > Save as...`で別名保存します。保存した`.effect.json`はWorld Editorのアセット一覧から配置し、Worldへ保存して通常の再生に使えます。`FollowEmitter`を切った場はWorld固定、入れた場は配置の移動と回転に追従します。2D専用操作・HDRとブルームは後続の範囲です。

## 雷の動的終点接続

`FEffectPathDefinition::bUseTarget`（JSONでは`Path.UseTarget`）を有効にした`Lightning`と`TreeLightning`は、再生側から動的ワールド終点が指定されている間、固定オフセット`Path.End`の代わりにそのワールド座標を終点として主幹と枝を生成します。既定の`false`ではJSONキーとハッシュ混合を省略し、既存の全`.effect.json`および既定ハッシュを維持します。`UseTarget`が真偽値でない場合は`null`として往復保持して検証エラーにし、記録も抑止します。`Path.End`は動的終点未指定時のフォールバックとして常に検証します。

`IEffectPlayback`には既定の空実装を持つ`SetTargetPosition(const FVector3&)`と`ClearTargetPosition()`を追加し、`FParticlePlayback`が`FParticleSimulation`へ反映します。動的終点はすでにワールド座標であるため、再生姿勢の回転や拡大率を二重に掛けません。非有限の終点座標が渡された場合は`Path.End`へ誤フォールバックせず、そのエミッターの雷記録を抑止します。同一エフェクト内で`bUseTarget`が有効なエミッターと無効なエミッターを混在させられます。

`CEffectComponent`は直接ワールド座標を指定する`SetTargetPosition`、別オブジェクトの`CTransformComponent`へローカル空間オフセット付きで追従する`SetTargetObject`（`TWeakPtr<CGameObject>`で保持し寿命を延ばさない）、指定を解除する`ClearTarget`、現在有効な終点を取得する`TryGetTargetPosition`を持ちます。`Play()`直後と毎フレームの`Tick()`更新前に再生へ同期し、追従先オブジェクトが破棄・未生存・変換なしになったフレームでは自動的に`ClearTargetPosition()`へ戻します。

## 雷の動的終点の制作UIとWorld接続

Effect Editorの`Path`欄（`Lightning` / `TreeLightning`）に`Use target`を追加しました。保存値`Path.UseTarget`としてUndo / Redoに対応し、型不正の値は表示してResetで復旧できます。試し再生の終点は文書へ保存しない編集用の状態で、有効化のチェック・ワールド座標の数値入力・プレビュー画面のクリック（水平面への配置）と移動ギズモ・発生位置から終点への接続線と三軸の目印（Target guides）・終点を含む範囲へのカメラ合わせ（Focus target）を持ちます。終点を使う雷の判定は選択中のエミッターに関係なく文書全体を見るので、板のエミッターを選んでいても雷は終点へ向かいます。動的終点つきの雷を持つ文書（組み込みPresetを含む）を開くと試し再生の終点が自動で有効になり、終点を含む範囲へカメラを一度だけ合わせます。

Worldの`Components.Effect`には、任意の`Target`（追従先オブジェクトのId）と`TargetOffset`（対象のローカル空間でのずれ）を追加しました。既定値ではどちらのJSONキーも書かず、既存の`.world`と同じ形を保ちます。型が壊れた値は`Target = -1`・有限でない`TargetOffset`として保持して検証エラーにし、自己参照・存在しないId・有限でないずれを検証で報告します。複製・貼り付けでは、一緒に複製された対象を指すようにIdを付け替え、複製されていない対象は元のまま残します（Objectプロパティと同じ扱い）。配置（`NWorld::Spawn`）は全オブジェクトを置いてから`CEffectComponent::SetTargetObject`へ接続するため、並び順に依存しません。自分自身・置かれなかった対象・有限でないずれは接続せず、警告して定義側の`Path.End`のままにします。World Editorのプレビューは対象のワールド姿勢と`TargetOffset`から毎フレーム終点を渡し、Inspectorの`Effect`欄で対象オブジェクトとオフセットをUndo付きで編集できます。

組み込みPreset`Effects/target_lightning.effect.json`（Presetメニューの`Target lightning`）は、動的終点を使う分岐雷です。複製して保存し、Worldの`Effect`部品の`Target`へ標的オブジェクトを指定すると、発射元から標的へ雷がつながります。

## コードとコメント

- 公開する主要な型は原則一つのヘッダーに置き、実装は同名cppへ分けます。templateの実装は同名ヘッダーまたはinlに置きます。
- 新しい型の役割は`A`（所有される多態オブジェクト）、`F`（値・ハンドル・サービス）、`T`（template）、`E`（enum）、`I`（インターフェース）で示します。既存の型は無関係な変更で一括改名しません。
- ヘッダー冒頭は`// SPDX-License-Identifier: MIT`と`#pragma once`を置き、長い使い方は文書へ分けます。
- 型や関数の宣言直前に、日本語で役割・入力・失敗条件を記述します。複数の引数を説明するときは引数ごとに改行します。
- field、変数、定数、列挙値にも、その場所の役割が分かる短い日本語コメントを付けます。専門用語が必要な場合は短く補足します。
- 関数・制御文・cast・macro・constructor・初期化子の括弧内部は一行に保ち、既存の波括弧の形式を維持します。途中のコメントで式を分断しません。
- 自作のh・cpp・inl・ps1はUTF-8 BOM一つ、CRLF、NULなしとします。第三者のソースとライセンスは元の形式を保ちます。
- 標準ライブラリより先に、フレームワークの機能を使います。数値の比較・範囲・有限判定は`Library/Core/Types/Numeric.h`（`NMath::Abs / Min / Max / Clamp / IsFinite / IsNaN / IsInfinite`と`TNumericLimits<T>`）、右辺値の受け渡しは`Library/Core/Types/Utility.h`（`MoveTemp / Forward`）にあります。浮動小数点の`NMath::Abs`は`std::fabs`と同じく符号ビットを落とし、`Min / Max`は`std::min / std::max`と同じ向きで値を返します。同等のものが無いときだけ`std::`を理由付きで使い、第三者のコードは対象外です。
- 機能、公開API、ソースの配置を変えた場合は、関連する文書と参照も更新します。

## ソリューションの構成

`project.sln`は、三つのプロジェクトからなります。

|プロジェクト|種類|中身|
|---|---|---|
|`mazigatiDX`|静的ライブラリ|`Library`（フレームワーク本体）と`VendorModules`（第三者のライブラリ）|
|`project`|実行ファイル|`Source`（このリポジトリのゲーム。検査を置いたときはその分も）|
|`Samples`|実行ファイル|`Samples/Source`（機能別のサンプル集。下の「サンプル集」）|

- フレームワークを使うプロジェクトは、`Build/mazigatiDX.props`を取り込み、`mazigatiDX`への参照を持ちます。propsが、インクルード検索先（`#include "Library/..."`）、C++17、DxLibとSDL3のライブラリ検索先、`main`で始められる入口の設定、`SDL3.dll`のコピーを引き受けるので、ゲーム側の`.vcxproj`は短く済みます。
- 入口は`main`です。`Source/main.cpp`が`NGameMain::RunGame<CGameApplication>()`を呼ぶだけで、リーク報告、ログ（コンソールとファイル）、窓の設定（`Config/ScreenConfig.json`）、終了時の後片付けは、`Library/Framework/Engine/GameMain.h`が担当します。
- サブシステムの登録（`REGISTER_SUBSYSTEM`）とコンソールコマンドの登録は、静的な初期化で行います。静的ライブラリの`.obj`は参照されないとリンクされず登録が消えるので、実行ファイルはライブラリ全体を取り込んでリンクします（propsが`/WHOLEARCHIVE:mazigatiDX.lib`を設定）。
- 各`.vcxproj`の`<MazigatiSourceRoots>`に書いたフォルダーの下のファイルを、`GenerateProjectFiles.bat`が`ClCompile` / `ClInclude`と`.filters`へ書き出します。プロジェクトを足すときも、`<MazigatiSourceRoots>`を書けば再生成の対象になります。
- 中間ファイルは、プロジェクトごとのフォルダー（`project/`、`mazigatiDX/`）に出ます。出力は`x64/Debug`と`x64/Release`です。
- 静的ライブラリは、デバッグ情報を`.obj`の中に持ちます（`/Z7`）。`.pdb`を指す形（`/Zi`）だと、Releaseのリンクで`LNK4020`（型レコードが壊れている）が出るためです。実行ファイルのリンクが、全部を1つの`.pdb`にまとめます。

## 新しいゲームを始める

`Tools/NewProject.ps1`が、`Templates/Minimal`から新しいゲームのプロジェクトを作ります。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewProject.ps1 -Name MyGame -Destination C:\dev
```

`C:\dev\MyGame`に、ソリューション・プロジェクト・`Source`（入口・ゲームインスタンス・タイトルとプレイのシーン・プレイヤー）・`Config/ScreenConfig.json`・`Assets`・`README.md`ができます。プロジェクトは、フレームワークのフォルダーを相対パスで指し（`.vcxproj`の`<MazigatiDXRoot>`と`.sln`）、`Build/mazigatiDX.props`と静的ライブラリ`mazigatiDX`を使います。DxLibは`C:\DxLib`を探します（別の場所なら、環境変数`MazigatiDxLibDir`と`MazigatiDxLibVcDir`）。ファイルを足したり消したりしたら、新しいプロジェクトの`GenerateProjectFiles.bat`で、ファイル一覧を作り直します。テンプレートの確認（作る・ビルド・起動・画面の確認）は、検査側の`RunTemplateTest.ps1`が行います。

## サンプル集

`Samples`は、機能を1つずつ動かして見られるサンプル集です。起動すると一覧が出て、上下キーで選び、Enterで動かし、サンプルの中からEscで一覧へ戻ります。`--sample=Hello`のように名前を付けて起動すると、一覧を飛ばしてそのサンプルから始まります（`NCommandLine::GetValue( "sample" )`で読みます）。

- **1サンプル = 1フォルダー**（`Samples/Source/<名前>/`）。中身は、シーン1つ（`CSampleScene`を継承）と、そのシーンが置くオブジェクトのクラスで、**1ファイルに1クラス**です。シーンは何をどこに置くかだけを決め、描き方・動き方はオブジェクトが持ちます。
- サンプルは、自分の`.cpp`の中で`TSampleRegistrar<T>`を静的変数として置いて、自分を`CSampleCatalog`へ登録します（名前・一覧に出す題・説明・並び順）。サンプルを足しても、アプリケーション（`Application/`）と一覧の画面（`Browser/`）は書き換えません。
- 共通の部分は`Catalog/`にあります。`CSampleScene`は、Escで一覧へ戻る処理と見出しを持ち、サンプルは`OnTick / OnDraw`の代わりに`OnSampleTick / OnSampleDraw`を書きます。
- サンプルを足すときは、`Tools/NewSample.ps1 -Name Physics -Title "..." -Description "..."`を使います。`Templates/Sample`から`Samples/Source/Physics/`にシーンのクラスを作り、プロジェクトのファイル一覧を更新します（並び順は、指定しなければ最後）。
- 確認は、検査側の`RunSamplesTest.ps1`が行います（Debug / Releaseのビルドで警告0、一覧と各サンプルの起動と画面の画素、知らない名前、`NewSample.ps1`で足して消す）。一覧の上下キー・Enter・Escの操作は、入力を差し込む仕組みがまだないので、自動では確かめていません。

## プロジェクト生成と確認

cpp・header・inlを追加・削除・移動したら、次でプロジェクトを再生成します（本体は`Tools/GenerateProjectFiles.ps1`で、`-Root`にプロジェクトのフォルダーを渡します。ルートの`.bat`はそれを呼びます）。

```powershell
cmd /c "GenerateProjectFiles.bat < nul"
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
```

自己確認（ecs・assets・world）はこのリポジトリに含めず、別のリポジトリ`framework-test`で管理します。ルートの`Source\SelfTest`へ置く（ジャンクション可）と、`GenerateProjectFiles.bat`がワイルドカードで取り込み、検査側が静的な登録で起動時の窓口（`Source/Application/StartupHook.h`）へ自分を差し込んで、`--selftest=ecs|assets|world`のシーンを登録します。`GameApplication`は有無を知らないので、置かないときは何も組み込まれず通常どおりビルドでき、足し引きしても再コンパイルの要否に影響しません。ソリューションエクスプローラーでは、`GenerateProjectFiles.bat`を実行したときに存在する検査ファイルが、`Source\SelfTest`のフォルダーにまとまります（Visual Studioは、ワイルドカード項目のフィルターを使わないため、`.filters`にはファイルを1つずつ書きます。後から足した検査は、再実行するまでプロジェクト直下に見えますが、ビルドには入ります）。

```powershell
.\Source\SelfTest\RunSelfTests.ps1 -Suite assets -Configuration Debug
.\Source\SelfTest\RunSelfTests.ps1 -Suite world -Configuration Debug
.\Source\SelfTest\RunSelfTests.ps1 -Suite ecs -Configuration Debug
```

両構成で実行し、ecsはワーカー0／1／4／15の結果を照合します。PowerShellから`-WorkerCounts`へ配列を渡す場合は、`-File`の文字列引数にせず、PowerShell内で呼び出します。項目数は検査を足すたびに増えるため、数値だけで合格とせず、失敗・スキップ・正常終了を確認します。Debugはリーク報告も調べます。描画の変更は実GPUの既知画素、状態復元、資源の解放を確認します。

## 変更の統合

`main`は安定版、`dev`は統合版です。作業は`Effect`・`Asset`・`Core`など責務に応じたブランチへ分けます。機能ブランチで実装と関連する確認を揃えてから`dev`へ統合し、安定版の確認を終えた区切りを`main`へ反映します。

コミットは変更のまとまりごとに分け、`feat`・`fix`・`refactor`・`other`と、原因や変化が分かる日本語の説明を使います。push前にはremoteとの差分と追跡ファイルを確認し、履歴の強制的な書き換えは行いません。

追跡するものは本体・必要なアセット・設定・ビルド手順・プロジェクト文書です。実行結果、個人のIDE設定、ローカルの作業記録、認証情報、一時テスト資料は含めません。第三者ライブラリの使用に必要なファイルとライセンスは保持します。

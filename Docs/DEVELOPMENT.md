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

オブジェクトの描画は、DX12のコマンドリストと同じ方式です。`CGameObject::Draw( FDrawContext& CommandList )`は、シーンが用意したコマンドリストに、`CommandList.Box(...)`などで「何を描くか」を記録するだけで、DxLibも実行先（`IDrawExecutor`）も知りません。シーンはオブジェクトごとにコマンドリストを空にして渡し（Reset）、記録されたものを描画順（`DrawOrder`）に実行する（ExecuteCommandLists）ので、ブレンドモードなどの状態は次のオブジェクトに漏れません。何も記録しなかったオブジェクトは、実行先を呼びません。

シーンの`OnDraw`、ゲームインスタンスの`OnDraw`など、オブジェクトの外から描くときは、DxLibを直接呼ばず`Library/Framework/Render/ImmediateDraw.h`の`FImmediateDraw`を使います。`FImmediateDraw Draw( GetDrawExecutor() );`と書くと、`FDrawContext`と同じ関数（`Box / Circle / Line / Text / Sprite`など）で描け、`Flush()`かスコープを抜けるときに、記録した順に`IDrawExecutor`で実行します。実行はメインスレッドで、描いたものがなければ実行先を呼びません。実行先が`nullptr`なら、描いたものは捨てます。

2Dの図形は、DxLibの関数と同じ形で書けます。整数の座標の`Pixel / Triangle / Quad / Oval / RoundBox`（と`Box / Circle / Line`）は`DrawPixel / DrawTriangle / DrawQuadrangle / DrawOval / DrawRoundRect`などに対応し、画素ちょうどで描きます。ゆっくり動くものや傾いた図形には、アンチエイリアス付きで`float32`の座標を取る`LineAA / BoxAA / CircleAA / OvalAA / TriangleAA / QuadAA / RoundBoxAA`を使います（`DrawLineAA`などに対応。DxLibの仕様で縁が半径の外へ約0.5ピクセルにじむので、円と楕円は整数版と同じ大きさに見えます）。円・楕円・角の丸みの`NumSegments`は何角形で近似するかで、0（既定）なら半径（楕円と角の丸みは大きいほうの半径）から、誤差が1/4ピクセル以下になる4の倍数（12〜256）を決めます。NaN・無限大・絶対値が1e7を超える座標・負の半径・0以下の太さは記録しません。見本は`Samples`の`Shapes`です（`Samples.exe --sample=Shapes`）。

2Dの画像は、`CommandList.Image( X, Y, Texture )`のように、画像（`TSharedPtr<CTexture>`・ポインター・参照のどれでもよい）を渡して描きます。画像は`Library/Core/Interfaces/Render/IImage.h`の`IImage`（`CTexture`が実装）か、型付きのハンドル`FImageHandle`で渡し、DxLibのハンドル（`int`）は公開しません。`nullptr`や無効な画像は、何も記録しません。引数の順番はDxLibに合わせてあります。`Image`（`DrawGraphF`。`bFlipX / bFlipY`で反転）、`ImageStretch`（`DrawExtendGraphF`。`X0 > X1`で反転）、`ImageQuad`（`DrawModiGraphF`。画像の左上・右上・右下・左下の行き先）、`ImagePart / ImagePartStretch / ImagePartQuad`（`DrawRectGraphF`などの、画像の一部分版）、`SpritePivot`（`DrawRotaGraph3F`。基準点・縦横別の拡大・回転・反転。反転しても基準点は動かない）、`Tile`（`DrawTile`。画像の左上を矩形の左上に合わせ、`OffsetX / OffsetY`だけ右下へずらす。DxLibの`DrawTile`は拡大した画像の中心を矩形の中心に置くので、実行のときに補正しています）、`CircleGauge`（`DrawCircleGaugeF`。真上から時計回りに`Fraction`（0〜1）だけ描く。描き始めの位置は`StartFraction`で、1を超えると先頭へ回り込む。DxLibのゲージは、画像の中心を中心とする円を描きます）。NaN・無限大・絶対値が1e7を超える座標・切り出す矩形の大きさが0以下・拡大率が0以下などは記録しません。見本は`Samples`の`Images`です（`Samples.exe --sample=Images`）。

描画の状態は、`CommandList.Blend( EDrawBlend::Add )`のように、記録して変えます。状態は、そのオブジェクトの`Draw`（1回の実行）が終わると、実行の前の値に戻るので、次のオブジェクトに引き継がれません。同じ`Draw`の中では続くので、使い終わったらその場で戻します。
- `Blend( Mode, Param )`（`SetDrawBlendMode`）: `Opaque / Alpha / Add / Subtract / Multiply`に、`Invert / MultiplyAlpha / PmaAlpha / PmaAdd / PmaSubtract / PmaInvert`を足しました（`Pma`は乗算済みアルファ用）。`Param`（0〜255。範囲の外は収める）は`Alpha`などの強さです。`GetBlendMode()`は、最後に記録した種類を返します。
- `CustomBlend( FCustomBlend )`（`SetDrawCustomBlendMode`）: 描く色（Src）と下の色（Dest）に掛ける係数（`EDrawBlendFactor`の11通り）と、組み合わせ方（`EDrawBlendOp`の足し算・引き算・小さいほう・大きいほう）を、色とアルファで別々に決めます。例: スクリーン合成は`SrcColor = One`、`DestColor = InvSrcColor`、`ColorOp = Add`。範囲の外の値は記録しません。
- `AddColor( R, G, B )`（`SetDrawAddColor`）: 描く色（図形も画像も）に足す色です。各成分は-255〜255（負なら引く）。`Tint`で掛けたあとに足されます。
- `AlphaTest( Compare, Reference )` / `AlphaTestOff()`（`SetDrawAlphaTest`）: 描く色のアルファ（画像の画素ごとの透明度と`Blend`の`Param`）を`Reference`と比べ、成り立つ画素だけを描きます。切り抜きの縁を半透明にしたくないときなどに使います。DxLibの`DX_CMP_NEVER`は何も止めないので、`Never`は「アルファ < 0」で代用しています。
- `Filter( EDrawFilter )`（`SetDrawMode`）: 画像を拡大・縮小・回転して描くときの色の決め方です。`Nearest`（既定。ドット絵向き）、`Bilinear`（なめらか）、`Anisotropic`（2Dの平らな描画では`Bilinear`と同じ）。
- `PushClip( X0, Y0, X1, Y1 )` / `PopClip()` / `FClipScope`（`SetDrawArea`）: 描ける範囲を絞ります。右と下の端は含みません。入れ子にすると、いまの範囲との共通部分になり、`PopClip`で1つ前に戻ります。`FClipScope`はスコープを抜けるときに`PopClip`します。共通部分が空（幅か高さが0以下）のあいだは、図形・画像・文字などの描く命令は飛ばし、状態を変える命令だけを実行します。クリップは画面の座標で決まり、`Transform2D`には影響されません。
- `Transform2D( Matrix )` / `ResetTransform2D()`（`SetTransformTo2D`）: 2Dの描画（図形・画像・文字）に掛ける変換です。行ベクトル（左から順に掛かる）で、アフィン変換だけ（最後の列は無視）。続けて呼ぶと置き換わります（掛け合わさりません）。拡大・回転・移動する2Dのカメラを、行列1つで作れます。DxLibには現在の2Dの変換を取る関数がないので、実行の終わりには、実行の前の値ではなく、変換なしに戻します。

NaN・無限大・絶対値が1e7を超える値、範囲の外の列挙型の値は記録しません。実行の終わりに戻す値は、DxLibに取得関数があるもの（加算する色・アルファテスト・描画モード・クリップ範囲）は実行の前の値、ないもの（2Dの変換）は既定の値です。見本は`Samples`の`DrawState`です（`Samples.exe --sample=DrawState`）。

描画先（レンダーターゲット）は、描き込める画像です。`Assets->CreateRenderTarget( "Minimap", Desc )`で作ります（`FRenderTargetDesc`: 幅・高さは1〜16384、`bAlpha`でアルファチャンネル（描いていない所を透明のまま残す）、`bDepth`でZバッファ、`MultiSamples`でマルチサンプル。作れない値は`nullptr`）。同じ名前・同じ設定なら共有し、最後の参照が消えると自動で解放します。`CRenderTarget`は`CTexture`でもあるので、描いたあとは`IImage`として、ふつうの画像と同じく`CommandList.Image( X, Y, Minimap )`などで画面や別の描画先へ貼れます。
- 描く: `FTargetScope Scope( CommandList, Minimap );`のスコープの中だけ、描く先が替わります（`PushTarget` / `PopTarget`でも同じ。入れ子にできます）。替えた先は、クリップ範囲なし・2Dの変換なしから始まり、戻ると、替える前の範囲・変換に戻ります。ブレンドなどの状態と3Dのカメラは、替えても変わりません。そのオブジェクトの分が終わると、`PopTarget`が足りなくても、始める前の描画先に戻ります。描画先にできない画像（ふつうの画像）を渡すと、対の`PopTarget`まで、描く命令を飛ばします（元の描画先へ描いてしまいません）。
- `Clear( 色 )`: いまの描画先の全体を、色とアルファのまま塗りつぶします（既定は透明な黒）。DxLibの`ClearDrawScreen`と同じく、クリップ範囲やブレンドには関係なく、全体を塗ります。
- `CopyScreen( X0, Y0, X1, Y1, 画像 )`（`GetDrawScreenGraph`）: いまの描画先の矩形を、同じ大きさの描画先へ写します（大きさが違う・矩形が描画先の外へはみ出す場合は、何もしません）。画面全体の一部を写すときは、画面（バックバッファ）が描画先のときに使います。
- 読む・保存する: `ReadPixels( X, Y, 幅, 高さ, 出力 )` / `ReadPixel( X, Y, 色 )`は画素（`FColor32`。アルファを持たない描画先は255）を、`Save( パス, FImageSaveOptions )` / `SaveRegion( ... )`はファイル（Png・Bmp・Jpeg。Pngだけ透明度も残る。途中のフォルダーは作ります）を作ります。矩形が描画先の外へはみ出す場合は`false`です。描画は「記録 → 実行」なので、読む・保存するのは、描いたもの（オブジェクトの`Draw`）が実行されたあと（次の`Tick`など）にします。どちらも、メインスレッドだけです。
見本は`Samples`の`RenderTarget`です（`Samples.exe --sample=RenderTarget`。ミニマップへ描き、貼り、画面の一部を写し、色を読み、Sキーで保存します）。

CPU側の画像（`FPixelImage`）は、画素の並びを持つ値です（DxLibに依存しません）。`FPixelImage Canvas( 64, 64, FColor32( 0, 0, 0, 255 ) );`で作り、`Get / Set`（範囲の外を読むと透明な黒、書くと何もしません）、`Fill / FillRect`、`Blit`（別の画像の矩形を重ねます。`EPixelBlend::Copy`は置き換え、`Alpha`は乗算されていないアルファの`over`で半透明を混ぜます。はみ出す分は切り、自分自身から重ねてもかまいません）、`Crop`、`FlipHorizontal / FlipVertical`が使えます。大きさは1〜16384（画素の総数は64Mまで）で、範囲の外の大きさは空の画像（`IsValid()`が`false`）になります。
- 画像にする: `Assets->CreateDynamicTexture( "Canvas", Canvas )`で`CDynamicTexture`（`CTexture`でもあるので、そのまま貼れます）を作り、`Update( Canvas )`で中身を替えます（同じ大きさだけ。ハンドルは変わらないので、貼ってある場所は全部、新しい絵になります）。毎フレームの更新は、数百ピクセル四方までの画像に向きます。透明な画素の色も、そのまま入ります。
- ファイルとの行き来: `LoadPixelImage( パス )` / `DecodePixelImage( バイト列, 大きさ )`（PNG・BMP・JPEGなど。透明度を持たない形式は不透明。読めなければ空の画像）と、`SavePixelImage( 画像, パス, FImageSaveOptions )`があります。DxLibは、途中で切れたPNG・BMP・JPEGを渡すと範囲外を読んで落ちるので、渡す前に、構造が最後まであるかを確かめ、壊れたものは空の画像にします。
- 描画先から: `CRenderTarget::ReadPixelImage()`で、描画先の全体をCPU側の画像にできます。
- 分ける（`LoadDivGraph`相当）: `Assets->SplitTexture( 画像, Grid )` / `LoadTextureGrid( パス, Grid )`は、格子（`FSpriteSheetGrid`。余白・隙間・コマの大きさ・並べ方）で分けた、1コマずつの画像の並びを返します。元の画像を共有する派生画像なのでメモリは増えず、元の画像は、派生画像が残るあいだ保たれます。画像の外へはみ出すコマは`nullptr`です。
見本は`Samples`の`Pixels`です（`Samples.exe --sample=Pixels`。画素を毎フレーム書いて画像を更新し、半透明の光を重ね、シートを分け、Sキーで保存して読み直します）。

画像の処理（フィルター・混ぜる）とマスクは、描画の命令として記録します。記録した順に実行されるので、描いたあとの描画先に掛けることも、掛けた画像をそのあと描くこともできます。描画先・クリップ・ブレンド・明るさ・2Dの変換には影響されず、変えもしません（空のクリップ範囲の中や、描画先にできない画像へ替えたあいだも、実行されます）。
- フィルター（`GraphFilter`）: `CommandList.ApplyFilter( Source, Dest, FGraphFilter::Gauss( 16, 300 ) )`は、Sourceにフィルターを掛けた結果をDestへ出します（Sourceは変わりません。Destは描画先`CRenderTarget`でも、ふつうの画像`CDynamicTexture`でもかまいません。大きさが違うと、拡大・縮小はせず、左上から重なる分だけが出ます）。`ApplyFilter( Target, Filter )`はその場で掛けます。`ApplyFilterRect( Source, 矩形, Dest, 位置, Filter )`は矩形つきです。`bAlphaBlend`を`true`にすると、結果をDestの中身にアルファブレンドで重ねます（既定は置き換え）。切り出した画像（`SplitTexture`）にも使えます（元の画像のその範囲だけが変わります）。
- 種類（`FGraphFilter::`）: `Mono`（単色化）、`Gauss`（ぼかし）、`DownScale`（縮小。Destは元の1/Divisorの大きさ）、`BrightClip`、`BrightScale`、`HsbRelative` / `HsbAbsolute`（色相・彩度・明るさ）、`Invert`、`Level`（レベル補正）、`TwoColor`（2階調化）、`GradientMap`、`Replacement`（色の置換）、`PremultiplyAlpha` / `UnpremultiplyAlpha`、`BicubicScale` / `Lanczos3Scale`（指定の大きさへ）、`FloatColorScale`。DxLibは範囲の外の引数でも失敗を返さず、おかしな結果にする（例: レベル補正のガンマが0以下だと真っ暗）ので、引数は作るときに範囲へ収めます。乗算済みアルファの画像には、`bPremultiplied`を`true`にします（`BrightClip`・`BrightScale`・`Hsb`・`Invert`・`Level`・`TwoColor`・`GradientMap`・`Replacement`だけ）。
- 画面そのもの（`DX_SCREEN_BACK`）には`GraphFilter`が効きません。画面全体に掛けたいときは、描画先へ描いて（`FTargetScope`）、その描画先にフィルターを掛けて、画面へ貼ります。
- 混ぜる（`GraphBlend`）: `CommandList.BlendImages( Base, Overlay, Dest, FGraphBlend::Make( EDrawGraphBlend::Multiply, 255 ) )`は、2枚の画像を混ぜてDestへ出します（Destが Base と同じなら、その場で）。`Normal`・`Multiply`・`Add`・`Screen`・`Difference`（引き算）・`Overlay`・`NormalAlphaCh`（アルファつきの通常合成）・`Mask`など。出力の成分を選ぶ`FGraphBlend::SelectMix`もあります。
- マスク（`SetMaskScreenGraph`）: `FMaskScope Scope( CommandList, MaskImage )`の中の描画は、マスクの画像の値で薄まります。マスクの画像は、アルファを持つ描画先（`CRenderTarget`の`bAlpha`が`true`。ふつうの画像は使えず、実行のときはマスクなしで描かれます）で、図形・画像・文字は、ふつうの描画（`FTargetScope`）で作ります。値が大きいところほど濃く描かれ（0で描かれず、255でそのまま）、段階的に効きます。`EDrawMaskChannel`でアルファ・赤・緑・青のどれをマスクの値にするかを、`bInvert`で向きを選べます。マスクの画像は、描画先の左上を合わせて使い、その外は値が255として扱われます。入れ子にすると、内側のマスクが外側に替わります（重なりません）。替えた先の描画先（`PushTarget`）はマスクなしから始まり、戻すと元のマスクに戻ります。そのオブジェクトの分が終わると、マスクは解除されます。
見本は`Samples`の`Filters`です（`Samples.exe --sample=Filters`。1枚の画像にフィルターと混ぜ方を並べ、動くスポットライトの形のマスクで絵を見せます）。

文字とフォントは、`Assets->GetDefaultFont( 24, Style )`（`LoadSystemFont`・`LoadFontFile`も同じ）で作る`CFont`で扱います。`FFontStyle`では、太さ（`Thickness`）・縁取り（`EdgeSize`。縁の色は描くときの`EdgeColor`）・斜体（`bItalic`）に加えて、アンチエイリアスの段階（`Antialias`: `None / Standard / High4x4 / High8x8 / High16x16`）、字間（`LetterSpacing`。全部の文字の幅に足す。負の値で詰める）、行の送り幅（`LineHeight`。-1は1行の文字の高さ）を選べます。指定が1つでも違えば別のフォントとして作り、同じ指定は共有します。
- 測る: `GetMetrics()`は、フォントを作るときに測った寸法（`Size`・`GlyphHeight`（1行の文字の高さ）・`LineHeight`・`Ascent`・`MaxWidth`・`EdgeSize`・`LetterSpacing`）を返します。作ったあとは変わらないので、描画システムの中（オブジェクトの`Draw`）から読んでかまいません。`MeasureWidth`は幅、`MeasureSize`は幅・高さ・行数です（複数行の高さは「(行数 - 1) x 行の送り幅 + 1行の文字の高さ」。空の文字列は0行で、末尾の改行のあとも1行と数えます）。`GetGlyphs`は、文字ごとのバイト位置・幅・位置を返します。`MeasureWidth / MeasureSize / GetGlyphs / Layout`はDxLibを呼ぶので、メインスレッドで呼びます。
- 折り返す: `Layout( 文字列, FTextLayoutOptions )`は、箱の幅（`MaxWidth`。0以下なら折り返さない）に収まるように行へ分けた`FTextLayout`を返します（`MaxLines`で行数を切ると`bTruncated`が`true`）。改行（LF）で段落を分け、英単語は空白で、漢字・かななどは1文字ごとに折り、箱より長い単語は途中で折ります。行末の空白は幅に数えず、折り返した次の行の頭の空白は読み飛ばします。`bAvoidPunctuationAtLineEdges`（既定は`true`）では、句読点・閉じ括弧・小さいかななどを行頭に、開き括弧を行末に置きません（折る位置が無いときは、行の端にはみ出して、ぶら下げます）。文字を測るので、`Tick`などで`Layout`を作っておき、`Draw`の中では`RecordLayout( CommandList, Layout, X, Y, 箱の幅, 色, 位置揃え )`で記録します（こちらは測りません）。`RecordWrapped / DrawWrapped`は、折り返しと記録を1度に行います。折り返しの計算は`Library/Framework/Text/TextLayout.h`の`NTextLayout::Build`にあり、DxLibに依存せず（`ITextMeasurer`が文字の幅を渡す）、単体で確かめられます。
- 飾る: `RecordStyled( CommandList, X, Y, 文字列, 色, FTextStyle )`（DxLibの`DrawRotaStringFToHandle`）は、小数の位置に、拡大（`ScaleX / ScaleY`）・回転（`AngleRadians`。時計回り）・縦書き（`bVertical`）・影（`bShadow`・`ShadowColor`・`ShadowOffsetX / Y`）を付けて描きます。`AnchorX / AnchorY`は、文字列の大きさに対する割合（0〜1）で、(X, Y)に置く点を選びます。拡大と回転も、その点が中心です。縦書きは、上から下へ並べ、改行で左の列へ進み、(0, 0)は列の右上です。拡大が0以下の値、NaN・無限大は記録せず、`false`を返します。
- 文字の代わりの画像: `Font->AddCharImage( "@", Icon, 幅 )`で、その1文字を画像に置き換えます（そのフォントだけに効きます）。`TSharedPtr<CTexture>`で渡すと、フォントが画像を持ち、`RemoveCharImage`かフォントの破棄まで手放しません。`IImage`で渡す版は、持ち主が手放さないでください。
- 環境のフォント: `Assets->IsFontAvailable( "Arial" )`と`GetSystemFontNames()`で、使えるフォントを調べられます。
- 包んでいないもの: `DrawFormatString`系（書式付きは`TextF`）、`AddSubstitutionFontToHandle`（Windowsのフォントリンクが先に効くため、実測で変化がありません）、字の組み合わせごとの詰め（カーニング）、`FontCacheStringDraw`（文字を画像にする）、フォントデータ（.dft）の読み込み。必要になったら足します。
見本は`Samples`の`Text`です（`Samples.exe --sample=Text`。フォントの指定を1つずつ変えた行、幅の動く箱への折り返し、拡大・回転・影・縦書き・文字の代わりのアイコン）。

3Dのカメラは、`Library/Framework/Render/Camera.h`の`FCamera`（見る位置`Position`・見る点`Target`・上の向き`Up`・射影`Projection`（`Perspective` / `Orthographic`）・縦の視野角`FovY`・正射影の縦の長さ`OrthoHeight`・`Near` / `Far`・横の拡大率`HorizontalStretch`・光軸が通る画面の点のずれ`ScreenOffset`）で表します。DxLibに依存しない値と計算で、`FCamera::MakePerspective( 位置, 見る点, 視野角 )` / `MakeOrthographic( 位置, 見る点, 縦の長さ )`で作ります。左手系・Y上で、`IsValid()`が`false`になる値（NaN・無限大、`0 < Near < Far`でない、視野角が0以下かπ付近、位置と見る点が同じ、上の向きが見る向きと平行）は、描画に渡しません。
- 使う: `FCameraScope Scope( CommandList, Camera );`（`PushCamera` / `PopCamera`でも同じ。入れ子にできます）の中の3Dの描画（`Sphere3D`・`Cube3D`・`Line3D`・モデルなど）が、そのカメラで見えます。カメラを替えるのはDxLibのカメラ（大域の状態）なので、`Draw`が終わると、`PopCamera`が足りなくても、始める前のカメラに戻ります。光軸が通る画面の点は、実行のときの描画先の大きさで決まり（描画先を替えたら、その描画先の中心。戻すと、元の画面の中心）、`ScreenOffset`の分だけずらせます。使えない値のカメラは、何も記録せず`false`を返します。
- 計算（DxLibを呼ばない）: `GetViewMatrix()` / `GetProjectionMatrix( FCameraViewport )`（DxLibの`GetCameraViewMatrix` / `GetCameraProjectionMatrix`と同じ。奥行きは`Near`で0、`Far`で1）、`WorldToScreen( Viewport, 世界の点, 画面の座標, &奥行き )`（`ConvWorldPosToScreenPos`と同じ。カメラの後ろ・`Near`より近い・`Far`より遠い点は`false`）、`ScreenToWorld( Viewport, 画面の座標, 奥行き, 世界の点 )`、`ScreenToRay`（画面の点を通る光線。始まりは`Near`の面の上。マウスで物を選ぶのに使えます）。画面の座標は、描画先の左上が(0, 0)、単位はピクセルで、描画先の大きさを`FCameraViewport`で渡します。
- 視錐台: `GetFrustum( Viewport )`の`FFrustum`が、点・球・軸に平行な箱との交差（`ContainsPoint` / `IntersectsSphere` / `IntersectsBox`）を答えます。見えない物を描かない（カリング）のに使えます。点の判定は、DxLibの`CheckCameraViewClip`と同じ答えで、箱は、見えるものを外とは言いません（保守的）。
見本は`Samples`の`Camera`です（`Samples.exe --sample=Camera`。マウスの左ボタンを押したまま動かすと回り、ホイールで近づき、Pで透視射影と正射影を切り替え、Q / Eで視野角を変えます。上から見下ろす2つ目のカメラを別の描画先へ描き、右下に貼ります）。

3Dの描画状態（深度バッファ・カリング・画像の貼り方・フォグ・異方性）は、`FDrawContext`に記録します。値は`Library/Framework/Render/RenderState3D.h`の`FDepthState` / `FFogSettings` / `FTextureTransform`で、DxLibに依存しません。どれも、そのオブジェクトの分（1回の`Draw`）が終わると、始める前の状態に戻ります。使えない値（範囲の外の種類、NaN・無限大など）は、何も記録しません。
- 深度バッファ: `Depth( FDepthState )`（`SetUseZBuffer3D` / `SetWriteZBuffer3D` / `SetZBufferCmpType3D` / `SetZBias3D`）。比べるか`bTestDepth`・書くか`bWriteDepth`・比べ方`Compare`（`EDrawCompare`。既定は`LessEqual`）・偏り`Bias`（負の値は手前に寄る。±`MaxBias`に収める）。`FDepthState::ReadOnly()`は半透明の物に（手前の物には隠れるが、あとの物を隠さない）、`Disabled()`は空や画面の飾りに使います。`ClearDepth()`（`ClearDrawScreenZBuffer`）は、いまの描画先の深度バッファを全体消します。ここまでに記録した3Dは、消す前の深度で描き終えてから消し（DxLibは3Dを溜めて描くので、先に描き出します）、クリップ範囲や描画範囲に関係なく全体を消します。手前に描く物（一人称の武器など）を、奥の物に隠されないよう、あとから描くときに使います。
- カリング: `Culling( EDrawCulling )`（`SetUseBackCulling`）。表は、画面で頂点が時計回りに見える面です。`BackFaces`は裏（反時計回り）を描かず、`FrontFaces`は表を描かず、`None`は両面を描きます。
- 画像の貼り方: `TextureAddress( U, V )`（`SetTextureAddressModeUV`。`Wrap`は繰り返す・`Mirror`は折り返す・`Clamp`は端を伸ばす・`Border`は外を描かない）。`TextureTransform( FTextureTransform )`（`SetTextureAddressTransform`）は、面の画像の位置・拡大・回転を動かします（水面や雲を流すときに）。UVは、回転（`RotationCenter`のまわり。正なら画面で反時計回り）→ ずらす（`Translation`）→ 拡大（`Scale`。原点から）の順に動きます（`Apply( UV )`が同じ計算）。何も変えない変換と`ResetTextureTransform()`は、変換をやめます。画像を拡大・縮小するときの色の決め方は、これまでの`Filter`（`Nearest` / `Bilinear` / `Anisotropic`）が、3Dの面にも効きます。`Anisotropy( 1〜16 )`（`SetMaxAnisotropy`）は、`Anisotropic`のときだけ効き、斜めから見た面の画像が、遠くでぼやけにくくなります。
- フォグ: `Fog( FFogSettings )`は距離（視点からの奥行き）、`HeightFog( FFogSettings )`は高さのフォグです。`FFogSettings::MakeLinear( 色, 始まり, 終わり )` / `MakeExponential( 色, 密度, 始まりの高さ )` / `MakeExponentialSquared`、`Off()`で作ります。距離のフォグは、直線なら奥行き`Start`で0・`End`で1、指数は`1 / exp( 距離 x 密度 )`（指数の2乗は`1 / exp( ( 距離 x 密度 )^2 )`）です。高さのフォグは、直線なら高さ`Start`で0・`End`で1（`End`のほうが低くてもよい）、指数は、高さ`Start`より下で、低いほど濃くなります。球・箱・カプセル・画像を貼った四角形・メッシュ・モデルに掛かり、**三角形（`Triangle3D`）・線（`Line3D`）・ビルボード（`Billboard3D`）には、DxLibの仕様で掛かりません**。
- 3D図形の変換の積み重ね: `PushWorldMatrix( 自分の変換 )` / `PopWorldMatrix()`（`FWorldMatrixScope`）は、`WorldMatrix`で置いた変換（または、積んだ変換）に、自分の座標 -> 親の座標の順に掛けます（腕・肩・肘・手のような入れ子に）。積んだ数より多く戻しても、何も起きません。`Draw`が終わると、戻さなくても変換なしに戻ります。

見本は`Samples`の`RenderState`です（`Samples.exe --sample=RenderState`。数字キーの1〜8で、ガラスの深度・部屋のカリング・距離のフォグ・足元の霧・地面のフィルターと異方性・水面の流れ・光線の深度の消去を切り替えます。マウスの左ボタンを押したまま動かすと回り、ホイールで近づきます）。

3Dのライティング（光）も、`FDrawContext`に記録します。値は`Library/Framework/Render/Lighting.h`の`FLight` / `FSurfaceMaterial` / `FLightingOptions`で、DxLibに依存しません。どれも、そのオブジェクトの分（1回の`Draw`）が終わると、始める前の状態に戻ります。使えない値（NaN・無限大、向きの長さ0、`Range`が0以下など）は、何も記録しません。何も記録しなければ、DxLibの既定の見た目（最初からある白い方向ライト1つ）のままです。光を受ける図形は、球（`Sphere3D`）・箱（`Cube3D`）・カプセル（`Capsule3D`）・画像を貼った四角形（`Quad3D`。見ている人の側が表）で、**三角形（`Triangle3D`）と線（`Line3D`）は、光を受けません**。
- ライト: `FLightScope Lamp( CommandList, FLight::MakePoint( 位置, Range, 色 ) );`（`PushLight` / `PopLight`でも同じ。入れ子にできます）の中の図形が、そのライトでも照らされます。種類は、方向ライト（`MakeDirectional( 光が進む向き, 色 )`。太陽・月。距離で暗くならない）、点光源（`MakePoint`。距離とともに暗くなり、`Range`より遠くには届かない）、スポットライト（`MakeSpot( 位置, 向き, 外側の角度, 内側の角度, Range, 色 )`）です。スポットライトの角度は全角（ラジアン）で、外側より外は照らさず、内側より中は全力、そのあいだは直線的に弱まります。色は、拡散光`Diffuse`・鏡面反射`Specular`・ライトの環境光`Ambient`の3つで、1を超えてもよく、負の色は光を吸います。距離の減衰は`1 / ( ConstantAttenuation + LinearAttenuation x 距離 + QuadraticAttenuation x 距離^2 )`で、`MakePoint`は`Range`で約5%になる値にします。向きの長さは問いません。足したライトは、`Draw`が終わると、外し忘れても全部外れます。
- ライトの数: **同時に効くライトは6つまで**です（DxLibの仕様）。DxLibに最初からある1つのライト（白い方向ライト。向き`(1, -1, 1)`）が有効なら、それも1つに数えます。自分のライトだけで照らすときは、`DefaultLight( false )`で切ります。6つを超えて足した分（先に足した6つが効きます）は、何も起きません。外すときの数は合わせます（`PopLight`は、効かなかったライトの分も、1つずつ戻します）。
- CPUで計算する: `FLight`の`GetAttenuation( 距離 )`・`GetSpotFactor( 光源から点への向き )`・`GetIntensityAt( 世界の点 )`は、DxLibと同じ計算で答えます（「この点は明るい場所か」を、ゲームの処理で調べるのに使えます）。
- 面の材質: `SurfaceMaterial( FSurfaceMaterial )`で、以降に描く図形の光の受け方を決めます。拡散色`Diffuse`・環境色`Ambient`・鏡面色`Specular`・自己発光`Emissive`・鋭さ`Shininess`（大きいほどハイライトが小さく鋭い）です。既定は、DxLibの既定の材質（拡散0.8・環境0・鏡面0.8・自己発光0・鋭さ20）と同じです。拡散光と鏡面反射の色は、既定では、図形に付けた色（`Sphere3D`の`Diffuse` / `Specular`など）を使い、`bUseVertexDiffuse` / `bUseVertexSpecular`を`false`にすると、材質の色だけを使います。**環境光は、材質の`Ambient`が0（既定）だと効きません**。環境光を受けるときは`FSurfaceMaterial::MakeMatte()`、自分で光る物には`MakeGlowing( 色 )`が使えます。
- 全体の環境光と光の入り切り: `AmbientLight( 色 )`（`SetGlobalAmbientLight`）は、すべての面を、向きに関係なく底上げします（材質の`Ambient`が掛かります）。`UseLighting( false )`（`SetUseLighting`）は、光の計算（ライト・環境光・自己発光）をやめて、拡散色と鏡面色を足した色をそのまま出します（図形の鏡面色が黒の既定なら、図形の色のままです）。空や画面の飾りに使います。
- 光の当たらない側: 既定のライト1つだけだと、光が当たらない裏側は**真っ黒**になります（環境光は、図形の色ではなく、材質の`Ambient`に掛かるので、既定の材質では効かず、効かせても、どの図形にも同じ色を足してしまい、色がにごります）。図形の色に応じた、やわらかい陰にするには、**反対側から弱い方向ライト（補助光）を足し**、`bWrapLighting`で境目をなだらかにします。`Samples`の`Source/Common/SoftLighting.h`の`FSoftLightingScope`が、この2つを1行で入れます（`Camera`と`RenderState`が使います。真っ黒な画素は、Cameraで4.1万 → 2,700、RenderStateで1.5万 → 750に減ります）。
- 光の計算の仕方: `LightingOptions( FLightingOptions )`。`bUseSpecular`（ハイライトを出すか。既定は`true`）、`bPixelLighting`（画素ごとに光を計算するか。**大きな面では、頂点ごと（既定）だと点光源・スポットライトの光の輪が出ません**。重い）、`bWrapLighting`（光の当たらない側もやわらかく照らす。いわゆるハーフランバート）、`bIgnoreSurfaceAngle`（面の向きと光の角度に関係なく、光が届く面を全力で照らす）の4つです。

見本は`Samples`の`Lighting`です（`Samples.exe --sample=Lighting`。夜の中庭を、月明かり（方向ライト）・床の上を回る電球（点光源）・床を舐めるスポットライト・周りの街灯（同時に6つまで）で照らします。数字キーの1〜9で、各ライト・全体の環境光・画素ごとの光・ハイライト・やわらかい光・街灯の数・光の計算そのものの入り切りを切り替えます。マウスの左ボタンを押したまま動かすと回り、ホイールで近づきます）。

2Dの機能を1つの画面にまとめた見本が、`Samples`の`Village`です（`Samples.exe --sample=Village`。WASDか矢印キーで歩き、Shiftで走り、Enterで村人に話しかけ、Nキーで昼と夜を切り替えます。キーを押すまでは、ひとりで歩きます）。クラスとファイルは、1つの仕事ごとに分けてあります。`CTileMap`（タイルの地図。見えている範囲だけを`ImagePart`で描く）、`CPlayer`（入力・衝突・カメラの追従）、`CCoin`、`CVillager`、`CDialogue`（`CFont::Layout`で折り返し、1文字ずつ表示）、`CDarkness`（夜）、`CStatusPanel`（金貨の数・体力の円グラフ・ミニマップ）で、オブジェクトどうしは`FVillageState`で状態を分け合います。絵はすべて`FPixelImage`で作る（`NVillageArt`）ので、画像のファイルは要りません。
- カメラ: 世界の座標で描くオブジェクトは、`Draw`の最初に`FWorldScope`（見せる範囲へのクリップと、`Transform2D`でカメラの移動を掛ける）を置きます。クリップと変換は、`Draw`が終わると戻るので、情報の表示や会話の窓のような、画面の座標のオブジェクトには影響しません。カメラの位置は、整数にそろえます（タイルの継ぎ目がずれないように）。
- 夜の明かり: 「夜の層」（アルファを持つ描画先）を、暗い青で塗り（アルファが夜の濃さ）、光の画像を、カスタムブレンド（`SrcAlpha = Zero`・`DestAlpha = InvSrcAlpha`。画像のアルファの分だけ、いまのアルファを減らす）で置いてから、その層を、ふつうのアルファブレンドで画面へ重ねます。光が重なると、さらに明るくなります。
- マスク（`FMaskScope`）で、画面（アルファを持つ描画先）へ直接描くのは、うまくいきません。DxLibは、アルファを持つ描画先へ、マスクつきで描くと、色を置き換えてしまい（下の絵に混ざらない）、描いたところのアルファが下がります。マスクで描く先は、アルファのない描画先にします（`Filters`の見本）。マスクで描くときの濃さは、マスクの値そのもので決まり、`Blend`の強さの指定は効きません。

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
- 自作のh・cpp・inl・ps1はUTF-8 BOM一つ、CRLF、NULなしとします。第三者のソースとライセンスは元の形式を保ちます。BOMは必須です。ビルドは`/execution-charset:utf-8`（`/utf-8`ではありません）なので、BOMのあるファイルだけがUTF-8として読まれ、BOMを落とすと、日本語の文字列リテラルが警告なしで化けます。
- 文字列は`FString`（UTF-8）です。文字列リテラルもUTF-8で、ImGui・SDL・JSON・DxLibにそのまま渡せます。Win32のAPIは`W`系を使い、パスは`ToWide()`で渡します。UTF-8でないファイルなどとの境目は`FromCodePage` / `ToCodePage`で変換します。詳しくは`WRAPPER_GUIDE.md`の第6節にあります。
- 標準ライブラリより先に、フレームワークの機能を使います。数値の比較・範囲・有限判定は`Library/Core/Types/Numeric.h`（`NMath::Abs / Min / Max / Clamp / IsFinite / IsNaN / IsInfinite`と`TNumericLimits<T>`）、右辺値の受け渡しは`Library/Core/Types/Utility.h`（`MoveTemp / Forward`）にあります。浮動小数点の`NMath::Abs`は`std::fabs`と同じく符号ビットを落とし、`Min / Max`は`std::min / std::max`と同じ向きで値を返します。同等のものが無いときだけ`std::`を理由付きで使い、第三者のコードは対象外です。
- 機能、公開API、ソースの配置を変えた場合は、関連する文書と参照も更新します。

## ソリューションの構成

`project.sln`は、三つのプロジェクトからなります。

|プロジェクト|種類|中身|
|---|---|---|
|`mazigatiDX`|静的ライブラリ|`Library`（フレームワーク本体）と`VendorModules`（第三者のライブラリ）|
|`project`|実行ファイル|`Source`（このリポジトリのゲーム。検査を置いたときはその分も）|
|`Samples`|実行ファイル|`Samples/Source`（機能別のサンプル集。下の「サンプル集」）|

- フレームワークを使うプロジェクトは、`Build/mazigatiDX.props`を取り込み、`mazigatiDX`への参照を持ちます。propsが、インクルード検索先（`#include "Library/..."`）、C++17、文字列リテラルをUTF-8にする設定（`/execution-charset:utf-8`）、DxLibとSDL3のライブラリ検索先、`main`で始められる入口の設定、`SDL3.dll`のコピーを引き受けるので、ゲーム側の`.vcxproj`は短く済みます。
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

# エフェクトの使い方

Effect Editorの`Preset`から元になる表現を選び、未保存のコピーを編集します。`File > Save as...`で`Effects/MyEffect.gxeffect`などへ保存し、World Editorのアセット一覧から配置します。

`Flowing energy veil`、`Distorted ribbon trail`、`Flowing lightning tree`では、ノイズの移動と強さの曲線によるUVディストーションを試せます。帯は移動プレビューで軌跡を確認でき、ゲーム内では配置したオブジェクトを動かして使います。`Realistic lightning tree`と`Growing lightning tree`では枝・孫枝・成長・明滅を編集できます。サンプル原本は`Assets/Effects`にあります。

平面・円環は`Mesh energy ring`と`Mesh tilted veil`から始められます。`Mesh geometry`で形状、円環の分割数と穴の比率、固定回転を変えます。大きさ・色・回転速度と材質の寿命曲線は粒子設定を共有します。MeshはPath・Flipbookと併用できません。描画上限は一つの再生全体で生成順の64粒子です。開いた直後と`Reset camera`時は、形状・発生範囲・粒子の移動分を画面へ一度だけ収め、その後の手動視点を保持します。`Move emitter preview`による円運動は、この自動調整の範囲に含みません。

力場は`Force field wind`と`Gravity well`から始められます。`Force fields`で追加・複製・削除・並べ替えを行い、加速度、World中心、半径、引力・反発を編集します。中心と半径はWorld座標に固定され、負の`Strength`は引力、正は反発です。`Show force field guides`で範囲線を切り替えられます。これらの例は発生元を動かさず、力場で粒子と帯の進路を曲げます。

編集メッシュではWorld Editorで対象を選び、Inspectorの各材質にある`Mesh material effect`を開きます。`Pulsing barrier`または`Flowing lava`で画像と効果をまとめて設定し、色やUVの移動、消失・発光・ゆがみの強さの曲線を調整できます。変更はUndo／Redoでき、ワールドの保存で保持します。選択メッシュの再生を`Play`／`Pause`／`Stop`／`Restart`で確認でき、Stopは通常の材質描画へ戻します。試すワールドは`Worlds/mesh_surface_showcase.gxworld`です。現在は照明を使わないLDRの半透明・加算合成です。自己発光の値を上げても、ブルームによる周囲への光の広がりはまだありません。

取り込みモデルはWorld Editorで選び、Inspectorの`Model material effect`から`Add material effect (disabled)`を押します。実際の材質一覧で色・画像の有無を確認し、`Choose material slot`で対象を選び、`Enable material effect`を有効にして調整します。無効中の設定も保存され、`Remove material effect`で設定自体を取り除けます。`Placement preview transport`は選択した配置だけを操作し、再生操作ではワールドの保存値を変えません。静的モデルの一材質が対象で、アニメーションが結び付いた個体は通常のモデル描画になります。

四角形の粒子は、ノードで作ったマテリアル（Particle ドメイン。[MATERIAL.md](MATERIAL.md)）でも描けます。`.gxeffect` のエミッターに `"MaterialAsset": "Engine:/Materials/Particle/Flame.gxmaterial"` のように書きます（いまは、ファイルに直接書きます。Effect Editor には、まだ項目がありません）。

エフェクトの再生をコードから使う見本は、`Samples`の`Effects`（`Samples/README.md`）です。ゲームのオブジェクトには、`CEffectComponent`を付けるのが手軽です。

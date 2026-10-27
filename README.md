# mazigatiDX

DxLibを使うWindows向けC++フレームワークです。描画・入力・アセットの窓口をインターフェースに分け、ワールド編集とエフェクト制作を同じアプリで行います。

主な機能:

- 描画命令の記録と実行、ECS、ジョブ、入力、音声、プロファイラー。
- 画像・モデル・アニメーションの管理と編集、ワールドとプレハブの配置・保存。
- エフェクトの複製・編集・保存・配置。粒子、リボン、分岐する雷、固定経路を成長・明滅させる樹状の雷。
- ディゾルブ、境界色、自己発光、UVスクロール、寿命に沿う曲線、四角形粒子のコマ送り、四角形・帯・雷のUVディストーション。
- 固定した向きの平面・円環メッシュ粒子。穴の形状、UV、消失、自己発光、ゆがみを組み合わせて編集。
- 編集メッシュの材質ごとのディゾルブ・自己発光・UV移動・ゆがみと、再生・一時停止・停止。
- 粒子へ一様な加速度と、距離で弱まる引力・反発を加える力場の編集・範囲表示。
- 編集できる組み込みエフェクト。

静的な取り込みモデルの一個体・一材質へ効果を付け、ワールドへ保存して配置時に再生できます。ワールドのビューポートでも配置ごとに再生し、同じPrefabの複数配置を独立して表示します。使い方と対応範囲は[開発資料](Docs/DEVELOPMENT.md)を参照してください。2D専用の制作操作、HDR・ブルームは今後の工程です。

## ビルド

検証構成はx64のDebug／Releaseです。C++17、MSVCツールセットv145、Windows SDK 10.0.26100.0を使用します。DxLib 3.24fのヘッダーとライブラリを`C:\DxLib`へ配置してください。別の場所を使う場合は`project.vcxproj`のインクルード・ライブラリ検索先を合わせます。

必要な第三者ライブラリは`VendorModules`に配置しています。SDLのシンボルファイルは含みません。各ライブラリのライセンスは同じフォルダー内に保持しています。

1. `GenerateProjectFiles.bat`を実行します。
2. `project.sln`を開き、x64のDebugまたはReleaseをビルドします。
3. 作業ディレクトリーをこのリポジトリのルートにして起動します。`Assets`と`Config`を使います。

Developer PowerShellからの例:

```powershell
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
```

自己確認は、このリポジトリに含めず別のリポジトリ`framework-test`で管理しています。ルートの`Source\SelfTest`へ置いたときだけ組み込まれ、置かなくても通常どおりビルドできます。詳しい起動・確認・開発規約は[開発手順](Docs/DEVELOPMENT.md)に、DxLibを包むときの設計規約は[ラッパーの設計規約](Docs/WRAPPER_GUIDE.md)にまとめています。

## エフェクトを使う

Effect Editorの`Preset`から元になる表現を選び、未保存のコピーを編集します。`File > Save as...`で`Effects/MyEffect.effect.json`などへ保存し、World Editorのアセット一覧から配置します。

`Flowing energy veil`、`Distorted ribbon trail`、`Flowing lightning tree`では、ノイズの移動と強さの曲線によるUVディストーションを試せます。帯は移動プレビューで軌跡を確認でき、ゲーム内では配置したオブジェクトを動かして使います。`Realistic lightning tree`と`Growing lightning tree`では枝・孫枝・成長・明滅を編集できます。サンプル原本は`Assets/Effects`にあります。

平面・円環は`Mesh energy ring`と`Mesh tilted veil`から始められます。`Mesh geometry`で形状、円環の分割数と穴の比率、固定回転を変えます。大きさ・色・回転速度と材質の寿命曲線は粒子設定を共有します。MeshはPath・Flipbookと併用できません。描画上限は一つの再生全体で生成順の64粒子です。開いた直後と`Reset camera`時は、形状・発生範囲・粒子の移動分を画面へ一度だけ収め、その後の手動視点を保持します。`Move emitter preview`による円運動は、この自動調整の範囲に含みません。

力場は`Force field wind`と`Gravity well`から始められます。`Force fields`で追加・複製・削除・並べ替えを行い、加速度、World中心、半径、引力・反発を編集します。中心と半径はWorld座標に固定され、負の`Strength`は引力、正は反発です。`Show force field guides`で範囲線を切り替えられます。これらの例は発生元を動かさず、力場で粒子と帯の進路を曲げます。

編集メッシュではWorld Editorで対象を選び、Inspectorの各材質にある`Mesh material effect`を開きます。`Pulsing barrier`または`Flowing lava`で画像と効果をまとめて設定し、色やUVの移動、消失・発光・ゆがみの強さの曲線を調整できます。変更はUndo／Redoでき、ワールドの保存で保持します。選択メッシュの再生を`Play`／`Pause`／`Stop`／`Restart`で確認でき、Stopは通常の材質描画へ戻します。試すワールドは`Worlds/mesh_surface_showcase.world`です。現在は照明を使わないLDRの半透明・加算合成です。自己発光の値を上げても、ブルームによる周囲への光の広がりはまだありません。

取り込みモデルはWorld Editorで選び、Inspectorの`Model material effect`から`Add material effect (disabled)`を押します。実際の材質一覧で色・画像の有無を確認し、`Choose material slot`で対象を選び、`Enable material effect`を有効にして調整します。無効中の設定も保存され、`Remove material effect`で設定自体を取り除けます。`Placement preview transport`は選択した配置だけを操作し、再生操作ではワールドの保存値を変えません。静的モデルの一材質が対象で、アニメーションが結び付いた個体は通常のモデル描画になります。

## ブランチ

|ブランチ|役割|
|---|---|
|`main`|検証を終えた安定版|
|`dev`|全機能を統合して確認する開発版|
|`Effect`|エフェクト機能の開発|
|`Asset`|アセット機能の開発|
|`Core`|基盤・共通インターフェースの開発|

機能ブランチから`dev`へ統合し、必要な確認を終えた区切りを`main`へ反映します。

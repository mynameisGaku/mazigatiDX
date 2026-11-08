# UI（ウィジェット）

ゲームにも、ツールにも使える、部品の木で作る UI です。見た目（ブラシ）・レイアウト・入力・部品が分かれていて、どれも差し替えられます。描画は、コマンドリスト（`FDrawContext`）へ記録するだけなので、DxLib 以外のバックエンドでも動きます。見本は `Samples.exe --sample=UI` です。

## 構成

|層|内容|場所（`Library/Framework/UI`）|
|---|---|---|
|部品の木とレイアウト|`CUIWidget`（余白・寄せ方・最小 / 最大・固定の大きさ・不透明度・当たり判定）、`CUICanvasPanel`（アンカー）、`CUIStackPanel`（縦横）、`CUIGridPanel`（格子）|`Core`|
|見た目|`FUIBrush`: 色（角丸・枠）・画像（引き伸ばし / 収める / 覆う / タイル / 9 分割）・コールバック・重ね|`Brush`|
|入力|`CUIContext`（ホバー・押下・クリック・フォーカス・決定・戻る）、`NUIInput::MakeFrame`（ゲームの入力 → UI の入力）|`Core`|
|部品|`CUIImage`・`CUILabel`・`CUIProgressBar`・`CUIButton`・`CUICheckBox`・`CUISlider`・`CUIRenderTargetView`|`Widgets`|
|既定のスタイル|`FUIDefaultStyle`: `EngineContent` の画像から作る、部品の既定の見た目|`Style`|

## 使い方

```cpp
// 1. 部品の木を作る
TSharedPtr<CUICanvasPanel> Root = MakeShared<CUICanvasPanel>();
Root->SetHitTestable( false );                       // 背景が全画面でも、下のゲームへクリックを通す

FUIDefaultStyle Style;
FUIDefaultStyle::Load( *Assets, Style );             // Engine:/UI/... の画像から、既定の見た目

TSharedPtr<CUIButton> Button = MakeShared<CUIButton>();
Button->SetStyle( Style.Button );
Button->OnClicked.Add( []( CUIWidget& ) { /* 押された */ } );
FUILayoutProperties Layout;
Layout.Offsets = FUIMargin( 100.0f, 100.0f, 160.0f, 44.0f );   // Canvas の中: 左上 (100, 100)、幅 160、高さ 44
Button->SetLayout( Layout );
Root->AddChild( Button );

CUIContext Context;
Context.SetRoot( Root );

// 2. 毎フレーム: レイアウト → 入力 → 描画
Root->UpdateLayout( FUIRect( 0, 0, 1280, 720 ) );                          // 変更があるときだけ計算し直す
const FUIInputFrame Frame = NUIInput::MakeFrame( Input, Input, Gamepad );  // マウス・キーボード・パッド（どれも null 可）
const FUIInputResult Result = Context.Update( Frame );
if ( !Result.ConsumesPointer() ) { /* UI の上でなければ、ゲームのクリックとして扱う */ }
Root->Paint( CommandList );                                                // オブジェクトの Draw の中で
```

## レイアウト

- 部品は、「ほしい大きさ」（`Measure`。子から親へ）と、「置き場所」（`Arrange`。親から子へ）の 2 段階で置かれます。置き方の指定は `FUILayoutProperties`（親の種類で、効く値が決まる。`UIWidget.h` の説明を参照）。
- **Canvas**: アンカー（親に対する割合）・ずれ・ピボットで置く。四隅・中央・全体に余白つき、を同じ指定で書ける。
- **Stack**: 縦または横に並べる。間隔・`Weight`（残りを分ける）。非表示（Collapsed）は、場所も間隔も取らない。
- **Grid**: 列・行を `Fixed`（画素）/ `Auto`（中身）/ `Weight`（残りを分ける）で指定し、`Column` / `Row` / `ColumnSpan` / `RowSpan` で置く。
- 置き場所より大きい部品は、縮められず、はみ出します（`SetClipChildren( true )` でクリップできる）。

## 見た目（ブラシ）

- 部品の見た目は、ブラシで決まります。`SetBackground`（どの部品にも）と、部品ごとのスタイル（ボタンの状態ごとのブラシなど）。
- 画像は 9 分割にできます（`FUIBrush::MakeNineSlice( 画像, 画像の大きさ, 余白 )`）。矩形が余白より小さいときは、四隅を比で縮めます。
- 色の面・画像・コールバック（`FUIBrush::MakeCallback`。自由に描く）・重ねを、組み合わせられます。
- 図形の色のアルファは、DxLib では自動では効かないので、ブラシが描くたびにブレンドを設定します（色のアルファ × 不透明度）。

## 入力

- ゲームの入力の窓口（`IKeyboardInput` / `IMouseInput` / `IGamepadInput`）から、`NUIInput::MakeFrame` で、UI の入力（`FUIInputFrame`）を作ります。割り当ては `FUIInputMapping` で替えられます（既定: 矢印 = 移動、Enter・Space = 決定、Escape = 戻る、Tab / Shift + Tab = 次 / 前、パッドは十字キー・South・East・肩ボタン）。
- ポインターの対象は、当たった部品から親へたどって、最初の `Interactive` な部品です。使えない部品（`SetEnabled( false )`）は、イベントを受けず、入力だけを止めます。
- フォーカスは、矢印で、位置から自動で動きます（`SetNavigationTarget` で、向きごとの移動先を名前で決められる）。スライダーのように矢印を使う部品は、`OnNavigate` で受けます。
- イベントは `TUIEvent`（`OnClicked`・`OnPressed`・`OnReleased`・`OnHoverBegin` など）。複数の受け手を登録でき、呼んでいる最中の登録・解除も安全です。
- 木から外した・破棄した部品が、ホバー・押下・フォーカスに残っていても、安全に外されます。

## 描画先（レンダーターゲット）を表示する

`CUIRenderTargetView` は、描画先を、ウィジェットとして表示します（ミニマップ・3D のプレビュー・キャラクターの立ち絵・ワールドの中のモニターなど）。

```cpp
View->SetTargetFactory( [Assets]( int32 Width, int32 Height ) -> TSharedPtr<IImage>
{
    FRenderTargetDesc Desc;
    Desc.Width = Width;
    Desc.Height = Height;
    return Assets->CreateRenderTarget( "Minimap", Desc );
} );
View->SetRenderFunction( []( FDrawContext& CommandList, int32 Width, int32 Height )
{
    // 描画先へ描く命令を記録する（ライブの 3D なら、カメラを替えて、ワールドを描く）
} );
View->SetResolutionScale( 0.5f );   // 矩形の半分の解像度
View->SetUpdateInterval( 3 );       // 3 回に 1 回だけ描き直す（0 なら、Invalidate したときだけ）
View->SetInteractive( true );
View->OnViewPressed.Add( []( CUIWidget&, const FVector2& ViewPosition ) { /* 描画先の画素の座標 */ } );
```

## エンジン組み込みの素材

UI の基本画像（窓・パネル・ボタンの 4 状態・チェック・スライダー・プログレスバーなど）と、アイコン（73 個。白一色なので、ブラシの色で好きな色にして使う）は、`EngineContent` にあります（`Engine:/UI/...`・`Engine:/Icons/...`）。中身と作り方は [`EngineContent/README.md`](../EngineContent/README.md) にあります。

## これから

スタイルのテーマ、スクロール枠・リスト・入力欄・ツールチップ、入力の文脈（メニューを開いている間は、ゲームの入力を止める）、プロパティの結び付け（MVVM）、マークアップ（`.gxui`）、マテリアルのブラシ（シェーダーでの表現）は、順に足していきます。

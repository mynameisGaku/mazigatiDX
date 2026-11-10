# UI（ウィジェット）

ゲームにも、ツールにも使える、部品の木で作る UI です。見た目（ブラシ）・レイアウト・入力・部品が分かれていて、どれも差し替えられます。描画は、コマンドリスト（`FDrawContext`）へ記録するだけなので、DxLib 以外のバックエンドでも動きます。見本は `Samples.exe --sample=UI` です。

## 構成

|層|内容|場所（`Library/Framework/UI`）|
|---|---|---|
|部品の木とレイアウト|`CUIWidget`（余白・寄せ方・最小 / 最大・固定の大きさ・不透明度・当たり判定）、`CUICanvasPanel`（アンカー）、`CUIStackPanel`（縦横）、`CUIGridPanel`（格子）|`Core`|
|見た目|`FUIBrush`: 色（角丸・枠）・画像（引き伸ばし / 収める / 覆う / タイル / 9 分割）・コールバック・重ね|`Brush`|
|入力|`CUIContext`（ホバー・押下・クリック・フォーカス・決定・戻る）、`NUIInput::MakeFrame`（ゲームの入力 → UI の入力）|`Core`|
|部品|`CUIImage`・`CUILabel`・`CUIProgressBar`・`CUIButton`・`CUICheckBox`・`CUISlider`・`CUIScrollBox`・`CUIListView`・`CUITextBox`（入力欄）・`CUIDropdown`・`CUIDialog`（確認のダイアログ）・`CUIRenderTargetView`|`Widgets`|
|画面のスタック|`CUIScreenStack`: 画面を積み重ねて、描く順と入力の行き先を決める（HUD・メニュー・ダイアログ）|`Core`|
|結び付け|`TObservable` / `TObservableArray` / `FUICommand`（ビューモデルの値・配列・操作）と、`CUIBindingSet`（部品との結び付け）|`Binding`|
|テーマ|`FUITheme`（名前つきの色・数・ブラシ・フォント・スタイル。親を持てる）。`FUIDefaultStyle::MakeTheme` が基本のテーマを作る|`Style`|
|マークアップ|`NUIXml`（XML の読み取り）・`CUIMarkupLoader`（XML から部品の木を作る）・`CUIMarkupDocument`（`.gxui` ファイルの読み込みとホットリロード）|`Markup`|
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

## スクロール枠

`CUIScrollBox` は、中身（子）が枠より大きいとき、スクロールして見せます（縦・横）。子は、枠の矩形でクリップされます（外へはみ出した部分は、描かれず、当たりません）。

- **ホイール**: 枠の上で回すと、`SetWheelStep`（既定 48 画素）ずつ動きます。動かせない（中身が収まっている）枠は、ホイールを使わずに、外側のスクロール枠へ伝えます。
- **フォーカス**: 中の部品がフォーカスを得ると、その部品が見える位置まで、自動でスクロールします（矢印・Tab で動いても、見失いません）。
- **スクロールバー**: `SetScrollBar( 溝, つまみ, 太さ )`。中身が収まらないときだけ、枠の端に出ます（つまみの長さは、見えている割合）。つまみは、つかんで動かせます（ドラッグ）。つまみの外の溝を押すと、押した側へ 1 ページ（枠の大きさ）ずつ送ります。
- 量は `SetScrollOffset` / `GetScrollOffset` / `GetMaxScrollOffset`、画面の矩形を見える位置へ動かすには `ScrollIntoView`。

## リスト（仮想化）

`CUIListView` は、行が何千・何万あっても、見えている分だけの部品（行）を作って、スクロールで使い回します。行の中身は、ユーザーが決めます（部品を作る関数と、データを行へ入れる関数を渡す）。

```cpp
List->SetItemFactory(
    []() -> TSharedPtr<CUIWidget> { return MakeShared<CUILabel>(); },        // 行の部品を作る（使い回すので、少ししか呼ばれない）
    [&Names]( CUIWidget& Item, int32 Index, bool bSelected )                 // データを行へ入れる（行が別の番号に使われるたびに）
    {
        static_cast<CUILabel&>( Item ).SetText( Names[Index] );
    } );
List->SetItemCount( Names.Num() );
List->OnSelectionChanged.Add( []( CUIWidget&, int32 Index ) { /* 選択が変わった */ } );
List->OnItemClicked.Add( []( CUIWidget&, int32 Index ) { /* クリック・決定された */ } );
```

- 行の大きさ（縦なら高さ、横なら幅）は、すべて同じ（`SetItemHeight`）。行ごとに違うなら、`SetItemSizeProvider( []( int32 Index ) { return 28.0f; } )`（データが変わって大きさも変わるときは、`SetItemCount` か `InvalidateItemSizes`）。向きは `SetOrientation( EUIOrientation::Horizontal )` で、横に並べられます（矢印は左右、ホイールも横）。実体のある行は、高さ 120 のリストで 4〜5 個ほどです（`GetNumRealizedItems`）。
- 選択は 1 つ（`SetSelectedIndex`。-1 は、なし）。クリックで選び、フォーカス中は上下の矢印で選びます（端では、フォーカスが外へ動く）。選んだ行は、見える位置までスクロールします。決定（Enter・A ボタン）は、選んでいる行のクリックです。
- データが変わったら、`SetItemCount`（数が変わるとき）か `Refresh`（中身だけ変わるとき）。数が減って、選択やスクロールの量が範囲の外になったら、範囲に収めます。
- ホイールとスクロールバー（つまみのドラッグ・溝のクリックの 1 ページ送りも）は、スクロール枠と同じです。スクロールバーを押した操作は、行の選択にも、クリックにも、なりません。

## 結び付け（MVVM）

ゲームのデータ（ビューモデル）と、UI の部品を、**結び付け**で結べます。イベントの結び付け（`OnClicked` など）・部品の直接の操作と、混ぜて使ってかまいません。ビューモデルは、UI の部品を、いっさい知りません。

```cpp
// ビューモデル: UI を知らない。値のルールは、ここに書く
struct FSettingsViewModel
{
    TObservable<float32> Volume { 50.0f };
    TObservable<bool> Music { true };
    TObservable<FString> Name;
    TObservable<FString> VolumeText { FString( "50%" ) };       // 音量から作る値
    TObservableArray<FString> Items;
    FUICommand Reset { []() { /* 既定へ戻す */ } };
    FSettingsViewModel() { Volume.OnChanged.Add( [this]( const float32& V ) { VolumeText.Set( FString::Printf( "%d%%", int32( V ) ) ); } ); }
};

// 窓: 結ぶだけ（組が消えると、すべて解除される）
CUIBindingSet Bindings;
Bindings.BindValue( ViewModel.Volume, VolumeSlider );        // 双方向: スライダーを動かすと Volume が変わり、Volume を変えるとスライダーが動く
Bindings.BindChecked( ViewModel.Music, MusicCheckBox );      // 双方向
Bindings.BindText( ViewModel.Name, NameBox );                // 双方向
Bindings.BindLabel( ViewModel.VolumeText, ValueLabel );      // 一方向
Bindings.BindList( ViewModel.Items, ItemList );              // 配列が変わると、リストの行の数と中身が替わる
Bindings.BindCommand( ResetButton, ViewModel.Reset );        // 押すと実行する。実行できるか（Enabled）で、押せるかも替わる
Bindings.BindEnabled( ViewModel.CanEdit, EditPanel );        // true なら使える
Bindings.BindVisible( ViewModel.ShowHelp, HelpPanel );       // true なら見える（false は、場所も取らない）
Bindings.Bind( ViewModel.Volume, []( const float32& V ) { /* 好きな反映 */ } );
Bindings.BindFunction<int32>( []() { return Score; }, []( const int32& V ) { /* 毎フレームの関数 */ } );   // Bindings.Update() を毎フレーム呼ぶ
```

- 結んだ直後に、いまの値を、部品へ 1 回反映します。
- `TObservable::Set` は、値が変わったときだけ通知します（同じ値なら何もしない）。双方向でも、往復して止まらなくなりません。入力欄のカーソルも、同じ値なら動きません。
- スライダーは、範囲と刻みに収めた値を見せます（観測値は、収める前の値のまま）。
- ビューモデルは、結び付けの組より先に消えても安全です（解除のとき、ビューモデルが、まだあるかを確かめます）。部品は、結び付けが共有の参照で持ちます。
- 見本は、サンプル `UI` の設定の窓（`Samples/Source/UI/SettingsViewModel.h`）です。

## マークアップ（XML）

UI は、コードで組む代わりに、XML で書けます（`Library/Framework/UI/Markup`）。読み込むと、コードで組んだものと同じ部品の木になります。名前（フォント・ブラシ・スタイル・イベントの受け手・ビューモデルの値）は、**文脈**（`FUIMarkupContext`）に登録したものを使います。ゲームの種類にも描画の方式にも、依存しません。

```xml
<Panel Name="Window" Padding="24,20" Background="Window">
  <VerticalStack Spacing="12">
    <Label Text="{Binding Title}" Font="Title" Color="#FFE296" />
    <CheckBox Checked="{Binding Music}" Style="Default" Width="28" Height="28" />
    <Slider Value="{Binding Volume}" Min="0" Max="100" Step="5" Height="28" Style="Default" />
    <TextBox Text="{Binding Name}" Placeholder="Your name" MaxLength="16" Height="38" Style="Default" />
    <HorizontalStack Spacing="8" Height="44">
      <Button Text="OK" Width="120" Style="Default" OnClicked="Confirm" />
      <Button Text="Reset" Width="120" Style="Default" Command="{Binding Reset}" />
    </HorizontalStack>
    <ListView Items="{Binding Items}" ItemHeight="24" Height="120" Font="Body" />
  </VerticalStack>
</Panel>
```

```cpp
FUIMarkupContext Context;
Context.RegisterFont( "Title", TitleFont );
Context.RegisterBrush( "Window", WindowBrush );
Context.RegisterButtonStyle( "Default", ButtonStyle );          // ほか RegisterCheckBoxStyle / RegisterSliderStyle / RegisterTextBoxStyle
Context.RegisterHandler( "Confirm", []() { /* ... */ } );
Context.RegisterString( "Title", ViewModel.Title );             // ほか RegisterFloat / RegisterBool / RegisterCommand / RegisterStringList
CUIBindingSet Bindings;                                         // 結び付けは、この組が持つ
const FUIMarkupResult Result = CUIMarkupLoader::Load( XmlText, Context, Bindings );
if ( Result.bSuccess ) { Screen->AddChild( Result.Root ); }
```

**要素**: `Panel`（子を重ねる）・`Canvas`・`VerticalStack`・`HorizontalStack`・`Stack`・`Grid`・`ScrollBox`・`Label`・`Image`・`ProgressBar`・`Button`・`CheckBox`・`Slider`・`TextBox`・`ListView`。

**どの要素にも書ける属性**

|属性|意味|
|---|---|
|`Name`|名前（`FindByName` で探せる）|
|`Width` `Height` `MinWidth` `MinHeight` `MaxWidth` `MaxHeight`|大きさ（0 以上の数）|
|`Margin` `Padding`|余白。数が 1 個（四辺）・2 個（左右, 上下）・4 個（左, 上, 右, 下）|
|`HAlign` `VAlign`|寄せ方: `Start` `Center` `End` `Stretch`|
|`Weight`|スタックの余りを分ける割合|
|`Column` `Row` `ColumnSpan` `RowSpan`|グリッドの置き場所（Span は 1 以上）|
|`Anchor` `AnchorMin` `AnchorMax` `Pivot` `Offsets`|キャンバスの置き場所。`Anchor` は `TopLeft` `Top` `TopRight` `Left` `Center` `Right` `BottomLeft` `Bottom` `BottomRight` `Stretch`。`Offsets` は 4 個の数|
|`Visibility`|`Visible` `Hidden` `Collapsed`|
|`Enabled` `Visible`|`true` `false`、または `{Binding 名前}`|
|`Opacity` `Clip` `HitTestable` `Interactive` `Focusable`|不透明度と、真偽の設定|
|`Background`|`#RRGGBB` `#RRGGBBAA` `#RGB`、または登録したブラシの名前|
|`Tooltip`|ツールチップの文字（ポインターが止まると出る）|

**要素ごとの属性**

|要素|属性|
|---|---|
|`VerticalStack` `HorizontalStack` `Stack`|`Spacing` `Orientation`|
|`Grid`|`Columns` `Rows`（`"100, Auto, *, 2*"`: 数 = 画素、`Auto` = 中身、`*` = 残りを 1、`n*` = 残りを n）|
|`ScrollBox`|`Orientation` `WheelStep` `ScrollBarWidth` `ScrollBarTrack` `ScrollBarThumb`|
|`Label`|`Text` `Font` `Color` `TextAlign` `VTextAlign` `WrapWidth` `Lines`|
|`Image`|`Source`（画像を読む関数を `SetImageLoader` で渡す）`Mode`（`Stretch` `Fit` `Fill` `Tile` `NineSlice`）`NineSlice`（余白。書くと 9 スライス）`Tint`|
|`ProgressBar`|`Value` `Orientation` `Back` `Fill` `FillInset`|
|`Button`|`Text` `Font` `TextColor` `Style` `OnClicked` `Command`|
|`CheckBox`|`Checked` `Style` `OnToggled` `OnClicked`|
|`Slider`|`Value` `Min` `Max` `Step` `Orientation` `Style` `OnValueChanged`|
|`TextBox`|`Text` `Placeholder` `MaxLength` `Password` `Font` `Style` `OnSubmitted` `OnCancelled`|
|`Dropdown`|`Items`（`{Binding 名前}`、または `"Low, Medium, High"`）`SelectedIndex`（番号、または `{Binding 名前}`）`Placeholder` `Font` `Style` `OnSelectionChanged`|
|`ListView`|`Items`（`{Binding 名前}`）`Orientation` `ItemHeight` `Font` `ItemColor` `SelectedBackground` `ItemPadding` `ScrollBarWidth` `ScrollBarTrack` `ScrollBarThumb` `OnItemClicked`|

**結び付け** `{Binding 名前}` は、`Label.Text`（一方向）・`TextBox.Text` `CheckBox.Checked` `Slider.Value`（双方向）・`ProgressBar.Value`・`Button.Command`・`ListView.Items`・`Dropdown.Items`・`Dropdown.SelectedIndex`（双方向）・`Enabled`・`Visible` に書けます。

**失敗と警告**
- 読めない XML・知らない要素・属性の値の書き間違い（数・色・列挙・範囲）は、**失敗**です（`bSuccess = false`。理由は `Error`、行番号つき。何も作らず、結び付けも残りません）。
- 文脈に無い名前（フォント・ブラシ・スタイル・イベントの受け手・結び付け）と、知らない属性は、**警告**（`Warnings`、行番号つき）にして、その属性を無視して続けます。`Context.bWarningsAreErrors = true` で、警告も失敗にできます。
- XML は、要素・属性（`"` か `'`）・文字・コメント・CDATA・`&lt; &gt; &amp; &quot; &apos; &#65; &#x41;` を読みます。名前空間と DTD は扱いません。入れ子は 100 段までです。

**ファイル（`.gxui`）とホットリロード**: マークアップの文字列は、ファイル（拡張子 `.gxui`）に書いて、アセットのパスで読めます。`CUIMarkupDocument` が、読み込みと作り直しを受け持ちます。

```cpp
CUIMarkupDocument Document;
if ( Document.Load( Assets, "UI/Settings.gxui", Context, Bindings ) )      // 先頭の BOM はあってもなくてもよい
{
    Parent->AddChild( Document.GetRoot() );
}
// 毎フレーム（または数秒おき）
if ( Document.ReloadIfChanged( Assets, Context, Bindings ) )                // 中身が前と違えば、作り直す
{
    Parent->RemoveChild( OldRoot.Get() );                                   // 古い木を外し、新しい木（GetRoot）を同じ場所へ付ける
    Parent->AddChild( Document.GetRoot() );
}
```

- 作り直しに失敗したとき（XML を書き換えている途中など）は、**古い木も結び付けも、そのまま残します**（`GetLastError()` に理由）。同じ壊れた中身は、繰り返し試しません。直して保存すると、また試します。
- 作り直しが成功すると、古い木の結び付けは外れ、新しい木が結ばれます（1 つの文書に、専用の `CUIBindingSet` を 1 つ使ってください）。
- 見本: サンプル `UI` の設定の窓は、`Samples/Assets/UI/Settings.gxui` に書いてあります。サンプルを動かしたまま、そのファイルを編集して保存すると、窓が作り直されます。

## マテリアルのブラシ

`FUIBrush::MakeMaterial( マテリアル )` は、UI ドメインのマテリアル（[Docs/MATERIAL.md](MATERIAL.md)）を、矩形に描くブラシです。シェーダーで、うにょうにょ・ビカビカさせられます。マテリアルの入力 `HoverAmount`・`PressAmount`・`Value01`・`Enabled` は、このブラシを描く部品の状態（ホバー・押下・スライダーなどの値・使えるか）で、`Time` は `CMaterialSubsystem` の時間です。ブラシの `Color` は、出力に掛ける色です。エンジン組み込みの `Engine:/Materials/UI/GlowButton`（ホバーで光る）・`Shimmer`（光の帯が流れる）が、すぐ使えます。マークアップでは、`Background="{Material Engine:/Materials/UI/GlowButton.gxmaterial}"` と書くと、`CMaterialSubsystem` が読んだ（キャッシュした）マテリアルのブラシになります（読めない・UI などのドメインではないときは、警告を出して、背景は空のままです）。

## テーマ

見た目の「名前つきの値」（色・数・ブラシ・フォント・部品のスタイル）は、**テーマ**（`FUITheme`。`Library/Framework/UI/Style/UITheme.h`）にまとめられます。UI は、値そのものではなく、名前で参照します。テーマを差し替えれば、同じ UI の見た目が、まとめて変わります。

```cpp
TSharedPtr<FUITheme> Base = Style.MakeTheme( BodyFont, TitleFont );     // FUIDefaultStyle から、基本のテーマ
Base->SetColor( "Danger", FColor32( 255, 120, 120 ) );                  // 自分の名前を足す
TSharedPtr<FUITheme> Blue = MakeShared<FUITheme>();
Blue->SetParent( Base );                                                // 親: 名前が自分に無ければ、親で探す
Blue->SetColor( "Accent", FColor32( 130, 190, 255 ) );                  // 違うところだけ上書き
Context.SetTheme( Blue );                                               // マークアップの文脈へ
```

- **名前の種類**: 色（`SetColor`）・数（`SetNumber`: 余白や間隔など）・ブラシ・フォント・ボタン／チェックボックス／スライダー／入力欄のスタイル。種類ごとに、別の名前の置き場です。大文字と小文字は区別します。
- **継承**: テーマは、親のテーマを持てます（親の親へも）。基本のテーマを作り、子で変えたいところだけを上書きします。輪になる親（自分を祖先に持つテーマ）は、`SetParent` が断ります。
- **基本のテーマ**: `FUIDefaultStyle::MakeTheme` が作ります。
  - ブラシ: `Window` `Panel` `Frame` `Tooltip` `TextBox` `TextBoxFocused` `Shadow` `ProgressBack` `ProgressFill`
  - スタイル: ボタン・チェックボックス・スライダー・入力欄の `Default`
  - 色: `Text` `TextMuted` `Accent` `Value` `Error`
  - 数: `Gap`（14）`Spacing`（18）
  - フォント: `Body` `Title`（渡したとき）
- **マークアップでの使い方**: 色と数の属性に、`@名前` と書けます（`Color="@Accent"`、`Spacing="@Gap"`、`Width="@Gap"`、`Background="@Panel"` など）。フォント・ブラシ・スタイルの名前は、**文脈に登録したものを先に**、無ければテーマから探します。テーマに無い `@名前` は、**失敗**です（名前と行番号つき）。余白（`Padding` など、数が 1〜4 個のもの）には、`@` は使えません。
- **テーマの切り替え**: 文脈のテーマを替えて、`CUIMarkupDocument::Rebuild` で作り直すと、同じ `.gxui` が別の見た目になります（`ReloadIfChanged` は、ファイルの中身が同じなら作り直さないので、テーマの切り替えには、`Rebuild` を使います）。ボタンの処理の最中に窓ごと作り直すと危ないので、印だけ付けて、次の更新で作り直します。
- 見本: サンプル `UI` の **Theme** ボタンが、基本・Blue・Green・Rose の順に切り替えます（子のテーマが、`Accent` と `Value` だけ上書きしています）。起動の引数 `--theme-steps=N` で、N 回進めた状態から始められます。

## 入力欄（文字入力）

`CUITextBox` は、1 行の入力欄です。フォーカスがあるとき、`ITextInput` が届ける文字と編集の操作（かな漢字変換を含む）を受けて、文字列を編集します。

```cpp
TSharedPtr<CUITextBox> Name = MakeShared<CUITextBox>();
Name->SetFont( Font );
Name->SetPlaceholder( "Your name" );
Name->SetMaxLength( 16 );
Name->OnSubmitted.Add( []( CUIWidget&, const FString& Text ) { /* Enter で確定された */ } );

// 毎フレーム: 文字入力の窓口（ITextInput）を、橋渡しへ渡し、結果を、窓口へ返す
const FUIInputFrame Frame = NUIInput::MakeFrame( Input, Input, Gamepad, FUIInputMapping(), TextInput, Clipboard );   // Clipboard は IClipboard*（コピー・貼り付けに使う。null なら、できない）
const FUIScreenInputResult Result = Screens.Update( Frame );
TextInput->SetTextInputActive( Result.bWantsTextInput );   // 入力欄にフォーカスがあるときだけ、文字入力（変換）を受ける
```

- 操作: 文字・Backspace・Delete・左右・Home・End。Enter で確定（`OnSubmitted`。フォーカスを手放す）、Escape で取り消し（フォーカスを得たときの文字列へ戻し、`OnCancelled`。フォーカスを手放す）。クリックで、その位置へカーソルを置く。範囲選択・コピー・貼り付けは、まだ扱いません。
- **範囲選択**: ドラッグで選びます。`Shift` + 左右・Home・End で広げ（`ITextInput` が `SelectLeft` などを届けます。DxLib の実装は、Shift が押されているときに、そう届けます）、`Ctrl` + A ですべて選びます。選んだ範囲は、`SelectionColor` の背景で見えます。範囲があるとき、文字を打つと範囲が置き換わり、Backspace・Delete は範囲を消し、左右の矢印は範囲の端へ動いて範囲をやめます。`HasSelection` / `GetSelectedText` / `SetSelection` / `SelectAll`。
- **コピー・切り取り・貼り付け**（`Ctrl` + C / X / V）は、クリップボード（`IClipboard`。DxLib は OS のクリップボード、Null は、アプリの中だけ）で行います。`NUIInput::MakeFrame` の最後の引数（または `FUIInputFrame::Clipboard`）で渡します。渡さなければ、選ぶだけです。パスワード表示の欄は、コピー・切り取りをしません。貼り付ける文字列の改行・タブは、空白になります。
- 入力欄にフォーカスがあるとき、**左右の矢印・決定・戻る**は、入力欄が使います（フォーカスの移動や、ボタンの決定、画面を戻すことには使われない）。上下の矢印と Tab は、これまでどおり、フォーカスを動かします。
- 文字列が欄より長いときは、カーソルが見える位置まで、横へずらして表示します（欄の矩形でクリップ）。かな漢字変換の途中の文字列は、カーソルの位置に、下線つきで出ます。`SetPasswordMode( true )` で `*` 表示。
- カーソルの点滅は、描いた回数で数えます（`SetCaretBlinkFrames`）。文字やカーソルが動いた直後は、必ず見えます。

## ツールチップ

部品に `SetTooltip( "説明" )`（文字）か `SetTooltipWidget( 部品 )`（好きな部品。木に入れない）を決めると、ポインターがその部品の上に、しばらく止まったとき、近くに説明が出ます。

```cpp
Button->SetTooltip( "Back to the defaults" );

FUITooltipStyle Tip;
Tip.Background = Style.Tooltip;       // 背景（9 分割など）
Tip.Font = Font;                      // 無ければ、文字のツールチップは出ない
Tip.Delay = 0.5f;                     // 出すまでの待ち時間（秒）
Screens.SetTooltipStyle( Tip );       // 出す仕事は、画面のスタックが受け持つ（全部の画面の上に描く）

FUIInputFrame Frame = NUIInput::MakeFrame( ... );
Frame.DeltaTime = DeltaTime;          // 待ち時間は、これを足して数える（0 のままだと、出ない）
```

- 当たった部品から親へたどって、**最初にツールチップを持つ部品**が対象です（子に無ければ、親の説明）。ボタンを押している間は出ず、押すと消えます。
- 待ち時間が過ぎると出て、**出る位置は、そのときのポインターの右下**（`Offset`、既定 14, 22）。出たあとは、動かしても追いません。画面からはみ出すときは、反対側（左・上）へ寄せて、画面の中に収めます。
- 上にモーダルな画面があるとき、下の画面のツールチップは出ません。`CUIContext` だけで使うときは、`GetTooltip()` の `bVisible`・`Source`・`Anchor` を読んで、自分で描きます（`SetTooltipDelay` で待ち時間）。
- マークアップでは、どの要素にも `Tooltip="説明"` と書けます。

## ドロップダウン

`CUIDropdown` は、選んでいる項目と矢印を見せるボタンです。押す（クリック・決定）と、下（入りきらなければ上）に、項目の一覧（`CUIListView`）が開きます。

```cpp
TSharedPtr<CUIDropdown> Quality = MakeShared<CUIDropdown>();
Quality->SetFont( Font );
Quality->SetDropdownStyle( Style );                     // FUIDropdownStyle: ボタン・矢印・一覧の背景・選んでいる項目の背景・項目の高さ・見せる数
Quality->SetPopupHost( &Screens );                      // 一覧を積む画面のスタック（ドロップダウンより長く生きること）
Quality->SetItems( { "Low", "Medium", "High" } );       // TArray<FString>
Quality->SetSelectedIndex( 1 );
Quality->OnSelectionChanged.Add( []( CUIWidget&, int32 Index ) { /* 選ばれた */ } );
```

- 一覧は、画面のスタックに**モーダルな画面**として積まれ、開いた直後は、一覧にフォーカスがあります。項目をクリックするか、上下の矢印 + 決定で選ぶと、決まって閉じます。一覧の外のクリックと Escape は、選ばずに閉じます。
- 選ぶ番号は -1（何も選んでいない。`SetPlaceholder` の文字を薄く出す）か 0 以上です。`SetItems` で項目が減ったら、範囲に収めます（番号が変わるなら `OnSelectionChanged`）。
- `SetPopupHost` が無い・項目が無い・使えない（Disabled）ときは、開きません。一覧が開いている間にドロップダウンが消えても、安全です。
- 結び付け: `Bindings.BindItems( 配列, ドロップダウン )`、`Bindings.BindSelectedIndex( TObservable<int32>, ドロップダウン )`（双方向）。マークアップでは `<Dropdown Items="{Binding 名前}" SelectedIndex="{Binding 名前}" />`（`Items="Low, Medium, High"` のように、コンマで区切った文字でもよい。`RegisterInt` と `SetPopupHost` を文脈へ）。
- テーマに `Default` などの名前で、ドロップダウンのスタイルを入れられます（`SetDropdownStyle`。`FUIDefaultStyle::MakeTheme` が `Default` を入れます。矢印の画像だけ、使うときに足してください）。

## ダイアログ

`CUIDialog` は、確認のダイアログ（タイトル・メッセージ・ボタンのいくつか）です。画面のスタックの 1 画面として使い、`NUIDialog::Show` で、モーダルな画面として積みます。

```cpp
FUIDialogStyle Style;   // 暗幕・窓・ボタンのブラシ、フォント、大きさ
TArray<FUIDialogButton> Buttons;
Buttons.Add( FUIDialogButton { "Yes", 1 } );
Buttons.Add( FUIDialogButton { "No", 2 } );
TSharedPtr<CUIDialog> Dialog = MakeShared<CUIDialog>( Style, "Quit?", "Unsaved changes will be lost.", Buttons, /*既定*/ 2, /*戻る*/ 2 );
Dialog->OnResult.Add( []( CUIWidget&, int32 Id ) { if ( Id == 1 ) { /* Yes */ } } );
NUIDialog::Show( Screens, Dialog );   // どれかを選ぶと、自動で閉じる
```

- 画面全体を暗幕で覆い（下の画面へ、クリックを通しません）、中央に窓を置きます。最初のフォーカスは、既定のボタンです（Enter・A ボタンですぐに決められる）。
- 結果は `OnResult`（選んだボタンの番号）。戻る（Escape・B ボタン）は、`CancelId` として届きます（`-1` なら、何も起こさず、ダイアログも閉じません）。
- 画面のスタックの、積む・外す（`Push`・`Pop`・`Remove`・`Clear`）は、`Update` の最中（ボタンのイベントの中など）に呼んでも安全です。その場合は、`Update` の終わりに、呼んだ順で反映されます（`Push` が返す番号は、すぐに使えます）。

## 画面のスタックと、ゲームの入力

`CUIScreenStack` は、画面（部品の木のルート）を積み重ねます。入力は、上の画面から先に渡し、上の画面が使った入力は、下の画面へは届きません。

```cpp
CUIScreenStack Screens;
Screens.SetArea( FUIRect( 0, 0, 1280, 720 ) );
Screens.Push( Hud, FUIScreenOptions { false, false, false } );   // HUD: モーダルでなく、ゲームの入力も止めず、戻るでも閉じない
const int32 Menu = Screens.Push( PauseMenu );                    // メニュー: モーダル・ゲームの入力を止める・Escape で閉じる

const FUIScreenInputResult Result = Screens.Update( Frame );     // 毎フレーム（レイアウトも更新する）
if ( !Result.bGameInputBlocked ) { /* ゲーム自身の入力 */ }
Screens.Paint( CommandList );                                    // 下から順に描く
```

- **モーダル**（既定）の画面は、下の画面へ、ポインターもナビゲーションも渡しません。モーダルでない画面は、自分の部品の上にポインターが無いときだけ、ポインターを下へ渡します。ナビゲーション（矢印・Tab・決定・戻る）は、いちばん上の画面にだけ渡します。
- **ゲームの入力を止めるか**は、`bGameInputBlocked` で分かります（止める画面がある・ポインターを UI が使っている・ナビゲーションやホイールを UI が使った）。メニューを開いている間は、攻撃が出ません。
- 戻る（Escape・B ボタン）で、いちばん上の画面が `bPopOnCancel` なら、その画面が外れます（`OnPopped( 画面の番号 )`）。外れた画面の部品は、ホバー・フォーカスを失います。
- 画面ごとにコンテキスト（ホバー・フォーカス）を持ちます（`FindContext( 番号 )`）。

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

## 動き（アニメーション）

部品を、レイアウトを変えずに動かせます（ほかの部品は、動きません）。

- **描くときの変換**: `Widget->SetRenderTransform( FUIRenderTransform )` で、ずらす（`Translation`）・拡大（`Scale`）・回す（`Rotation`。ラジアン）を、矩形の中の `Pivot`（既定は中央）を中心に掛けます。子孫にも掛かります。当たり判定とレイアウトは、変わりません。図形・画像・文字に掛かります（マテリアルのブラシには、掛かりません）。
- **`CUIAnimator`**（`Library/Framework/UI/Animation`）: 値を、時間をかけて動かします。ホストが 1 つ持ち、毎フレーム `Update( 秒 )` を呼びます。

```cpp
CUIAnimator Animator;
Animator.Play( Panel, EUIAnimatedProperty::Opacity, 0.0f, 1.0f );                  // 0.25 秒で、ふわっと出す
FUIAnimationOptions Slide;
Slide.Duration = 0.4f;
Slide.Easing = EUIEasing::BackOut;                                                 // 少し行き過ぎて、戻る
Slide.OnFinished = [this]() { /* 終わったとき */ };
Animator.Play( Panel, EUIAnimatedProperty::TranslationY, 40.0f, 0.0f, Slide );
Animator.PlayValue( 0.0f, 100.0f, Slide, [this]( float32 Value ) { /* 好きな値 */ } );
Animator.Update( DeltaTime );
```

  - 動かせる値は、`Opacity`・`TranslationX` / `Y`・`Scale`・`ScaleX` / `Y`・`Rotation` です。`PlayTo` は、いまの値から動かします。
  - `FUIAnimationOptions`: `Duration`・`Delay`・`Easing`・`Loop`（`Once`・`Loop`・`PingPong`）・`OnFinished`。
  - イージング（`EUIEasing`）は、`Linear`・`QuadIn` / `Out` / `InOut`・`CubicIn` / `Out` / `InOut`・`SineInOut`・`BackOut`・`ElasticOut`・`BounceOut` です。
  - 同じ部品の同じ値を、もう一度 `Play` すると、前の動きを置き換えます。部品が消えると、その動きも消えます。
- **画面の出入り**: `FUIScreenOptions::Transition`（`Fade`・`SlideUp` / `Down` / `Left` / `Right`・`Scale`、`Duration`、`Easing`）を決めて `Push` すると、出るときと消えるときに、演出が付きます。消える演出の間の画面は、描かれますが、入力は受けません（ゲームの入力も、止めません）。演出が終わると、実際に外れて、`OnPopped` が起きます。ダイアログは、`NUIDialog::Show( Screens, Dialog, Transition )` です。

## エンジン組み込みの素材

UI の基本画像（窓・パネル・ボタンの 4 状態・チェック・スライダー・プログレスバーなど）と、アイコン（73 個。白一色なので、ブラシの色で好きな色にして使う）は、`EngineContent` にあります（`Engine:/UI/...`・`Engine:/Icons/...`）。中身と作り方は [`EngineContent/README.md`](../EngineContent/README.md) にあります。

## これから

テーマの JSON ファイル、状態ごとのスタイル、入力欄の複数行とダブルクリックの選択、マークアップでの動きの指定は、順に足していきます。

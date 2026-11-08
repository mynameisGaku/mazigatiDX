# UI（ウィジェット）

ゲームにも、ツールにも使える、部品の木で作る UI です。見た目（ブラシ）・レイアウト・入力・部品が分かれていて、どれも差し替えられます。描画は、コマンドリスト（`FDrawContext`）へ記録するだけなので、DxLib 以外のバックエンドでも動きます。見本は `Samples.exe --sample=UI` です。

## 構成

|層|内容|場所（`Library/Framework/UI`）|
|---|---|---|
|部品の木とレイアウト|`CUIWidget`（余白・寄せ方・最小 / 最大・固定の大きさ・不透明度・当たり判定）、`CUICanvasPanel`（アンカー）、`CUIStackPanel`（縦横）、`CUIGridPanel`（格子）|`Core`|
|見た目|`FUIBrush`: 色（角丸・枠）・画像（引き伸ばし / 収める / 覆う / タイル / 9 分割）・コールバック・重ね|`Brush`|
|入力|`CUIContext`（ホバー・押下・クリック・フォーカス・決定・戻る）、`NUIInput::MakeFrame`（ゲームの入力 → UI の入力）|`Core`|
|部品|`CUIImage`・`CUILabel`・`CUIProgressBar`・`CUIButton`・`CUICheckBox`・`CUISlider`・`CUIScrollBox`・`CUIListView`・`CUITextBox`（入力欄）・`CUIDialog`（確認のダイアログ）・`CUIRenderTargetView`|`Widgets`|
|画面のスタック|`CUIScreenStack`: 画面を積み重ねて、描く順と入力の行き先を決める（HUD・メニュー・ダイアログ）|`Core`|
|結び付け|`TObservable` / `TObservableArray` / `FUICommand`（ビューモデルの値・配列・操作）と、`CUIBindingSet`（部品との結び付け）|`Binding`|
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
- **スクロールバー**: `SetScrollBar( 溝, つまみ, 太さ )`。中身が収まらないときだけ、枠の端に出ます（つまみの長さは、見えている割合）。いまは、見せるだけです。
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

- 行の高さは、すべて同じ（`SetItemHeight`）。実体のある行は、高さ 120 のリストで 4〜5 個ほどです（`GetNumRealizedItems`）。
- 選択は 1 つ（`SetSelectedIndex`。-1 は、なし）。クリックで選び、フォーカス中は上下の矢印で選びます（端では、フォーカスが外へ動く）。選んだ行は、見える位置までスクロールします。決定（Enter・A ボタン）は、選んでいる行のクリックです。
- データが変わったら、`SetItemCount`（数が変わるとき）か `Refresh`（中身だけ変わるとき）。数が減って、選択やスクロールの量が範囲の外になったら、範囲に収めます。
- ホイールとスクロールバーは、スクロール枠と同じです。

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

## 入力欄（文字入力）

`CUITextBox` は、1 行の入力欄です。フォーカスがあるとき、`ITextInput` が届ける文字と編集の操作（かな漢字変換を含む）を受けて、文字列を編集します。

```cpp
TSharedPtr<CUITextBox> Name = MakeShared<CUITextBox>();
Name->SetFont( Font );
Name->SetPlaceholder( "Your name" );
Name->SetMaxLength( 16 );
Name->OnSubmitted.Add( []( CUIWidget&, const FString& Text ) { /* Enter で確定された */ } );

// 毎フレーム: 文字入力の窓口（ITextInput）を、橋渡しへ渡し、結果を、窓口へ返す
const FUIInputFrame Frame = NUIInput::MakeFrame( Input, Input, Gamepad, FUIInputMapping(), TextInput );
const FUIScreenInputResult Result = Screens.Update( Frame );
TextInput->SetTextInputActive( Result.bWantsTextInput );   // 入力欄にフォーカスがあるときだけ、文字入力（変換）を受ける
```

- 操作: 文字・Backspace・Delete・左右・Home・End。Enter で確定（`OnSubmitted`。フォーカスを手放す）、Escape で取り消し（フォーカスを得たときの文字列へ戻し、`OnCancelled`。フォーカスを手放す）。クリックで、その位置へカーソルを置く。範囲選択・コピー・貼り付けは、まだ扱いません。
- 入力欄にフォーカスがあるとき、**左右の矢印・決定・戻る**は、入力欄が使います（フォーカスの移動や、ボタンの決定、画面を戻すことには使われない）。上下の矢印と Tab は、これまでどおり、フォーカスを動かします。
- 文字列が欄より長いときは、カーソルが見える位置まで、横へずらして表示します（欄の矩形でクリップ）。かな漢字変換の途中の文字列は、カーソルの位置に、下線つきで出ます。`SetPasswordMode( true )` で `*` 表示。
- カーソルの点滅は、描いた回数で数えます（`SetCaretBlinkFrames`）。文字やカーソルが動いた直後は、必ず見えます。

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

## エンジン組み込みの素材

UI の基本画像（窓・パネル・ボタンの 4 状態・チェック・スライダー・プログレスバーなど）と、アイコン（73 個。白一色なので、ブラシの色で好きな色にして使う）は、`EngineContent` にあります（`Engine:/UI/...`・`Engine:/Icons/...`）。中身と作り方は [`EngineContent/README.md`](../EngineContent/README.md) にあります。

## これから

スタイルのテーマ、ツールチップ・ドロップダウン・入力欄の範囲選択とコピー・貼り付け・スクロールバーのドラッグ・横向きのリスト・行の高さが違うリスト、マークアップ（`.gxui`）での結び付けの指定（`{Binding ...}`）、マテリアルのブラシ（シェーダーでの表現）は、順に足していきます。

# DxLib ラッパーの設計規約

mazigatiDX は、DxLib を本気で包み、モダンな C++ で使いやすくするための土台です。この文書は、新しく DxLib の機能を包むとき（と、既存の窓口を直すとき）に守る約束をまとめます。ゲーム固有の機能（インベントリ・戦闘・AI・UI の部品・セーブの形式など）は土台に入れず、ユーザーのコードに任せます。

## 1. 層と依存の向き

```
ユーザーのゲーム（Source/…）
   │ 使う
   ▼
Library/Framework   … シーン・オブジェクト・アセット・描画の記録（FDrawContext）・エディタが使う定義
   │ 使う
   ▼
Library/Core/Interfaces  … 窓口（IPlatform / IInput / IAudioBackend / IAssetBackend / IDrawExecutor …）と、値の型（*Types.h）
   ▲ 実装する
   │
Library/Platform/DxLib  … DxLib を呼ぶ実装（DxLibBackend / DxLibAssetBackend / DxLibDrawExecutor / DxLibInput …）
```

- `Library` は静的ライブラリ（`mazigatiDX`）で、ゲームの実行ファイル（`Source`）がリンクする。フレームワークがゲームのコードを include することはない（入口は `NGameMain::RunGame<TGame>()`）。
- ゲームの実行ファイルは `Library` を使う側（`Source`）に置く。レベルエディタ・エフェクトエディタなどの**エディタは外部ツール**（別の実行ファイル）で、ゲームの実行ファイルには入れない。どちらも同じ `Library` を使う（移行中: 現在は 1 つの実行ファイルに同居し、`--tool=` で起動する）。
- **DxLib を呼んでよいのは `Library/Platform/DxLib` だけ**。窓口（`Core/Interfaces`）と `Framework` は DxLib のヘッダーを含まない。Jolt・ImGui も同じ（窓口には出さない）。
- 窓口は**インターフェース先行**で作る。実装を差し替えられる（確認用の偽の実装、将来の別バックエンド）ことが目的で、DxLib の関数をそのまま転送するだけの層にしない。
- 値の型（設定・結果・列挙）は `*Types.h` に分ける。窓口を使う側が、実装のヘッダーを include しなくて済むようにする。

## 2. 型と単位

| 対象 | 規約 |
|---|---|
| 名前 | 第三者でない名前空間は `N` で始める。型の接頭辞は `F`（値・構造体）、`C`（クラス）、`I`（インターフェース）、`E`（`enum class`）、`T`（テンプレート） |
| 整数・実数 | `int32` / `uint32` / `float32` / `float64` など、`Library/Core/Types` の型。素の `int` / `long` を窓口に出さない |
| 色 | `FColor32`（`r g b a`、0..255）。DxLib の色（`unsigned int`）への変換は実装の中だけ |
| ベクトル・行列 | `FVector2/3` `FMatrix4x4` `FQuaternion`（左手系・Y 上・行ベクトル）。DxLib の `VECTOR` / `MATRIX` への変換は実装の中だけ |
| 角度 | ラジアン（度は編集用 JSON の境界だけ） |
| 時間 | 秒（`float32`、長い時間は `float64`） |
| 画面座標 | ピクセル、左上が原点 |
| DxLib の定数（`DX_…`） | `enum class` に置き換える。組み合わせるものはフラグ型にする。窓口に `DX_` の値を出さない |
| 文字列 | 窓口は `FString` / `const char*`（UTF-8。第 6 節） |

## 3. 資源とハンドル

- DxLib のハンドル（`int`）を**そのまま公開しない**。画像・モデル・音・フォントなどは、型のある所有者（`CTexture` `CModel` `CSound` `CFont` など。`CAssetManager` が読み込み・共有・破棄を行う）か、型付きのハンドルで扱う。画像は、描画の窓口（`FDrawContext`）に、インターフェース `IImage`（`CTexture` が実装。`Core/Interfaces/Render/IImage.h`）か、型付きのハンドル `FImageHandle` で渡す。描画の側は、`CTexture` などの具体的な型を知らない。
- 資源の寿命は参照（共有ポインター）で決める。使う側が強参照を持つ間は有効で、最後の参照が消えたとき、メインスレッドで破棄する（別スレッドからの解放は遅延させて受ける）。
- 読み込みの失敗は、空の参照とログで返す。準備待ち（非同期）と恒久的な失敗を取り違えない。
- 無効なハンドルを表す値（`-1`）は実装の中の都合で、利用者に見せない。

## 4. スレッドと状態

- DxLib はスレッドセーフではない。**DxLib を呼ぶ関数は、メインスレッドからだけ呼ぶ**。窓口のコメントに明記する。
- 描画は「記録 → 実行」に分ける。記録（`FDrawContext`）は並行にできるが、実行（`IDrawExecutor`）はメインスレッドで、記録された順に DxLib を呼ぶ。新しい描画を足すときは、命令（`EDrawCommand`）・データ・記録・実行をそろえる。旧命令の番号と形式は変えない。オブジェクトの描画は、DX12 のコマンドリストと同じ方式にする: `CGameObject::Draw( FDrawContext& CommandList )` は、シーンが渡すコマンドリストへ記録するだけで、DxLib も実行先も知らない（シーンがオブジェクトごとにコマンドリストを空にして渡し、記録されたものを描画順に実行する）。オブジェクトの外（シーンの `OnDraw` など、メインスレッド）から描くときは、`FImmediateDraw Draw( GetDrawExecutor() );` で同じ書き方のまま、その場で実行できる。
- DxLib は**大域の状態**（ブレンド・明るさ・描画先・カメラ・ライト・Z バッファ・カリング・描画領域など）を持つ。包むときは、状態を変えたら**元に戻す**ことを実装の責任にするか、スコープで戻す型を用意する。呼び出し側に「前の状態を覚えておいて戻す」ことを要求しない。
- 取得できない状態（DxLib に getter がないもの）は推測せず、フレームワーク側が明示的に持つ。

## 5. エラー

- 例外は使わない。失敗は `TResult<T>` か `bool` と、原因を書いたログ（カテゴリ付き）で返す。
- DxLib が返す `-1` などの値を、そのまま返さない。窓口の意味（空の参照・`false`・エラー値）に直す。
- 前提が破れたとき（不正な引数・NaN・範囲外）は、何もせずにログを出して失敗を返す。DxLib 側のクラッシュや assert に任せない。

## 6. 文字列と文字コード

- **文字列は UTF-8 に統一した**（2026-10-28）。`FString` の中身は UTF-8 のバイト列で、ソースの文字列リテラルも UTF-8。ImGui・SDL・JSON（nlohmann）・ログのファイルにも、DxLib にも、そのまま渡せる。窓口の文字列は `FString` / `const char*`（UTF-8）。
- ソースファイルは **BOM つき UTF-8** で保存する。ビルドは `/execution-charset:utf-8`（文字列リテラルを UTF-8 にする）で、`/utf-8` ではない。`/utf-8` は BOM のないファイルも UTF-8 として読むので、BOM のない Shift-JIS の DxLib のヘッダー（`DxLib.h` など）が壊れる。BOM のあるファイルだけが UTF-8 として読まれるので、**BOM を落とすと、日本語のリテラルが警告なしで化ける**。BOM を保つこと。
- DxLib は UTF-8 モード（`SetUseCharCodeFormat( DX_CHARCODEFORMAT_UTF8 )`）で使う。`CDxLibBackend` が `DxLib_Init` の前に切り替えるので、描画する文字・ファイル名・フォント名・ウィンドウの題・モデルのアニメーション名とフレーム名には、`FString` をそのまま渡せる。ゲームのコードが DxLib を直接呼ぶときも、UTF-8 で渡す。
- Windows の API は **W 系**（`CreateFileW` など）を使い、パスは `FString::ToWide()` で渡して、受け取った UTF-16 は `FString( const wchar_t* )` で UTF-8 にする。A 系（`...A`）はシステムのコードページ（日本語では Shift-JIS）の列を受け取るので使わない。起動引数は `NCommandLine`。
- UTF-8 でない相手（昔の Shift-JIS のファイルなど）とのやりとりは、境目で `FString::FromCodePage( バイト列, 長さ, 932 )` / `ToCodePage( 932 )` で変換する。
- `FString` の検索・置換・分割・連結は、バイト単位でも UTF-8 の文字を壊さない（2 バイト目以降は ASCII と重ならない）。大文字小文字の変換と、区別なしの比較は ASCII だけ。`Left` / `Mid` などの位置はバイト単位なので、文字の途中で切ると不正な UTF-8 になる（`IsValidUtf8()` で確かめ、`ToValidUtf8()` で直す。文字数は `CharCount()`）。外から来た文字列を JSON に入れるときは、不正な UTF-8 を U+FFFD に直す（`NJson::ToJsonString`）。

## 7. 素の DxLib へ降りる出口

包みきれない機能や、まれにしか使わない機能（Tier 3）のために、素の DxLib を呼ぶ出口を残す。

- ゲームのコードで DxLib を直接呼ぶときは、`Library/Platform/DxLib/DxLibInclude.h`（`NOMINMAX` を定義して `DxLib.h` を読み込む）を include する。
- 直接呼ぶ側が、**状態を元に戻す**責任を持つ（第 4 節）。描画は命令の記録と順序が混ざるので、記録された描画の前後を意識する。
- 出口を使う機能が増えてきたら、それは包む対象に昇格させる合図。

## 8. 新しい機能を包む手順

1. DxLib の関数群を読み、機能を**用途で**まとめる（関数ごとの 1 対 1 の転送にしない）。よく使う使い方が数行で書けるようにする。
2. 窓口（`Core/Interfaces`）と値の型（`*Types.h`）を決める。DxLib の型と定数を出さない。
3. 実装（`Platform/DxLib`）を書く。状態を元に戻す。失敗を第 5 節のとおりに返す。
4. 検査を書く。DxLib を使わない部分（変換・検証・規則）は単体で、DxLib を使う部分は実際に描画して画素で確かめる。変異テストで検査の穴を探す。
5. 文書（`DEVELOPMENT.md`）に使い方を足し、DxLib の網羅表を更新する。
6. 同じ機能を使う小さなサンプルを足す（`Tools/NewSample.ps1` で `Samples/Source/<名前>/` に作る。サンプルは土台の外のコード。シーン 1 つと、それが置くオブジェクトのクラスに分け、1 ファイルに 1 クラスにする）。

## 9. いまの課題（直す予定の差）

規約とのずれが残っている箇所です。直したら、この表から消します。

| 箇所 | ずれ |
|---|---|
| `FDrawContext` の `TextWithFont` / `Billboard3D` / `Quad3D` / `Model` / `Mesh3D` | フォント・モデル・3D の画像を `int32` のハンドルで受け取っている。型のある資源か型付きハンドルで受けたい（2D の画像の描画 `Image` など と `Sprite` / `SpriteRect` は、`IImage` / `FImageHandle` で受けられる。M2-2。フォントは M2-7、3D は M3・M4 で） |
| `Source/Scene/GameScene` のサンプル | 描画に DxLib の関数（`DrawBox` `DrawString`）を直接呼んでいる。`FImmediateDraw`（`TitleScene` は移行済み）で書き直す（`Source/Object` のオブジェクトは、コマンドリスト方式に移行済み） |
| 2D 描画の座標 | 図形は、DxLib の分け方どおり、整数の座標（`Box` / `Circle` / `Line` / `Pixel` / `Triangle` / `Quad` / `Oval` / `RoundBox`）と、`float32` の座標でなめらかな `*AA` に分けた（M2-1）。画像の描画（`Image` など。M2-2）は、座標を `float32` にした（`Tile` だけは、DxLib の `DrawTile` に合わせて整数）。`Box` / `Circle` / `Line` の整数（DxLib の整数版）との混在は、そのまま |
| DxLib の網羅 | 関数名で約 8%。2D・文字・3D のライトと影・シェーダー・フィルター・ムービー・ファイル（DXA）などが未 |

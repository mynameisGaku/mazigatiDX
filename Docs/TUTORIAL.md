# チュートリアル: 最初のゲームを作る

テンプレートから新しいプロジェクトを作り、「コインを拾うゲーム」にして、人に渡せる形にまとめるまでを、順に進めます。30 分ほどです。

できあがるもの: 矢印キーで四角形を動かして、画面に散らばったコインを拾う。30 秒で終わり、点数が出る。

ここに載せたコードは、そのまま足せば、ビルドできて動きます（この文書のコードで、実際に作って確かめています）。

## 1. 用意する

[README](../README.md) の「はじめる」のとおりに、Visual Studio と、DxLib（`FetchDxLib.bat`）を用意します。フレームワークのフォルダーが `C:\dev\mazigatiDX` にあるとします。

## 2. プロジェクトを作る

フレームワークのフォルダーで、次を実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\NewProject.ps1 -Name CoinGame -Destination C:\dev
```

`C:\dev\CoinGame` ができます。

|場所|役割|
|---|---|
|`CoinGame.sln`|Visual Studio で開くファイル|
|`Source/main.cpp`|入口。`NGameMain::RunGame<CGameApplication>()` を呼ぶだけ|
|`Source/Application/`|ゲームインスタンス（`CGameApplication`）。シーンを登録して、最初のシーンへ入る|
|`Source/Scene/`|シーン（`CTitleScene`・`CPlayScene`）。画面の単位|
|`Source/Object/`|オブジェクト（`CPlayer`）。シーンに置いて、毎フレーム動かして描くもの|
|`Config/ScreenConfig.json`|ウィンドウの大きさ・タイトル|
|`Assets/`|自分の画像・音・モデルの置き場|
|`EngineContent.location`|エンジン組み込みの素材（フレームワークの `EngineContent`）の場所|

## 3. 動かす

`CoinGame.sln` を Visual Studio で開き、x64 の Debug でビルドして、実行します（最初は、フレームワーク本体もビルドするので、数分かかります）。

タイトルの画面で Enter を押すと、黄色い四角形を、矢印キー / WASD で動かせます。Esc でタイトルへ戻ります。

## 4. 仕組みを知る

- **ゲームインスタンス**（`CGameApplication`）: ゲーム全体で 1 つ。`OnInit` で、シーンに名前を付けて登録し（`RegisterScene<CPlayScene>( "Play" )`）、最初のシーンへ入ります（`ChangeScene( "Title" )`）。
- **シーン**（`CScene`）: 画面の単位。`OnEnter`（入ったとき）・`OnTick`（毎フレーム）・`OnDraw`（毎フレーム、描くとき）を書きます。オブジェクトは、`SpawnObject<T>( 引数 )` で置きます。置いたオブジェクトは、シーンが持ち、シーンを出るときに消えます。
- **オブジェクト**（`CGameObject`）: `Tick( DeltaTime )` で動かし、`Draw( CommandList )` で描きます。`Draw` は、その場で画面に描くのではなく、**何を描くかを記録**します（`CommandList.Box(...)` など）。記録したものを、フレームワークが、あとでまとめて描きます。
- **サブシステム**: 入力（`IInput`）やアセット（`CAssetSubsystem`）などの機能は、`GetAppSubsystem<T>()` で取り出します。

`Source/Object/Player.cpp` を読むと、この 3 つ（入力を読む・位置を動かす・四角形を記録する）が、そのまま書いてあります。

## 5. コインを足す

新しいオブジェクトを作ります。`Source/Object/Coin.h` を作ります。

```cpp
#pragma once

#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/Texture.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CCoin
//   拾うと点が入るコイン。位置と絵を持って、描くだけ（拾ったかどうかは、シーンが調べる）。
//
//   SpawnObject<CCoin>( FVector2( 200.0f, 300.0f ), CoinImage );
//=============================================================================
class CCoin : public CGameObject
{
public:
	// コインに触れたとみなす距離（ピクセル）
	static constexpr float32 PickupDistance = 32.0f;

	CCoin( const FVector2& InPosition, const TSharedPtr<CTexture>& InImage );

	void Draw( FDrawContext& CommandList ) override;

	const FVector2& GetPosition() const { return Position; }

private:
	FVector2 Position;
	TSharedPtr<CTexture> Image;
};
```

`Source/Object/Coin.cpp` を作ります。

```cpp
#include "Coin.h"

#include "Library/Framework/Render/Draw/DrawContext.h"

namespace
{
	// 絵の半分の大きさ（ピクセル）と、色（金色）
	constexpr float32 HalfSize = 20.0f;
	const FColor32 Gold( 255, 205, 60 );
}

CCoin::CCoin( const FVector2& InPosition, const TSharedPtr<CTexture>& InImage )
	: Position( InPosition )
	, Image( InImage )
{
}

void CCoin::Draw( FDrawContext& CommandList )
{
	if ( !Image.IsValid() )
	{
		// 絵を読めなかったときは、丸で代わりにする
		CommandList.Circle( static_cast<int32>( Position.x ), static_cast<int32>( Position.y ), static_cast<int32>( HalfSize ), Gold );
		return;
	}
	// アイコンは白一色なので、色を掛けて金色にする（掛ける色は、あとに描くものにも効くので、白へ戻す）
	CommandList.Tint( Gold );
	CommandList.ImageStretch( Position.x - HalfSize, Position.y - HalfSize, Position.x + HalfSize, Position.y + HalfSize, Image );
	CommandList.Tint( FColor32::White );
}
```

- 画像は `TSharedPtr<CTexture>` で持ちます。最後の持ち主がいなくなると、自動で解放されます。
- `Tint`（掛ける色）や `Blend`（重ね方）は、「描画の状態」です。変えたら、あとに描くものにも効くので、使い終わったら戻します。

## 6. プレイヤーの位置を読めるようにする

コインに触れたかを調べるために、プレイヤーの位置が要ります。`Source/Object/Player.h` の `public:` に、1 つ足します。

```cpp
	// いまの位置（コインに触れたかを、シーンが調べる）
	const FVector2& GetPosition() const { return Position; }
```

## 7. シーンで、コインを置いて、拾う

`Source/Scene/PlayScene.h` を、次のように書き換えます。

```cpp
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture/Texture.h"
#include "Library/Framework/Scene/Scene.h"
#include "Library/Math/RandomStream.h"

class CCoin;
class CPlayer;

//=============================================================================
// CPlayScene
//   遊ぶ画面。プレイヤーを動かして、コインを拾う。30 秒で終わり、Enter でやり直す。Esc でタイトルへ戻る。
//=============================================================================
class CPlayScene : public CScene
{
protected:
	void OnEnter() override;
	void OnTick( float32 DeltaTime ) override;
	void OnDraw() override;

private:
	// コインを 1 枚、ばらばらな位置に置く
	void SpawnCoin();

	TSharedPtr<CPlayer> Player;
	TArray<TSharedPtr<CCoin>> Coins;
	TSharedPtr<CTexture> CoinImage;
	FRandomStream Random;
	int32 Score = 0;
	float32 TimeLeft = 0.0f;
};
```

`Source/Scene/PlayScene.cpp` を、次のように書き換えます。

```cpp
#include "PlayScene.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Source/Object/Coin.h"
#include "Source/Object/Player.h"

namespace
{
	// 1 回の遊びの長さ（秒）と、いちどに置くコインの数
	constexpr float32 PlaySeconds = 30.0f;
	constexpr int32 NumCoins = 5;
}

void CPlayScene::OnEnter()
{
	// シーンに入るたびに呼ばれるので、ここで、最初の状態にする
	Score = 0;
	TimeLeft = PlaySeconds;
	Coins.Empty();

	// オブジェクトは SpawnObject で置く。シーンが所有して、毎フレーム Tick / Draw する
	Player = SpawnObject<CPlayer>( FVector2( 640.0f, 360.0f ) );

	// 絵は、アセット管理から読む。"Engine:/" で始まるパスは、エンジン組み込みの素材（自分の絵は "Assets/..." に置く）
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
	if ( Assets != nullptr )
	{
		CoinImage = Assets->LoadTexture( "Engine:/Icons/Coin.png" );
	}
	for ( int32 Index = 0; Index < NumCoins; ++Index )
	{
		SpawnCoin();
	}
}

void CPlayScene::SpawnCoin()
{
	const FVector2 Position( static_cast<float32>( Random.NextInt( 60, 1220 ) ), static_cast<float32>( Random.NextInt( 120, 660 ) ) );
	Coins.Add( SpawnObject<CCoin>( Position, CoinImage ) );
}

void CPlayScene::OnTick( float32 DeltaTime )
{
	const IInput* Input = GetAppSubsystem<IInput>();
	if ( Input != nullptr && Input->IsKeyPressed( EKey::Escape ) )
	{
		GetSceneManager()->ChangeScene( "Title" );
		return;
	}
	if ( TimeLeft <= 0.0f )
	{
		// 時間切れ: Enter で、もう一度
		if ( Input != nullptr && Input->IsKeyPressed( EKey::Enter ) )
		{
			GetSceneManager()->ChangeScene( "Play" );
		}
		return;
	}
	TimeLeft -= DeltaTime;

	// プレイヤーが触れたコインを消して、点を足し、新しいコインを置く
	for ( int32 Index = Coins.Num() - 1; Index >= 0; --Index )
	{
		const FVector2 Offset = Coins[Index]->GetPosition() - Player->GetPosition();
		if ( Offset.x * Offset.x + Offset.y * Offset.y <= CCoin::PickupDistance * CCoin::PickupDistance )
		{
			Coins[Index]->Destroy();
			Coins.RemoveAt( Index );
			++Score;
			SpawnCoin();
		}
	}
}

void CPlayScene::OnDraw()
{
	// オブジェクトの Draw() のあとに呼ばれるので、画面の手前に出る（点数・説明の文字など）
	FImmediateDraw Screen( GetDrawExecutor() );
	Screen.Text( 16, 16, "Arrow keys / WASD: move    Esc: back to the title", FColor32::White );
	Screen.Text( 16, 44, *FString::Printf( "Score: %d    Time: %d", Score, static_cast<int32>( TimeLeft > 0.0f ? TimeLeft + 1.0f : 0.0f ) ), FColor32::Yellow );
	if ( TimeLeft <= 0.0f )
	{
		Screen.Text( 540, 340, *FString::Printf( "Time is up!  Score: %d", Score ), FColor32::White );
		Screen.Text( 540, 368, "Enter: play again", FColor32::Gray );
	}
}
```

- `SpawnObject` は、置いたオブジェクトを返します。あとで使うものは、覚えておきます（`Player`・`Coins`）。
- オブジェクトを消すときは `Destroy()` を呼びます。そのフレームの終わりに、シーンから外れます。自分で覚えている分（`Coins`）からも、外します。
- 乱数は `FRandomStream`。同じシードなら、いつも同じ並びになります（毎回変えたいときは、時刻などをシードにします）。
- `IsKeyPressed` は「押した瞬間」、`IsKeyDown` は「押している間」です。
- シーンの `OnDraw` では、`FImmediateDraw` を作って、同じ関数（`Text`・`Box`・`Image` など）で描きます。関数を抜けるときに、画面へ出ます。

## 8. ファイルを足したら

`.cpp` や `.h` を足したり消したりしたら、プロジェクトのフォルダーの `GenerateProjectFiles.bat` を実行して、プロジェクトのファイル一覧を作り直します（Visual Studio が「再読み込みしますか」と聞いたら、再読み込みします）。

ビルドして実行すると、コインが 5 枚出て、触れると点が入り、30 秒で終わります。

## 9. 調べる道具を出す

Debug のビルドでは、調べる道具（FPS・入力・ログの窓、プロファイラー、コンソール）を出せます。`CGameApplication` に `CDebugOverlay` を持たせて、`OnInit`・`OnTick`・`OnDraw`・`OnTerm` から呼びます（書き方は、プロジェクトの `README.md` の「デバッグ表示を使う」）。F1 で窓、F2 でプロファイラー、F3 でコンソールです。

## 10. 人に渡す形にまとめる

Release でビルドしてから、フレームワークのフォルダーで、次を実行します。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\PackageGame.ps1 -ProjectRoot C:\dev\CoinGame -Version 0.1.0 -Zip
```

`C:\dev\CoinGame\Packages\CoinGame-0.1.0`（と、同じ名前の zip）ができます。実行ファイル・DLL・`Config`・`Assets`・エンジン組み込みの素材・ライセンス文が入っていて、このフォルダーだけで動きます。詳しくは、[開発手順](DEVELOPMENT.md) の「配布物を作る」にあります。

## 次に読むもの

|やりたいこと|読むもの|
|---|---|
|自分の画像・音・モデルを使う|`Assets/` に置いて、`Assets->LoadTexture( "Images/Hero.png" )` のように読む。[開発手順](DEVELOPMENT.md)|
|ボタンや設定の画面を作る|[UI](UI.md) と、サンプル `UI`・`Inventory`|
|3D を描く|サンプル `Camera`・`Lighting`・`Shapes3D`・`Island`|
|見た目（マテリアル）を作る|[マテリアル](MATERIAL.md) と、サンプル `Materials`|
|エフェクトを出す|[エフェクトの使い方](EFFECTS.md) と、サンプル `Effects`|
|敵の頭脳（ステートマシン・ビヘイビアツリー）|[開発手順](DEVELOPMENT.md) の「AI」と、サンプル `AI`・`Platformer2D`・`Adventure`|
|セーブデータ|[開発手順](DEVELOPMENT.md) の「保存先とセーブデータ」と、サンプル `Inventory`|
|機能ごとの動く見本|サンプル集（[Samples/README.md](../Samples/README.md)）|

#include "PlasmaView.h"

#include "Library/Core/Interfaces/Input/IInput.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Platform/FileSystem.h"

namespace
{
	// 絵の拡大（画像 128 x 128 を、画面で 2 倍に描く）
	constexpr int32 Zoom = 2;
	// 光（半透明の円）の大きさ（ピクセル）
	constexpr int32 GlowSize = 48;
	// 見出しから絵までの高さ（ピクセル）
	constexpr int32 TitleHeight = 24;

	// 画面の座標（float）を、整数の座標を取る描画に渡すために、一番近いピクセルに丸める
	FORCEINLINE int32 Px( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}

	// 中心が濃く、縁へ向かって、だんだん透明になる白い円（光）
	FPixelImage MakeGlow()
	{
		FPixelImage Image( GlowSize, GlowSize, FColor32( 0, 0, 0, 0 ) );
		const float32 Center = static_cast<float32>( GlowSize - 1 ) * 0.5f;
		for ( int32 Y = 0; Y < GlowSize; ++Y )
		{
			for ( int32 X = 0; X < GlowSize; ++X )
			{
				const float32 DeltaX = static_cast<float32>( X ) - Center;
				const float32 DeltaY = static_cast<float32>( Y ) - Center;
				const float32 Distance = NMath::Sqrt( DeltaX * DeltaX + DeltaY * DeltaY ) / Center;
				const float32 Alpha = NMath::Clamp( 1.0f - Distance, 0.0f, 1.0f );
				Image.Set( X, Y, FColor32( 255, 255, 255, static_cast<uint8>( Alpha * 200.0f ) ) );
			}
		}
		return Image;
	}

	// 保存先（プロジェクトのルートの Saved フォルダー）
	FString GetSavePath()
	{
		return NFileSystem::FindProjectRoot() / "Saved" / "Pixels.png";
	}
}

CPlasmaView::CPlasmaView( const FVector2& InOrigin, const FPixelImage& InCanvas, const TSharedPtr<CDynamicTexture>& InTexture )
	: Origin( InOrigin )
	, Canvas( InCanvas )
	, Glow( MakeGlow() )
	, Texture( InTexture )
{
}

void CPlasmaView::Tick( float32 DeltaTime )
{
	Time += DeltaTime;
	if ( !Canvas.IsValid() || !Texture.IsValid() )
	{
		return;
	}

	// 1. 画素ごとに、模様を計算して書く（CPU 側）
	for ( int32 Y = 0; Y < Canvas.GetHeight(); ++Y )
	{
		for ( int32 X = 0; X < Canvas.GetWidth(); ++X )
		{
			const float32 Value = NMath::Sin( static_cast<float32>( X ) * 0.09f + Time ) + NMath::Sin( static_cast<float32>( Y ) * 0.07f - Time * 1.3f ) + NMath::Sin( static_cast<float32>( X + Y ) * 0.05f + Time * 0.7f );
			Canvas.Set( X, Y, FColor32( static_cast<uint8>( 128.0f + 127.0f * NMath::Sin( Value ) ), static_cast<uint8>( 128.0f + 127.0f * NMath::Sin( Value + 2.1f ) ), static_cast<uint8>( 128.0f + 127.0f * NMath::Sin( Value + 4.2f ) ) ) );
		}
	}

	// 2. 半透明の光を重ねる（下の模様が透けて見える）
	const int32 GlowX = Canvas.GetWidth() / 2 - GlowSize / 2 + Px( 40.0f * NMath::Cos( Time * 1.5f ) );
	const int32 GlowY = Canvas.GetHeight() / 2 - GlowSize / 2 + Px( 40.0f * NMath::Sin( Time * 1.1f ) );
	Canvas.Blit( Glow, GlowX, GlowY, EPixelBlend::Alpha );

	// 3. 画像の中身を替える（貼ってある場所は、そのまま新しい絵になる）
	Texture->Update( Canvas );

	// 4. CPU 側の画像から、1 画素を読む（GPU から読み戻さなくてよい）
	ProbeColor = Canvas.Get( Canvas.GetWidth() / 2, Canvas.GetHeight() / 2 );

	// 5. S キーで、PNG に保存して、読み直して、同じ画像か確かめる
	const IInput* Input = GetAppSubsystem<IInput>();
	CAssetManager* Assets = GetAppSubsystem<CAssetManager>();
	if ( Input != nullptr && Assets != nullptr && Input->IsKeyPressed( EKey::S ) )
	{
		const FString Path = GetSavePath();
		if ( Assets->SavePixelImage( Canvas, Path ) )
		{
			const FPixelImage Reloaded = Assets->LoadPixelImage( Path );
			SaveStatus = FString::Printf( "Saved and reloaded: %s (%d x %d, %s)", *Path, Reloaded.GetWidth(), Reloaded.GetHeight(), Reloaded == Canvas ? "identical" : "different" );
		}
		else
		{
			SaveStatus = FString::Printf( "Failed to save: %s", *Path );
		}
	}
}

void CPlasmaView::Draw( FDrawContext& CommandList )
{
	const int32 Left = Px( Origin.x );
	const int32 Top = Px( Origin.y ) + TitleHeight;
	CommandList.Text( Left, Px( Origin.y ), "FPixelImage -> CDynamicTexture::Update (every frame)", FColor32::White );
	if ( !Texture.IsValid() )
	{
		return;
	}
	CommandList.ImageStretch( static_cast<float32>( Left ), static_cast<float32>( Top ), static_cast<float32>( Left + Canvas.GetWidth() * Zoom ), static_cast<float32>( Top + Canvas.GetHeight() * Zoom ), Texture );
	CommandList.Box( Left, Top, Left + Canvas.GetWidth() * Zoom, Top + Canvas.GetHeight() * Zoom, FColor32( 90, 90, 90 ), false );

	// 色の見本: 中心の画素（CPU 側の画像から読んだ色）
	const int32 Below = Top + Canvas.GetHeight() * Zoom + 16;
	CommandList.TextF( Left, Below, FColor32::Gray, "Canvas.Get( %d, %d ) = ( R %d, G %d, B %d, A %d )", Canvas.GetWidth() / 2, Canvas.GetHeight() / 2, ProbeColor.r, ProbeColor.g, ProbeColor.b, ProbeColor.a );
	CommandList.Box( Left, Below + 24, Left + 96, Below + 24 + 32, FColor32( ProbeColor.r, ProbeColor.g, ProbeColor.b ) );
	CommandList.Text( Left, Below + 72, "Press S: SavePixelImage( Canvas, Saved/Pixels.png ), then LoadPixelImage and compare", FColor32::White );
	if ( !SaveStatus.IsEmpty() )
	{
		CommandList.Text( Left, Below + 96, *SaveStatus, FColor32::Gray );
	}
}

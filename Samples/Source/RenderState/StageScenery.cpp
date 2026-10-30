#include "StageScenery.h"

#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/PixelImage.h"
#include "Source/Common/TestPattern.h"

namespace
{
	// 遠くの空の色。距離のフォグの色を、これにそろえると、遠くの物が空へ溶けて見える
	const FColor32 HorizonColor( 30, 38, 66 );

	// 地面の大きさ（半分）と、市松模様の 1 周期（ワールドの単位。画像の 1 周期 = 2 x 2 マス）
	constexpr float32 GroundHalfSize = 80.0f;
	constexpr float32 GroundTileSize = 2.0f;

	// 部屋（壁だけの四角い囲み）の半分の幅と、高さ
	constexpr float32 RoomHalfWidth = 4.0f;
	constexpr float32 RoomHeight = 3.5f;

	// 球の分割数
	constexpr int32 SphereDivisions = 24;

	EDrawFilter ToFilter( FRenderStateDemoState::EGroundFilter Filter )
	{
		switch ( Filter )
		{
		case FRenderStateDemoState::EGroundFilter::Nearest:
			return EDrawFilter::Nearest;
		case FRenderStateDemoState::EGroundFilter::Bilinear:
			return EDrawFilter::Bilinear;
		default:
			return EDrawFilter::Anisotropic;
		}
	}

	// 画像を貼った、水平（y が一定）の四角形
	FDrawQuad3DData MakeFlatQuad( const FVector3& Center, float32 Width, float32 Depth, int32 Handle )
	{
		FDrawQuad3DData Quad;
		Quad.Pos[0] = Center.x;
		Quad.Pos[1] = Center.y;
		Quad.Pos[2] = Center.z;
		Quad.Right[0] = 1.0f;
		Quad.Up[1] = 0.0f;
		Quad.Up[2] = 1.0f;
		Quad.Width = Width;
		Quad.Height = Depth;
		Quad.Handle = Handle;
		Quad.Orient = static_cast<int32>( EDrawQuadOrient::Fixed );
		return Quad;
	}

	// 地面: 市松模様の画像を、全体に繰り返し貼る。フィルターと異方性は、遠くで斜めに縮む模様の見え方を決める
	void RecordGround( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		if ( !State.GroundTexture.IsValid() )
		{
			return;
		}
		FDrawQuad3DData Quad = MakeFlatQuad( FVector3::Zero, GroundHalfSize * 2.0f, GroundHalfSize * 2.0f, State.GroundTexture->GetImageHandle().Value );
		Quad.U1 = Quad.Width / GroundTileSize;
		Quad.V1 = Quad.Height / GroundTileSize;
		CommandList.TextureAddress( EDrawTextureAddress::Wrap );
		CommandList.Filter( ToFilter( State.GroundFilter ) );
		CommandList.Anisotropy( State.GroundAnisotropy );
		CommandList.Quad3D( Quad );
		CommandList.Filter( EDrawFilter::Nearest );
	}

	// 遠くの柱: 輪に並べる。距離のフォグで、遠いほど空の色へ溶ける
	void RecordPillars( FDrawContext& CommandList )
	{
		const FColor32 Colors[6] = {
			FColor32( 230, 90, 80 ), FColor32( 240, 190, 70 ), FColor32( 90, 200, 120 ), FColor32( 80, 180, 220 ), FColor32( 130, 110, 230 ), FColor32( 230, 120, 190 ),
		};
		struct FRing
		{
			float32 Radius;
			int32 Count;
			float32 Height;
			float32 Phase;
		};
		const FRing Rings[2] = { { 34.0f, 12, 9.0f, 0.0f }, { 62.0f, 14, 15.0f, 0.2f } };
		for ( const FRing& Ring : Rings )
		{
			for ( int32 Index = 0; Index < Ring.Count; ++Index )
			{
				const float32 Angle = Ring.Phase + static_cast<float32>( Index ) / static_cast<float32>( Ring.Count ) * NMath::PI * 2.0f;
				const FVector3 Center( NMath::Sin( Angle ) * Ring.Radius, Ring.Height * 0.5f, NMath::Cos( Angle ) * Ring.Radius );
				const FVector3 Half( 1.3f, Ring.Height * 0.5f, 1.3f );
				CommandList.Cube3D( Center - Half, Center + Half, Colors[Index % 6], FColor32( 60, 60, 60 ) );
			}
		}
	}

	// 壁 1 枚（三角形 2 枚）。(Left, Top) -> (Right, Top) -> (Right, Bottom) -> (Left, Bottom) を、内側から見て時計回りに並べる（内側が表）
	void RecordRoomWall( FDrawContext& CommandList, const FVector3& BottomLeft, const FVector3& TopLeft, const FVector3& TopRight, const FVector3& BottomRight, const FColor32& Color )
	{
		CommandList.Triangle3D( BottomLeft, TopLeft, TopRight, Color );
		CommandList.Triangle3D( BottomLeft, TopRight, BottomRight, Color );
	}

	// 部屋: 4 枚の壁を、内側が表になる向きで並べる。外から見ると、手前の壁は裏向き
	void RecordRoom( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		const float32 W = RoomHalfWidth;
		const float32 H = RoomHeight;
		CommandList.Culling( State.CardCulling );
		// 北（+Z）の壁: 内側から見て、左が -X
		RecordRoomWall( CommandList, FVector3( -W, 0.0f, W ), FVector3( -W, H, W ), FVector3( W, H, W ), FVector3( W, 0.0f, W ), FColor32( 230, 120, 90 ) );
		// 東（+X）の壁: 内側から見て、左が +Z
		RecordRoomWall( CommandList, FVector3( W, 0.0f, W ), FVector3( W, H, W ), FVector3( W, H, -W ), FVector3( W, 0.0f, -W ), FColor32( 90, 170, 230 ) );
		// 南（-Z）の壁: 内側から見て、左が +X
		RecordRoomWall( CommandList, FVector3( W, 0.0f, -W ), FVector3( W, H, -W ), FVector3( -W, H, -W ), FVector3( -W, 0.0f, -W ), FColor32( 120, 220, 140 ) );
		// 西（-X）の壁: 内側から見て、左が -Z
		RecordRoomWall( CommandList, FVector3( -W, 0.0f, -W ), FVector3( -W, H, -W ), FVector3( -W, H, W ), FVector3( -W, 0.0f, W ), FColor32( 230, 210, 100 ) );
		CommandList.Culling( EDrawCulling::None );
	}

	// 水面: 半透明の波の画像を、流しながら貼る（画像の変換）。深度は比べるだけ（半透明なので、書かない）
	void RecordWater( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		if ( !State.WaterTexture.IsValid() )
		{
			return;
		}
		FDrawQuad3DData Quad = MakeFlatQuad( FVector3( -11.0f, 0.04f, 0.0f ), 10.0f, 10.0f, State.WaterTexture->GetImageHandle().Value );
		Quad.U1 = 2.0f;
		Quad.V1 = 2.0f;
		Quad.Color = FDrawRgba { 255, 255, 255, 210 };
		CommandList.Depth( FDepthState::ReadOnly() );
		CommandList.Filter( EDrawFilter::Bilinear );
		if ( State.bScrollWater )
		{
			CommandList.TextureTransform( FTextureTransform::MakeScroll( FVector2( State.Time * 0.12f, State.Time * 0.05f ) ) );
		}
		CommandList.Quad3D( Quad );
		CommandList.ResetTextureTransform();
		CommandList.Filter( EDrawFilter::Nearest );
		CommandList.Depth( FDepthState::Normal() );
	}

	// 壁 4 枚: 同じ画像を、UV -0.75..1.75 で貼る。外側の貼り方（繰り返す・折り返す・端を伸ばす・描かない）の違いが並んで見える
	void RecordAddressWalls( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		if ( !State.WallTexture.IsValid() )
		{
			return;
		}
		const EDrawTextureAddress Modes[4] = { EDrawTextureAddress::Wrap, EDrawTextureAddress::Mirror, EDrawTextureAddress::Clamp, EDrawTextureAddress::Border };
		CommandList.Filter( EDrawFilter::Bilinear );
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			FDrawQuad3DData Quad;
			Quad.Pos[0] = -9.0f + static_cast<float32>( Index ) * 6.0f;
			Quad.Pos[1] = 2.3f;
			Quad.Pos[2] = 13.0f;
			Quad.Width = 4.0f;
			Quad.Height = 4.0f;
			Quad.U0 = -0.75f;
			Quad.V0 = -0.75f;
			Quad.U1 = 1.75f;
			Quad.V1 = 1.75f;
			Quad.Handle = State.WallTexture->GetImageHandle().Value;
			Quad.Orient = static_cast<int32>( EDrawQuadOrient::Fixed );
			CommandList.TextureAddress( Modes[Index] );
			CommandList.Quad3D( Quad );
		}
		CommandList.TextureAddress( EDrawTextureAddress::Wrap );
		CommandList.Filter( EDrawFilter::Nearest );
	}

	// 腕: 土台 -> 上の腕 -> 下の腕 -> 手を、入れ子の変換で並べる。上の腕を動かすと、下の腕と手も動く
	void RecordArm( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		const float32 Time = State.Time;
		FWorldMatrixScope Base( CommandList, FMatrix4x4::Translate( FVector3( 2.0f, 0.6f, -12.0f ) ) );
		CommandList.Cube3D( FVector3( -1.2f, -0.6f, -1.2f ), FVector3( 1.2f, 0.6f, 1.2f ), FColor32( 150, 150, 165 ), FColor32( 60, 60, 60 ) );
		{
			// 肩: 土台の上にあり、Z の軸まわりに振る
			FWorldMatrixScope Upper( CommandList, FMatrix4x4::RotateZ( NMath::Sin( Time * 0.9f ) * 0.7f + 0.9f ) * FMatrix4x4::Translate( FVector3( 0.0f, 0.6f, 0.0f ) ) );
			CommandList.Cube3D( FVector3( 0.0f, -0.4f, -0.4f ), FVector3( 4.0f, 0.4f, 0.4f ), FColor32( 230, 120, 80 ), FColor32( 60, 60, 60 ) );
			{
				// 肘: 上の腕の先にあり、もう一つの軸の振りが重なる
				FWorldMatrixScope Lower( CommandList, FMatrix4x4::RotateZ( NMath::Sin( Time * 1.4f ) * 0.9f - 0.5f ) * FMatrix4x4::Translate( FVector3( 4.0f, 0.0f, 0.0f ) ) );
				CommandList.Cube3D( FVector3( 0.0f, -0.3f, -0.3f ), FVector3( 3.2f, 0.3f, 0.3f ), FColor32( 240, 200, 80 ), FColor32( 60, 60, 60 ) );
				{
					// 手
					FWorldMatrixScope Hand( CommandList, FMatrix4x4::Translate( FVector3( 3.2f, 0.0f, 0.0f ) ) );
					CommandList.Sphere3D( FVector3::Zero, 0.55f, SphereDivisions, FColor32( 110, 220, 150 ), FColor32( 90, 90, 90 ) );
				}
			}
		}
	}

	// ガラス: 3 枚の半透明の板を、同じ向きで、奥行きにそって並べる（赤 -> 緑 -> 青の順に描く）。
	// 深度を書く（Normal）と、先に描いた板の奥に描く板は、深度の比べで消える（-Z 側から見ると、赤だけ）。書かない（ReadOnly）と、重なって見える
	void RecordGlass( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		const FColor32 Colors[3] = { FColor32( 235, 70, 70 ), FColor32( 70, 215, 95 ), FColor32( 80, 110, 245 ) };
		CommandList.Depth( State.GlassDepth == FRenderStateDemoState::EGlassDepth::ReadOnly ? FDepthState::ReadOnly() : FDepthState::Normal() );
		CommandList.Blend( EDrawBlend::Alpha, 130 );
		for ( int32 Index = 0; Index < 3; ++Index )
		{
			const float32 Z = static_cast<float32>( Index - 1 ) * 3.0f;
			CommandList.Cube3D( FVector3( 6.5f, 0.0f, Z - 0.1f ), FVector3( 11.5f, 4.0f, Z + 0.1f ), Colors[Index], FColor32( 80, 80, 80 ) );
		}
		CommandList.Blend( EDrawBlend::Opaque );
		CommandList.Depth( FDepthState::Normal() );
	}

	// 手前に描く光線: カメラの足元から、舞台の中心まで。深度を消してから描くと、舞台の物に隠されずに、全体が見える
	void RecordBeam( FDrawContext& CommandList, const FRenderStateDemoState& State )
	{
		if ( !State.Camera.IsValid() )
		{
			return;
		}
		if ( State.bBeamClearsDepth )
		{
			CommandList.ClearDepth();
		}
		const FVector3 Start = State.Camera.Position + State.Camera.GetForward() * 2.0f + State.Camera.GetRight() * 0.9f - State.Camera.GetUpVector() * 0.8f;
		const FVector3 End( 0.0f, 1.2f, 0.0f );
		CommandList.Capsule3D( Start, End, 0.07f, 12, FColor32( 255, 150, 60 ), FColor32( 90, 90, 90 ) );
		CommandList.Sphere3D( End, 0.3f, 16, FColor32( 255, 230, 110 ), FColor32( 90, 90, 90 ) );
	}
}

bool NStageScenery::MakeTextures( CAssetManager& Assets, FRenderStateDemoState& State )
{
	// 地面: 4 x 4 画素の濃淡の市松模様（1 周期 = 2 x 2 マス）
	FPixelImage Checker( 8, 8 );
	for ( int32 Y = 0; Y < 8; ++Y )
	{
		for ( int32 X = 0; X < 8; ++X )
		{
			const bool bLight = ( ( X / 4 + Y / 4 ) % 2 ) == 0;
			Checker.Set( X, Y, bLight ? FColor32( 205, 205, 212 ) : FColor32( 96, 104, 124 ) );
		}
	}
	State.GroundTexture = Assets.CreateDynamicTexture( "Samples/RenderState/Ground", Checker );

	// 水面: 斜めの波の模様（上下左右につなげても切れ目が出ない周期）
	FPixelImage Water( 16, 16 );
	for ( int32 Y = 0; Y < 16; ++Y )
	{
		for ( int32 X = 0; X < 16; ++X )
		{
			const float32 Wave = 0.5f + 0.5f * NMath::Sin( static_cast<float32>( X * 2 + Y ) * NMath::PI * 2.0f / 16.0f );
			Water.Set( X, Y, FColor32( static_cast<uint8>( 30.0f + Wave * 70.0f ), static_cast<uint8>( 100.0f + Wave * 80.0f ), static_cast<uint8>( 170.0f + Wave * 70.0f ) ) );
		}
	}
	State.WaterTexture = Assets.CreateDynamicTexture( "Samples/RenderState/Water", Water );

	// 壁: 向きが分かる確認用の画像（四隅の色が決まっている）
	const TArray<uint8> Bitmap = NTestPattern::MakeBitmap();
	State.WallTexture = Assets.CreateTextureFromMemory( "Samples/RenderState/Wall", Bitmap.GetData(), static_cast<usize>( Bitmap.Num() ) );
	return State.GroundTexture.IsValid() && State.WaterTexture.IsValid() && State.WallTexture.IsValid();
}

TArray<NStageScenery::FStageLabel> NStageScenery::MakeLabels()
{
	TArray<FStageLabel> Labels;
	const char* const AddressNames[4] = { "Wrap", "Mirror", "Clamp", "Border" };
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		Labels.Add( FStageLabel { AddressNames[Index], FVector3( -9.0f + static_cast<float32>( Index ) * 6.0f, 5.0f, 13.0f ) } );
	}
	Labels.Add( FStageLabel { "Glass (depth)", FVector3( 9.0f, 4.7f, 0.0f ) } );
	Labels.Add( FStageLabel { "Room (culling)", FVector3( 0.0f, RoomHeight + 0.8f, 0.0f ) } );
	Labels.Add( FStageLabel { "Water (texture transform)", FVector3( -11.0f, 1.2f, 0.0f ) } );
	Labels.Add( FStageLabel { "Arm (world matrix stack)", FVector3( 2.0f, 7.5f, -12.0f ) } );
	Labels.Add( FStageLabel { "Pillars (fog)", FVector3( 0.0f, 10.5f, 34.0f ) } );
	return Labels;
}

void NStageScenery::RecordStage( FDrawContext& CommandList, const FRenderStateDemoState& State )
{
	// 距離のフォグ（球・箱・カプセル・画像を貼った四角形・ガラスに掛かる。三角形・線・ビルボードには、DxLib の仕様で掛からない）
	switch ( State.Fog )
	{
	case FRenderStateDemoState::EFogChoice::Linear:
		CommandList.Fog( FFogSettings::MakeLinear( HorizonColor, 25.0f, 90.0f ) );
		break;
	case FRenderStateDemoState::EFogChoice::Exponential:
		CommandList.Fog( FFogSettings::MakeExponential( HorizonColor, 0.03f ) );
		break;
	case FRenderStateDemoState::EFogChoice::ExponentialSquared:
		CommandList.Fog( FFogSettings::MakeExponentialSquared( HorizonColor, 0.02f ) );
		break;
	default:
		CommandList.Fog( FFogSettings::Off() );
		break;
	}
	// 高さのフォグ（足元の霧）: 高さ 1.5 より下で、低いほど濃い
	CommandList.HeightFog( State.bHeightFog ? FFogSettings::MakeExponential( FColor32( 120, 150, 205 ), 0.2f, 1.5f ) : FFogSettings::Off() );

	RecordGround( CommandList, State );
	RecordPillars( CommandList );
	RecordRoom( CommandList, State );
	RecordAddressWalls( CommandList, State );
	RecordArm( CommandList, State );
	RecordWater( CommandList, State );
	RecordGlass( CommandList, State );
	RecordBeam( CommandList, State );
}

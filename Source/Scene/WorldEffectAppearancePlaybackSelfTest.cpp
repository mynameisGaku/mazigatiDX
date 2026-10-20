// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <cmath>
#include <cstring>
#include <utility>

#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawMaterialRibbon.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// Spriteと帯の材質命令からAppearance結果を読むための小さな記録。
	struct FAppearanceDrawValues
	{
		// Sprite材質四角形に含まれるサイズとRGBA。
		TArray<FDrawMaterialQuadData> Quads;
		// Ribbon/Lightning材質帯のheaderと所有点列。
		TArray<FDrawMaterialRibbonData> Ribbons;
		// 命令payloadから複写した材質Ribbon点列。
		TArray<TArray<FDrawMaterialRibbonData::FPoint>> RibbonPoints;
		// Appearance倍率が入った通常四角形の個数。
		int32 PlainQuads = 0;
		// 材質OFF四角形の所有payload。
		TArray<FDrawQuad3DData> PlainQuadData;
		// 従来Ribbon命令の個数。
		int32 PlainRibbons = 0;
		// 従来Ribbonのheaderと所有点列。
		TArray<FDrawRibbonData> PlainRibbonHeaders;
		// 従来Ribbonの可変長点payload。
		TArray<TArray<FDrawRibbonData::FPoint>> PlainRibbonPoints;
	};

	// Playbackの所有bufferから描画命令を安全に複写する。
	FAppearanceDrawValues ReadAppearanceCommands( const TSharedPtr<IEffectPlayback>& Playback )
	{
		// Playbackが記録した自己所有命令列。
		FDrawBuffer Buffer;
		// DrawContextへ命令を追加する記録窓口。
		FDrawContext Draw( Buffer );
		if ( Playback.IsValid() ) Playback->Record( Draw );
		// 出力の各commandだけを型付き所有領域へ複写する。
		FAppearanceDrawValues Result;
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::MaterialQuad3D && Command.NumDataWords * 4 >= static_cast<int32>( sizeof( FDrawMaterialQuadData ) ) ) Result.Quads.Add( Command.Read<FDrawMaterialQuadData>() );
			if ( Command.Type == EDrawCommand::Quad3D && Command.NumDataWords * 4 >= static_cast<int32>( sizeof( FDrawQuad3DData ) ) ) { ++Result.PlainQuads; Result.PlainQuadData.Add( Command.Read<FDrawQuad3DData>() ); }
			if ( Command.Type == EDrawCommand::Ribbon3D && Command.NumDataWords * 4 >= static_cast<int32>( sizeof( FDrawRibbonData ) ) )
			{
				// 従来Ribbon命令の固定header。
				const FDrawRibbonData Header = Command.Read<FDrawRibbonData>();
				if ( Header.NumPoints < 2 || Header.NumPoints > FDrawRibbonData::MaxPoints ) return;
				// headerと点列のbyte長。
				const usize RequiredBytes = sizeof( Header ) + static_cast<usize>( Header.NumPoints ) * sizeof( FDrawRibbonData::FPoint );
				if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
				// command bufferから独立した所有点列。
				TArray<FDrawRibbonData::FPoint> Points;
				Points.SetNum( Header.NumPoints );
				std::memcpy( Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), static_cast<usize>( Points.Num() ) * sizeof( FDrawRibbonData::FPoint ) );
				Result.PlainRibbonHeaders.Add( Header );
				Result.PlainRibbonPoints.Add( std::move( Points ) );
				++Result.PlainRibbons;
			}
			if ( Command.Type != EDrawCommand::MaterialRibbon3D || Command.NumDataWords * 4 < static_cast<int32>( sizeof( FDrawMaterialRibbonData ) ) ) return;
			// 先頭headerを読む前にcommand iteratorが固定部長を検査している。
			const FDrawMaterialRibbonData Header = Command.Read<FDrawMaterialRibbonData>();
			if ( Header.Ribbon.NumPoints < 2 || Header.Ribbon.NumPoints > FDrawRibbonData::MaxPoints ) return;
			// headerと点列の厳密なbyte長を確認する。
			const usize RequiredBytes = sizeof( Header ) + static_cast<usize>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint );
			if ( RequiredBytes > static_cast<usize>( Command.NumDataWords ) * 4 ) return;
			// 命令bufferから独立した点列を保持する。
			TArray<FDrawMaterialRibbonData::FPoint> Points;
			Points.SetNum( Header.Ribbon.NumPoints );
			std::memcpy( Points.GetData(), reinterpret_cast<const uint8*>( Command.Data ) + sizeof( Header ), static_cast<usize>( Points.Num() ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
			Result.Ribbons.Add( Header );
			Result.RibbonPoints.Add( std::move( Points ) );
		} );
		return Result;
	}

	// 寿命0.5秒でサイズ3とRGBA(21,15,45,80)になる固定再生条件を定義へ設定する。
	void SetAppearanceFixture( FParticleEmitterDefinition& Emitter )
	{
		Emitter.Lifetime = { 2.0f, 2.0f };
		Emitter.StartSize = { 1.0f, 1.0f };
		Emitter.EndSize = { 3.0f, 3.0f };
		Emitter.StartColor = FColor32( 10, 20, 40, 200 );
		Emitter.EndColor = FColor32( 140, 180, 120, 40 );
		Emitter.Appearance.Size.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Appearance.Size.Keys.Add( { 0.25f, 2.0f } );
		Emitter.Appearance.Size.Keys.Add( { 1.0f, 2.0f } );
		Emitter.Appearance.Color.Interpolation = FEffectScalarCurve::EInterpolation::Linear;
		Emitter.Appearance.Color.Keys.Add( { 0.0f, FColor( 1.0f, 1.0f, 1.0f, 1.0f ) } );
		Emitter.Appearance.Color.Keys.Add( { 0.25f, FColor( 0.5f, 0.25f, 0.75f, 0.5f ) } );
		Emitter.Appearance.Color.Keys.Add( { 1.0f, FColor( 0.5f, 0.25f, 0.75f, 0.5f ) } );
		Emitter.Bursts.Add( NEffectTest::Burst( 0.0f, 1 ) );
	}
}

// 実シミュレーションとPlaybackを通し、Appearanceのfloat値と描画命令を検証する。
void NWorldTest::RunEffectAppearancePlaybackTests( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Dir )
{
	using namespace NEffectTest;
	C.Group = "effect-appearance-playback";
	Assets.SetRootDirectory( Dir );
	// Spriteの材質ON/OFFとAlpha/Addを同じ寿命位置で比較する。
	for ( int32 PathIndex = 0; PathIndex < 3; ++PathIndex ) for ( int32 MaterialIndex = 0; MaterialIndex < 2; ++MaterialIndex ) for ( int32 BlendIndex = 0; BlendIndex < 2; ++BlendIndex )
	{
		// 一つの組合せにだけ使うエミッター定義。
		FParticleEmitterDefinition Emitter = QuietEmitter();
		SetAppearanceFixture( Emitter );
		Emitter.Blend = BlendIndex == 0 ? EParticleBlend::Alpha : EParticleBlend::Add;
		Emitter.Material.bEnabled = MaterialIndex != 0;
		if ( PathIndex == 1 ) { Emitter.Path.Kind = FEffectPathDefinition::EKind::Ribbon; Emitter.Rate = 4.0f; Emitter.Speed = { 0.0f, 0.0f }; Emitter.Bursts.Reset(); }
		if ( PathIndex == 2 ) { Emitter.Path.Kind = FEffectPathDefinition::EKind::Lightning; Emitter.Path.End = FVector3( 0.0f, 2.0f, 0.0f ); Emitter.Path.Branches = 1; }
		// シミュレーションから読み取る独立した再生instance。
		TSharedPtr<IEffectPlayback> Playback = NEffect::CreateParticlePlayback( MakeEffect( { Emitter } ), &Assets, FEffectPlayParams(), FTransform::Identity, FString::Printf( "appearance-playback-%d-%d-%d", PathIndex, MaterialIndex, BlendIndex ) );
		C.Check( Playback.IsValid(), "Appearance fixtureのPlaybackを作成する" );
		if ( !Playback.IsValid() ) continue;
		if ( PathIndex == 1 )
		{
			Playback->SetTransform( FTransform( FVector3( -1.0f, 0.0f, 0.0f ) ) ); Playback->Update( 0.25f );
			Playback->SetTransform( FTransform( FVector3( 0.0f, 0.5f, 0.0f ) ) ); Playback->Update( 0.25f );
			Playback->SetTransform( FTransform( FVector3( 1.0f, 0.0f, 0.0f ) ) ); Playback->Update( 0.25f );
		}
		else { Playback->Update( 0.01f ); Playback->Update( 0.5f ); }
		// 実際にrecordしたsprite・ribbon・lightningの各material命令。
		const FAppearanceDrawValues Values = ReadAppearanceCommands( Playback );
		if ( PathIndex == 0 && MaterialIndex != 0 )
		{
			C.Check( Values.Quads.Num() == 1, "材質SpriteはPlaybackからMaterialQuad命令を一つ記録する" );
			if ( Values.Quads.Num() == 1 )
			{
				const FDrawMaterialQuadData& Quad = Values.Quads[0];
				C.Check( std::fabs( Quad.Quad.Width - 3.0f ) < 1.0e-5f && std::fabs( Quad.Quad.Height - 3.0f ) < 1.0e-5f, "Age 0.5秒のStart/End幅1.5へAppearance倍率2を掛けて3を渡す" );
				C.Check( Quad.Quad.Color.R == 21 && Quad.Quad.Color.G == 15 && Quad.Quad.Color.B == 45 && Quad.Quad.Color.A == 80, "基本RGBAをfloat補間後に倍率して最後に一度だけbyte化する" );
				C.Check( Quad.Quad.Color.R != 22, "先に8bitへ丸めてから倍率を掛ける誤差を出さない" );
			}
		}
		if ( PathIndex == 0 && MaterialIndex == 0 )
		{
			C.Check( Values.PlainQuads == 1 && Values.Quads.IsEmpty(), "材質OFF SpriteはAppearanceを反映した従来Quadだけを記録する" );
			if ( Values.PlainQuads == 1 )
			{
				const FDrawRgba& Color = Values.PlainQuadData[0].Color;
				if ( BlendIndex == 0 ) C.Check( Color.R == 21 && Color.G == 15 && Color.B == 45 && Color.A == 80, "材質OFF Alpha Spriteもfloat補間後のAppearance RGBAを一度だけbyte化する" );
				else C.Check( Color.R == 7 && Color.G == 5 && Color.B == 14 && Color.A == 255, "材質OFF Add SpriteはAppearance後alphaを明るさへ一度だけ掛ける" );
			}
		}
		if ( PathIndex == 1 && MaterialIndex != 0 )
		{
			C.Check( Values.Ribbons.Num() == 1 && Values.RibbonPoints[0].Num() == 3, "材質Ribbonは3つの実particleから一本のMaterialRibbon命令を記録する" );
			if ( Values.Ribbons.Num() == 1 && Values.RibbonPoints[0].Num() == 3 )
			{
				// 古い粒子から順に並ぶ、材質Ribbonの3点。
				const TArray<FDrawMaterialRibbonData::FPoint>& Points = Values.RibbonPoints[0];
				C.Check( std::fabs( Points[0].Path.Width - 3.0f ) < 1.0e-5f && std::fabs( Points[1].Path.Width - 1.875f ) < 1.0e-5f && std::fabs( Points[2].Path.Width - 1.0f ) < 1.0e-5f, "Ribbon点幅はAge .5/.25/0の基礎幅へ倍率2/1.5/1を掛ける" );
				C.Check( Points[0].Path.Color == 0x502D0F15u && Points[1].Path.Color == 0x872C1914u && Points[2].Path.Color == 0xC828140Au, "材質Ribbon点はRGBA (21,15,45,80)/(20,25,44,135)/(10,20,40,200)を保つ" );
			}
		}
		if ( PathIndex == 1 && MaterialIndex == 0 )
		{
			C.Check( Values.PlainRibbons == 1 && Values.PlainRibbonPoints[0].Num() == 3, "材質OFF Ribbonは3点の従来Ribbon命令を使う" );
			if ( Values.PlainRibbons == 1 && Values.PlainRibbonPoints[0].Num() == 3 )
			{
				// 材質OFFで後段へ渡るRibbon色。
				const TArray<FDrawRibbonData::FPoint>& Points = Values.PlainRibbonPoints[0];
				if ( BlendIndex == 0 ) C.Check( Points[0].Color == 0x502D0F15u && Points[1].Color == 0x872C1914u && Points[2].Color == 0xC828140Au, "材質OFF Alpha Ribbonは点ごとのAppearance RGBAを保つ" );
				else C.Check( Points[0].Color == 0xFF0E0406u && Points[1].Color == 0xFF170D0Au && Points[2].Color == 0xFF1F0F07u, "材質OFF Add Ribbonは旧経路の切捨てalpha前掛け値を保つ" );
			}
		}
		if ( PathIndex == 2 && MaterialIndex != 0 )
		{
			C.Check( !Values.Ribbons.IsEmpty(), "材質Lightningは実際のparticle寿命値をMaterialRibbonへ記録する" );
			// haloとcoreから幅・生色・alphaを独立したpixel値の式で確認する。
			bool bFoundHalo = false;
			bool bFoundCore = false;
			C.Check( Values.Ribbons.Num() == 4, "材質Lightningはhalo主幹/枝・core主幹/枝の4命令を順に記録する (got %d)", Values.Ribbons.Num() );
			if ( Values.Ribbons.Num() == 4 )
			{
				const TArray<FDrawMaterialRibbonData::FPoint>& MainHalo = Values.RibbonPoints[0];
				const TArray<FDrawMaterialRibbonData::FPoint>& BranchHalo = Values.RibbonPoints[1];
				const TArray<FDrawMaterialRibbonData::FPoint>& MainCore = Values.RibbonPoints[2];
				const TArray<FDrawMaterialRibbonData::FPoint>& BranchCore = Values.RibbonPoints[3];
				C.Check( MainHalo.Num() >= 2 && MainCore.Num() == MainHalo.Num(), "Lightning主幹halo/coreは同じ複数点を持つ" );
				C.Check( BranchHalo.Num() >= 3 && BranchCore.Num() == BranchHalo.Num(), "Lightning枝halo/coreは同じ3点以上を持つ" );
				if ( MainHalo.Num() > 0 && MainCore.Num() > 0 )
				{
					C.Check( std::fabs( MainHalo[0].Path.Width - 9.0f ) < 1.0e-4f && MainHalo[0].Path.Color == 0x0E2D0F15u, "Lightning主幹halo先頭は幅9・RGBA21/15/45/14を保つ" );
					C.Check( std::fabs( MainCore[0].Path.Width - 3.0f ) < 1.0e-4f && MainCore[0].Path.Color == 0x502D0F15u, "Lightning主幹core先頭は幅3・RGBA21/15/45/80を保つ" );
					bFoundHalo = std::fabs( MainHalo[0].Path.Width - 9.0f ) < 1.0e-4f && MainHalo[0].Path.Color == 0x0E2D0F15u;
					bFoundCore = std::fabs( MainCore[0].Path.Width - 3.0f ) < 1.0e-4f && MainCore[0].Path.Color == 0x502D0F15u;
				}
				C.Check( Values.Ribbons[2].Material.EdgeEmission == 0.0f && Values.Ribbons[3].Material.EdgeEmission == 0.0f, "Lightningの2つのcoreだけ境界発光を止める" );
				if ( BranchHalo.Num() == BranchCore.Num() && BranchCore.Num() >= 3 )
				{
					for ( int32 PointIndex = 0; PointIndex < BranchCore.Num(); ++PointIndex )
					{
						// 枝のこの点が根元から終点へ進む割合。
						const float32 T = static_cast<float32>( PointIndex ) / static_cast<float32>( BranchCore.Num() - 1 );
						// geometry側で丸めた枝のcore alpha。
						const uint8 CoreAlpha = static_cast<uint8>( std::lround( 80.0f * ( 1.0f - T ) ) );
						// halo側はcore alphaへ0.18を掛けて整数切捨てする。
						const uint8 HaloAlpha = static_cast<uint8>( CoreAlpha * 0.18f );
						// 材質payloadのRGBA byte並びを期待値へ詰める。
						const uint32 ExpectedCore = static_cast<uint32>( CoreAlpha ) << 24 | 0x002D0F15u;
						const uint32 ExpectedHalo = static_cast<uint32>( HaloAlpha ) << 24 | 0x002D0F15u;
						C.Check( std::fabs( BranchCore[PointIndex].Path.Width - 1.65f * ( 1.0f - T ) ) < 1.0e-4f && BranchCore[PointIndex].Path.Color == ExpectedCore, "Lightning枝core点%dは幅1.65*(1-t)とalpha lround(80*(1-t))を保つ (got %.4f,0x%08X)", PointIndex, BranchCore[PointIndex].Path.Width, BranchCore[PointIndex].Path.Color );
						C.Check( std::fabs( BranchHalo[PointIndex].Path.Width - 4.95f * ( 1.0f - T ) ) < 1.0e-4f && BranchHalo[PointIndex].Path.Color == ExpectedHalo, "Lightning枝halo点%dは幅4.95*(1-t)とalpha trunc(core*.18)を保つ (got %.4f,0x%08X)", PointIndex, BranchHalo[PointIndex].Path.Width, BranchHalo[PointIndex].Path.Color );
					}
				}
			}
			C.Check( bFoundHalo && bFoundCore, "材質Lightningは主幹のhalo・core期待値を記録する" );
		}
		if ( PathIndex == 2 && MaterialIndex == 0 )
		{
			C.Check( Values.PlainRibbons > 0, "材質OFF Lightningは従来Ribbon命令を使う" );
			bool bFoundHalo = false;
			bool bFoundCore = false;
			for ( const TArray<FDrawRibbonData::FPoint>& Points : Values.PlainRibbonPoints ) for ( const FDrawRibbonData::FPoint& Point : Points )
			{
				// 選択中の合成方式で旧pathが記録するhalo色。
				const uint32 ExpectedHalo = BlendIndex == 0 ? 0x0E2D0F15u : 0xFF020001u;
				// 選択中の合成方式で旧pathが記録するcore色。
				const uint32 ExpectedCore = BlendIndex == 0 ? 0x502D0F15u : 0xFF0E0406u;
				if ( std::fabs( Point.Width - 9.0f ) < 1.0e-4f && Point.Color == ExpectedHalo ) bFoundHalo = true;
				if ( std::fabs( Point.Width - 3.0f ) < 1.0e-4f && Point.Color == ExpectedCore ) bFoundCore = true;
			}
			C.Check( bFoundHalo && bFoundCore, "材質OFF LightningはAlpha/Add別の旧halo・core色変換を保つ" );
		}
		Playback.Reset();
	}
}

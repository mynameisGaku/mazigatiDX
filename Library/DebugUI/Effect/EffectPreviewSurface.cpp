// SPDX-License-Identifier: MIT
#include "EffectPreviewSurface.h"
#include "Library/DebugUI/Common/EditorLog.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"


#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Asset/Texture/RenderTarget.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Framework/World/Editor/EditorCamera.h"

namespace
{
	// 計算不能な視点や表示範囲を描画APIへ渡さない。
	bool IsValidView( const FEditorCamera& Camera, const FEditorViewport& View )
	{
		if ( !View.IsValid() || !NMath::IsFinite( View.Near ) || !NMath::IsFinite( View.Far ) || !NMath::IsFinite( View.FovY ) || View.Near <= 0.0f || View.Far <= View.Near || View.FovY <= 0.0f || View.FovY >= 3.14159265f ) return false;
		if ( !NMath::IsFinite( Camera.Target.x ) || !NMath::IsFinite( Camera.Target.y ) || !NMath::IsFinite( Camera.Target.z ) || !NMath::IsFinite( Camera.Yaw ) || !NMath::IsFinite( Camera.Pitch ) || !NMath::IsFinite( Camera.Distance ) || Camera.Distance <= 0.0f ) return false;
		// 注視点から視点を引いた結果も、有限である必要がある。
		const FVector3 Eye = Camera.GetEye();
		return NMath::IsFinite( Eye.x ) && NMath::IsFinite( Eye.y ) && NMath::IsFinite( Eye.z );
	}

	// 力場guide用のLine3D / wire Sphere3Dだけを読み、長さと描画値が安全か確かめる。
	bool IsValidGuideOverlay( const FDrawSpan& Overlay )
	{
		if ( Overlay.NumWords < 0 || Overlay.NumWords > NDrawCommand::MaxWords || ( Overlay.NumWords > 0 && Overlay.Words == nullptr ) ) return false;
		// 現在検証している命令の開始位置。
		int32 Offset = 0;
		while ( Offset < Overlay.NumWords )
		{
			// 命令全体のword数を示すheader。
			const uint32 Header = Overlay.Words[Offset];
			// 命令全体のword数。
			const int32 TotalWords = static_cast<int32>( Header >> 16 );
			// 命令種別。
			const EDrawCommand Type = static_cast<EDrawCommand>( Header & 0xFFu );
			// 命令flag。
			const uint8 Flags = static_cast<uint8>( ( Header >> 8 ) & 0xFFu );
			if ( TotalWords < 1 || TotalWords > Overlay.NumWords - Offset ) return false;
			// headerの次から始まるpayload。
			const uint32* const Payload = Overlay.Words + Offset + 1;
			if ( Type == EDrawCommand::Line3D )
			{
				if ( TotalWords != 1 + static_cast<int32>( sizeof( FDrawLine3DData ) / sizeof( uint32 ) ) || Flags != 0 ) return false;
				// 読み取り済みの線分座標。
				FDrawLine3DData Line;
				FMemory::Memcpy( &Line, Payload, sizeof( Line ) );
				for ( int32 Axis = 0; Axis < 3; ++Axis ) if ( !NMath::IsFinite( Line.P0[Axis] ) || !NMath::IsFinite( Line.P1[Axis] ) ) return false;
			}
			else if ( Type == EDrawCommand::Sphere3D )
			{
				if ( TotalWords != 1 + static_cast<int32>( sizeof( FDrawSphere3DData ) / sizeof( uint32 ) ) || ( Flags & FDrawFlag::Fill ) != 0 || ( Flags & ~FDrawFlag::Fill ) != 0 ) return false;
				// 読み取り済みのwire sphere設定。
				FDrawSphere3DData Sphere;
				FMemory::Memcpy( &Sphere, Payload, sizeof( Sphere ) );
				if ( !NMath::IsFinite( Sphere.Radius ) || Sphere.Radius <= 0.0f || Sphere.Div < 3 || Sphere.Div > 128 ) return false;
				for ( int32 Axis = 0; Axis < 3; ++Axis ) if ( !NMath::IsFinite( Sphere.Center[Axis] ) ) return false;
			}
			else return false;
			Offset += TotalWords;
		}
		return Offset == Overlay.NumWords;
	}

	// ワールドエディタと同じ距離刻みの床格子と、向きを示す3軸を記録する。
	void RecordGrid( FDrawContext& Draw, const FEditorCamera& Camera )
	{
		// 視点の距離に応じた、10の整数乗の格子間隔。
		const float32 Cell = NMath::Pow( 10.0f, NMath::Floor( NMath::Log10( NMath::Max( Camera.Distance * 0.25f, 0.01f ) ) ) );
		// 中心から片側へ引く格子線の数。
		constexpr int32 Half = 24;
		// 大きな注視点でも整数への変換で範囲を超えないよう、格子番号は実数で持つ。
		const float64 CenterX = NMath::Round( static_cast<float64>( Camera.Target.x ) / Cell );
		// 注視点を中心にしたZ方向の格子番号。
		const float64 CenterZ = NMath::Round( static_cast<float64>( Camera.Target.z ) / Cell );
		// 格子のX方向の始点。
		const float32 MinX = static_cast<float32>( ( CenterX - Half ) * Cell );
		// 格子のX方向の終点。
		const float32 MaxX = static_cast<float32>( ( CenterX + Half ) * Cell );
		// 格子のZ方向の始点。
		const float32 MinZ = static_cast<float32>( ( CenterZ - Half ) * Cell );
		// 格子のZ方向の終点。
		const float32 MaxZ = static_cast<float32>( ( CenterZ + Half ) * Cell );
		// 通常の格子線の色。
		const FColor32 Minor( 58, 61, 70, 255 );
		// 10本ごとの格子線の色。
		const FColor32 Major( 82, 86, 98, 255 );
		// 原点を通るX軸の色。
		const FColor32 AxisX( 190, 75, 75, 255 );
		// 原点を通るZ軸の色。
		const FColor32 AxisZ( 70, 100, 190, 255 );
		// 注視点の両側へ格子を1本ずつ増やす番号。
		for ( int32 Index = -Half; Index <= Half; ++Index )
		{
			// 今回のX方向の格子番号。
			const float64 IndexX = CenterX + Index;
			// 今回のZ方向の格子番号。
			const float64 IndexZ = CenterZ + Index;
			// 今回のZ軸と平行な線の位置。
			const float32 X = static_cast<float32>( IndexX * Cell );
			// 今回のX軸と平行な線の位置。
			const float32 Z = static_cast<float32>( IndexZ * Cell );
			Draw.Line3D( FVector3( X, 0.0f, MinZ ), FVector3( X, 0.0f, MaxZ ), IndexX == 0.0 ? AxisZ : NMath::Fmod( IndexX, 10.0 ) == 0.0 ? Major : Minor );
			Draw.Line3D( FVector3( MinX, 0.0f, Z ), FVector3( MaxX, 0.0f, Z ), IndexZ == 0.0 ? AxisX : NMath::Fmod( IndexZ, 10.0 ) == 0.0 ? Major : Minor );
		}
		Draw.Line3D( FVector3::Zero, FVector3::Up * ( Cell * 3.0f ), FColor32( 75, 185, 100, 255 ) );
	}
}

FEffectPreviewSurface::~FEffectPreviewSurface()
{
	Release();
}

bool FEffectPreviewSurface::Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor )
{
	// 従来の呼び出しは追加命令なしの経路へ渡し、描画内容を保つ。
	return Render( Camera, View, Playback, Executor, FDrawSpan() );
}

bool FEffectPreviewSurface::Render( const FEditorCamera& Camera, const FEditorViewport& View, const IEffectPlayback* Playback, IDrawExecutor& Executor, const FDrawSpan& Overlay )
{
	if ( !IsValidGuideOverlay( Overlay ) || !IsValidView( Camera, View ) )
	{
		return false;
	}
	if ( !Target.IsValid() || Width != View.Width || Height != View.Height )
	{
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
		if ( Assets == nullptr )
		{
			return false;
		}
		// 確保に失敗しても以前の表示画像を残すため、先に新しい描画先を作る（名前に大きさを入れて、前のものと別にする）
		FRenderTargetDesc Desc;
		Desc.Width = View.Width;
		Desc.Height = View.Height;
		Desc.bAlpha = false;
		Desc.bDepth = true;
		const TSharedPtr<CRenderTarget> NewTarget = Assets->CreateRenderTarget( FString::Printf( "Editor/Effect/Preview/%dx%d", View.Width, View.Height ), Desc );
		if ( !NewTarget.IsValid() )
		{
			LOG( LogEditorUI, Warning, "プレビュー画面を確保できませんでした: %d x %d", View.Width, View.Height );
			return false;
		}
		Target = NewTarget;
		Width = View.Width;
		Height = View.Height;
	}

	// プレビューのカメラ（描画先の大きさで、縦横比が決まる）
	FCamera PreviewCamera = FCamera::MakePerspective( Camera.GetEye(), Camera.Target, View.FovY );
	PreviewCamera.Near = View.Near;
	PreviewCamera.Far = View.Far;

	// 描画先・深度・カメラを決めて、Record を記録して、実行する（終わると、描画先・カメラ・深度は元に戻る）
	const auto Run = [&]( bool bClear, bool bWriteDepth, const auto& Record )
	{
		FDrawBuffer Buffer;
		{
			FDrawContext Draw( Buffer );
			if ( !Draw.PushTarget( Target ) )
			{
				return false;
			}
			if ( bClear )
			{
				Draw.Clear( FColor32( 38, 40, 48, 255 ) );
				Draw.ClearDepth();
			}
			FDepthState DepthState;
			DepthState.bTestDepth = true;
			DepthState.bWriteDepth = bWriteDepth;
			Draw.Depth( DepthState );
			const bool bCamera = Draw.PushCamera( PreviewCamera );
			// 呼び出し元が変えたままの物の変換・ブレンド・明るさを、プレビューへ漏らさない（格子と粒子は、記録済みのワールド座標で描く。
			// 実行の終わりに、実行側が、どれも実行の前の値へ戻す）
			Draw.WorldMatrix( FMatrix4x4::Identity );
			Draw.Blend( EDrawBlend::Opaque );
			Draw.Tint( FColor32( 255, 255, 255 ) );
			Record( Draw, Buffer );
			if ( bCamera )
			{
				Draw.PopCamera();
			}
			Draw.PopTarget();
		}
		Executor.Execute( FDrawSpan { Buffer.Words.GetData(), Buffer.Words.Num() } );
		return true;
	};

	// 不透明な床格子
	if ( !Run( true, true, [&]( FDrawContext& Draw, FDrawBuffer& )
	{
		RecordGrid( Draw, Camera );
	} ) )
	{
		return false;
	}
	// 半透明な粒子は、深度を書かずに（後ろの粒子を隠さない）。時間更新は呼び出し元で済ませる
	if ( Playback != nullptr )
	{
		Run( false, false, [&]( FDrawContext& Draw, FDrawBuffer& )
		{
			Playback->Record( Draw );
		} );
	}
	// 範囲線（確かめ済みの Line3D / Sphere3D だけの並び）を、同じカメラの中に、そのまま差し込む
	if ( Overlay.NumWords > 0 )
	{
		Run( false, false, [&]( FDrawContext&, FDrawBuffer& Buffer )
		{
			for ( int32 Index = 0; Index < Overlay.NumWords; ++Index )
			{
				Buffer.Words.Add( Overlay.Words[Index] );
			}
		} );
	}
	return true;
}

void FEffectPreviewSurface::Release()
{
	Target.Reset();
	Width = 0;
	Height = 0;
}

int32 FEffectPreviewSurface::GetHandle() const
{
	return Target.IsValid() ? Target->GetImageHandle().Value : -1;
}

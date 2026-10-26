// SPDX-License-Identifier: NOASSERTION
#include "EffectPreviewSurface.h"

#include <algorithm>
#include <cmath>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

DEFINE_LOG_CATEGORY_STATIC( LogEffectPreview, Log );

namespace
{
	// 描画先の切り替えと再生描画で変わる設定を、この呼び出しの終了時に戻す。
	struct FPreviewDrawState
	{
		// 呼び出し元が描いていた画面。
		int32 Screen = GetDrawScreen();
		// 画面切り替え時に設定を初期化するか。
		int32 ScreenReset = GetUseSetDrawScreenSettingReset();
		// 呼び出し元の近い側の表示限界。
		float32 Near = GetCameraNear();
		// 呼び出し元の遠い側の表示限界。
		float32 Far = GetCameraFar();
		// 呼び出し元の視野角。
		float32 Fov = GetCameraFov();
		// 呼び出し元の画素の縦横比。
		float32 DotAspect = GetCameraDotAspect();
		// 呼び出し元のカメラの表示中心。
		float32 CenterX = 0.0f;
		// 呼び出し元のカメラの表示中心。
		float32 CenterY = 0.0f;
		// 呼び出し元のカメラの向きと位置。
		MATRIX CameraView = GetCameraViewMatrix();
		// 呼び出し元のカメラの投影方法。
		MATRIX CameraProjection = GetCameraProjectionMatrix();
		// 呼び出し元の物体からワールドへの変換。
		MATRIX World = {};
		// 呼び出し元が直接指定した視点への変換。
		MATRIX View = {};
		// 呼び出し元が直接指定した投影の変換。
		MATRIX Projection = {};
		// 呼び出し元の画面上の配置。
		MATRIX Viewport = {};
		// 呼び出し元が描画を許した範囲。
		RECT Area = {};
		// 呼び出し元の背景の赤成分。
		int32 BackgroundR = 0;
		// 呼び出し元の背景の緑成分。
		int32 BackgroundG = 0;
		// 呼び出し元の背景の青成分。
		int32 BackgroundB = 0;
		// 呼び出し元の背景の不透明度。
		int32 BackgroundA = 0;
		// 呼び出し元の色の混ぜ方。
		int32 BlendMode = DX_BLENDMODE_NOBLEND;
		// 呼び出し元の混ぜる強さ。
		int32 BlendParam = 0;
		// 呼び出し元の描画色の赤の倍率。
		int32 BrightR = 255;
		// 呼び出し元の描画色の緑の倍率。
		int32 BrightG = 255;
		// 呼び出し元の描画色の青の倍率。
		int32 BrightB = 255;

		// 現在の設定を保存する。照明は変えず、画面切り替えによる初期化も止める。
		FPreviewDrawState()
		{
			GetBackgroundColor( &BackgroundR, &BackgroundG, &BackgroundB, &BackgroundA );
			GetDrawBlendMode( &BlendMode, &BlendParam );
			GetDrawBright( &BrightR, &BrightG, &BrightB );
			GetDrawArea( &Area );
			GetCameraScreenCenter( &CenterX, &CenterY );
			GetTransformToWorldMatrix( &World );
			GetTransformToViewMatrix( &View );
			GetTransformToProjectionMatrix( &Projection );
			GetTransformToViewportMatrix( &Viewport );
			SetUseSetDrawScreenSettingReset( FALSE );
		}

		// 残った頂点を専用画面へ描き終えてから、元の画面と取得できる設定を戻す。
		~FPreviewDrawState()
		{
			RenderVertex();
			SetDrawScreen( Screen );
			SetBackgroundColor( BackgroundR, BackgroundG, BackgroundB, BackgroundA );
			SetCameraNearFar( Near, Far );
			SetupCamera_Perspective( Fov );
			SetCameraDotAspect( DotAspect );
			SetCameraScreenCenter( CenterX, CenterY );
			SetCameraViewMatrix( CameraView );
			SetupCamera_ProjectionMatrix( CameraProjection );
			SetTransformToWorld( &World );
			SetTransformToView( &View );
			SetTransformToProjection( &Projection );
			SetTransformToViewport( &Viewport );
			SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			SetDrawBlendMode( BlendMode, BlendParam );
			SetDrawBright( BrightR, BrightG, BrightB );
			// DxLibに元のZ設定の取得口がないため、ツールの2D表示へ戻す契約にする。
			SetUseZBuffer3D( FALSE );
			SetWriteZBuffer3D( FALSE );
			SetUseSetDrawScreenSettingReset( ScreenReset );
		}
	};

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
	if ( !IsValidGuideOverlay( Overlay ) ) return false;
	if ( !IsValidView( Camera, View ) ) return false;
	// 呼び出し元の頂点と、作り直す画面への参照を先に描き終える。
	RenderVertex();
	// この呼び出しで触る描画状態。失敗で途中終了しても元に戻る。
	const FPreviewDrawState SavedState;
	if ( Handle < 0 || Width != View.Width || Height != View.Height )
	{
		// 呼び出し元の描画先を削除してしまうサイズ変更は行わない。
		if ( Handle >= 0 && SavedState.Screen == Handle ) return false;
		// 確保に失敗しても以前の表示画像を残すため、先に新しい画面を作る。
		const int32 NewHandle = MakeScreen( View.Width, View.Height, FALSE );
		if ( NewHandle < 0 )
		{
			LOG( LogEffectPreview, Warning, "プレビュー画面を確保できませんでした: %d x %d", View.Width, View.Height );
			return false;
		}
		if ( SetUseGraphZBuffer( NewHandle, TRUE ) < 0 )
		{
			DeleteGraph( NewHandle );
			LOG( LogEffectPreview, Warning, "プレビュー画面の深度用領域を確保できませんでした" );
			return false;
		}
		Release();
		Handle = NewHandle;
		Width = View.Width;
		Height = View.Height;
	}
	if ( SetDrawScreen( Handle ) < 0 ) return false;
	SetDrawAreaFull();
	SetBackgroundColor( 38, 40, 48, 255 );
	if ( ClearDrawScreen() < 0 ) return false;
	SetCameraNearFar( View.Near, View.Far );
	SetCameraDotAspect( 1.0f );
	SetCameraScreenCenter( static_cast<float32>( View.Width ) * 0.5f, static_cast<float32>( View.Height ) * 0.5f );
	SetupCamera_Perspective( View.FovY );
	SetCameraPositionAndTargetAndUpVec( Camera.GetEye(), Camera.Target, FVector3::Up );
	// 元の描画先の縦横比に影響されない、プレビューの投影行列。
	const MATRIX PreviewProjection = Camera.GetProjectionMatrix( View );
	SetupCamera_ProjectionMatrix( PreviewProjection );
	// 元の描画先の表示領域に影響されない、プレビュー全体への配置。
	MATRIX PreviewViewport = {};
	CreateViewportMatrix( &PreviewViewport, static_cast<float32>( View.Width ) * 0.5f, static_cast<float32>( View.Height ) * 0.5f, static_cast<float32>( View.Width ), static_cast<float32>( View.Height ) );
	SetTransformToViewport( &PreviewViewport );
	// 格子と粒子は記録済みのワールド座標で描く。
	const MATRIX Identity = MGetIdent();
	SetTransformToWorld( &Identity );
	SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
	SetDrawBright( 255, 255, 255 );
	SetUseZBuffer3D( TRUE );
	SetWriteZBuffer3D( TRUE );
	// 不透明な床格子の描画コマンド。
	FDrawBuffer GridBuffer;
	// 格子を既存の実行窓口へ渡すための記録先。
	FDrawContext GridDraw( GridBuffer );
	RecordGrid( GridDraw, Camera );
	Executor.Execute( FDrawSpan { GridBuffer.Words.GetData(), GridBuffer.Words.Num() } );
	if ( Playback != nullptr )
	{
		// 半透明な粒子の描画コマンド。時間更新は呼び出し元で済ませる。
		FDrawBuffer EffectBuffer;
		// 再生状態が記録する粒子の受け取り先。
		FDrawContext EffectDraw( EffectBuffer );
		Playback->Record( EffectDraw );
		if ( !EffectBuffer.Words.IsEmpty() )
		{
			SetWriteZBuffer3D( FALSE );
			Executor.Execute( FDrawSpan { EffectBuffer.Words.GetData(), EffectBuffer.Words.Num() } );
		}
	}
	if ( Overlay.NumWords > 0 )
	{
		SetWriteZBuffer3D( FALSE );
		Executor.Execute( Overlay );
	}
	return true;
}

void FEffectPreviewSurface::Release()
{
	if ( Handle >= 0 ) DeleteGraph( Handle );
	Handle = -1;
	Width = 0;
	Height = 0;
}

int32 FEffectPreviewSurface::GetHandle() const
{
	return Handle;
}

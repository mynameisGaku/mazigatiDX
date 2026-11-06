// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Draw/DrawContext.h"

//=============================================================================
// 描画の状態を、スコープの間だけ変えて、抜けるときに元へ戻す入れ物（FClipScope など）
//   FDrawContext にコマンドを記録するだけで、状態そのものは持たない。FDrawContext 本体とは役割が違うので、別のファイルにしている。
//=============================================================================

//=============================================================================
// FClipScope
//   コマンドリストにクリップ範囲を足して（PushClip）、スコープを抜けるとき、1 つ前の範囲に戻す（PopClip）。入れ子にできる。
//
//   {
//       FClipScope Clip( CommandList, 100, 100, 300, 200 );   // この中の描画は、(100, 100)-(300, 200) の外へはみ出さない
//       CommandList.Image( 90.0f, 90.0f, Texture );
//   }
//=============================================================================
class FClipScope
{
public:
	FClipScope( FDrawContext& InCommandList, int32 X0, int32 Y0, int32 X1, int32 Y1 )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushClip( X0, Y0, X1, Y1 ) )
	{
	}

	// PushClip が何も記録しなかったとき（値が大きすぎるとき）は、PopClip もしない（外側の範囲を戻してしまわないため）
	~FClipScope()
	{
		if ( bPushed )
		{
			CommandList->PopClip();
		}
	}

	FClipScope( const FClipScope& ) = delete;
	FClipScope& operator=( const FClipScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FTargetScope
//   コマンドリストの描画先を、描き込める画像（CRenderTarget など）に替えて（PushTarget）、スコープを抜けるとき、1 つ前の描画先に戻す（PopTarget）。
//   入れ子にできる。画像が無効なら、何も記録せず、戻すこともしない。
//
//   {
//       FTargetScope Scope( CommandList, Minimap );   // この中の描画は、Minimap に描かれる
//       CommandList.Clear();
//       CommandList.Circle( 64, 64, 30, FColor32::Red );
//   }
//   CommandList.Image( 10.0f, 10.0f, Minimap );       // ふつうの画像として、元の描画先へ貼る
//=============================================================================
class FTargetScope
{
public:
	template<typename TImage>
	FTargetScope( FDrawContext& InCommandList, const TImage& Target )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushTarget( Target ) )
	{
	}

	// PushTarget が何も記録しなかったとき（無効な画像）は、PopTarget もしない（外側の描画先を戻してしまわないため）
	~FTargetScope()
	{
		if ( bPushed )
		{
			CommandList->PopTarget();
		}
	}

	FTargetScope( const FTargetScope& ) = delete;
	FTargetScope& operator=( const FTargetScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FMaskScope
//   コマンドリストの以降の描画を、マスクの画像で薄めて（PushMask）、スコープを抜けるとき、1 つ前のマスクに戻す（PopMask）。
//   画像が無効なら、何も記録せず、戻すこともしない。
//
//   {
//       FMaskScope Scope( CommandList, SpotlightMask );   // この中の描画は、SpotlightMask の白いところだけに見える（段階的に）
//       CommandList.Image( 0.0f, 0.0f, Scene );
//   }
//=============================================================================
class FMaskScope
{
public:
	template<typename TImage>
	FMaskScope( FDrawContext& InCommandList, const TImage& Mask, EDrawMaskChannel Channel = EDrawMaskChannel::Alpha, bool bInvert = false )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushMask( Mask, Channel, bInvert ) )
	{
	}

	// PushMask が何も記録しなかったとき（無効な画像）は、PopMask もしない（外側のマスクを戻してしまわないため）
	~FMaskScope()
	{
		if ( bPushed )
		{
			CommandList->PopMask();
		}
	}

	FMaskScope( const FMaskScope& ) = delete;
	FMaskScope& operator=( const FMaskScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FWorldMatrixScope
//   コマンドリストの以降の 3D 図形の変換に Local を掛けて（PushWorldMatrix）、スコープを抜けるとき、1 つ前の変換に戻す（PopWorldMatrix）。
//   値が使えないときは、何も記録せず、戻すこともしない。
//
//   {
//       FWorldMatrixScope Tower( CommandList, FMatrix4x4::Translate( FVector3( 10.0f, 0.0f, 0.0f ) ) );   // この中の図形は、10 だけ横にずれる
//       CommandList.Cube3D( ... );
//   }
//=============================================================================
class FWorldMatrixScope
{
public:
	FWorldMatrixScope( FDrawContext& InCommandList, const FMatrix4x4& Local )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushWorldMatrix( Local ) )
	{
	}

	~FWorldMatrixScope()
	{
		if ( bPushed )
		{
			CommandList->PopWorldMatrix();
		}
	}

	FWorldMatrixScope( const FWorldMatrixScope& ) = delete;
	FWorldMatrixScope& operator=( const FWorldMatrixScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FLightScope
//   コマンドリストの以降の 3D の描画を、ライト Light でも照らして（PushLight）、スコープを抜けるとき、そのライトを外す（PopLight）。
//   値が使えないときは、何も記録せず、外すこともしない。
//
//   {
//       FLightScope Lamp( CommandList, FLight::MakePoint( FVector3( 0.0f, 3.0f, 0.0f ), 20.0f, FColor::Orange ) );
//       CommandList.Sphere3D( ... );
//   }
//=============================================================================
class FLightScope
{
public:
	FLightScope( FDrawContext& InCommandList, const FLight& Light )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushLight( Light ) )
	{
	}

	~FLightScope()
	{
		if ( bPushed )
		{
			CommandList->PopLight();
		}
	}

	FLightScope( const FLightScope& ) = delete;
	FLightScope& operator=( const FLightScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

//=============================================================================
// FShadowMapPassScope
//   コマンドリストの以降の 3D の描画を、シャドウマップ ShadowMap へ描く（影を落とすものを描く。BeginShadowMap）ようにして、スコープを抜けるとき、元の描画先へ戻す（EndShadowMap）。
//   シャドウマップか View が使えないときも、対の EndShadowMap は記録する（IsValid() が false。このスコープの中の図形は、画面に描かれず、飛ばされる）。
//
//   {
//       FShadowMapPassScope Pass( CommandList, ShadowMap, FShadowMapView::MakeForArea( FVector3( 1.0f, -1.0f, 1.0f ), AreaMin, AreaMax ) );
//       CommandList.Cube3D( ... );     // 影を落とすもの
//   }
//=============================================================================
class FShadowMapPassScope
{
public:
	template<typename TShadowMap>
	FShadowMapPassScope( FDrawContext& InCommandList, const TShadowMap& ShadowMap, const FShadowMapView& View )
		: CommandList( &InCommandList )
		, bValid( InCommandList.BeginShadowMap( ShadowMap, View ) )
	{
	}

	~FShadowMapPassScope()
	{
		CommandList->EndShadowMap();
	}

	// シャドウマップと View が使える値で、影を描くか
	FORCEINLINE bool IsValid() const { return bValid; }

	FShadowMapPassScope( const FShadowMapPassScope& ) = delete;
	FShadowMapPassScope& operator=( const FShadowMapPassScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bValid;
};

//=============================================================================
// FShadowMapUseScope
//   コマンドリストの以降の光を受ける 3D の図形に、スロット Slot のシャドウマップの影を付けて（UseShadowMap）、スコープを抜けるとき、そのスロットを使わなくする（StopUsingShadowMap）。
//   スロットかシャドウマップが使えないときは、何も記録せず、外すこともしない。
//
//   {
//       FShadowMapUseScope Shadow( CommandList, 0, ShadowMap );
//       CommandList.Cube3D( ... );     // 影を受けるもの
//   }
//=============================================================================
class FShadowMapUseScope
{
public:
	template<typename TShadowMap>
	FShadowMapUseScope( FDrawContext& InCommandList, int32 InSlot, const TShadowMap& ShadowMap )
		: CommandList( &InCommandList )
		, Slot( InSlot )
		, bUsing( InCommandList.UseShadowMap( InSlot, ShadowMap ) )
	{
	}

	~FShadowMapUseScope()
	{
		if ( bUsing )
		{
			CommandList->StopUsingShadowMap( Slot );
		}
	}

	// シャドウマップを使っているか（記録されたか）
	FORCEINLINE bool IsUsing() const { return bUsing; }

	FShadowMapUseScope( const FShadowMapUseScope& ) = delete;
	FShadowMapUseScope& operator=( const FShadowMapUseScope& ) = delete;

private:
	FDrawContext* CommandList;
	int32 Slot;
	bool bUsing;
};

//=============================================================================
// FShaderScope
//   コマンドリストの以降の、シェーダーで描く命令に、頂点シェーダーとピクセルシェーダーを使わせて（UseVertexShader / UsePixelShader）、スコープを抜けると外す。
//   どちらかが使えない（種類が違う・無効）ときは、使える方だけ使わせる（IsUsing() は、両方使えたときだけ true）。
//
//   {
//       FShaderScope Shaders( CommandList, VertexShader, PixelShader );
//       CommandList.ShaderRect( 0.0f, 0.0f, 640.0f, 480.0f );
//   }
//=============================================================================
class FShaderScope
{
public:
	template<typename TVertexShader, typename TPixelShader>
	FShaderScope( FDrawContext& InCommandList, const TVertexShader& VertexShader, const TPixelShader& PixelShader )
		: CommandList( &InCommandList )
		, bVertexUsing( InCommandList.UseVertexShader( VertexShader ) )
		, bPixelUsing( InCommandList.UsePixelShader( PixelShader ) )
	{
	}

	~FShaderScope()
	{
		if ( bVertexUsing )
		{
			CommandList->StopUsingVertexShader();
		}
		if ( bPixelUsing )
		{
			CommandList->StopUsingPixelShader();
		}
	}

	// 2 つとも使わせたか
	FORCEINLINE bool IsUsing() const { return bVertexUsing && bPixelUsing; }

	FShaderScope( const FShaderScope& ) = delete;
	FShaderScope& operator=( const FShaderScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bVertexUsing;
	bool bPixelUsing;
};

//=============================================================================
// FCameraScope
//   コマンドリストの以降の 3D の描画を、Camera で見て（PushCamera）、スコープを抜けるとき、1 つ前のカメラに戻す（PopCamera）。
//   カメラが使えない値のときは、何も記録せず、戻すこともしない。
//
//   {
//       FCameraScope Scope( CommandList, FCamera::MakePerspective( FVector3( 0.0f, 5.0f, -10.0f ), FVector3::Zero ) );
//       CommandList.Cube3D( ... );
//   }
//=============================================================================
class FCameraScope
{
public:
	FCameraScope( FDrawContext& InCommandList, const FCamera& Camera )
		: CommandList( &InCommandList )
		, bPushed( InCommandList.PushCamera( Camera ) )
	{
	}

	~FCameraScope()
	{
		if ( bPushed )
		{
			CommandList->PopCamera();
		}
	}

	// カメラが使えない値で、記録されなかったか
	FORCEINLINE bool IsPushed() const { return bPushed; }

	FCameraScope( const FCameraScope& ) = delete;
	FCameraScope& operator=( const FCameraScope& ) = delete;

private:
	FDrawContext* CommandList;
	bool bPushed;
};

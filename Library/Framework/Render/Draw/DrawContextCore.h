// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Interfaces/Render/IIndexBuffer.h"
#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Core/Interfaces/Render/IShadowMap.h"
#include "Library/Core/Interfaces/Render/IVertexBuffer.h"
#include "Library/Framework/Render/Draw/DrawDistortedMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawDistortedMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawModelMaterial.h"
#include "Library/Framework/Render/Draw/DrawModelFrameOverrideData.h"
#include "Library/Framework/Render/Draw/DrawMaterialMesh.h"
#include "Library/Framework/Render/Draw/DrawMaterialQuad.h"
#include "Library/Framework/Render/Draw/DrawMaterialRibbon.h"
#include "Library/Framework/Render/Draw/DrawRibbon.h"
#include "Library/Framework/Render/Scene/DrawSceneKeyData.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Types/BufferRange.h"
#include "Library/Framework/Render/Camera/Camera.h"
#include "Library/Framework/Render/State/GraphFilter.h"
#include "Library/Framework/Render/State/Lighting.h"
#include "Library/Framework/Render/State/RenderState3D.h"
#include "Library/Framework/Render/Types/ShaderVertex.h"
#include "Library/Framework/Render/State/ShadowMapView.h"
#include "Library/Framework/Render/State/TextStyle.h"
#include "Library/Framework/Render/Types/Vertex3D.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// FDrawBuffer
//   描画コマンドの入れ物（4 バイト単位の並び。形式は DrawCommand.h を参照）。
//=============================================================================
struct FDrawBuffer
{
	TArray<uint32> Words;

	// 中身を空にする（確保した領域は残す。毎フレーム使い回すため）
	void Clear() { Words.Reset(); }
};

//=============================================================================
// FDrawContextCore
//   描画コマンドを記録する窓口の土台。記録先（FDrawBuffer）と、命令を 1 つ書き込む処理（Push）、値の変換の補助、並べ替え可能な3D範囲の印を持つ。
//   命令の系統ごとの記録は、この上に積む（FDrawContext2DShapes → 2DImages → Targets → CameraState → Lighting → Shaders → FDrawContext）。
//=============================================================================
class FDrawContextCore
{
public:
	explicit FDrawContextCore( FDrawBuffer& InBuffer ) : Buffer( &InBuffer ) {}

	// これまでに記録したワード数（システムの実行機構が、オブジェクトごとの範囲を決めるのに使う）
	FORCEINLINE int32 GetNumWords() const { return Buffer->Words.Num(); }

	// 並べ替え可能な3D範囲を開始する。同じSpan内で閉じる。入れ子や影生成内は実行時に並べ替えを無効にする。
	void BeginScene3D();
	// 現在の3D範囲を閉じる。カメラ・状態・描画先は変更しない。
	void EndScene3D();
	// 直後の単独3D描画へ分類と奥行きを付ける。非有限値・無効分類は記録しない。
	// 間に状態変更・2D・未対応描画が入るとキーは失効し、その境界を越えて並べない。
	bool SceneDrawKey3D( ESceneDrawClass Class, float32 Depth );

protected:
	static FORCEINLINE FDrawRgb Rgb( const FColor32& Color ) { return FDrawRgb{ Color.r, Color.g, Color.b, 0 }; }
	static FORCEINLINE uint8 Flip( bool bFlipX, bool bFlipY ) { return static_cast<uint8>( ( bFlipX ? FDrawFlag::FlipX : 0 ) | ( bFlipY ? FDrawFlag::FlipY : 0 ) ); }

	// 描画に渡された画像から、ハンドルを取り出す（記録のとき、1 回読む）。nullptr は、無効なハンドルになる
	static FORCEINLINE FImageHandle ToHandle( FImageHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FImageHandle ToHandle( const IImage* Image )
	{
		return Image != nullptr ? Image->GetImageHandle() : FImageHandle();
	}

	static FORCEINLINE FImageHandle ToHandle( const IImage& Image )
	{
		return Image.GetImageHandle();
	}

	template<typename T>
	static FORCEINLINE FImageHandle ToHandle( const TSharedPtr<T>& Image )
	{
		return ToHandle( Image.Get() );
	}

	// 画像から切り出す矩形として使える値か（大きさが 1 以上。位置と大きさが、実行のときに int で扱える範囲）
	static FORCEINLINE bool IsValidSource( int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH )
	{
		return SrcW > 0 && SrcH > 0 && SrcW <= 10000000 && SrcH <= 10000000 && SrcX >= -10000000 && SrcX <= 10000000 && SrcY >= -10000000 && SrcY <= 10000000;
	}

	// 3D の値として使えるか（有限。NaN・無限大でない）
	static FORCEINLINE bool IsFiniteVector( const FVector3& Vector )
	{
		return NMath::IsFinite( Vector.x ) && NMath::IsFinite( Vector.y ) && NMath::IsFinite( Vector.z );
	}

	template<typename... TValues>
	static FORCEINLINE bool AllFinite( TValues... Values ) { return ( NMath::IsFinite( Values ) && ... ); }

	// 描画に渡されたバッファから、ハンドルを取り出す（記録のとき、1 回読む）。nullptr は、無効なハンドルになる
	static FORCEINLINE FVertexBufferHandle ToVertexHandle( FVertexBufferHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FVertexBufferHandle ToVertexHandle( const IVertexBuffer* Buffer )
	{
		return Buffer != nullptr ? Buffer->GetVertexBufferHandle() : FVertexBufferHandle();
	}

	static FORCEINLINE FVertexBufferHandle ToVertexHandle( const IVertexBuffer& Buffer )
	{
		return Buffer.GetVertexBufferHandle();
	}

	template<typename T>
	static FORCEINLINE FVertexBufferHandle ToVertexHandle( const TSharedPtr<T>& Buffer )
	{
		return ToVertexHandle( Buffer.Get() );
	}

	static FORCEINLINE FShadowMapHandle ToShadowMapHandle( FShadowMapHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FShadowMapHandle ToShadowMapHandle( const IShadowMap* ShadowMap )
	{
		return ShadowMap != nullptr ? ShadowMap->GetShadowMapHandle() : FShadowMapHandle();
	}

	static FORCEINLINE FShadowMapHandle ToShadowMapHandle( const IShadowMap& ShadowMap )
	{
		return ShadowMap.GetShadowMapHandle();
	}

	template<typename T>
	static FORCEINLINE FShadowMapHandle ToShadowMapHandle( const TSharedPtr<T>& ShadowMap )
	{
		return ToShadowMapHandle( ShadowMap.Get() );
	}

	static FORCEINLINE FShaderHandle ToShaderHandle( FShaderHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FShaderHandle ToShaderHandle( const IShader* Shader )
	{
		return Shader != nullptr ? Shader->GetShaderHandle() : FShaderHandle();
	}

	static FORCEINLINE FShaderHandle ToShaderHandle( const IShader& Shader )
	{
		return Shader.GetShaderHandle();
	}

	template<typename T>
	static FORCEINLINE FShaderHandle ToShaderHandle( const TSharedPtr<T>& Shader )
	{
		return ToShaderHandle( Shader.Get() );
	}

	static FORCEINLINE FIndexBufferHandle ToIndexHandle( FIndexBufferHandle Handle )
	{
		return Handle;
	}

	static FORCEINLINE FIndexBufferHandle ToIndexHandle( const IIndexBuffer* Buffer )
	{
		return Buffer != nullptr ? Buffer->GetIndexBufferHandle() : FIndexBufferHandle();
	}

	static FORCEINLINE FIndexBufferHandle ToIndexHandle( const IIndexBuffer& Buffer )
	{
		return Buffer.GetIndexBufferHandle();
	}

	template<typename T>
	static FORCEINLINE FIndexBufferHandle ToIndexHandle( const TSharedPtr<T>& Buffer )
	{
		return ToIndexHandle( Buffer.Get() );
	}

	// 2D の図形の座標として描ける値か（有限で、実行のときに int にしても溢れない範囲）
	static FORCEINLINE bool IsDrawable( float32 Value ) { return NMath::IsFinite( Value ) && Value >= -1.0e7f && Value <= 1.0e7f; }
	template<typename... TValues>
	static FORCEINLINE bool AllDrawable( TValues... Values ) { return ( IsDrawable( Values ) && ... ); }
	static FORCEINLINE bool IsPositiveThickness( float32 Thickness ) { return NMath::IsFinite( Thickness ) && Thickness > 0.0f && Thickness <= 1.0e4f; }
	// 近似する角数。0 は「半径から決める」。それ以外は 4..512 に収める
	static FORCEINLINE int32 ClampSegments( int32 NumSegments ) { return NumSegments <= 0 ? 0 : NMath::Clamp( NumSegments, 4, 512 ); }
	// フィルターの命令を記録する（画像・設定・矩形が使える値のときだけ）
	bool PushImageFilter( FImageHandle Source, FImageHandle Dest, const FGraphFilter& Filter, bool bAlphaBlend, bool bRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY );
	// 列挙型の値が、0 から最後の値（Max）の範囲に入っているか
	static FORCEINLINE bool IsInEnumRange( int32 Value, int32 Max )
	{
		return Value >= 0 && Value <= Max;
	}

	void PushTriangle( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags );

	void PushQuad( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, const FColor32& Color, bool bFill, float32 Thickness, uint8 Flags );

	void PushOval( float32 X, float32 Y, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags );

	void PushRoundBox( float32 X0, float32 Y0, float32 X1, float32 Y1, float32 Rx, float32 Ry, const FColor32& Color, bool bFill, float32 Thickness, int32 NumSegments, uint8 Flags );
	static void Set3( float32 ( &Out )[3], const FVector3& V );
	static void PackAffine( float32 ( &Out )[12], const FMatrix4x4& Matrix );

	// コマンド 1 つを末尾に足す。足した分は 0 で埋まってから書くので、隙間にも決まった値が入る（ビット単位で比べられる）
	template<typename TData>
	FORCEINLINE void Push( EDrawCommand Type, uint8 Flags, const TData& Data )
	{
		constexpr int32 DataWords = static_cast<int32>( sizeof( TData ) / 4 );
		TArray<uint32>& Words = Buffer->Words;
		const int32 Begin = Words.AddZeroed( 1 + DataWords );
		uint32* const Out = Words.GetData() + Begin;
		Out[0] = NDrawCommand::MakeHeader( Type, Flags, 1 + DataWords );
		FMemory::Memcpy( Out + 1, &Data, sizeof( TData ) );
	}

protected:
	FDrawBuffer* Buffer;
};

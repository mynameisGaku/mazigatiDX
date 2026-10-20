#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/Matrix4x4.h"

//=============================================================================
// FTransform
//   位置・回転・スケール。適用順は Scale -> Rotation -> Position。
//=============================================================================
struct FTransform
{
	FVector3    Position = FVector3::Zero;
	FQuaternion Rotation = FQuaternion::Identity;
	FVector3    Scale    = FVector3::One;

	constexpr FTransform() = default;
	constexpr FTransform( const FVector3& InPosition, const FQuaternion& InRotation = FQuaternion::Identity, const FVector3& InScale = FVector3::One )
		: Position( InPosition ), Rotation( InRotation ), Scale( InScale )
	{}

	static const FTransform Identity;

	FORCEINLINE FString ToString() const
	{
		return FString::Printf( "Pos(%s) Rot(%s) Scale(%s)", *Position.ToString(), *Rotation.ToString(), *Scale.ToString() );
	}

	//-------------------------------------------------------------------------
	// 行列
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FMatrix4x4 ToMatrix() const { return FMatrix4x4::TRS( Position, Rotation, Scale ); }

	FORCEINLINE static FTransform FromMatrix( const FMatrix4x4& M )
	{
		return { M.GetTranslation(), M.GetRotation(), M.GetScale() };
	}

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	// ローカル座標の点 -> ワールド
	FORCEINLINE constexpr FVector3 TransformPoint( const FVector3& P ) const { return Rotation.Rotate( P * Scale ) + Position; }
	// ローカルの方向 -> ワールド（スケールを含む、平行移動なし）
	FORCEINLINE constexpr FVector3 TransformVector( const FVector3& V ) const { return Rotation.Rotate( V * Scale ); }
	// ローカルの方向 -> ワールド（回転のみ）
	FORCEINLINE constexpr FVector3 TransformDirection( const FVector3& V ) const { return Rotation.Rotate( V ); }

	// ワールド座標の点 -> ローカル
	FORCEINLINE constexpr FVector3 InverseTransformPoint( const FVector3& P ) const { return Rotation.UnrotateVector( P - Position ) / Scale; }
	FORCEINLINE constexpr FVector3 InverseTransformVector( const FVector3& V ) const { return Rotation.UnrotateVector( V ) / Scale; }
	FORCEINLINE constexpr FVector3 InverseTransformDirection( const FVector3& V ) const { return Rotation.UnrotateVector( V ); }

	//-------------------------------------------------------------------------
	// 合成
	//-------------------------------------------------------------------------
	// Child（Parent 空間でのローカル）を Parent のワールドへ
	//   ※ 非一様スケールと回転が混在する場合、せん断は表現できないため近似になる
	FORCEINLINE static constexpr FTransform Combine( const FTransform& Parent, const FTransform& Child )
	{
		return {
			Parent.TransformPoint( Child.Position ),
			Parent.Rotation * Child.Rotation,
			Parent.Scale * Child.Scale };
	}

	// 逆変換（非一様スケール + 回転では近似）
	FORCEINLINE FTransform Inverse() const
	{
		const FQuaternion InvRotation = Rotation.Inverse();
		const FVector3    InvScale( SafeReciprocal( Scale.x ), SafeReciprocal( Scale.y ), SafeReciprocal( Scale.z ) );
		return { InvRotation.Rotate( -Position ) * InvScale, InvRotation, InvScale };
	}

	//-------------------------------------------------------------------------
	// 方向
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector3 GetForward() const { return Rotation.GetForward(); }
	FORCEINLINE constexpr FVector3 GetRight() const { return Rotation.GetRight(); }
	FORCEINLINE constexpr FVector3 GetUp() const { return Rotation.GetUp(); }

	FORCEINLINE static FTransform Lerp( const FTransform& A, const FTransform& B, float32 Alpha )
	{
		return {
			FVector3::Lerp( A.Position, B.Position, Alpha ),
			FQuaternion::Slerp( A.Rotation, B.Rotation, Alpha ),
			FVector3::Lerp( A.Scale, B.Scale, Alpha ) };
	}

	FORCEINLINE bool Equals( const FTransform& T, float32 Tolerance = NMath::Epsilon ) const
	{
		return Position.Equals( T.Position, Tolerance ) && Rotation.Equals( T.Rotation, Tolerance ) && Scale.Equals( T.Scale, Tolerance );
	}

private:
	FORCEINLINE static constexpr float32 SafeReciprocal( float32 V ) { return NMath::Abs( V ) > NMath::SmallNumber ? 1.0f / V : 0.0f; }
};

inline const FTransform FTransform::Identity {};

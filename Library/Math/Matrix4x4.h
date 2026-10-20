#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Math/Vector4.h"
#include "Library/Math/Quaternion.h"

//=============================================================================
// FMatrix4x4
//   DxLib の MATRIX と同じ規約:
//     - 行ベクトル (v' = v * M)、行優先格納 m[行][列]
//     - 平行移動は m[3][0..2]
//     - 左手系（Perspective / LookAt は LH）
//   A * B は「A を適用した後に B を適用」。
//   ワールド行列は Scale * Rotate * Translate の順で掛ける。
//=============================================================================
struct alignas( 16 ) FMatrix4x4
{
	float32 m[4][4] = {
		{ 1.0f, 0.0f, 0.0f, 0.0f },
		{ 0.0f, 1.0f, 0.0f, 0.0f },
		{ 0.0f, 0.0f, 1.0f, 0.0f },
		{ 0.0f, 0.0f, 0.0f, 1.0f },
	};

	constexpr FMatrix4x4() = default;
	constexpr FMatrix4x4(
		float32 m00, float32 m01, float32 m02, float32 m03,
		float32 m10, float32 m11, float32 m12, float32 m13,
		float32 m20, float32 m21, float32 m22, float32 m23,
		float32 m30, float32 m31, float32 m32, float32 m33 )
		: m{ { m00, m01, m02, m03 }, { m10, m11, m12, m13 }, { m20, m21, m22, m23 }, { m30, m31, m32, m33 } }
	{}

	static const FMatrix4x4 Identity;
	static const FMatrix4x4 Zero;

	//-------------------------------------------------------------------------
	// DxLib 変換（規約が同じなので要素をそのままコピー）
	//   MV1SetMatrix( Handle, Transform.ToMatrix() );   // MATRIX に暗黙変換
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FMatrix4x4( const MATRIX& M )
	{
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				m[i][j] = M.m[i][j];
	}
	FORCEINLINE constexpr operator MATRIX() const
	{
		MATRIX M{};
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				M.m[i][j] = m[i][j];
		return M;
	}

	FORCEINLINE constexpr explicit FMatrix4x4( const MATRIX_D& M )
	{
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				m[i][j] = static_cast<float32>( M.m[i][j] );
	}
	FORCEINLINE constexpr MATRIX_D ToDxD() const
	{
		MATRIX_D M{};
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				M.m[i][j] = m[i][j];
		return M;
	}

	//-------------------------------------------------------------------------
	// 生成
	//-------------------------------------------------------------------------
	FORCEINLINE static constexpr FMatrix4x4 Translate( const FVector3& T )
	{
		return {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			T.x,  T.y,  T.z,  1.0f };
	}

	FORCEINLINE static constexpr FMatrix4x4 Scale( const FVector3& S )
	{
		return {
			S.x,  0.0f, 0.0f, 0.0f,
			0.0f, S.y,  0.0f, 0.0f,
			0.0f, 0.0f, S.z,  0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };
	}
	FORCEINLINE static constexpr FMatrix4x4 Scale( float32 S ) { return Scale( FVector3( S ) ); }

	FORCEINLINE static FMatrix4x4 RotateX( float32 Rad )
	{
		const float32 c = NMath::Cos( Rad ), s = NMath::Sin( Rad );
		return {
			1.0f, 0.0f, 0.0f, 0.0f,
			0.0f, c,    s,    0.0f,
			0.0f, -s,   c,    0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };
	}

	FORCEINLINE static FMatrix4x4 RotateY( float32 Rad )
	{
		const float32 c = NMath::Cos( Rad ), s = NMath::Sin( Rad );
		return {
			c,    0.0f, -s,   0.0f,
			0.0f, 1.0f, 0.0f, 0.0f,
			s,    0.0f, c,    0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };
	}

	FORCEINLINE static FMatrix4x4 RotateZ( float32 Rad )
	{
		const float32 c = NMath::Cos( Rad ), s = NMath::Sin( Rad );
		return {
			c,    s,    0.0f, 0.0f,
			-s,   c,    0.0f, 0.0f,
			0.0f, 0.0f, 1.0f, 0.0f,
			0.0f, 0.0f, 0.0f, 1.0f };
	}

	FORCEINLINE static constexpr FMatrix4x4 Rotate( const FQuaternion& Q )
	{
		const float32 xx = Q.x * Q.x, yy = Q.y * Q.y, zz = Q.z * Q.z;
		const float32 xy = Q.x * Q.y, xz = Q.x * Q.z, yz = Q.y * Q.z;
		const float32 wx = Q.w * Q.x, wy = Q.w * Q.y, wz = Q.w * Q.z;
		// 各行 = 回転後の X / Y / Z 軸
		return {
			1.0f - 2.0f * ( yy + zz ), 2.0f * ( xy + wz ),        2.0f * ( xz - wy ),        0.0f,
			2.0f * ( xy - wz ),        1.0f - 2.0f * ( xx + zz ), 2.0f * ( yz + wx ),        0.0f,
			2.0f * ( xz + wy ),        2.0f * ( yz - wx ),        1.0f - 2.0f * ( xx + yy ), 0.0f,
			0.0f,                      0.0f,                      0.0f,                      1.0f };
	}
	FORCEINLINE static FMatrix4x4 Rotate( const FVector3& Axis, float32 Rad ) { return Rotate( FQuaternion::FromAxisAngle( Axis, Rad ) ); }

	// Scale -> Rotate -> Translate
	FORCEINLINE static constexpr FMatrix4x4 TRS( const FVector3& T, const FQuaternion& R, const FVector3& S )
	{
		FMatrix4x4 M = Rotate( R );
		for ( int32 c = 0; c < 3; ++c )
		{
			M.m[0][c] *= S.x;
			M.m[1][c] *= S.y;
			M.m[2][c] *= S.z;
		}
		M.m[3][0] = T.x;
		M.m[3][1] = T.y;
		M.m[3][2] = T.z;
		return M;
	}

	// 透視射影（左手系、Z: 0..1）
	FORCEINLINE static FMatrix4x4 Perspective( float32 FovYRad, float32 Aspect, float32 Near, float32 Far )
	{
		const float32 YScale = 1.0f / NMath::Tan( FovYRad * 0.5f );
		const float32 XScale = YScale / Aspect;
		const float32 Range  = Far / ( Far - Near );
		return {
			XScale, 0.0f,   0.0f,          0.0f,
			0.0f,   YScale, 0.0f,          0.0f,
			0.0f,   0.0f,   Range,         1.0f,
			0.0f,   0.0f,   -Near * Range, 0.0f };
	}

	// 正射影（左手系、Z: 0..1）
	FORCEINLINE static constexpr FMatrix4x4 Orthographic( float32 Width, float32 Height, float32 Near, float32 Far )
	{
		const float32 Range = 1.0f / ( Far - Near );
		return {
			2.0f / Width, 0.0f,          0.0f,          0.0f,
			0.0f,         2.0f / Height, 0.0f,          0.0f,
			0.0f,         0.0f,          Range,         0.0f,
			0.0f,         0.0f,          -Near * Range, 1.0f };
	}

	// ビュー行列（左手系）
	FORCEINLINE static FMatrix4x4 LookAt( const FVector3& Eye, const FVector3& Target, const FVector3& Up = FVector3::Up )
	{
		const FVector3 AxisZ = ( Target - Eye ).Normalized();
		const FVector3 AxisX = FVector3::Cross( Up, AxisZ ).Normalized();
		const FVector3 AxisY = FVector3::Cross( AxisZ, AxisX );
		return {
			AxisX.x,                       AxisY.x,                       AxisZ.x,                       0.0f,
			AxisX.y,                       AxisY.y,                       AxisZ.y,                       0.0f,
			AxisX.z,                       AxisY.z,                       AxisZ.z,                       0.0f,
			-FVector3::Dot( AxisX, Eye ), -FVector3::Dot( AxisY, Eye ), -FVector3::Dot( AxisZ, Eye ), 1.0f };
	}

	//-------------------------------------------------------------------------
	// 演算子
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FMatrix4x4 operator*( const FMatrix4x4& B ) const
	{
		FMatrix4x4 R = Zero;
		for ( int32 i = 0; i < 4; ++i )
		{
			for ( int32 j = 0; j < 4; ++j )
			{
				R.m[i][j] = m[i][0] * B.m[0][j] + m[i][1] * B.m[1][j] + m[i][2] * B.m[2][j] + m[i][3] * B.m[3][j];
			}
		}
		return R;
	}
	FMatrix4x4& operator*=( const FMatrix4x4& B ) { *this = *this * B; return *this; }

	FORCEINLINE constexpr bool operator==( const FMatrix4x4& B ) const
	{
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				if ( m[i][j] != B.m[i][j] ) return false;
		return true;
	}
	constexpr bool operator!=( const FMatrix4x4& B ) const { return !( *this == B ); }

	//-------------------------------------------------------------------------
	// 変換
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector4 Transform( const FVector4& V ) const
	{
		return {
			V.x * m[0][0] + V.y * m[1][0] + V.z * m[2][0] + V.w * m[3][0],
			V.x * m[0][1] + V.y * m[1][1] + V.z * m[2][1] + V.w * m[3][1],
			V.x * m[0][2] + V.y * m[1][2] + V.z * m[2][2] + V.w * m[3][2],
			V.x * m[0][3] + V.y * m[1][3] + V.z * m[2][3] + V.w * m[3][3] };
	}

	// 点を変換 (w = 1)。射影行列の場合は w 除算も行う
	FORCEINLINE constexpr FVector3 TransformPoint( const FVector3& P ) const
	{
		const FVector4 R = Transform( FVector4( P, 1.0f ) );
		return ( R.w != 0.0f && R.w != 1.0f ) ? R.XYZ() / R.w : R.XYZ();
	}

	// 方向を変換 (w = 0、平行移動を無視)
	FORCEINLINE constexpr FVector3 TransformVector( const FVector3& V ) const
	{
		return Transform( FVector4( V, 0.0f ) ).XYZ();
	}

	//-------------------------------------------------------------------------
	// 取得
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FVector3 GetTranslation() const { return { m[3][0], m[3][1], m[3][2] }; }
	FORCEINLINE constexpr FVector3 GetAxisX() const { return { m[0][0], m[0][1], m[0][2] }; }
	FORCEINLINE constexpr FVector3 GetAxisY() const { return { m[1][0], m[1][1], m[1][2] }; }
	FORCEINLINE constexpr FVector3 GetAxisZ() const { return { m[2][0], m[2][1], m[2][2] }; }
	FORCEINLINE FVector3 GetScale() const { return { GetAxisX().Length(), GetAxisY().Length(), GetAxisZ().Length() }; }
	FORCEINLINE FQuaternion GetRotation() const
	{
		return FQuaternion::FromBasis( GetAxisX().Normalized(), GetAxisY().Normalized(), GetAxisZ().Normalized() );
	}

	//-------------------------------------------------------------------------
	// 行列演算
	//-------------------------------------------------------------------------
	FORCEINLINE constexpr FMatrix4x4 Transposed() const
	{
		return {
			m[0][0], m[1][0], m[2][0], m[3][0],
			m[0][1], m[1][1], m[2][1], m[3][1],
			m[0][2], m[1][2], m[2][2], m[3][2],
			m[0][3], m[1][3], m[2][3], m[3][3] };
	}

	FORCEINLINE constexpr float32 Determinant() const
	{
		const float32 s0 = m[0][0] * m[1][1] - m[1][0] * m[0][1];
		const float32 s1 = m[0][0] * m[1][2] - m[1][0] * m[0][2];
		const float32 s2 = m[0][0] * m[1][3] - m[1][0] * m[0][3];
		const float32 s3 = m[0][1] * m[1][2] - m[1][1] * m[0][2];
		const float32 s4 = m[0][1] * m[1][3] - m[1][1] * m[0][3];
		const float32 s5 = m[0][2] * m[1][3] - m[1][2] * m[0][3];
		const float32 c5 = m[2][2] * m[3][3] - m[3][2] * m[2][3];
		const float32 c4 = m[2][1] * m[3][3] - m[3][1] * m[2][3];
		const float32 c3 = m[2][1] * m[3][2] - m[3][1] * m[2][2];
		const float32 c2 = m[2][0] * m[3][3] - m[3][0] * m[2][3];
		const float32 c1 = m[2][0] * m[3][2] - m[3][0] * m[2][2];
		const float32 c0 = m[2][0] * m[3][1] - m[3][0] * m[2][1];
		return s0 * c5 - s1 * c4 + s2 * c3 + s3 * c2 - s4 * c1 + s5 * c0;
	}

	// 逆行列を求める。特異行列なら false を返し OutInverse は変更しない
	bool TryInverse( FMatrix4x4& OutInverse ) const;

	// 逆行列（特異行列なら Identity）
	FORCEINLINE FMatrix4x4 Inverse() const
	{
		FMatrix4x4 R;
		return TryInverse( R ) ? R : Identity;
	}

	FORCEINLINE bool Equals( const FMatrix4x4& B, float32 Tolerance = NMath::Epsilon ) const
	{
		for ( int32 i = 0; i < 4; ++i )
			for ( int32 j = 0; j < 4; ++j )
				if ( NMath::Abs( m[i][j] - B.m[i][j] ) > Tolerance ) return false;
		return true;
	}
};

inline constexpr FMatrix4x4 FMatrix4x4::Identity {
	1.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 1.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 1.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 1.0f };

inline constexpr FMatrix4x4 FMatrix4x4::Zero {
	0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f,
	0.0f, 0.0f, 0.0f, 0.0f };

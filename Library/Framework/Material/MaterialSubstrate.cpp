// SPDX-License-Identifier: MIT
#include "MaterialSubstrate.h"

#include "Library/Framework/Material/MaterialEval.h"
#include "Library/Math/MathUtils.h"

// ここの C++ と、下の HLSL は、同じ計算。片方を変えたら、もう片方も変える（検査が、本物の画素と、C++ の結果を比べている）
namespace
{
	constexpr float32 Pi = 3.14159265f;
	constexpr float32 MaxSpecular = 16.0f;    // 鏡面の山の高さの上限（とても滑らかな面で、値が飛ばないように）
	// 簡略版の光（平行光の強さと、環境の明るさ。決まった値）
	constexpr float32 SimplifiedDirectLight = 0.85f;
	constexpr float32 SimplifiedAmbientLight = 0.15f;

	float32 Saturate( float32 Value )
	{
		return NMath::Clamp( Value, 0.0f, 1.0f );
	}

	FVector3 Lerp( const FVector3& A, const FVector3& B, float32 Alpha )
	{
		return A + ( B - A ) * Alpha;
	}

	float32 Lerp( float32 A, float32 B, float32 Alpha )
	{
		return A + ( B - A ) * Alpha;
	}

	float32 Pow5( float32 Value )
	{
		const float32 Squared = Value * Value;
		return Squared * Squared * Value;
	}

	FVector3 SafeNormalize( const FVector3& Vector, const FVector3& Fallback )
	{
		const float32 LengthSquared = FVector3::Dot( Vector, Vector );
		return LengthSquared > 1.0e-12f ? Vector * ( 1.0f / NMath::Sqrt( LengthSquared ) ) : Fallback;
	}

	// 法線に垂直な、接線（渡された接線が、法線と同じ向き・長さ 0 なら、適当な垂直の向き）
	FVector3 MakeTangent( const FVector3& Normal, const FVector3& Tangent )
	{
		const FVector3 Other = NMath::Abs( Normal.y ) < 0.99f ? FVector3::Cross( FVector3( 0.0f, 1.0f, 0.0f ), Normal ) : FVector3( 1.0f, 0.0f, 0.0f );
		const FVector3 Fallback = SafeNormalize( Other - Normal * FVector3::Dot( Normal, Other ), FVector3( 1.0f, 0.0f, 0.0f ) );
		return SafeNormalize( Tangent - Normal * FVector3::Dot( Normal, Tangent ), Fallback );
	}

	// 鏡面の山の高さ（分布 x 遮蔽 x π）。等方（クリアコート用）
	float32 SpecularLobe( float32 NdotH, float32 NdotL, float32 NdotV, float32 Alpha )
	{
		const float32 AlphaSquared = Alpha * Alpha;
		const float32 Denominator = NdotH * NdotH * ( AlphaSquared - 1.0f ) + 1.0f;
		const float32 Distribution = AlphaSquared / ( Pi * Denominator * Denominator );
		const float32 Visibility = 0.5f / NMath::Max( Lerp( 2.0f * NdotL * NdotV, NdotL + NdotV, Alpha ), 1.0e-4f );
		return NMath::Min( Distribution * Visibility * Pi, MaxSpecular );
	}

	// 環境の色（Direction は、長さ 1。Blur は 0〜1）
	FVector3 EnvironmentColor( const FSubstrateLighting& Lighting, const FVector3& Direction, float32 Blur )
	{
		if ( Lighting.Environment != nullptr && Lighting.Environment->IsValid() )
		{
			return Lighting.Environment->Sample( Direction, Blur ) * Lighting.EnvironmentIntensity;
		}
		const FVector3 Gradient = Lerp( Lighting.GroundColor, Lighting.SkyColor, Saturate( Direction.y * 0.5f + 0.5f ) );
		return Lerp( Gradient, Lighting.AmbientColor, Saturate( Blur ) );
	}

	// 見る向きで決まる、層の表面の性質（光 1 つごとの計算で、共通に使う）
	struct FSurface
	{
		FVector3 N;
		FVector3 V;
		FVector3 T;
		FVector3 B;
		float32 NdotV = 1.0f;
		FVector3 SpecularColor;
		float32 AlphaT = 1.0f;
		float32 AlphaB = 1.0f;
		FVector3 DiffuseColor;
		FVector3 SubsurfaceColor;
		float32 Coat = 0.0f;
		float32 CoatAlpha = 1.0f;
	};

	// 光 1 つ（向き L・明るさ Radiance）の分を、足す。Direct は、層そのもの（拡散・サブサーフェス・鏡面）、CoatDirect は、クリアコートの鏡面
	void AddLight( const FSurface& S, const FVector3& L, const FVector3& Radiance, FVector3& Direct, FVector3& CoatDirect )
	{
		const FVector3 H = SafeNormalize( L + S.V, S.N );
		const float32 NdotLRaw = FVector3::Dot( S.N, L );
		const float32 NdotL = Saturate( NdotLRaw );
		const float32 NdotH = Saturate( FVector3::Dot( S.N, H ) );
		const float32 VdotH = Saturate( FVector3::Dot( S.V, H ) );
		const FVector3 Fresnel = S.SpecularColor + ( FVector3( 1.0f, 1.0f, 1.0f ) - S.SpecularColor ) * Pow5( 1.0f - VdotH );

		// 異方性の GGX: 接線の向きと、従接線の向きで、粗さを変える
		const float32 TdotH = FVector3::Dot( S.T, H );
		const float32 BdotH = FVector3::Dot( S.B, H );
		const float32 Denominator = TdotH * TdotH / ( S.AlphaT * S.AlphaT ) + BdotH * BdotH / ( S.AlphaB * S.AlphaB ) + NdotH * NdotH;
		const float32 Distribution = 1.0f / ( Pi * S.AlphaT * S.AlphaB * Denominator * Denominator );
		const float32 Visibility = 0.5f / NMath::Max( Lerp( 2.0f * NdotL * S.NdotV, NdotL + S.NdotV, NMath::Sqrt( S.AlphaT * S.AlphaB ) ), 1.0e-4f );
		const FVector3 Specular = Fresnel * ( NMath::Min( Distribution * Visibility * Pi, MaxSpecular ) * NdotL );

		// 拡散: 鏡面が反射した残り。サブサーフェス: 光の回り込みと、逆光で透ける分
		const FVector3 Diffuse = S.DiffuseColor * ( FVector3( 1.0f, 1.0f, 1.0f ) - Fresnel ) * NdotL;
		const float32 Wrap = Saturate( ( NdotLRaw + 0.5f ) / 1.5f );
		const float32 BackLight = Saturate( -FVector3::Dot( S.V, L ) );
		const FVector3 Subsurface = S.SubsurfaceColor * ( ( Wrap - NdotL ) + 0.25f * BackLight * BackLight * BackLight * BackLight );
		Direct = Direct + ( Diffuse + Subsurface + Specular ) * Radiance;

		const float32 CoatFresnel = ( 0.04f + 0.96f * Pow5( 1.0f - VdotH ) ) * S.Coat;
		CoatDirect = CoatDirect + Radiance * ( SpecularLobe( NdotH, NdotL, S.NdotV, S.CoatAlpha ) * NdotL * CoatFresnel );
	}

	FVector3 ToLinear( const FColor32& Color )
	{
		return FVector3( static_cast<float32>( Color.r ) / 255.0f, static_cast<float32>( Color.g ) / 255.0f, static_cast<float32>( Color.b ) / 255.0f );
	}

	// 全周の絵の、画素 (X, Y) の中心の向き（幅 Width・高さ Height）
	FVector3 PanoramaDirection( int32 X, int32 Y, int32 Width, int32 Height )
	{
		const float32 Azimuth = ( ( static_cast<float32>( X ) + 0.5f ) / static_cast<float32>( Width ) - 0.5f ) * 2.0f * Pi;
		const float32 Polar = ( static_cast<float32>( Y ) + 0.5f ) / static_cast<float32>( Height ) * Pi;
		const float32 Ring = NMath::Sin( Polar );
		return FVector3( NMath::Sin( Azimuth ) * Ring, NMath::Cos( Polar ), NMath::Cos( Azimuth ) * Ring );
	}

	// 絵（Width x Height の色の並び）を、双線形で読む（横は回り込み、縦は端で止める）。X・Y は、画素の座標（中心が .5）
	FVector3 ReadBilinear( const TArray<FVector3>& Texels, int32 Width, int32 Height, float32 X, float32 Y )
	{
		const float32 FloorX = NMath::Floor( X - 0.5f );
		const float32 FloorY = NMath::Floor( Y - 0.5f );
		const float32 FractionX = X - 0.5f - FloorX;
		const float32 FractionY = Y - 0.5f - FloorY;
		auto Texel = [&]( int32 TexelX, int32 TexelY )
		{
			const int32 WrappedX = ( ( TexelX % Width ) + Width ) % Width;
			const int32 ClampedY = NMath::Clamp( TexelY, 0, Height - 1 );
			return Texels[ClampedY * Width + WrappedX];
		};
		const int32 X0 = static_cast<int32>( FloorX );
		const int32 Y0 = static_cast<int32>( FloorY );
		const FVector3 Top = Lerp( Texel( X0, Y0 ), Texel( X0 + 1, Y0 ), FractionX );
		const FVector3 Bottom = Lerp( Texel( X0, Y0 + 1 ), Texel( X0 + 1, Y0 + 1 ), FractionX );
		return Lerp( Top, Bottom, FractionY );
	}
}

// ---- 環境マップ ----------------------------------------------------------------------------------
bool FSubstrateEnvironment::Build( const FPixelImage& Panorama )
{
	if ( !Panorama.IsValid() )
	{
		return false;
	}
	// 段 0: 全周の絵を、LevelWidth x LevelHeight に、面積で平均して縮める（小さい絵は、近い画素を引き伸ばす）
	TArray<FVector3> Sharp;
	Sharp.SetNum( LevelWidth * LevelHeight, FVector3( 0.0f, 0.0f, 0.0f ) );
	const int32 SourceWidth = Panorama.GetWidth();
	const int32 SourceHeight = Panorama.GetHeight();
	const FColor32* Source = Panorama.GetData();
	for ( int32 Y = 0; Y < LevelHeight; ++Y )
	{
		const int32 Y0 = Y * SourceHeight / LevelHeight;
		const int32 Y1 = NMath::Max( Y0 + 1, ( Y + 1 ) * SourceHeight / LevelHeight );
		for ( int32 X = 0; X < LevelWidth; ++X )
		{
			const int32 X0 = X * SourceWidth / LevelWidth;
			const int32 X1 = NMath::Max( X0 + 1, ( X + 1 ) * SourceWidth / LevelWidth );
			FVector3 Sum( 0.0f, 0.0f, 0.0f );
			for ( int32 SourceY = Y0; SourceY < Y1; ++SourceY )
			{
				for ( int32 SourceX = X0; SourceX < X1; ++SourceX )
				{
					Sum = Sum + ToLinear( Source[NMath::Min( SourceY, SourceHeight - 1 ) * SourceWidth + NMath::Min( SourceX, SourceWidth - 1 )] );
				}
			}
			Sharp[Y * LevelWidth + X] = Sum * ( 1.0f / static_cast<float32>( ( Y1 - Y0 ) * ( X1 - X0 ) ) );
		}
	}

	// ぼかす段は、小さな絵（SmallWidth x SmallHeight）で計算して、引き伸ばす（ぼけた絵は、細かさが要らない）
	constexpr int32 Shrink = 4;
	constexpr int32 SmallWidth = LevelWidth / Shrink;
	constexpr int32 SmallHeight = LevelHeight / Shrink;
	TArray<FVector3> Small;
	TArray<FVector3> SmallDirections;
	TArray<float32> SmallAreas;
	Small.SetNum( SmallWidth * SmallHeight, FVector3( 0.0f, 0.0f, 0.0f ) );
	SmallDirections.SetNum( SmallWidth * SmallHeight, FVector3( 0.0f, 0.0f, 0.0f ) );
	SmallAreas.SetNum( SmallWidth * SmallHeight, 0.0f );
	for ( int32 Y = 0; Y < SmallHeight; ++Y )
	{
		for ( int32 X = 0; X < SmallWidth; ++X )
		{
			FVector3 Sum( 0.0f, 0.0f, 0.0f );
			for ( int32 InnerY = 0; InnerY < Shrink; ++InnerY )
			{
				for ( int32 InnerX = 0; InnerX < Shrink; ++InnerX )
				{
					Sum = Sum + Sharp[( Y * Shrink + InnerY ) * LevelWidth + X * Shrink + InnerX];
				}
			}
			const int32 Index = Y * SmallWidth + X;
			Small[Index] = Sum * ( 1.0f / static_cast<float32>( Shrink * Shrink ) );
			SmallDirections[Index] = PanoramaDirection( X, Y, SmallWidth, SmallHeight );
			// 画素が、球の上で占める広さ（極に近いほど、狭い）
			SmallAreas[Index] = NMath::Sin( ( static_cast<float32>( Y ) + 0.5f ) / static_cast<float32>( SmallHeight ) * Pi );
		}
	}

	FPixelImage Result( LevelWidth, LevelHeight * NumLevels, FColor32( 0, 0, 0, 255 ) );
	auto Store = [&]( int32 Level, int32 X, int32 Y, const FVector3& Color )
	{
		auto ToByte = []( float32 Value )
		{
			return static_cast<uint8>( NMath::Clamp( Value, 0.0f, 1.0f ) * 255.0f + 0.5f );
		};
		Result.Set( X, Level * LevelHeight + Y, FColor32( ToByte( Color.x ), ToByte( Color.y ), ToByte( Color.z ), 255 ) );
	};
	for ( int32 Y = 0; Y < LevelHeight; ++Y )
	{
		for ( int32 X = 0; X < LevelWidth; ++X )
		{
			Store( 0, X, Y, Sharp[Y * LevelWidth + X] );
		}
	}
	TArray<FVector3> Blurred;
	Blurred.SetNum( SmallWidth * SmallHeight, FVector3( 0.0f, 0.0f, 0.0f ) );
	for ( int32 Level = 1; Level < NumLevels; ++Level )
	{
		// 段の粗さから、光沢の山の鋭さ（GGX の広がりに近い、コサインのべき）。最後の段は、コサインそのもの（拡散の光）
		const float32 Roughness = static_cast<float32>( Level ) / static_cast<float32>( NumLevels - 1 );
		const float32 Alpha = Roughness * Roughness;
		const float32 Exponent = NMath::Clamp( 2.0f / ( Alpha * Alpha ) - 2.0f, 1.0f, 256.0f );
		// 重みが、ごく小さくなる角度より外は、足さない
		const float32 Cutoff = NMath::Pow( 0.002f, 1.0f / Exponent );
		for ( int32 Target = 0; Target < SmallWidth * SmallHeight; ++Target )
		{
			const FVector3& Direction = SmallDirections[Target];
			FVector3 Sum( 0.0f, 0.0f, 0.0f );
			float32 Total = 0.0f;
			for ( int32 Index = 0; Index < SmallWidth * SmallHeight; ++Index )
			{
				const float32 Cosine = FVector3::Dot( Direction, SmallDirections[Index] );
				if ( Cosine <= Cutoff )
				{
					continue;
				}
				const float32 Weight = NMath::Pow( Cosine, Exponent ) * SmallAreas[Index];
				Sum = Sum + Small[Index] * Weight;
				Total += Weight;
			}
			Blurred[Target] = Total > 0.0f ? Sum * ( 1.0f / Total ) : Small[Target];
		}
		for ( int32 Y = 0; Y < LevelHeight; ++Y )
		{
			for ( int32 X = 0; X < LevelWidth; ++X )
			{
				Store( Level, X, Y, ReadBilinear( Blurred, SmallWidth, SmallHeight, ( static_cast<float32>( X ) + 0.5f ) / static_cast<float32>( Shrink ), ( static_cast<float32>( Y ) + 0.5f ) / static_cast<float32>( Shrink ) ) );
			}
		}
	}
	Atlas = Result;
	return true;
}

FVector3 FSubstrateEnvironment::SampleLevel( const FVector3& Direction, int32 Level ) const
{
	// 向きから、絵の座標（横が方位、縦が仰角）。真上・真下は、方位が決まらないので 0
	const float32 Azimuth = NMath::Abs( Direction.x ) + NMath::Abs( Direction.z ) < 1.0e-6f ? 0.0f : NMath::Atan2( Direction.x, Direction.z );
	const float32 X = ( Azimuth / ( 2.0f * Pi ) + 0.5f ) * static_cast<float32>( LevelWidth ) - 0.5f;
	const float32 Y = NMath::Acos( Direction.y ) / Pi * static_cast<float32>( LevelHeight ) - 0.5f;
	const float32 FloorX = NMath::Floor( X );
	const float32 FloorY = NMath::Floor( Y );
	const int32 X0 = static_cast<int32>( FloorX );
	const int32 Y0 = static_cast<int32>( FloorY );
	const FColor32* Texels = Atlas.GetData();
	auto Texel = [&]( int32 TexelX, int32 TexelY )
	{
		const int32 WrappedX = ( ( TexelX % LevelWidth ) + LevelWidth ) % LevelWidth;
		const int32 ClampedY = NMath::Clamp( TexelY, 0, LevelHeight - 1 );
		return ToLinear( Texels[( Level * LevelHeight + ClampedY ) * LevelWidth + WrappedX] );
	};
	const FVector3 Top = Lerp( Texel( X0, Y0 ), Texel( X0 + 1, Y0 ), X - FloorX );
	const FVector3 Bottom = Lerp( Texel( X0, Y0 + 1 ), Texel( X0 + 1, Y0 + 1 ), X - FloorX );
	return Lerp( Top, Bottom, Y - FloorY );
}

FVector3 FSubstrateEnvironment::Sample( const FVector3& Direction, float32 Blur ) const
{
	if ( !IsValid() )
	{
		return FVector3( 0.0f, 0.0f, 0.0f );
	}
	const FVector3 Unit = SafeNormalize( Direction, FVector3( 0.0f, 1.0f, 0.0f ) );
	const float32 Position = Saturate( Blur ) * static_cast<float32>( NumLevels - 1 );
	const float32 Lower = NMath::Floor( Position );
	const int32 Level0 = static_cast<int32>( Lower );
	const int32 Level1 = NMath::Min( Level0 + 1, NumLevels - 1 );
	return Lerp( SampleLevel( Unit, Level0 ), SampleLevel( Unit, Level1 ), Position - Lower );
}

// ---- 陰影 ----------------------------------------------------------------------------------------
FVector3 NMaterialSubstrate::ShadeSlab( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry, const FSubstrateLighting& Lighting )
{
	FSurface S;
	const FVector3 GeometryNormal = SafeNormalize( Geometry.Normal, FVector3( 0.0f, 0.0f, -1.0f ) );
	S.N = SafeNormalize( Slab.Normal, GeometryNormal );
	S.V = SafeNormalize( Geometry.ToCamera, S.N );
	S.T = MakeTangent( S.N, Geometry.Tangent );
	S.B = FVector3::Cross( S.N, S.T );
	S.NdotV = NMath::Max( FVector3::Dot( S.N, S.V ), 1.0e-4f );

	const float32 Roughness = Saturate( Slab.Roughness );
	const float32 Metallic = Saturate( Slab.Metallic );

	// 鏡面の色: 非金属は F0、金属は BaseColor。薄膜は、見る角度で色相が回る色を混ぜる
	S.SpecularColor = Lerp( Slab.F0, Slab.BaseColor, Metallic );
	const float32 Film = Saturate( Slab.ThinFilm );
	const float32 Phase = 2.0f * Pi * ( 1.0f + 2.0f * Film ) * S.NdotV;
	const FVector3 FilmColor( 0.5f + 0.5f * NMath::Cos( Phase ), 0.5f + 0.5f * NMath::Cos( Phase + 2.0943951f ), 0.5f + 0.5f * NMath::Cos( Phase + 4.1887902f ) );
	S.SpecularColor = Lerp( S.SpecularColor, FilmColor, Film );

	const float32 Alpha = NMath::Max( Roughness * Roughness, 0.002f );
	const float32 Anisotropy = NMath::Clamp( Slab.Anisotropy, -1.0f, 1.0f );
	const float32 Aspect = NMath::Sqrt( 1.0f - 0.9f * NMath::Abs( Anisotropy ) );
	S.AlphaT = NMath::Min( Anisotropy >= 0.0f ? Alpha / Aspect : Alpha * Aspect, 1.0f );
	S.AlphaB = NMath::Min( Anisotropy >= 0.0f ? Alpha * Aspect : Alpha / Aspect, 1.0f );

	// 層の中で散らばる光（拡散・サブサーフェス）は、下の層へ抜ける分だけ減る
	S.DiffuseColor = Slab.BaseColor * ( 1.0f - Metallic ) * Slab.DiffuseScale;
	S.SubsurfaceColor = FVector3( NMath::Max( Slab.Subsurface.x, 0.0f ), NMath::Max( Slab.Subsurface.y, 0.0f ), NMath::Max( Slab.Subsurface.z, 0.0f ) ) * Slab.DiffuseScale;
	S.Coat = Saturate( Slab.ClearCoat );
	const float32 CoatRoughness = Saturate( Slab.ClearCoatRoughness );
	S.CoatAlpha = NMath::Max( CoatRoughness * CoatRoughness, 0.002f );

	// 光: 平行光（影の中では、届かない）と、点光源
	FVector3 Direct( 0.0f, 0.0f, 0.0f );
	FVector3 CoatDirect( 0.0f, 0.0f, 0.0f );
	AddLight( S, SafeNormalize( Geometry.ToLight, S.N ), Lighting.LightColor * Saturate( Lighting.Shadow ), Direct, CoatDirect );
	const int32 NumPointLights = NMath::Clamp( Lighting.NumPointLights, 0, FSubstrateLighting::MaxPointLights );
	for ( int32 Index = 0; Index < NumPointLights; ++Index )
	{
		const FSubstratePointLight& Light = Lighting.PointLights[Index];
		const FVector3 Offset = Light.Position - Geometry.Position;
		const float32 Falloff = Saturate( 1.0f - FVector3::Dot( Offset, Offset ) / NMath::Max( Light.Radius * Light.Radius, 1.0e-6f ) );
		AddLight( S, SafeNormalize( Offset, S.N ), Light.Color * ( Falloff * Falloff ), Direct, CoatDirect );
	}

	// 環境: 拡散は、まわり全体からの光。鏡面は、反射の向きの映り込み（粗いほど、ぼける）
	const FVector3 Reflected = S.N * ( 2.0f * FVector3::Dot( S.N, S.V ) ) - S.V;
	const FVector3 EnvironmentFresnel = S.SpecularColor + ( FVector3( 1.0f, 1.0f, 1.0f ) - S.SpecularColor ) * ( Pow5( 1.0f - S.NdotV ) * ( 1.0f - Roughness ) );
	const FVector3 Ambient = S.DiffuseColor * EnvironmentColor( Lighting, S.N, 1.0f ) + EnvironmentFresnel * EnvironmentColor( Lighting, Reflected, Roughness );
	FVector3 Color = Direct + Ambient;

	// クリアコート: 下の層を、コートが反射した分だけ暗くして、上に、鏡面と、映り込みを足す
	const float32 CoatFresnelView = ( 0.04f + 0.96f * Pow5( 1.0f - S.NdotV ) ) * S.Coat;
	Color = Color * ( 1.0f - CoatFresnelView ) + CoatDirect + EnvironmentColor( Lighting, Reflected, CoatRoughness ) * CoatFresnelView;

	return ( Color + Slab.Emissive ) * Slab.Throughput;
}

FVector3 NMaterialSubstrate::ShadeSlabSimplified( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry )
{
	const FVector3 N = SafeNormalize( Slab.Normal, SafeNormalize( Geometry.Normal, FVector3( 0.0f, 0.0f, -1.0f ) ) );
	const FVector3 L = SafeNormalize( Geometry.ToLight, N );
	const FVector3 V = SafeNormalize( Geometry.ToCamera, N );
	const FVector3 H = SafeNormalize( L + V, N );
	const float32 NdotL = Saturate( FVector3::Dot( N, L ) );
	const float32 NdotH = Saturate( FVector3::Dot( N, H ) );
	const float32 Roughness = Saturate( Slab.Roughness );
	const float32 Metallic = Saturate( Slab.Metallic );
	const float32 Shininess = NMath::Pow( 2.0f, 10.0f * ( 1.0f - Roughness ) + 1.0f );
	const FVector3 DiffuseColor = Slab.BaseColor * ( 1.0f - Metallic );
	const FVector3 SpecularColor = Lerp( Slab.F0, Slab.BaseColor, Metallic );
	const float32 SpecularTerm = NMath::Pow( NdotH, Shininess ) * NdotL * ( 1.0f - Roughness * 0.5f );
	FVector3 Color = DiffuseColor * ( NdotL * SimplifiedDirectLight + SimplifiedAmbientLight ) + SpecularColor * SpecularTerm + Slab.Emissive;
	const float32 Coat = Saturate( Slab.ClearCoat );
	const float32 CoatShininess = NMath::Pow( 2.0f, 10.0f * ( 1.0f - Saturate( Slab.ClearCoatRoughness ) ) + 1.0f );
	const float32 CoatSpecular = NMath::Pow( NdotH, CoatShininess ) * NdotL * Coat * 0.6f;
	Color = Color * ( 1.0f - 0.04f * Coat ) + FVector3( CoatSpecular, CoatSpecular, CoatSpecular );
	return Color;
}

FVector3 NMaterialSubstrate::Shade( const TArray<FSubstrateSlab>& Slabs, const FSubstrateGeometry& Geometry, bool bFull, float32* OutOpacity, const FSubstrateLighting& Lighting )
{
	FVector3 Color( 0.0f, 0.0f, 0.0f );
	float32 Opacity = 0.0f;
	for ( int32 Index = 0; Index < Slabs.Num(); ++Index )
	{
		const FSubstrateSlab& Slab = Slabs[Index];
		Color = Color + ( bFull ? ShadeSlab( Slab, Geometry, Lighting ) : ShadeSlabSimplified( Slab, Geometry ) ) * Slab.Weight;
		Opacity += Slab.Opacity * Slab.Weight;
	}
	if ( OutOpacity != nullptr )
	{
		*OutOpacity = Opacity;
	}
	return Color;
}

bool NMaterialSubstrate::ReadSlabs( const FMaterialProgram& Program, const TArray<FVector4>& Values, TArray<FSubstrateSlab>& OutSlabs )
{
	OutSlabs.Reset();
	if ( !Program.bValid || Program.Domain != EMaterialDomain::Surface || Program.Shading == EMaterialShading::Unlit || Program.bCustomHlsl )
	{
		return false;
	}
	// 命令の値を読む（命令が無ければ、そのまま）
	auto ReadVector = [&]( int32 Instruction, FVector3& Target )
	{
		if ( Values.IsValidIndex( Instruction ) )
		{
			Target = FVector3( Values[Instruction].x, Values[Instruction].y, Values[Instruction].z );
		}
	};
	auto ReadNumber = [&]( int32 Instruction, float32& Target )
	{
		if ( Values.IsValidIndex( Instruction ) )
		{
			Target = Values[Instruction].x;
		}
	};
	// 層 1 つを、値（命令の番号。NMaterialSlabField の順）から作る
	auto MakeSlab = [&]( const int32* Fields )
	{
		FSubstrateSlab Slab;
		ReadVector( Fields[NMaterialSlabField::BaseColor], Slab.BaseColor );
		ReadVector( Fields[NMaterialSlabField::F0], Slab.F0 );
		ReadNumber( Fields[NMaterialSlabField::Roughness], Slab.Roughness );
		ReadNumber( Fields[NMaterialSlabField::Metallic], Slab.Metallic );
		ReadVector( Fields[NMaterialSlabField::Emissive], Slab.Emissive );
		ReadNumber( Fields[NMaterialSlabField::Opacity], Slab.Opacity );
		ReadNumber( Fields[NMaterialSlabField::ClearCoat], Slab.ClearCoat );
		ReadNumber( Fields[NMaterialSlabField::ClearCoatRoughness], Slab.ClearCoatRoughness );
		ReadVector( Fields[NMaterialSlabField::Subsurface], Slab.Subsurface );
		ReadNumber( Fields[NMaterialSlabField::Anisotropy], Slab.Anisotropy );
		ReadNumber( Fields[NMaterialSlabField::ThinFilm], Slab.ThinFilm );
		ReadVector( Fields[NMaterialSlabField::Normal], Slab.Normal );
		return Slab;
	};
	if ( Program.bFullSubstrate && !Program.SubstrateSlabs.IsEmpty() )
	{
		for ( int32 Index = 0; Index < Program.SubstrateSlabs.Num(); ++Index )
		{
			const FMaterialSubstrateSlab& Source = Program.SubstrateSlabs[Index];
			FSubstrateSlab Slab = MakeSlab( Source.Fields );
			ReadNumber( Source.Weight, Slab.Weight );
			ReadVector( Source.Throughput, Slab.Throughput );
			ReadVector( Source.DiffuseScale, Slab.DiffuseScale );
			OutSlabs.Add( Slab );
		}
		return true;
	}
	// 簡略版: 値を混ぜた 1 つの層（出力の値）。DefaultLit は、F0 が、Specular の 8%
	int32 Fields[NMaterialSlabField::Num];
	for ( int32 Field = 0; Field < NMaterialSlabField::Num; ++Field )
	{
		const FMaterialOutputValue* Output = Program.FindOutput( NMaterialSlabField::GetName( Field ) );
		Fields[Field] = Output != nullptr ? Output->Value : -1;
	}
	FSubstrateSlab Slab = MakeSlab( Fields );
	if ( Program.Shading == EMaterialShading::DefaultLit )
	{
		float32 Specular = 0.5f;
		const FMaterialOutputValue* Output = Program.FindOutput( "Specular" );
		ReadNumber( Output != nullptr ? Output->Value : -1, Specular );
		Slab.F0 = FVector3( 0.08f * Specular, 0.08f * Specular, 0.08f * Specular );
		Slab.ClearCoat = 0.0f;
	}
	OutSlabs.Add( Slab );
	return true;
}

float32 NMaterialSubstrate::ReadOpacity( const FMaterialProgram& Program, const TArray<FVector4>& Values )
{
	const FMaterialOutputValue* Output = Program.FindOutput( "Opacity" );
	return Output != nullptr && Values.IsValidIndex( Output->Value ) ? Values[Output->Value].x : 1.0f;
}

const char* NMaterialSubstrate::GetHlslFunctions()
{
	return R"HLSL(
// ---- Substrate (the full model). The same math as NMaterialSubstrate::ShadeSlab ----
struct SubstrateSlab
{
	float3 BaseColor;
	float3 F0;
	float Roughness;
	float Metallic;
	float3 Emissive;
	float ClearCoat;
	float ClearCoatRoughness;
	float3 Subsurface;
	float Anisotropy;
	float ThinFilm;
	float3 Normal;         // the normal of this slab (world). Zero: the normal of the surface
	float3 Throughput;     // how much of this slab shows through the slabs above it
	float3 DiffuseScale;   // how much of the light scatters inside this slab
};

struct SubstrateGeometry
{
	float3 Normal;
	float3 Tangent;
	float3 ToLight;
	float3 ToCamera;
	float3 Position;
};

struct SubstrateLighting
{
	float3 LightColor;
	float Shadow;
	float3 SkyColor;
	float3 GroundColor;
	float3 AmbientColor;
	int NumPointLights;
	float4 PointLightPositions[4];   // xyz, w = radius
	float4 PointLightColors[4];
	float4 Environment;              // the environment atlas: x = width of a level, y = height of a level, z = number of levels, w = intensity
};

// What depends on the view only (shared by every light)
struct SubstrateSurface
{
	float3 N;
	float3 V;
	float3 T;
	float3 B;
	float NdotV;
	float3 SpecularColor;
	float AlphaT;
	float AlphaB;
	float3 DiffuseColor;
	float3 SubsurfaceColor;
	float Coat;
	float CoatAlpha;
};

float3 SubstrateSafeNormalize( float3 v, float3 fallback )
{
	float lengthSquared = dot( v, v );
	return lengthSquared > 1.0e-12 ? v * rsqrt( lengthSquared ) : fallback;
}

// A tangent perpendicular to the normal (an arbitrary one when the given tangent is degenerate)
float3 SubstrateMakeTangent( float3 N, float3 tangent )
{
	float3 other = abs( N.y ) < 0.99 ? cross( float3( 0.0, 1.0, 0.0 ), N ) : float3( 1.0, 0.0, 0.0 );
	float3 fallback = SubstrateSafeNormalize( other - N * dot( N, other ), float3( 1.0, 0.0, 0.0 ) );
	return SubstrateSafeNormalize( tangent - N * dot( N, tangent ), fallback );
}

// The tangent of the pixel (the direction in which U grows), from the screen-space derivatives of the position and the UV
float3 SubstrateTangent( float3 N, float3 worldPosition, float2 uv )
{
	float3 dpx = ddx( worldPosition );
	float3 dpy = ddy( worldPosition );
	float2 dux = ddx( uv );
	float2 duy = ddy( uv );
	float side = dux.x * duy.y - dux.y * duy.x < 0.0 ? -1.0 : 1.0;
	return SubstrateMakeTangent( N, ( dpx * duy.y - dpy * dux.y ) * side );
}

// The bitangent of the pixel (the direction in which V grows)
float3 SubstrateBitangent( float3 N, float3 worldPosition, float2 uv )
{
	float3 dpx = ddx( worldPosition );
	float3 dpy = ddy( worldPosition );
	float2 dux = ddx( uv );
	float2 duy = ddy( uv );
	float side = dux.x * duy.y - dux.y * duy.x < 0.0 ? -1.0 : 1.0;
	return SubstrateMakeTangent( N, ( dpy * dux.x - dpx * duy.x ) * side );
}

float SubstratePow5( float v )
{
	float squared = v * v;
	return squared * squared * v;
}

float SubstrateSpecularLobe( float ndh, float ndl, float ndv, float alpha )
{
	float alphaSquared = alpha * alpha;
	float denominator = ndh * ndh * ( alphaSquared - 1.0 ) + 1.0;
	float distribution = alphaSquared / ( 3.14159265 * denominator * denominator );
	float visibility = 0.5 / max( lerp( 2.0 * ndl * ndv, ndl + ndv, alpha ), 1.0e-4 );
	return min( distribution * visibility * 3.14159265, 16.0 );
}

#ifdef SUBSTRATE_ENVIRONMENT_MAP
// One level of the environment atlas, read with a bilinear filter (wraps around sideways, stops at the top and the bottom)
float3 SubstrateEnvironmentLevel( float3 direction, int level, float4 environment )
{
	int width = ( int )environment.x;
	int height = ( int )environment.y;
	float azimuth = abs( direction.x ) + abs( direction.z ) < 1.0e-6 ? 0.0 : atan2( direction.x, direction.z );
	float x = ( azimuth / ( 2.0 * 3.14159265 ) + 0.5 ) * environment.x - 0.5;
	float y = acos( clamp( direction.y, -1.0, 1.0 ) ) / 3.14159265 * environment.y - 0.5;
	float floorX = floor( x );
	float floorY = floor( y );
	int x0 = ( ( ( int )floorX % width ) + width ) % width;
	int x1 = ( x0 + 1 ) % width;
	int y0 = clamp( ( int )floorY, 0, height - 1 ) + level * height;
	int y1 = clamp( ( int )floorY + 1, 0, height - 1 ) + level * height;
	float3 top = lerp( SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x0, y0, 0 ) ).rgb, SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x1, y0, 0 ) ).rgb, x - floorX );
	float3 bottom = lerp( SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x0, y1, 0 ) ).rgb, SUBSTRATE_ENVIRONMENT_MAP.Load( int3( x1, y1, 0 ) ).rgb, x - floorX );
	return lerp( top, bottom, y - floorY );
}
#endif

// The color of the environment in a direction (unit length). blur: 0 = a mirror, 1 = everything around
float3 SubstrateEnvironment( float3 direction, float blur, SubstrateLighting lighting )
{
#ifdef SUBSTRATE_ENVIRONMENT_MAP
	float position = saturate( blur ) * ( lighting.Environment.z - 1.0 );
	float lower = floor( position );
	int level0 = ( int )lower;
	int level1 = min( level0 + 1, ( int )lighting.Environment.z - 1 );
	return lerp( SubstrateEnvironmentLevel( direction, level0, lighting.Environment ), SubstrateEnvironmentLevel( direction, level1, lighting.Environment ), position - lower ) * lighting.Environment.w;
#else
	float3 gradient = lerp( lighting.GroundColor, lighting.SkyColor, saturate( direction.y * 0.5 + 0.5 ) );
	return lerp( gradient, lighting.AmbientColor, saturate( blur ) );
#endif
}

// Adds one light (direction L, brightness radiance): direct = the slab itself, coatDirect = the highlight of the clear coat
void SubstrateAddLight( SubstrateSurface s, float3 L, float3 radiance, inout float3 direct, inout float3 coatDirect )
{
	float3 H = SubstrateSafeNormalize( L + s.V, s.N );
	float ndlRaw = dot( s.N, L );
	float ndl = saturate( ndlRaw );
	float ndh = saturate( dot( s.N, H ) );
	float vdh = saturate( dot( s.V, H ) );
	float3 fresnel = s.SpecularColor + ( 1.0 - s.SpecularColor ) * SubstratePow5( 1.0 - vdh );

	float tdh = dot( s.T, H );
	float bdh = dot( s.B, H );
	float denominator = tdh * tdh / ( s.AlphaT * s.AlphaT ) + bdh * bdh / ( s.AlphaB * s.AlphaB ) + ndh * ndh;
	float distribution = 1.0 / ( 3.14159265 * s.AlphaT * s.AlphaB * denominator * denominator );
	float visibility = 0.5 / max( lerp( 2.0 * ndl * s.NdotV, ndl + s.NdotV, sqrt( s.AlphaT * s.AlphaB ) ), 1.0e-4 );
	float3 specular = fresnel * ( min( distribution * visibility * 3.14159265, 16.0 ) * ndl );

	float3 diffuse = s.DiffuseColor * ( 1.0 - fresnel ) * ndl;
	float wrap = saturate( ( ndlRaw + 0.5 ) / 1.5 );
	float backLight = saturate( -dot( s.V, L ) );
	float3 subsurface = s.SubsurfaceColor * ( ( wrap - ndl ) + 0.25 * backLight * backLight * backLight * backLight );
	direct += ( diffuse + subsurface + specular ) * radiance;

	float coatFresnel = ( 0.04 + 0.96 * SubstratePow5( 1.0 - vdh ) ) * s.Coat;
	coatDirect += radiance * ( SubstrateSpecularLobe( ndh, ndl, s.NdotV, s.CoatAlpha ) * ndl * coatFresnel );
}

float3 SubstrateShadeSlab( SubstrateSlab slab, SubstrateGeometry geometry, SubstrateLighting lighting )
{
	SubstrateSurface s;
	float3 geometryNormal = SubstrateSafeNormalize( geometry.Normal, float3( 0.0, 0.0, -1.0 ) );
	s.N = SubstrateSafeNormalize( slab.Normal, geometryNormal );
	s.V = SubstrateSafeNormalize( geometry.ToCamera, s.N );
	s.T = SubstrateMakeTangent( s.N, geometry.Tangent );
	s.B = cross( s.N, s.T );
	s.NdotV = max( dot( s.N, s.V ), 1.0e-4 );

	float roughness = saturate( slab.Roughness );
	float metallic = saturate( slab.Metallic );

	s.SpecularColor = lerp( slab.F0, slab.BaseColor, metallic );
	float film = saturate( slab.ThinFilm );
	float phase = 2.0 * 3.14159265 * ( 1.0 + 2.0 * film ) * s.NdotV;
	float3 filmColor = 0.5 + 0.5 * cos( phase + float3( 0.0, 2.0943951, 4.1887902 ) );
	s.SpecularColor = lerp( s.SpecularColor, filmColor, film );

	float alpha = max( roughness * roughness, 0.002 );
	float anisotropy = clamp( slab.Anisotropy, -1.0, 1.0 );
	float aspect = sqrt( 1.0 - 0.9 * abs( anisotropy ) );
	s.AlphaT = min( anisotropy >= 0.0 ? alpha / aspect : alpha * aspect, 1.0 );
	s.AlphaB = min( anisotropy >= 0.0 ? alpha * aspect : alpha / aspect, 1.0 );

	s.DiffuseColor = slab.BaseColor * ( 1.0 - metallic ) * slab.DiffuseScale;
	s.SubsurfaceColor = max( slab.Subsurface, 0.0 ) * slab.DiffuseScale;
	s.Coat = saturate( slab.ClearCoat );
	float coatRoughness = saturate( slab.ClearCoatRoughness );
	s.CoatAlpha = max( coatRoughness * coatRoughness, 0.002 );

	float3 direct = ( float3 )0.0;
	float3 coatDirect = ( float3 )0.0;
	SubstrateAddLight( s, SubstrateSafeNormalize( geometry.ToLight, s.N ), lighting.LightColor * saturate( lighting.Shadow ), direct, coatDirect );
	int numPointLights = clamp( lighting.NumPointLights, 0, 4 );
	for ( int index = 0; index < numPointLights; ++index )
	{
		float3 offset = lighting.PointLightPositions[index].xyz - geometry.Position;
		float radius = lighting.PointLightPositions[index].w;
		float falloff = saturate( 1.0 - dot( offset, offset ) / max( radius * radius, 1.0e-6 ) );
		SubstrateAddLight( s, SubstrateSafeNormalize( offset, s.N ), lighting.PointLightColors[index].rgb * ( falloff * falloff ), direct, coatDirect );
	}

	float3 reflected = s.N * ( 2.0 * dot( s.N, s.V ) ) - s.V;
	float3 environmentFresnel = s.SpecularColor + ( 1.0 - s.SpecularColor ) * ( SubstratePow5( 1.0 - s.NdotV ) * ( 1.0 - roughness ) );
	float3 ambient = s.DiffuseColor * SubstrateEnvironment( s.N, 1.0, lighting ) + environmentFresnel * SubstrateEnvironment( reflected, roughness, lighting );
	float3 color = direct + ambient;

	float coatFresnelView = ( 0.04 + 0.96 * SubstratePow5( 1.0 - s.NdotV ) ) * s.Coat;
	color = color * ( 1.0 - coatFresnelView ) + coatDirect + SubstrateEnvironment( reflected, coatRoughness, lighting ) * coatFresnelView;

	return ( color + slab.Emissive ) * slab.Throughput;
}
)HLSL";
}

// SPDX-License-Identifier: MIT
#include "MaterialSubstrate.h"

#include "Library/Framework/Material/MaterialEval.h"
#include "Library/Math/MathUtils.h"

// ここの C++ と、下の HLSL は、同じ計算。片方を変えたら、もう片方も変える（検査が、本物の画素と、C++ の結果を比べている）
namespace
{
	constexpr float32 Pi = 3.14159265f;
	constexpr float32 DirectLight = 0.85f;    // 平行光の強さ
	constexpr float32 AmbientLight = 0.15f;   // 環境の明るさ
	constexpr float32 MaxSpecular = 16.0f;    // 鏡面の山の高さの上限（とても滑らかな面で、値が飛ばないように）

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
}

FVector3 NMaterialSubstrate::ShadeSlab( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry )
{
	const FVector3 N = SafeNormalize( Geometry.Normal, FVector3( 0.0f, 0.0f, -1.0f ) );
	const FVector3 L = SafeNormalize( Geometry.ToLight, N );
	const FVector3 V = SafeNormalize( Geometry.ToCamera, N );
	const FVector3 T = MakeTangent( N, Geometry.Tangent );
	const FVector3 B = FVector3::Cross( N, T );
	const FVector3 H = SafeNormalize( L + V, N );
	const float32 NdotLRaw = FVector3::Dot( N, L );
	const float32 NdotL = Saturate( NdotLRaw );
	const float32 NdotV = NMath::Max( FVector3::Dot( N, V ), 1.0e-4f );
	const float32 NdotH = Saturate( FVector3::Dot( N, H ) );
	const float32 VdotH = Saturate( FVector3::Dot( V, H ) );

	const float32 Roughness = Saturate( Slab.Roughness );
	const float32 Metallic = Saturate( Slab.Metallic );

	// 鏡面の色: 非金属は F0、金属は BaseColor。薄膜は、見る角度で色相が回る色を混ぜる
	FVector3 SpecularColor = Lerp( Slab.F0, Slab.BaseColor, Metallic );
	const float32 Film = Saturate( Slab.ThinFilm );
	const float32 Phase = 2.0f * Pi * ( 1.0f + 2.0f * Film ) * NdotV;
	const FVector3 FilmColor( 0.5f + 0.5f * NMath::Cos( Phase ), 0.5f + 0.5f * NMath::Cos( Phase + 2.0943951f ), 0.5f + 0.5f * NMath::Cos( Phase + 4.1887902f ) );
	SpecularColor = Lerp( SpecularColor, FilmColor, Film );
	const FVector3 Fresnel = SpecularColor + ( FVector3( 1.0f, 1.0f, 1.0f ) - SpecularColor ) * Pow5( 1.0f - VdotH );

	// 異方性の GGX: 接線の向きと、従接線の向きで、粗さを変える
	const float32 Alpha = NMath::Max( Roughness * Roughness, 0.002f );
	const float32 Anisotropy = NMath::Clamp( Slab.Anisotropy, -1.0f, 1.0f );
	const float32 Aspect = NMath::Sqrt( 1.0f - 0.9f * NMath::Abs( Anisotropy ) );
	const float32 AlphaT = NMath::Min( Anisotropy >= 0.0f ? Alpha / Aspect : Alpha * Aspect, 1.0f );
	const float32 AlphaB = NMath::Min( Anisotropy >= 0.0f ? Alpha * Aspect : Alpha / Aspect, 1.0f );
	const float32 TdotH = FVector3::Dot( T, H );
	const float32 BdotH = FVector3::Dot( B, H );
	const float32 Denominator = TdotH * TdotH / ( AlphaT * AlphaT ) + BdotH * BdotH / ( AlphaB * AlphaB ) + NdotH * NdotH;
	const float32 Distribution = 1.0f / ( Pi * AlphaT * AlphaB * Denominator * Denominator );
	const float32 Visibility = 0.5f / NMath::Max( Lerp( 2.0f * NdotL * NdotV, NdotL + NdotV, NMath::Sqrt( AlphaT * AlphaB ) ), 1.0e-4f );
	const FVector3 Specular = Fresnel * ( NMath::Min( Distribution * Visibility * Pi, MaxSpecular ) * NdotL );

	// 拡散: 鏡面が反射した残り。サブサーフェス: 光の回り込みと、逆光で透ける分
	const FVector3 DiffuseColor = Slab.BaseColor * ( 1.0f - Metallic );
	const FVector3 Diffuse = DiffuseColor * ( FVector3( 1.0f, 1.0f, 1.0f ) - Fresnel ) * NdotL;
	const FVector3 SubsurfaceColor( NMath::Max( Slab.Subsurface.x, 0.0f ), NMath::Max( Slab.Subsurface.y, 0.0f ), NMath::Max( Slab.Subsurface.z, 0.0f ) );
	const float32 Wrap = Saturate( ( NdotLRaw + 0.5f ) / 1.5f );
	const float32 BackLight = Saturate( -FVector3::Dot( V, L ) );
	const FVector3 Subsurface = SubsurfaceColor * ( ( Wrap - NdotL ) + 0.25f * BackLight * BackLight * BackLight * BackLight );

	// 環境: 拡散は、一様な明るさ。鏡面は、反射の向きが上（空）なら明るく、下（地面）なら暗い、ぼんやりした映り込み（粗いほど、一様に近づく）
	const FVector3 Reflected = N * ( 2.0f * FVector3::Dot( N, V ) ) - V;
	const float32 Sky = Lerp( 0.05f, 0.40f, Saturate( Reflected.y * 0.5f + 0.5f ) );
	const float32 EnvironmentLight = Lerp( Sky, AmbientLight, Roughness );
	const FVector3 EnvironmentFresnel = SpecularColor + ( FVector3( 1.0f, 1.0f, 1.0f ) - SpecularColor ) * ( Pow5( 1.0f - NdotV ) * ( 1.0f - Roughness ) );
	const FVector3 Ambient = DiffuseColor * AmbientLight + EnvironmentFresnel * EnvironmentLight;
	FVector3 Color = ( Diffuse + Subsurface + Specular ) * DirectLight + Ambient;

	// クリアコート: 下の層を、コートが反射した分だけ暗くして、上に、鏡面を足す
	const float32 Coat = Saturate( Slab.ClearCoat );
	const float32 CoatRoughness = Saturate( Slab.ClearCoatRoughness );
	const float32 CoatAlpha = NMath::Max( CoatRoughness * CoatRoughness, 0.002f );
	const float32 CoatFresnel = ( 0.04f + 0.96f * Pow5( 1.0f - VdotH ) ) * Coat;
	const float32 CoatFresnelView = ( 0.04f + 0.96f * Pow5( 1.0f - NdotV ) ) * Coat;
	const float32 CoatSpecular = SpecularLobe( NdotH, NdotL, NdotV, CoatAlpha ) * NdotL * CoatFresnel * DirectLight;
	Color = Color * ( 1.0f - CoatFresnelView ) + FVector3( CoatSpecular, CoatSpecular, CoatSpecular );

	return Color + Slab.Emissive;
}

FVector3 NMaterialSubstrate::ShadeSlabSimplified( const FSubstrateSlab& Slab, const FSubstrateGeometry& Geometry )
{
	const FVector3 N = SafeNormalize( Geometry.Normal, FVector3( 0.0f, 0.0f, -1.0f ) );
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
	FVector3 Color = DiffuseColor * ( NdotL * DirectLight + AmbientLight ) + SpecularColor * SpecularTerm + Slab.Emissive;
	const float32 Coat = Saturate( Slab.ClearCoat );
	const float32 CoatShininess = NMath::Pow( 2.0f, 10.0f * ( 1.0f - Saturate( Slab.ClearCoatRoughness ) ) + 1.0f );
	const float32 CoatSpecular = NMath::Pow( NdotH, CoatShininess ) * NdotL * Coat * 0.6f;
	Color = Color * ( 1.0f - 0.04f * Coat ) + FVector3( CoatSpecular, CoatSpecular, CoatSpecular );
	return Color;
}

FVector3 NMaterialSubstrate::Shade( const TArray<FSubstrateSlab>& Slabs, const FSubstrateGeometry& Geometry, bool bFull, float32* OutOpacity )
{
	FVector3 Color( 0.0f, 0.0f, 0.0f );
	float32 Opacity = 0.0f;
	for ( int32 Index = 0; Index < Slabs.Num(); ++Index )
	{
		const FSubstrateSlab& Slab = Slabs[Index];
		Color = Color + ( bFull ? ShadeSlab( Slab, Geometry ) : ShadeSlabSimplified( Slab, Geometry ) ) * Slab.Weight;
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
	if ( !Program.bValid || Program.Domain != EMaterialDomain::Surface || Program.Shading != EMaterialShading::Substrate )
	{
		return false;
	}
	auto ValueOf = [&]( int32 Instruction )
	{
		return Values.IsValidIndex( Instruction ) ? Values[Instruction] : FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	};
	auto ToVector3 = []( const FVector4& Value )
	{
		return FVector3( Value.x, Value.y, Value.z );
	};
	// 層 1 つを、11 の値（命令の番号。NMaterialSlabField の順）から作る
	auto MakeSlab = [&]( const int32* Fields, float32 Weight )
	{
		FSubstrateSlab Slab;
		Slab.BaseColor = ToVector3( ValueOf( Fields[NMaterialSlabField::BaseColor] ) );
		Slab.F0 = ToVector3( ValueOf( Fields[NMaterialSlabField::F0] ) );
		Slab.Roughness = ValueOf( Fields[NMaterialSlabField::Roughness] ).x;
		Slab.Metallic = ValueOf( Fields[NMaterialSlabField::Metallic] ).x;
		Slab.Emissive = ToVector3( ValueOf( Fields[NMaterialSlabField::Emissive] ) );
		Slab.Opacity = ValueOf( Fields[NMaterialSlabField::Opacity] ).x;
		Slab.ClearCoat = ValueOf( Fields[NMaterialSlabField::ClearCoat] ).x;
		Slab.ClearCoatRoughness = ValueOf( Fields[NMaterialSlabField::ClearCoatRoughness] ).x;
		Slab.Subsurface = ToVector3( ValueOf( Fields[NMaterialSlabField::Subsurface] ) );
		Slab.Anisotropy = ValueOf( Fields[NMaterialSlabField::Anisotropy] ).x;
		Slab.ThinFilm = ValueOf( Fields[NMaterialSlabField::ThinFilm] ).x;
		Slab.Weight = Weight;
		return Slab;
	};
	if ( Program.bFullSubstrate && !Program.SubstrateSlabs.IsEmpty() )
	{
		for ( int32 Index = 0; Index < Program.SubstrateSlabs.Num(); ++Index )
		{
			const FMaterialSubstrateSlab& Source = Program.SubstrateSlabs[Index];
			OutSlabs.Add( MakeSlab( Source.Fields, ValueOf( Source.Weight ).x ) );
		}
		return true;
	}
	// 簡略版: 値を混ぜた 1 つの層（出力の 11 の値）
	int32 Fields[NMaterialSlabField::Num];
	for ( int32 Field = 0; Field < NMaterialSlabField::Num; ++Field )
	{
		const FMaterialOutputValue* Output = Program.FindOutput( NMaterialSlabField::GetName( Field ) );
		Fields[Field] = Output != nullptr ? Output->Value : -1;
	}
	OutSlabs.Add( MakeSlab( Fields, 1.0f ) );
	return true;
}

const char* NMaterialSubstrate::GetHlslFunctions()
{
	return R"HLSL(
// ---- Substrate (the full model). The same math as NMaterialSubstrate::ShadeSlab ----
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

// The tangent of the pixel (the direction of U), from the screen-space derivatives of the position and the UV
float3 SubstrateTangent( float3 N, float3 worldPosition, float2 uv )
{
	float3 dpx = ddx( worldPosition );
	float3 dpy = ddy( worldPosition );
	float2 dux = ddx( uv );
	float2 duy = ddy( uv );
	return SubstrateMakeTangent( N, dpx * duy.y - dpy * dux.y );
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

float3 SubstrateShadeSlab( float3 baseColor, float3 f0, float roughnessIn, float metallicIn, float3 emissive, float clearCoat, float clearCoatRoughness, float3 subsurfaceIn, float anisotropyIn, float thinFilm,
						   float3 normal, float3 tangent, float3 toLight, float3 toCamera )
{
	float3 N = SubstrateSafeNormalize( normal, float3( 0.0, 0.0, -1.0 ) );
	float3 L = SubstrateSafeNormalize( toLight, N );
	float3 V = SubstrateSafeNormalize( toCamera, N );
	float3 T = SubstrateMakeTangent( N, tangent );
	float3 B = cross( N, T );
	float3 H = SubstrateSafeNormalize( L + V, N );
	float ndlRaw = dot( N, L );
	float ndl = saturate( ndlRaw );
	float ndv = max( dot( N, V ), 1.0e-4 );
	float ndh = saturate( dot( N, H ) );
	float vdh = saturate( dot( V, H ) );

	float roughness = saturate( roughnessIn );
	float metallic = saturate( metallicIn );

	float3 specularColor = lerp( f0, baseColor, metallic );
	float film = saturate( thinFilm );
	float phase = 2.0 * 3.14159265 * ( 1.0 + 2.0 * film ) * ndv;
	float3 filmColor = 0.5 + 0.5 * cos( phase + float3( 0.0, 2.0943951, 4.1887902 ) );
	specularColor = lerp( specularColor, filmColor, film );
	float3 fresnel = specularColor + ( 1.0 - specularColor ) * SubstratePow5( 1.0 - vdh );

	float alpha = max( roughness * roughness, 0.002 );
	float anisotropy = clamp( anisotropyIn, -1.0, 1.0 );
	float aspect = sqrt( 1.0 - 0.9 * abs( anisotropy ) );
	float alphaT = min( anisotropy >= 0.0 ? alpha / aspect : alpha * aspect, 1.0 );
	float alphaB = min( anisotropy >= 0.0 ? alpha * aspect : alpha / aspect, 1.0 );
	float tdh = dot( T, H );
	float bdh = dot( B, H );
	float denominator = tdh * tdh / ( alphaT * alphaT ) + bdh * bdh / ( alphaB * alphaB ) + ndh * ndh;
	float distribution = 1.0 / ( 3.14159265 * alphaT * alphaB * denominator * denominator );
	float visibility = 0.5 / max( lerp( 2.0 * ndl * ndv, ndl + ndv, sqrt( alphaT * alphaB ) ), 1.0e-4 );
	float3 specular = fresnel * ( min( distribution * visibility * 3.14159265, 16.0 ) * ndl );

	float3 diffuseColor = baseColor * ( 1.0 - metallic );
	float3 diffuse = diffuseColor * ( 1.0 - fresnel ) * ndl;
	float3 subsurfaceColor = max( subsurfaceIn, 0.0 );
	float wrap = saturate( ( ndlRaw + 0.5 ) / 1.5 );
	float backLight = saturate( -dot( V, L ) );
	float3 subsurface = subsurfaceColor * ( ( wrap - ndl ) + 0.25 * backLight * backLight * backLight * backLight );

	float3 reflected = N * ( 2.0 * dot( N, V ) ) - V;
	float sky = lerp( 0.05, 0.40, saturate( reflected.y * 0.5 + 0.5 ) );
	float environmentLight = lerp( sky, 0.15, roughness );
	float3 environmentFresnel = specularColor + ( 1.0 - specularColor ) * ( SubstratePow5( 1.0 - ndv ) * ( 1.0 - roughness ) );
	float3 ambient = diffuseColor * 0.15 + environmentFresnel * environmentLight;
	float3 color = ( diffuse + subsurface + specular ) * 0.85 + ambient;

	float coat = saturate( clearCoat );
	float coatRoughness = saturate( clearCoatRoughness );
	float coatAlpha = max( coatRoughness * coatRoughness, 0.002 );
	float coatFresnel = ( 0.04 + 0.96 * SubstratePow5( 1.0 - vdh ) ) * coat;
	float coatFresnelView = ( 0.04 + 0.96 * SubstratePow5( 1.0 - ndv ) ) * coat;
	float coatSpecular = SubstrateSpecularLobe( ndh, ndl, ndv, coatAlpha ) * ndl * coatFresnel * 0.85;
	color = color * ( 1.0 - coatFresnelView ) + ( float3 )coatSpecular;

	return color + emissive;
}
)HLSL";
}

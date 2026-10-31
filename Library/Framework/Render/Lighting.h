// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Math/Math.h"

//=============================================================================
// 3D のライティング（光）の値
//   純粋な値と計算（DxLib を使わない）。描画に使うときは、FDrawContext の PushLight / FLightScope / AmbientLight / SurfaceMaterial などで記録する
//   （実行のときに DxLib へ反映する）。どれも、そのオブジェクトの分（1 回の Draw）が終わると、始める前の状態に戻る。
//
//   光を受ける図形: 球（Sphere3D）・箱（Cube3D）・カプセル（Capsule3D）・画像を貼った四角形（Quad3D）。三角形（Triangle3D）と線（Line3D）は、光を受けない。
//   面の明るさ ＝ 自己発光 ＋ 環境光 ＋ ライトごとの（距離と円錐の減衰 x ( 拡散光 ＋ 鏡面反射 ＋ ライトの環境光 )）。
//     拡散光   ＝ 図形の色（FSurfaceMaterial::bUseVertexDiffuse が true のとき）x ライトの拡散色 x 面の向き（面の法線と光の向きの内積）
//     鏡面反射 ＝ 材質の鏡面色 x ライトの鏡面色 x ハイライト（面の法線と、光と視線の中間の向きの内積）の Shininess 乗
//
//   FLightScope Sun( CommandList, FLight::MakeDirectional( FVector3( 1.0f, -1.0f, 1.0f ), FColor::White ) );     // この中の図形は、太陽に照らされる
//   FLightScope Lamp( CommandList, FLight::MakePoint( FVector3( 0.0f, 3.0f, 0.0f ), 20.0f, FColor::Orange ) );   // 電球も足す
//=============================================================================

//=============================================================================
// FLight
//   ライト 1 つ。種類（方向 / 点 / スポット）ごとに、使う項目が違う。
//     Directional  Direction。距離で暗くならない
//     Point        Position・Range・減衰。距離とともに暗くなり、Range より遠くには届かない
//     Spot         Position・Direction・Range・減衰・OuterAngle・InnerAngle。Point に、円錐の形の制限が付く
//   色は、1 を超えてもよい（強い光）。負の値は、光を吸う（暗くする）。
//=============================================================================
struct FLight
{
	// 同時に効くライトの数の上限（DxLib の仕様。DxLib に最初からある 1 つのライト（DefaultLight）が有効なら、それも 1 つに数える）。超えた分のライトは、効かない
	static constexpr int32 MaxActiveLights = NDrawCommand::MaxActiveLights;

	// 種類
	EDrawLightType Type = EDrawLightType::Directional;

	// 光源の位置（Point / Spot）
	FVector3 Position = FVector3( 0.0f, 0.0f, 0.0f );

	// 光が進む向き（Directional / Spot）。長さは問わない（実行のときに正規化される）。長さ 0 は使えない
	FVector3 Direction = FVector3( 0.0f, -1.0f, 0.0f );

	// 拡散光（面を照らす色）
	FColor Diffuse = FColor::White;

	// 鏡面反射（ハイライト）の色
	FColor Specular = FColor::White;

	// ライトの環境光（面の向きに関係なく底上げする色）。材質の Ambient が 0（既定）だと、効かない
	FColor Ambient = FColor::Black;

	// 光が届く距離（Point / Spot）。これより遠くは、急に真っ暗になる
	float32 Range = 1000.0f;

	// 距離による減衰（Point / Spot）。距離 d での強さは 1 / ( ConstantAttenuation + LinearAttenuation x d + QuadraticAttenuation x d^2 )。
	// 0 以上で、3 つの合計が 0 より大きいこと
	float32 ConstantAttenuation = 1.0f;
	float32 LinearAttenuation = 0.0f;
	float32 QuadraticAttenuation = 0.0f;

	// このライトが、どのシャドウマップ（影の絵）の影を受けるか。ビットの和（NDrawShadowMap::SlotBit( スロット )。スロットは 0〜2）。既定は全部のスロット（DxLib の既定）。
	// 太陽だけが影を落とすなら、ほかのライトは 0（影を受けない）にする。UseShadowMap で使っているスロットだけが、実際に効く
	int32 ShadowMapSlots = NDrawShadowMap::AllSlots;

	// スポットライトの円錐の全角（ラジアン。軸から左右に、この半分ずつ広がる）。OuterAngle より外は照らさず、InnerAngle より中は全力で、そのあいだは直線的に弱まる。
	// 0 < InnerAngle <= OuterAngle <= π
	float32 OuterAngle = 1.0f;
	float32 InnerAngle = 0.5f;

	// 方向ライト（太陽・月）。Direction は、光が進む向き。拡散光と鏡面反射の色が Color
	static FLight MakeDirectional( const FVector3& InDirection, const FColor& Color = FColor::White )
	{
		FLight Light;
		Light.Type = EDrawLightType::Directional;
		Light.Direction = InDirection;
		Light.Diffuse = Color;
		Light.Specular = Color;
		return Light;
	}

	// 点光源（電球・たいまつ）。Range の手前まで、距離とともになめらかに暗くなり、Range では約 5% になって、そこで切れる
	static FLight MakePoint( const FVector3& InPosition, float32 InRange, const FColor& Color = FColor::White )
	{
		FLight Light;
		Light.Type = EDrawLightType::Point;
		Light.Position = InPosition;
		Light.Diffuse = Color;
		Light.Specular = Color;
		Light.Range = InRange;
		Light.QuadraticAttenuation = InRange > 0.0f ? 20.0f / ( InRange * InRange ) : 0.0f;
		return Light;
	}

	// スポットライト（懐中電灯・舞台の照明）。距離の減衰は MakePoint と同じ。角度は全角（ラジアン）
	static FLight MakeSpot( const FVector3& InPosition, const FVector3& InDirection, float32 InOuterAngle, float32 InInnerAngle, float32 InRange, const FColor& Color = FColor::White )
	{
		FLight Light = MakePoint( InPosition, InRange, Color );
		Light.Type = EDrawLightType::Spot;
		Light.Direction = InDirection;
		Light.OuterAngle = InOuterAngle;
		Light.InnerAngle = InInnerAngle;
		return Light;
	}

	// 値が使えるか（種類が範囲の中。影を受けるスロットが 0〜2 の中。使う項目がすべて有限。向きの長さ・Range・減衰・角度が決まりの範囲の中）
	bool IsValid() const
	{
		const int32 TypeValue = static_cast<int32>( Type );
		if ( TypeValue < static_cast<int32>( EDrawLightType::Directional ) || TypeValue > static_cast<int32>( EDrawLightType::Spot ) )
		{
			return false;
		}
		if ( ( ShadowMapSlots & ~NDrawShadowMap::AllSlots ) != 0 )
		{
			return false;
		}
		if ( !IsFiniteColor( Diffuse ) || !IsFiniteColor( Specular ) || !IsFiniteColor( Ambient ) )
		{
			return false;
		}
		if ( Type != EDrawLightType::Point && ( !IsFiniteVector( Direction ) || Direction.Length() <= NMath::SmallNumber ) )
		{
			return false;
		}
		if ( Type == EDrawLightType::Directional )
		{
			return true;
		}
		if ( !IsFiniteVector( Position ) || !NMath::IsFinite( Range ) || Range <= 0.0f )
		{
			return false;
		}
		if ( !NMath::IsFinite( ConstantAttenuation ) || !NMath::IsFinite( LinearAttenuation ) || !NMath::IsFinite( QuadraticAttenuation ) )
		{
			return false;
		}
		if ( ConstantAttenuation < 0.0f || LinearAttenuation < 0.0f || QuadraticAttenuation < 0.0f || ConstantAttenuation + LinearAttenuation + QuadraticAttenuation <= 0.0f )
		{
			return false;
		}
		if ( Type == EDrawLightType::Spot )
		{
			return NMath::IsFinite( OuterAngle ) && NMath::IsFinite( InnerAngle ) && InnerAngle > 0.0f && InnerAngle <= OuterAngle && OuterAngle <= NMath::PI;
		}
		return true;
	}

	// 距離 Distance での、距離による強さ（DxLib の計算と同じ）。方向ライトは 1。Range より遠くは 0
	float32 GetAttenuation( float32 Distance ) const
	{
		if ( Type == EDrawLightType::Directional )
		{
			return 1.0f;
		}
		if ( Distance > Range )
		{
			return 0.0f;
		}
		return 1.0f / ( ConstantAttenuation + LinearAttenuation * Distance + QuadraticAttenuation * Distance * Distance );
	}

	// 光源から照らす点への向き DirectionFromLight（長さは問わない）での、円錐による強さ（0..1。DxLib の計算と同じ）。スポットライト以外は 1
	float32 GetSpotFactor( const FVector3& DirectionFromLight ) const
	{
		if ( Type != EDrawLightType::Spot )
		{
			return 1.0f;
		}
		const float32 DirectionLength = Direction.Length();
		const float32 TargetLength = DirectionFromLight.Length();
		if ( DirectionLength <= NMath::SmallNumber || TargetLength <= NMath::SmallNumber )
		{
			return 0.0f;
		}
		const float32 CosAngle = FVector3::Dot( Direction, DirectionFromLight ) / ( DirectionLength * TargetLength );
		const float32 CosOuter = NMath::Cos( OuterAngle * 0.5f );
		const float32 CosInner = NMath::Cos( InnerAngle * 0.5f );
		if ( CosAngle <= CosOuter )
		{
			return 0.0f;
		}
		if ( CosAngle >= CosInner )
		{
			return 1.0f;
		}
		return ( CosAngle - CosOuter ) / ( CosInner - CosOuter );
	}

	// 世界の点 Point に届く光の強さ（距離の減衰 x 円錐）。面の向きは考えない（面が光に向いているかは別）。「この点は、明るい場所か」の判断にも使える
	float32 GetIntensityAt( const FVector3& Point ) const
	{
		if ( Type == EDrawLightType::Directional )
		{
			return 1.0f;
		}
		const FVector3 Offset = Point - Position;
		return GetAttenuation( Offset.Length() ) * GetSpotFactor( Offset );
	}

private:
	static bool IsFiniteVector( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}

	static bool IsFiniteColor( const FColor& Value )
	{
		return NMath::IsFinite( Value.r ) && NMath::IsFinite( Value.g ) && NMath::IsFinite( Value.b );
	}
};

//=============================================================================
// FSurfaceMaterial
//   面の材質（光の受け方）。図形を描く前に記録する（FDrawContext::SurfaceMaterial）と、そのあとに描く図形に効く。
//   既定値は、DxLib の既定の材質と同じ（何も記録しなければ、この材質）。
//
//   拡散光と鏡面反射の色は、既定では、図形に付けた色（Sphere3D の Diffuse / Specular など）を使う。材質の色で決めたいときは、
//   bUseVertexDiffuse / bUseVertexSpecular を false にする（図形に付けた色は、使われなくなる）。光の計算を切る UseLighting( false ) のときも、この設定に従う
//   （図形の色を使わない設定のままだと、光を切った図形も、材質の色で描かれる）。
//   環境光（FDrawContext::AmbientLight とライトの Ambient）を受けるには、Ambient を 0 より大きくする。
//=============================================================================
struct FSurfaceMaterial
{
	// 拡散光の色（bUseVertexDiffuse が false のときの、面の色）。アルファは使わない
	FColor Diffuse = FColor( 0.8f, 0.8f, 0.8f, 1.0f );

	// 環境光を受ける色。0 なら環境光を受けない（既定）
	FColor Ambient = FColor( 0.0f, 0.0f, 0.0f, 0.0f );

	// 鏡面反射の色（bUseVertexSpecular が false のときの、ハイライトの色）
	FColor Specular = FColor( 0.8f, 0.8f, 0.8f, 0.0f );

	// 自分で光る色（光がなくても、この色が足される。光の計算をやめる UseLighting( false ) のときは、効かない）
	FColor Emissive = FColor( 0.0f, 0.0f, 0.0f, 0.0f );

	// ハイライトの鋭さ（0 以上。大きいほど、小さく鋭い。金属は大きく、布は小さく）
	float32 Shininess = 20.0f;

	// true なら、拡散光の色に、図形に付けた色を使う（Diffuse は使われない）。false なら、Diffuse だけを使う
	bool bUseVertexDiffuse = true;

	// true なら、鏡面反射の色に、図形に付けた色を使う（Specular は使われない）。false なら、Specular だけを使う
	bool bUseVertexSpecular = true;

	// 環境光を受ける、つや消しの材質（図形の色で、環境光も受ける）
	static FSurfaceMaterial MakeMatte()
	{
		FSurfaceMaterial Material;
		Material.Ambient = FColor( 1.0f, 1.0f, 1.0f, 0.0f );
		Material.Specular = FColor( 0.0f, 0.0f, 0.0f, 0.0f );
		return Material;
	}

	// 自分で光る材質（Color の分だけ、光がなくても明るい）
	static FSurfaceMaterial MakeGlowing( const FColor& Color )
	{
		FSurfaceMaterial Material = MakeMatte();
		Material.Emissive = Color;
		return Material;
	}

	// 値が使えるか（すべて有限で、Shininess が 0 以上）
	bool IsValid() const
	{
		return IsFiniteColor( Diffuse ) && IsFiniteColor( Ambient ) && IsFiniteColor( Specular ) && IsFiniteColor( Emissive ) && NMath::IsFinite( Shininess ) && Shininess >= 0.0f;
	}

private:
	static bool IsFiniteColor( const FColor& Value )
	{
		return NMath::IsFinite( Value.r ) && NMath::IsFinite( Value.g ) && NMath::IsFinite( Value.b );
	}
};

//=============================================================================
// FLightingOptions
//   光の計算の仕方。既定値は、DxLib の既定と同じ（何も記録しなければ、この設定）。
//=============================================================================
struct FLightingOptions
{
	// ハイライト（鏡面反射）を計算するか。false だと、鏡面反射が出ない
	bool bUseSpecular = true;

	// true なら、画素ごとに光を計算する（大きな面でも、点光源・スポットライトの光の輪が円く出る。頂点ごと（false）だと、広い面では光の輪が出ない）。重い
	bool bPixelLighting = false;

	// true なら、光の当たらない側もやわらかく照らす（面の向きの明るさが ( 内積 x 0.5 + 0.5 )^2 になる。いわゆるハーフランバート）
	bool bWrapLighting = false;

	// true なら、面の向きと光の角度に関係なく、光が届く面を全力で照らす（のっぺりした見た目になる）。距離・円錐の減衰は残る
	bool bIgnoreSurfaceAngle = false;
};

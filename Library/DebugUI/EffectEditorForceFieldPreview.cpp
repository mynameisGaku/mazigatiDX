// SPDX-License-Identifier: NOASSERTION
#include "EffectEditorInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	// 3軸の長さをdoubleで求め、floatの二乗overflowを避ける。
	float64 ForceFieldVectorLength( const FVector3& Value )
	{
		// 3成分をdoubleへ広げた値。
		const float64 X = Value.x;
		// 3成分をdoubleへ広げた値。
		const float64 Y = Value.y;
		// 3成分をdoubleへ広げた値。
		const float64 Z = Value.z;
		return std::sqrt( X * X + Y * Y + Z * Z );
	}

	// boundsへ加える座標が有限で、半径が有限かつ非負であることを確認する。
	bool IsValidForceFieldSphere( const FVector3& Center, float64 Radius )
	{
		return NMath::IsFinite( Center.x ) && NMath::IsFinite( Center.y ) && NMath::IsFinite( Center.z ) && NMath::IsFinite( Radius ) && Radius >= 0.0 && Radius <= TNumericLimits<float32>::Max();
	}

	// 中心と半径の球を既存の保守boundsへ加える。途中失敗では出力を変えない。
	bool AddForceFieldBoundsSphere( const FVector3& SphereCenter, float64 SphereRadius, FVector3& BoundsCenter, float64& BoundsRadius, bool& bHasBounds )
	{
		if ( !IsValidForceFieldSphere( SphereCenter, SphereRadius ) ) return false;
		if ( !bHasBounds )
		{
			BoundsCenter = SphereCenter;
			BoundsRadius = SphereRadius;
			bHasBounds = true;
			return true;
		}
		// 球の中心差をdoubleで計算する。
		const float64 DeltaX = static_cast<float64>( SphereCenter.x ) - BoundsCenter.x;
		// 球の中心差をdoubleで計算する。
		const float64 DeltaY = static_cast<float64>( SphereCenter.y ) - BoundsCenter.y;
		// 球の中心差をdoubleで計算する。
		const float64 DeltaZ = static_cast<float64>( SphereCenter.z ) - BoundsCenter.z;
		// 中心間の距離。
		const float64 Distance = std::sqrt( DeltaX * DeltaX + DeltaY * DeltaY + DeltaZ * DeltaZ );
		if ( !NMath::IsFinite( Distance ) ) return false;
		if ( Distance + SphereRadius <= BoundsRadius ) return true;
		if ( Distance + BoundsRadius <= SphereRadius )
		{
			BoundsCenter = SphereCenter;
			BoundsRadius = SphereRadius;
			return true;
		}
		if ( Distance <= 1.0e-12 )
		{
			BoundsRadius = NMath::Max( BoundsRadius, SphereRadius );
			return true;
		}
		// 二球を含める新しい球の半径と中心移動率。
		const float64 NewRadius = ( Distance + BoundsRadius + SphereRadius ) * 0.5;
		// 元の中心から新しい中心へ動かす割合。
		const float64 Move = ( NewRadius - BoundsRadius ) / Distance;
		// floatへ丸める前の新しい中心座標。
		const float64 NewX = static_cast<float64>( BoundsCenter.x ) + DeltaX * Move;
		// floatへ丸める前の新しい中心座標。
		const float64 NewY = static_cast<float64>( BoundsCenter.y ) + DeltaY * Move;
		// floatへ丸める前の新しい中心座標。
		const float64 NewZ = static_cast<float64>( BoundsCenter.z ) + DeltaZ * Move;
		if ( !NMath::IsFinite( NewRadius ) || !NMath::IsFinite( NewX ) || !NMath::IsFinite( NewY ) || !NMath::IsFinite( NewZ ) || NewRadius > TNumericLimits<float32>::Max() ) return false;
		// float中心へ丸めた際に失う距離。
		const FVector3 RoundedCenter( static_cast<float32>( NewX ), static_cast<float32>( NewY ), static_cast<float32>( NewZ ) );
		// 丸め誤差の3成分。
		const float64 RoundX = NewX - RoundedCenter.x;
		// 丸め誤差の3成分。
		const float64 RoundY = NewY - RoundedCenter.y;
		// 丸め誤差の3成分。
		const float64 RoundZ = NewZ - RoundedCenter.z;
		// 丸めで中心がずれる距離。
		const float64 RoundError = std::sqrt( RoundX * RoundX + RoundY * RoundY + RoundZ * RoundZ );
		if ( !NMath::IsFinite( RoundError ) || NewRadius + RoundError > TNumericLimits<float32>::Max() ) return false;
		BoundsCenter = RoundedCenter;
		BoundsRadius = NewRadius + RoundError;
		return true;
	}

	// 寿命・速度・サイズ範囲がカメラ計算で使える値か調べる。
	bool IsValidForceFieldRange( const FParticleRange& Range, bool bNeedPositiveMinimum )
	{
		return NMath::IsFinite( Range.Min ) && NMath::IsFinite( Range.Max ) && Range.Min <= Range.Max && ( bNeedPositiveMinimum ? Range.Min > 0.0f : Range.Min >= 0.0f );
	}

	// 視界へ合わせた後もboundsが投影範囲に収まり、clip面を越えないか確認する。
	bool IsFocusedForceFieldBoundsVisible( const FEditorCamera& Camera, const FEditorViewport& Viewport, const FVector3& Center, float32 Radius )
	{
		// 中心の画面座標。
		FVector2 Pixel;
		// 中心の奥行き。
		float32 Depth = 0.0f;
		if ( !Camera.WorldToScreen( Viewport, Center, Pixel, &Depth ) || !NMath::IsFinite( Depth ) || !NMath::IsFinite( Pixel.x ) || !NMath::IsFinite( Pixel.y ) ) return false;
		if ( Depth - Radius <= Viewport.Near || Depth + Radius >= Viewport.Far || Pixel.x < 1.0f || Pixel.y < 1.0f || Pixel.x > Viewport.Width - 1.0f || Pixel.y > Viewport.Height - 1.0f ) return false;
		// 縦横のうち狭い側の半視野角。
		const float32 HalfFovY = Viewport.FovY * 0.5f;
		// 横方向の半視野角。
		const float32 HalfFovX = std::atan( std::tan( HalfFovY ) * Viewport.GetAspect() );
		// 球の収まりを制限する半視野角。
		const float32 HalfFov = NMath::Min( HalfFovX, HalfFovY );
		return NMath::IsFinite( HalfFov ) && HalfFov > 0.0f && Radius <= Depth * std::sin( HalfFov ) * 0.92f;
	}

	// Vortexの回転軸矢印の長さ。半径に比例させ、極端に小さい・大きい場でも見える範囲へ収める。
	float32 GetForceFieldAxisArrowLength( float32 Radius )
	{
		return NMath::Clamp( Radius * 0.6f, 0.3f, 3.0f );
	}

	// 矢印の先端に二本の短い線を記録し、World方向を見分けやすくする。
	void RecordForceFieldArrow( FDrawContext& Draw, const FVector3& Start, const FVector3& Direction, float32 Length, const FColor32& Color )
	{
		// 矢印の方向をdoubleで正規化するための長さ。
		const float64 Magnitude = ForceFieldVectorLength( Direction );
		if ( !NMath::IsFinite( Magnitude ) || Magnitude <= 1.0e-9 || !NMath::IsFinite( Length ) || Length <= 0.0f ) return;
		// World方向の単位ベクトル。
		const FVector3 Unit( static_cast<float32>( Direction.x / Magnitude ), static_cast<float32>( Direction.y / Magnitude ), static_cast<float32>( Direction.z / Magnitude ) );
		// 矢印の先端。
		const FVector3 End = Start + Unit * Length;
		// 上方向と平行にならない基準軸。
		const FVector3 Reference = NMath::Abs( Unit.y ) < 0.85f ? FVector3::Up : FVector3::Right;
		// 矢印の横方向。
		const FVector3 Side = FVector3::Cross( Unit, Reference ).Normalized();
		// 矢尻の長さ。
		const float32 HeadLength = NMath::Min( Length * 0.22f, 0.28f );
		// 矢尻の半幅。
		const float32 HeadWidth = HeadLength * 0.55f;
		Draw.Line3D( Start, End, Color );
		Draw.Line3D( End, End - Unit * HeadLength + Side * HeadWidth, Color );
		Draw.Line3D( End, End - Unit * HeadLength - Side * HeadWidth, Color );
	}

	// 指定した直交2軸と半径で、中心・半径が正確なguide円を32本の線で記録する。
	void RecordForceFieldGuideCircle( FDrawContext& Draw, const FVector3& Center, float32 Radius, const FVector3& AxisA, const FVector3& AxisB, const FColor32& Color )
	{
		// 円周を分ける線分数。
		constexpr int32 SegmentCount = 32;
		// 一周をラジアンで表す定数。
		constexpr float32 TwoPi = 6.283185307f;
		// 円周上の隣り合う点を結ぶ線。
		for ( int32 Index = 0; Index < SegmentCount; ++Index )
		{
			// 線分の始点角度。
			const float32 Angle0 = TwoPi * static_cast<float32>( Index ) / SegmentCount;
			// 線分の終点角度。
			const float32 Angle1 = TwoPi * static_cast<float32>( Index + 1 ) / SegmentCount;
			// 円周上の線分始点。
			const FVector3 Point0 = Center + ( AxisA * std::cos( Angle0 ) + AxisB * std::sin( Angle0 ) ) * Radius;
			// 円周上の線分終点。
			const FVector3 Point1 = Center + ( AxisA * std::cos( Angle1 ) + AxisB * std::sin( Angle1 ) ) * Radius;
			Draw.Line3D( Point0, Point1, Color );
		}
	}
}

// 全emitterと有効な力場範囲を包む球へ一度だけcameraを合わせる。無効・空・過大なboundsでは変更しない。
bool NEffectEditorUI::FocusCameraOnForceFields( FEffectEditorState& State, const FEditorViewport& Viewport )
{
	if ( !Viewport.IsValid() || !NMath::IsFinite( Viewport.FovY ) || Viewport.FovY <= 0.0f || Viewport.FovY >= 3.14159265f || !NMath::IsFinite( Viewport.Near ) || !NMath::IsFinite( Viewport.Far ) || Viewport.Near <= 0.0f || Viewport.Far <= Viewport.Near ) return false;
	// 少なくとも一つの力場を持つ定義だけをfocus対象にする。
	bool bHasForceField = false;
	// 力場の有無を調べるemitter。
	for ( const FParticleEmitterDefinition& Emitter : State.Document.Get().Emitters ) if ( !Emitter.ForceFields.IsEmpty() ) { bHasForceField = true; break; }
	if ( !bHasForceField ) return false;
	// 画角計算に使う縦横比。
	const float32 Aspect = Viewport.GetAspect();
	if ( !NMath::IsFinite( Aspect ) || Aspect <= 0.0f ) return false;
	// 力場を含む編集中の定義。
	const FParticleEffectDefinition& Definition = State.Document.Get();
	// cameraへ収める空間範囲の中心。
	FVector3 BoundsCenter = FVector3::Zero;
	// cameraへ収める空間範囲の半径。
	float64 BoundsRadius = 0.0;
	// 一つ以上の範囲を追加したか。
	bool bHasBounds = false;
	// いま空間範囲を計算するemitter。
	for ( const FParticleEmitterDefinition& Emitter : Definition.Emitters )
	{
		if ( Emitter.ForceFields.Num() > FEffectForceFieldDefinition::MaxFields || !IsValidForceFieldRange( Emitter.Lifetime, true ) || !IsValidForceFieldRange( Emitter.Speed, false ) || !IsValidForceFieldRange( Emitter.StartSize, false ) || !IsValidForceFieldRange( Emitter.EndSize, false ) ) return false;
		if ( !NMath::IsFinite( Emitter.Gravity.x ) || !NMath::IsFinite( Emitter.Gravity.y ) || !NMath::IsFinite( Emitter.Gravity.z ) || !NMath::IsFinite( Emitter.Direction.x ) || !NMath::IsFinite( Emitter.Direction.y ) || !NMath::IsFinite( Emitter.Direction.z ) || !NMath::IsFinite( Emitter.ShapeRadius ) || Emitter.ShapeRadius < 0.0f || !NMath::IsFinite( Emitter.BoxSize.x ) || !NMath::IsFinite( Emitter.BoxSize.y ) || !NMath::IsFinite( Emitter.BoxSize.z ) || Emitter.BoxSize.x < 0.0f || Emitter.BoxSize.y < 0.0f || Emitter.BoxSize.z < 0.0f || !NMath::IsFinite( Emitter.ConeAngle ) || Emitter.ConeAngle < 0.0f || Emitter.ConeAngle > 180.0f ) return false;
		if ( Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None || Emitter.Flipbook.bEnabled || !Emitter.Mesh.IsValidSettings() ) ) return false;
		if ( !Emitter.Appearance.Size.Validate().IsEmpty() ) return false;
		// 寿命中の最大時間。
		const float64 Lifetime = Emitter.Lifetime.Max;
		// 重力加速度の長さ。
		const float64 GravityMagnitude = ForceFieldVectorLength( Emitter.Gravity );
		// 全有効場の保守加速度上限。
		float64 ForceAcceleration = 0.0;
		// このemitterに保存された場。
		for ( const FEffectForceFieldDefinition& Field : Emitter.ForceFields )
		{
			if ( !Field.IsValidSettings() ) return false;
			if ( Field.Kind != FEffectForceFieldDefinition::EKind::Uniform )
			{
				// guideに含めるfield球の半径。
				float64 GuideRadius = Field.Radius;
				// Vortexだけが描く回転軸矢印は、強さに依らず含める。
				if ( Field.Kind == FEffectForceFieldDefinition::EKind::Vortex ) GuideRadius = NMath::Max( GuideRadius, static_cast<float64>( GetForceFieldAxisArrowLength( Field.Radius ) ) );
				if ( Field.Strength != 0.0f )
				{
					// wire球から出る方向矢印の長さ。
					const float64 GuideArrowLength = NMath::Clamp( static_cast<float64>( Field.Radius ) * 0.2, 0.25, 1.25 );
					// Turbulenceの矢印は中心から出る。RadialとVortexは半径境界付近から出る。
					const float64 ArrowReach = Field.Kind == FEffectForceFieldDefinition::EKind::Turbulence ? GuideArrowLength : static_cast<float64>( Field.Radius ) * 0.52 + GuideArrowLength;
					GuideRadius = NMath::Max( GuideRadius, ArrowReach );
				}
				if ( !AddForceFieldBoundsSphere( Field.Position, GuideRadius, BoundsCenter, BoundsRadius, bHasBounds ) ) return false;
			}
			else if ( ForceFieldVectorLength( Field.Acceleration ) > 1.0e-9 && !AddForceFieldBoundsSphere( FVector3::Zero, 1.25, BoundsCenter, BoundsRadius, bHasBounds ) ) return false;
			if ( !Field.bEnabled ) continue;
			// fieldの加速度上限。
			float64 FieldMagnitude = Field.Kind == FEffectForceFieldDefinition::EKind::Uniform ? ForceFieldVectorLength( Field.Acceleration ) : NMath::Abs( static_cast<float64>( Field.Strength ) );
			// 乱流は3軸それぞれが最大2|Strength|まで変わるため、大きさの上限は2*sqrt(3)倍になる。
			if ( Field.Kind == FEffectForceFieldDefinition::EKind::Turbulence ) FieldMagnitude *= 3.4641016151377544;
			if ( !NMath::IsFinite( FieldMagnitude ) ) return false;
			ForceAcceleration += FieldMagnitude;
			if ( !NMath::IsFinite( ForceAcceleration ) ) return false;
		}
		// speedMax*T + (gravityLen + all enabled field limits)*T^2 の保守移動距離。
		const float64 AccelerationMagnitude = GravityMagnitude + ForceAcceleration;
		const float64 TravelRadius = static_cast<float64>( Emitter.Speed.Max ) * Lifetime + AccelerationMagnitude * Lifetime * Lifetime;
		if ( !NMath::IsFinite( TravelRadius ) ) return false;
		// Cone・Sphereの発生範囲、またはBoxの半対角。
		float64 SpawnRadius = static_cast<float64>( Emitter.ShapeRadius );
		if ( Emitter.Shape == EParticleShape::Box ) SpawnRadius = 0.5 * std::sqrt( static_cast<float64>( Emitter.BoxSize.x ) * Emitter.BoxSize.x + static_cast<float64>( Emitter.BoxSize.y ) * Emitter.BoxSize.y + static_cast<float64>( Emitter.BoxSize.z ) * Emitter.BoxSize.z );
		else if ( Emitter.Shape != EParticleShape::Cone && Emitter.Shape != EParticleShape::Sphere ) return false;
		// 寿命全体で使うsize倍率の最大値。
		float64 AppearanceScale = 1.0;
		if ( !Emitter.Appearance.Size.Keys.IsEmpty() )
		{
			AppearanceScale = 0.0;
			// size曲線上の倍率候補。
			for ( const FEffectScalarCurve::FKey& Key : Emitter.Appearance.Size.Keys )
			{
				if ( !NMath::IsFinite( Key.Value ) || Key.Value < 0.0f || Key.Value > 32.0f ) return false;
				AppearanceScale = NMath::Max( AppearanceScale, static_cast<float64>( Key.Value ) );
			}
		}
		// 粒子の最大辺長。
		const float64 BaseSize = NMath::Max( static_cast<float64>( Emitter.StartSize.Max ), static_cast<float64>( Emitter.EndSize.Max ) );
		// 四角形の半対角。Tree・帯の太さもこの値を元にする。
		const float64 ParticleRadius = BaseSize * AppearanceScale * 0.7071067811865476;
		// 固定meshの最大外周。
		float64 MeshRadius = 0.0;
		if ( Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None )
		{
			if ( !Emitter.Mesh.IsValidSettings() ) return false;
			MeshRadius = BaseSize * AppearanceScale * ( Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::Plane ? 0.7071067811865476 : 0.5 );
		}
		// Lightningの終点・揺れ・枝を含む形状半径。
		float64 PathRadius = 0.0;
		if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None )
		{
			if ( !Emitter.Path.Validate().IsEmpty() || !NMath::IsFinite( Emitter.Path.End.x ) || !NMath::IsFinite( Emitter.Path.End.y ) || !NMath::IsFinite( Emitter.Path.End.z ) ) return false;
			// path終点までの長さ。
			const float64 EndLength = ForceFieldVectorLength( Emitter.Path.End );
			if ( !NMath::IsFinite( EndLength ) ) return false;
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning )
			{
				// Treeが返す始点中心の球半径。
				FVector3 TreeCenter = FVector3::Zero;
				// Treeが返す始点中心の球半径。
				float32 TreeRadius = 0.0f;
				// 寿命曲線の揺れと枝幅を含むTreeの保守bounds。
				const float32 TreePadding = static_cast<float32>( NMath::Max( ParticleRadius, 0.001 ) );
				if ( !NEffectPath::GetTreeLightningBounds( Emitter.Path, FVector3::Zero, Emitter.Path.End, TreePadding, TreeCenter, TreeRadius ) || !NMath::IsFinite( TreeRadius ) || TreeRadius <= 0.0f ) return false;
				PathRadius = ForceFieldVectorLength( TreeCenter ) + TreeRadius;
			}
			else if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning )
			{
				// 枝が作られる条件を描画側と同じ長さ・枝数で判定する。
				const bool bHasBranches = Emitter.Path.Branches > 0 && Emitter.Path.BranchLength > 0.0f;
				// 主幹横揺れと枝先までを包含する倍率。
				const float64 SpreadFactor = 1.0 + std::sqrt( 2.0 ) * Emitter.Path.Jitter + ( bHasBranches ? Emitter.Path.BranchLength + std::sqrt( 2.0 ) * 0.35 * Emitter.Path.Jitter : 0.0 );
				PathRadius = EndLength * SpreadFactor + ParticleRadius;
			}
			else if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon ) PathRadius = ParticleRadius;
			else return false;
		}
		// 発生範囲・寿命中移動・粒子形状を合算したemitter球。
		const float64 EmitterRadius = SpawnRadius + TravelRadius + NMath::Max( ParticleRadius, MeshRadius ) + PathRadius;
		if ( !AddForceFieldBoundsSphere( FVector3::Zero, EmitterRadius, BoundsCenter, BoundsRadius, bHasBounds ) ) return false;
	}
	if ( !bHasBounds || !IsValidForceFieldSphere( BoundsCenter, BoundsRadius ) || BoundsRadius > FEditorCamera::MaxDistance ) return false;
	// doubleの半径をcameraへ渡す有限float。
	const float32 SafeRadius = static_cast<float32>( BoundsRadius );
	if ( !NMath::IsFinite( SafeRadius ) || SafeRadius <= 0.0f ) return false;
	// focus結果の検査に失敗した場合へ戻すcamera。
	const FEditorCamera PreviousCamera = State.Camera;
	State.Camera.Focus( BoundsCenter, SafeRadius, Viewport );
	// near面の手前へ球が出ないための正の余裕。
	const float32 NearMargin = NMath::Max( 0.01f, SafeRadius * 0.05f );
	// 球の手前端をnear面より奥へ置く最小距離。
	const float32 MinimumDistance = Viewport.Near + SafeRadius + NearMargin;
	if ( !NMath::IsFinite( MinimumDistance ) || MinimumDistance > FEditorCamera::MaxDistance )
	{
		State.Camera = PreviousCamera;
		return false;
	}
	State.Camera.Distance = NMath::Max( State.Camera.Distance, MinimumDistance );
	if ( !NMath::IsFinite( State.Camera.Distance ) || State.Camera.Distance > FEditorCamera::MaxDistance )
	{
		State.Camera = PreviousCamera;
		return false;
	}
	if ( !IsFocusedForceFieldBoundsVisible( State.Camera, Viewport, BoundsCenter, SafeRadius ) )
	{
		State.Camera = PreviousCamera;
		return false;
	}
	return true;
}

// 選択中の有効な力場だけをWorld座標のwire guideとして記録する。時間・文書・資源は変更しない。
void NEffectEditorUI::RecordForceFieldGuides( const FEffectEditorState& State, FDrawContext& Draw )
{
	RecordForceFieldGuidesForTransform( State, Draw, GetPreviewEmitterTransform( State ) );
}

// 動的終点とエミッター始点のつながりを、粒子と同じWorld座標のoverlayへ記録する。
void NEffectEditorUI::RecordPreviewTargetGuides( const FEffectEditorState& State, FDrawContext& Draw )
{
	if ( !State.bShowPreviewTargetGuides || !State.bPreviewTargetEnabled || FindTargetPathEmitter( State.Document.Get() ) == nullptr ) return;
	const FTransform Transform = GetPreviewEmitterTransform( State );
	const FVector3 Start = Transform.Position;
	const FVector3 Target = State.PreviewTargetPosition;
	if ( !NMath::IsFinite( Start.x ) || !NMath::IsFinite( Start.y ) || !NMath::IsFinite( Start.z ) || !NMath::IsFinite( Target.x ) || !NMath::IsFinite( Target.y ) || !NMath::IsFinite( Target.z ) ) return;
	const FColor32 ConnectionColor( 255, 170, 45, 255 );
	const FColor32 AxisXColor( 255, 80, 70, 255 );
	const FColor32 AxisYColor( 90, 245, 120, 255 );
	const FColor32 AxisZColor( 80, 150, 255, 255 );
	constexpr float32 MarkerHalfLength = 0.16f;
	Draw.Line3D( Start, Target, ConnectionColor );
	Draw.Line3D( Target - FVector3::Right * MarkerHalfLength, Target + FVector3::Right * MarkerHalfLength, AxisXColor );
	Draw.Line3D( Target - FVector3::Up * MarkerHalfLength, Target + FVector3::Up * MarkerHalfLength, AxisYColor );
	Draw.Line3D( Target - FVector3::Forward * MarkerHalfLength, Target + FVector3::Forward * MarkerHalfLength, AxisZColor );
}

// 追従する場は渡されたエミッター姿勢の位置・回転で、世界固定の場はWorld座標のまま描く。拡大は使わない。
void NEffectEditorUI::RecordForceFieldGuidesForTransform( const FEffectEditorState& State, FDrawContext& Draw, const FTransform& EmitterTransform )
{
	if ( !State.bShowForceFieldGuides || !State.Document.Get().Emitters.IsValidIndex( State.SelectedEmitter ) ) return;
	// guideを表示する選択中emitter。
	const FParticleEmitterDefinition& Emitter = State.Document.Get().Emitters[State.SelectedEmitter];
	if ( !Emitter.ForceFields.IsValidIndex( State.SelectedForceField ) ) return;
	// guideを表示する選択中field。
	const FEffectForceFieldDefinition& Field = Emitter.ForceFields[State.SelectedForceField];
	if ( !Field.IsValidSettings() ) return;
	// 追従する場へ使うエミッター位置。
	const FVector3& FollowPosition = EmitterTransform.Position;
	// 追従する場へ使う回転。非有限・ゼロ回転の姿勢では何も描かない。
	const FQuaternion& FollowRotation = EmitterTransform.Rotation;
	if ( Field.bFollowEmitter && ( !NMath::IsFinite( FollowPosition.x ) || !NMath::IsFinite( FollowPosition.y ) || !NMath::IsFinite( FollowPosition.z ) || !NMath::IsFinite( FollowRotation.x ) || !NMath::IsFinite( FollowRotation.y ) || !NMath::IsFinite( FollowRotation.z ) || !NMath::IsFinite( FollowRotation.w ) || FollowRotation.LengthSquared() <= 1.0e-12f ) ) return;
	// guideを描く座標系の原点。世界固定の場はWorld原点。
	const FVector3 FrameOrigin = Field.bFollowEmitter ? FollowPosition : FVector3::Zero;
	// guideを描く座標系の回転。世界固定の場は回転しない。
	const FQuaternion FrameRotation = Field.bFollowEmitter ? FollowRotation.Normalized() : FQuaternion::Identity;
	// 有効なfieldを粒子と分けて見せる青緑。
	const FColor32 EnabledColor( 40, 220, 230, 255 );
	// 無効fieldの範囲を控えめに示す灰色。
	const FColor32 DisabledColor( 125, 140, 150, 255 );
	// 今fieldの表示に使う色。
	const FColor32 Color = Field.bEnabled ? EnabledColor : DisabledColor;
	if ( Field.Kind == FEffectForceFieldDefinition::EKind::Uniform )
	{
		// 一様加速度を表示する原点。世界固定ならWorld原点、追従ならエミッター位置。
		const FVector3 Origin = FrameOrigin;
		// 方向だけを示す固定長。
		constexpr float32 ArrowLength = 1.25f;
		RecordForceFieldArrow( Draw, Origin, FrameRotation.Rotate( Field.Acceleration ), ArrowLength, Color );
		return;
	}
	if ( Field.Kind != FEffectForceFieldDefinition::EKind::Radial && Field.Kind != FEffectForceFieldDefinition::EKind::Vortex && Field.Kind != FEffectForceFieldDefinition::EKind::Turbulence ) return;
	// 場の中心のWorld座標。
	const FVector3 Center = FrameOrigin + FrameRotation.Rotate( Field.Position );
	// 場の局所X・Y・Z軸のWorld向き。
	const FVector3 AxisX = FrameRotation.Rotate( FVector3::Right );
	const FVector3 AxisY = FrameRotation.Rotate( FVector3::Up );
	const FVector3 AxisZ = FrameRotation.Rotate( FVector3::Forward );
	// XY・XZ・YZの大円だけで球の範囲を疎に示す。
	RecordForceFieldGuideCircle( Draw, Center, Field.Radius, AxisX, AxisY, Color );
	RecordForceFieldGuideCircle( Draw, Center, Field.Radius, AxisX, AxisZ, Color );
	RecordForceFieldGuideCircle( Draw, Center, Field.Radius, AxisY, AxisZ, Color );
	// 大きい半径でも画面を横切りにくい矢印長さ。
	const float32 ArrowLength = NMath::Clamp( Field.Radius * 0.2f, 0.25f, 1.25f );
	if ( Field.Kind == FEffectForceFieldDefinition::EKind::Radial )
	{
		if ( Field.Strength == 0.0f ) return;
		// 半径境界上で符号による引力・反発方向を示す始点。
		const FVector3 DirectionStart = Center + AxisX * ( Field.Radius * 0.52f );
		// 正なら外向き、負なら中心向きにする方向。
		const FVector3 Direction = FrameRotation.Rotate( Field.Strength >= 0.0f ? FVector3::Right : FVector3::Left );
		RecordForceFieldArrow( Draw, DirectionStart, Direction, ArrowLength, Color );
	}
	else if ( Field.Kind == FEffectForceFieldDefinition::EKind::Vortex )
	{
		// 回転軸の長さ。IsValidSettingsで有限かつゼロでないと確認済み。
		const float64 AxisLength = ForceFieldVectorLength( Field.Axis );
		// 局所座標での正規化した回転軸。
		const FVector3 LocalAxis( static_cast<float32>( Field.Axis.x / AxisLength ), static_cast<float32>( Field.Axis.y / AxisLength ), static_cast<float32>( Field.Axis.z / AxisLength ) );
		// 軸と平行にならない基準方向。
		const FVector3 LocalReference = NMath::Abs( LocalAxis.y ) < 0.85f ? FVector3::Up : FVector3::Right;
		// 回転軸に垂直で、周回方向の矢印を置く向き。
		const FVector3 LocalPerpendicular = FVector3::Cross( LocalAxis, LocalReference ).Normalized();
		// 回転軸の矢印は強さに依らず中心から出す。
		RecordForceFieldArrow( Draw, Center, FrameRotation.Rotate( LocalAxis ), GetForceFieldAxisArrowLength( Field.Radius ), Color );
		if ( Field.Strength == 0.0f ) return;
		// 周回方向は正のStrengthでUnitAxis x Delta、負で逆向き。
		const FVector3 LocalTangent = FVector3::Cross( LocalAxis, LocalPerpendicular ) * ( Field.Strength >= 0.0f ? 1.0f : -1.0f );
		// 半径境界付近に置く周回矢印の始点。
		const FVector3 TangentStart = Center + FrameRotation.Rotate( LocalPerpendicular ) * ( Field.Radius * 0.52f );
		RecordForceFieldArrow( Draw, TangentStart, FrameRotation.Rotate( LocalTangent ), ArrowLength, Color );
	}
	else
	{
		if ( Field.Strength == 0.0f ) return;
		// 乱流は特定の向きを持たないため、中心から3軸へ短い矢印を出して揺らぎの場であることを示す。
		RecordForceFieldArrow( Draw, Center, AxisX, ArrowLength, Color );
		RecordForceFieldArrow( Draw, Center, AxisY, ArrowLength, Color );
		RecordForceFieldArrow( Draw, Center, AxisZ, ArrowLength, Color );
	}
}

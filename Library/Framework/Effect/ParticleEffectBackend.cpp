// SPDX-License-Identifier: NOASSERTION
#include "ParticleEffectBackend.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Framework/Render/DrawContext.h"

namespace
{
	constexpr int32 SoftDotSize = 64;

	// 組み込みの丸の画像（Assets の上で共有される。名前が同じなら同じ画像）
	TSharedPtr<CTexture> GetSoftDot( CAssetManager* Assets )
	{
		if ( Assets == nullptr ) return nullptr;
		const TArray<uint8> Image = NEffect::MakeSoftDotImage();
		return Assets->CreateTextureFromMemory( "effect:soft-dot", Image.GetData(), static_cast<usize>( Image.Num() ) );
	}

	EDrawQuadOrient ToQuadOrient( EParticleOrient Orient )
	{
		static const EDrawQuadOrient Table[] = { EDrawQuadOrient::Camera, EDrawQuadOrient::CameraY, EDrawQuadOrient::Fixed };
		return Table[NMath::Clamp( static_cast<int32>( Orient ), 0, 2 )];
	}

	EDrawBlend ToDrawBlend( EParticleBlend Blend )
	{
		return Blend == EParticleBlend::Add ? EDrawBlend::Add : EDrawBlend::Alpha;
	}

	// 曲線のどこかで強さが正なら、再生開始時にゆがみ画像を用意する。
	bool NeedsDistortionTexture( const FEffectUVDistortionDefinition& Distortion )
	{
		if ( !Distortion.bEnabled ) return false;
		if ( Distortion.Strength.Keys.IsEmpty() ) return true;
		for ( const FEffectScalarCurve::FKey& Key : Distortion.Strength.Keys ) if ( Key.Value > 0.0f ) return true;
		return false;
	}

	// 材質評価値を固定mesh命令へ移し、必要なnoiseが無ければ呼び出しを失敗させる。
	bool BuildMaterialMeshData( const FParticleEmitterDefinition& Emitter, const NParticle::FDrawParticle& Particle, int32 Texture, int32 MaskTexture, int32 DistortionTexture, FDrawMaterialMeshData& Out )
	{
		if ( Texture < 0 ) return false;
		Out = FDrawMaterialMeshData();
		Out.Texture = Texture;
		Out.bTwoSided = 1;
		if ( !Emitter.Material.bEnabled ) return true;
		Out.Material.MaskTexture = MaskTexture;
		Out.Material.UVScale[0] = NMath::IsFinite( Emitter.Material.UVScale.x ) && Emitter.Material.UVScale.x > 0.0f ? Emitter.Material.UVScale.x : 1.0f;
		Out.Material.UVScale[1] = NMath::IsFinite( Emitter.Material.UVScale.y ) && Emitter.Material.UVScale.y > 0.0f ? Emitter.Material.UVScale.y : 1.0f;
		Out.Material.MaskScale = NMath::IsFinite( Emitter.Material.MaskScale ) && Emitter.Material.MaskScale > 0.0f ? Emitter.Material.MaskScale : 1.0f;
		Out.Material.EdgeWidth = NMath::IsFinite( Emitter.Material.EdgeWidth ) ? NMath::Clamp( Emitter.Material.EdgeWidth, 0.0f, 1.0f ) : 0.08f;
		Out.Material.EdgeEmission = NMath::IsFinite( Emitter.Material.EdgeEmission ) ? NMath::Clamp( Emitter.Material.EdgeEmission, 0.0f, 32.0f ) : 2.0f;
		Out.Material.EdgeColor = { Emitter.Material.EdgeColor.r, Emitter.Material.EdgeColor.g, Emitter.Material.EdgeColor.b, Emitter.Material.EdgeColor.a };
		Out.Material.bDissolve = Emitter.Material.bDissolve ? 1 : 0;
		Out.Dissolve = Emitter.Material.Dissolve.Evaluate( Particle.NormalizedAge, 0.0f );
		Out.Emission = NMath::Clamp( Emitter.Material.Emission.Evaluate( Particle.NormalizedAge, 1.0f ), 0.0f, 32.0f );
		// 秒齢からUVの一周未満の移動量を求める。
		const double Age = NMath::IsFinite( Particle.Age ) ? static_cast<double>( Particle.Age ) : 0.0;
		const double ScrollU = NMath::IsFinite( Emitter.Material.UVScroll.x ) ? NMath::Fmod( Age * Emitter.Material.UVScroll.x, 1.0 ) : 0.0;
		const double ScrollV = NMath::IsFinite( Emitter.Material.UVScroll.y ) ? NMath::Fmod( Age * Emitter.Material.UVScroll.y, 1.0 ) : 0.0;
		Out.UVOffset[0] = static_cast<float32>( ScrollU < 0.0 ? ScrollU + 1.0 : ScrollU );
		Out.UVOffset[1] = static_cast<float32>( ScrollV < 0.0 ? ScrollV + 1.0 : ScrollV );
		const FEffectUVDistortionDefinition& Distortion = Emitter.Material.Distortion;
		if ( !Distortion.bEnabled ) return true;
		if ( !Distortion.IsValidSettings() || !NMath::IsFinite( Particle.Age ) || !NMath::IsFinite( Particle.NormalizedAge ) ) return false;
		Out.DistortionStrength = Distortion.Strength.Evaluate( Particle.NormalizedAge, 0.05f );
		if ( !NMath::IsFinite( Out.DistortionStrength ) || Out.DistortionStrength < 0.0f || Out.DistortionStrength > 1.0f ) return false;
		if ( Out.DistortionStrength <= 0.0f ) return true;
		if ( DistortionTexture < 0 ) return false;
		Out.DistortionTexture = DistortionTexture;
		Out.DistortionUVScale[0] = Distortion.UVScale.x;
		Out.DistortionUVScale[1] = Distortion.UVScale.y;
		// noiseの移動量は秒齢で計算し、命令へ一周未満だけ渡す。
		const double DistortionU = NMath::Fmod( Age * Distortion.UVScroll.x, 1.0 );
		const double DistortionV = NMath::Fmod( Age * Distortion.UVScroll.y, 1.0 );
		Out.DistortionUVOffset[0] = static_cast<float32>( DistortionU < 0.0 ? DistortionU + 1.0 : DistortionU );
		Out.DistortionUVOffset[1] = static_cast<float32>( DistortionV < 0.0 ? DistortionV + 1.0 : DistortionV );
		return true;
	}

	// 加算では不透明度を明るさへ移し、既存の粒子と同じ消え方にする。
	uint32 PackPathColor( FColor32 Color, EParticleBlend Blend, float32 Brightness = 1.0f )
	{
		// 輝きの外側だけを弱める倍率。
		const float32 Factor = Blend == EParticleBlend::Add ? NMath::Clamp( Brightness, 0.0f, 1.0f ) * Color.a / 255.0f : 1.0f;
		Color.r = static_cast<uint8>( Color.r * Factor );
		Color.g = static_cast<uint8>( Color.g * Factor );
		Color.b = static_cast<uint8>( Color.b * Factor );
		if ( Blend == EParticleBlend::Add ) Color.a = 255;
		else Color.a = static_cast<uint8>( Color.a * NMath::Clamp( Brightness, 0.0f, 1.0f ) );
		return static_cast<uint32>( Color.r ) | ( static_cast<uint32>( Color.g ) << 8 ) | ( static_cast<uint32>( Color.b ) << 16 ) | ( static_cast<uint32>( Color.a ) << 24 );
	}

	// 記録する値が描画系へ非有限の座標を持ち込まないか確認する。
	bool IsFinitePathPosition( const FVector3& Position )
	{
		return NMath::IsFinite( Position.x ) && NMath::IsFinite( Position.y ) && NMath::IsFinite( Position.z );
	}

	// 一つの経路を既存の記録窓口へ渡す。描画や GPU 資源の操作は行わない。
	void RecordPathStrip( FDrawContext& Draw, const FEffectPathGeometry::FStrip& Strip, int32 Texture, int32 MaskTexture, const FParticleEmitterDefinition& Emitter, int32 DistortionTexture, float32 WidthScale = 1.0f, float32 Brightness = 1.0f, float32 EdgeScale = 1.0f )
	{
		// 各記録スレッドだけが使う、描画契約への変換領域。
		thread_local TArray<FDrawRibbonData::FPoint> Points;
		// 材質帯では形状点と同じ順の曲線値・UV移動量を渡す。
		thread_local TArray<FDrawMaterialRibbonData::FPoint> MaterialPoints;
		// 先行chunkを書いた後で不正値が見つからないよう、全点を先に検証する。
		thread_local TArray<FDrawMaterialRibbonData::FPoint> ValidatedMaterialPoints;
		// UV歪みを使う帯だけが持つ、全点検証済みpayload。
		thread_local TArray<FDrawDistortedMaterialRibbonData::FPoint> ValidatedDistortionPoints;
		Points.Reset();
		MaterialPoints.Reset();
		ValidatedMaterialPoints.Reset();
		ValidatedDistortionPoints.Reset();
		// 材質payloadを記録するか。
		const bool bMaterialEnabled = Emitter.Material.bEnabled;
		// 材質付き経路だけで保存済みの歪み設定を読む。
		const FEffectUVDistortionDefinition& Distortion = Emitter.Material.Distortion;
		// 材質と歪みの両方が有効な経路か。
		const bool bDistortionEnabled = bMaterialEnabled && Distortion.bEnabled;
		// 全点で強さが0ならnoiseなしの旧命令を使う。
		bool bAnyDistortionStrength = false;
		// 全chunk共通の帯先頭秒齢。
		const double AgeReference = !Strip.Points.IsEmpty() && NMath::IsFinite( Strip.Points[0].AgeSeconds ) ? Strip.Points[0].AgeSeconds : 0.0;
		// 距離方向の秒ごとの画像移動量。
		const double ScrollU = Emitter.Material.UVScroll.x;
		// 幅方向の秒ごとの画像移動量。
		const double ScrollV = Emitter.Material.UVScroll.y;
		// 剰余済みの基準横位相。
		const double BaseU = NMath::IsFinite( ScrollU ) ? NMath::Fmod( AgeReference * ScrollU, 1.0 ) : 0.0;
		// 剰余済みの基準縦位相。
		const double BaseV = NMath::IsFinite( ScrollV ) ? NMath::Fmod( AgeReference * ScrollV, 1.0 ) : 0.0;
		if ( bMaterialEnabled )
		{
			if ( Texture < 0 || MaskTexture < -1 || !NMath::IsFinite( AgeReference ) || !NMath::IsFinite( ScrollU ) || !NMath::IsFinite( ScrollV ) || !NMath::IsFinite( Emitter.Path.UVRepeat ) || Emitter.Path.UVRepeat <= 0.0f || !NMath::IsFinite( Emitter.Material.UVScale.x ) || !NMath::IsFinite( Emitter.Material.UVScale.y ) || Emitter.Material.UVScale.x <= 0.0f || Emitter.Material.UVScale.y <= 0.0f || Emitter.Material.UVScale.x > 128.0f || Emitter.Material.UVScale.y > 128.0f || !NMath::IsFinite( Emitter.Material.MaskScale ) || Emitter.Material.MaskScale <= 0.0f || Emitter.Material.MaskScale > 128.0f || !NMath::IsFinite( Emitter.Material.EdgeWidth ) || Emitter.Material.EdgeWidth < 0.0f || Emitter.Material.EdgeWidth > 1.0f || !NMath::IsFinite( Emitter.Material.EdgeEmission ) || Emitter.Material.EdgeEmission < 0.0f || Emitter.Material.EdgeEmission > 32.0f ) return;
			// 一つ前に検査した累積距離。
			float32 PreviousU = 0.0f;
			for ( int32 Index = 0; Index < Strip.Points.Num(); ++Index )
			{
				// 検査する元の経路点。
				const FEffectPathGeometry::FPoint& Source = Strip.Points[Index];
				if ( !IsFinitePathPosition( Source.Position ) || !NMath::IsFinite( Source.Width ) || Source.Width < 0.0f || !NMath::IsFinite( Source.U ) || Source.U < 0.0f || Source.U < PreviousU || !NMath::IsFinite( Source.AgeSeconds ) || !NMath::IsFinite( Source.NormalizedAge ) ) return;
				// paddingも初期化してshaderへ渡す検証済みの一点。
				FDrawMaterialRibbonData::FPoint MaterialPoint{};
				MaterialPoint.Path.Pos[0] = Source.Position.x;
				MaterialPoint.Path.Pos[1] = Source.Position.y;
				MaterialPoint.Path.Pos[2] = Source.Position.z;
				MaterialPoint.Path.Width = NMath::Max( Source.Width * WidthScale, 0.0f );
				MaterialPoint.Path.U = Source.U;
				// 描画側と同じ精度で計算する累積画像座標。
				const float32 RibbonU = Source.U * Emitter.Path.UVRepeat;
				// 材質shaderへ渡す未加工色。
				FColor32 RawColor = Source.Color;
				RawColor.a = static_cast<uint8>( RawColor.a * NMath::Clamp( Brightness, 0.0f, 1.0f ) );
				MaterialPoint.Path.Color = PackPathColor( RawColor, EParticleBlend::Alpha );
				// 粒子の寿命割合。
				const float32 CurveAge = NMath::Clamp( Source.NormalizedAge, 0.0f, 1.0f );
				// この点の消失曲線値。
				const float32 Dissolve = Emitter.Material.Dissolve.Evaluate( CurveAge, 0.0f );
				// この点の発光曲線値。
				const float32 Emission = Emitter.Material.Emission.Evaluate( CurveAge, 1.0f );
				MaterialPoint.Dissolve = NMath::IsFinite( Dissolve ) ? NMath::Clamp( Dissolve, 0.0f, 1.0f ) : 0.0f;
				MaterialPoint.Emission = NMath::IsFinite( Emission ) ? NMath::Clamp( Emission, 0.0f, 32.0f ) : 1.0f;
				// 剰余前の連続した秒齢差。
				const double AgeDelta = static_cast<double>( Source.AgeSeconds ) - AgeReference;
				// 剰余前の横方向移動量。
				const double OffsetU = BaseU + AgeDelta * ScrollU;
				// 剰余前の縦方向移動量。
				const double OffsetV = BaseV + AgeDelta * ScrollV;
				if ( !NMath::IsFinite( OffsetU ) || !NMath::IsFinite( OffsetV ) || NMath::Abs( OffsetU ) > 1.0e6 || NMath::Abs( OffsetV ) > 1.0e6 ) return;
				MaterialPoint.UVOffset[0] = static_cast<float32>( OffsetU );
				MaterialPoint.UVOffset[1] = static_cast<float32>( OffsetV );
				// 描画側と同じfloat順序で求める表示座標。
				const float32 ScaledTextureU = RibbonU * Emitter.Material.UVScale.x + MaterialPoint.UVOffset[0];
				// 描画側で使うmask座標。
				const float32 ScaledMaskU = RibbonU * Emitter.Material.MaskScale;
				if ( !NMath::IsFinite( MaterialPoint.Path.Width ) || !NMath::IsFinite( RibbonU ) || !NMath::IsFinite( ScaledTextureU ) || !NMath::IsFinite( ScaledMaskU ) || !NMath::IsFinite( Dissolve ) || !NMath::IsFinite( Emission ) || !NMath::IsFinite( MaterialPoint.UVOffset[0] ) || !NMath::IsFinite( MaterialPoint.UVOffset[1] ) ) return;
				ValidatedMaterialPoints.Add( MaterialPoint );
				if ( bDistortionEnabled )
				{
					if ( !Distortion.IsValidSettings() || !NMath::IsFinite( Source.AgeSeconds ) || !NMath::IsFinite( Source.NormalizedAge ) ) return;
					// 元粒子の寿命割合でnoise強度を評価する。
					const float32 Strength = Distortion.Strength.Evaluate( CurveAge, 0.05f );
					if ( !NMath::IsFinite( Strength ) || Strength < 0.0f || Strength > 1.0f ) return;
					// 最初の点だけ秒齢を剰余し、後続点は連続差を足す。
					const double NoiseBaseU = NMath::Fmod( AgeReference * static_cast<double>( Distortion.UVScroll.x ), 1.0 );
					const double NoiseBaseV = NMath::Fmod( AgeReference * static_cast<double>( Distortion.UVScroll.y ), 1.0 );
					const double NoiseOffsetU = NoiseBaseU + AgeDelta * static_cast<double>( Distortion.UVScroll.x );
					const double NoiseOffsetV = NoiseBaseV + AgeDelta * static_cast<double>( Distortion.UVScroll.y );
					if ( !NMath::IsFinite( NoiseOffsetU ) || !NMath::IsFinite( NoiseOffsetV ) || NMath::Abs( NoiseOffsetU ) > 1.0e6 || NMath::Abs( NoiseOffsetV ) > 1.0e6 ) return;
					// 次の描画命令へ渡す検証済みノイズ付き一点。
					FDrawDistortedMaterialRibbonData::FPoint DistortionPoint{};
					DistortionPoint.Material = MaterialPoint;
					DistortionPoint.Strength = Strength;
					DistortionPoint.UVOffset[0] = static_cast<float32>( NoiseOffsetU );
					DistortionPoint.UVOffset[1] = static_cast<float32>( NoiseOffsetV );
					if ( !NMath::IsFinite( DistortionPoint.UVOffset[0] ) || !NMath::IsFinite( DistortionPoint.UVOffset[1] ) ) return;
					ValidatedDistortionPoints.Add( DistortionPoint );
					bAnyDistortionStrength |= Strength > 0.0f;
				}
				PreviousU = Source.U;
			}
			// 正の強さが一つでもあれば、noiseなしの帯は一部も記録しない。
			if ( bAnyDistortionStrength && ( DistortionTexture < 0 || !Distortion.IsValidSettings() ) ) return;
			// DrawContextが各chunkで拒否する画像反復数も、記録を始める前に全て数える。
			int32 CheckedStart = 0;
			while ( CheckedStart + 1 < Strip.Points.Num() )
			{
				// 前側のghostを含む開始位置。
				const int32 CheckedFirst = CheckedStart > 0 ? CheckedStart - 1 : 0;
				// ここから残る点数。
				const int32 CheckedRemaining = Strip.Points.Num() - CheckedFirst;
				// このchunkの可視終点。
				const int32 CheckedEnd = CheckedRemaining <= FDrawRibbonData::MaxPoints ? Strip.Points.Num() - 1 : CheckedFirst + FDrawRibbonData::MaxPoints - 2;
				// 後側ghostを含む終点。
				const int32 CheckedLast = NMath::Min( CheckedEnd + 1, Strip.Points.Num() - 1 );
				// 接線だけに使う端点。
				const int32 CheckedJoinFlags = ( CheckedFirst < CheckedStart ? FDrawRibbonData::PreviousPoint : 0 ) | ( CheckedLast > CheckedEnd ? FDrawRibbonData::NextPoint : 0 );
				// このchunkが使う画像反復区間数。
				int32 TextureSections = 0;
				for ( int32 Index = CheckedFirst; Index <= CheckedLast; ++Index )
				{
					// DrawContextから見た点番号。
					const int32 LocalIndex = Index - CheckedFirst;
					// ghostだけで作る区間を除外する。
					const bool bVisibleSegment = LocalIndex > 0 && !( LocalIndex == 1 && ( CheckedJoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( LocalIndex + 1 == CheckedLast - CheckedFirst + 1 && ( CheckedJoinFlags & FDrawRibbonData::NextPoint ) != 0 );
					if ( !bVisibleSegment ) continue;
					// 区間の画像始点。
					const float32 PreviousU = Strip.Points[Index - 1].U * Emitter.Path.UVRepeat;
					// 区間の画像終点。
					const float32 CurrentU = Strip.Points[Index].U * Emitter.Path.UVRepeat;
					// 始点の反復番号。
					const double FirstRepeat = NMath::Floor( static_cast<double>( PreviousU ) );
					// GPU側と同じ分割数。
					const double Sections = NMath::Max( 1.0, NMath::Ceil( static_cast<double>( CurrentU ) ) - FirstRepeat );
					if ( !NMath::IsFinite( Sections ) || Sections > static_cast<double>( FDrawRibbonData::MaxTextureSections - TextureSections ) ) return;
					TextureSections += static_cast<int32>( Sections );
				}
				CheckedStart = CheckedEnd;
			}
		}
		// 描く区間の開始点。前後の隣接点を渡し、命令を分けても曲がり角を同じ形にする。
		int32 VisibleStart = 0;
		while ( VisibleStart + 1 < Strip.Points.Num() )
		{
			// 接線の計算にだけ使う、前側の一点も含めた範囲。
			const int32 First = VisibleStart > 0 ? VisibleStart - 1 : 0;
			// 後側の接線用に一点分の空きを残した、描く区間の終点。
			// 前側の補助点から末尾までに残る、記録可能な点数。
			const int32 RemainingPoints = Strip.Points.Num() - First;
			const int32 VisibleEnd = RemainingPoints <= FDrawRibbonData::MaxPoints ? Strip.Points.Num() - 1 : First + FDrawRibbonData::MaxPoints - 2;
			// 後側の接線用の点までを含めた、記録範囲の終点。
			const int32 Last = NMath::Min( VisibleEnd + 1, Strip.Points.Num() - 1 );
			// 補助点の区間は描かず、接続部分の形の計算だけへ使う。
			const int32 JoinFlags = ( First < VisibleStart ? FDrawRibbonData::PreviousPoint : 0 ) | ( Last > VisibleEnd ? FDrawRibbonData::NextPoint : 0 );
			Points.Reset();
			MaterialPoints.Reset();
			for ( int32 Index = First; Index <= Last; ++Index )
			{
				// 純粋な計算側が作った、位置と幅と色。
				const FEffectPathGeometry::FPoint& Source = Strip.Points[Index];
				if ( Emitter.Material.bEnabled )
				{
					// 共有endpointとghostは全stripで検証済みの値をそのまま使う。
					MaterialPoints.Add( ValidatedMaterialPoints[Index] );
				}
				else
				{
					if ( !IsFinitePathPosition( Source.Position ) || !NMath::IsFinite( Source.Width ) ) return;
					// 従来帯の点と同じ幅・色変換を維持する。
					FDrawRibbonData::FPoint Point;
					Point.Pos[0] = Source.Position.x;
					Point.Pos[1] = Source.Position.y;
					Point.Pos[2] = Source.Position.z;
					Point.Width = NMath::Max( Source.Width * WidthScale, 0.0f );
					Point.U = Source.U;
					Point.Color = PackPathColor( Source.Color, Emitter.Blend, Brightness );
					Points.Add( Point );
				}
			}
			if ( Emitter.Material.bEnabled )
			{
				// 帯全体の材質設定。Lightning の芯では境界発光だけを止める。
				FDrawMaterialRibbonData Data;
				Data.Ribbon.Texture = Texture;
				Data.Ribbon.NumPoints = MaterialPoints.Num();
				Data.Ribbon.UVRepeat = Emitter.Path.UVRepeat;
				Data.Ribbon.JoinFlags = JoinFlags;
				Data.Material.MaskTexture = MaskTexture;
				Data.Material.UVScale[0] = Emitter.Material.UVScale.x;
				Data.Material.UVScale[1] = Emitter.Material.UVScale.y;
				Data.Material.MaskScale = Emitter.Material.MaskScale;
				Data.Material.EdgeWidth = Emitter.Material.EdgeWidth;
				Data.Material.EdgeEmission = Emitter.Material.EdgeEmission * EdgeScale;
				Data.Material.EdgeColor = { Emitter.Material.EdgeColor.r, Emitter.Material.EdgeColor.g, Emitter.Material.EdgeColor.b, Emitter.Material.EdgeColor.a };
				Data.Material.bDissolve = Emitter.Material.bDissolve ? 1 : 0;
				if ( bAnyDistortionStrength )
				{
					// 既存材質設定とノイズ設定をまとめた帯命令。
					FDrawDistortedMaterialRibbonData DistortedData;
					DistortedData.Material = Data;
					DistortedData.DistortionTexture = DistortionTexture;
					DistortedData.UVScale[0] = Distortion.UVScale.x;
					DistortedData.UVScale[1] = Distortion.UVScale.y;
					// 現chunk用の検証済み点payload。
					thread_local TArray<FDrawDistortedMaterialRibbonData::FPoint> ChunkDistortionPoints;
					ChunkDistortionPoints.Reset();
					for ( int32 Index = First; Index <= Last; ++Index ) ChunkDistortionPoints.Add( ValidatedDistortionPoints[Index] );
					Draw.DistortedMaterialRibbon3D( DistortedData, ChunkDistortionPoints.GetData() );
				}
				else Draw.MaterialRibbon3D( Data, MaterialPoints.GetData() );
			}
			else Draw.Ribbon3D( Points.GetData(), Points.Num(), Texture, Emitter.Path.UVRepeat, JoinFlags );
			VisibleStart = VisibleEnd;
		}
	}

	// 同じ発生元の粒子を生まれた順に結ぶ。瞬間移動や消えた区間は別の帯にする。
	void RecordRibbon( FDrawContext& Draw, const TArray<NParticle::FDrawParticle>& Particles, int32 EmitterIndex, const FParticleEmitterDefinition& Emitter, int32 Texture, int32 MaskTexture, int32 DistortionTexture, float32 Scale )
	{
		// このスレッドで組み立てている、一本の連続した帯。
		thread_local FEffectPathGeometry::FStrip Strip;
		Strip.Points.Reset();
		// 設定した長さを超えた飛びは、帯を伸ばさず分断する。
		const float32 BreakDistance = NMath::Max( Emitter.Path.BreakDistance * Scale, 0.001f );
		for ( const NParticle::FDrawParticle& Particle : Particles )
		{
			if ( Particle.Emitter != EmitterIndex ) continue;
			if ( !IsFinitePathPosition( Particle.Position ) || Particle.Size <= 0.0f || Particle.Color.a == 0 )
			{
				RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture );
				Strip.Points.Reset();
				continue;
			}
			if ( !Strip.Points.IsEmpty() && ( Particle.Position - Strip.Points.Last().Position ).Length() > BreakDistance )
			{
				RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture );
				Strip.Points.Reset();
			}
			// 点が同じ場所なら、ゼロ面積の節を増やさず最新の幅と色を使う。
			FEffectPathGeometry::FPoint Point;
			Point.Position = Particle.Position;
			Point.Width = Particle.Size;
			Point.Color = Particle.Color;
			Point.AgeSeconds = Particle.Age;
			Point.NormalizedAge = Particle.NormalizedAge;
			if ( !Strip.Points.IsEmpty() ) Point.U = Strip.Points.Last().U + ( Point.Position - Strip.Points.Last().Position ).Length();
			if ( !Strip.Points.IsEmpty() && ( Point.Position - Strip.Points.Last().Position ).LengthSquared() < 1.0e-10f ) Strip.Points.Last() = Point;
			else Strip.Points.Add( Point );
		}
		RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture );
		Strip.Points.Reset();
	}

	// 主幹と枝を記録する。細い芯と淡い外側を重ね、帯の太さを画面で読みやすくする。
	void RecordLightning( FDrawContext& Draw, const FParticleSimulation& Simulation, const TArray<NParticle::FDrawParticle>& Particles, int32 EmitterIndex, int32 Texture, int32 MaskTexture, int32 DistortionTexture, uint32 Seed )
	{
		// エミッターごとの雷の形と再生の変換。
		const FParticleEmitterDefinition& Emitter = Simulation.Definition->Emitters[EmitterIndex];
		if ( Emitter.Path.HasInvalidUseTarget() ) return;
		// 定義と再生状態の両方で雷の動的終点が有効か。
		const bool bUseDynamicTarget = Emitter.Path.bUseTarget && Simulation.bHasTarget;
		if ( bUseDynamicTarget && !IsFinitePathPosition( Simulation.TargetPosition ) ) return;
		// シードから作る経路。描画の記録は共有状態を変えない。
		thread_local FEffectPathGeometry Geometry;
		// 終点の指定はエフェクトのローカル座標で、実際の長さに直してから計算する。
		const FVector3 EndOffset = Simulation.Transform.Rotation.Rotate( Emitter.Path.End * NParticle::GetEffectScale( Simulation ) );
		// 一度に描く雷の本数を制限し、過剰な定義でも記録量を抑える。
		int32 Count = 0;
		for ( const NParticle::FDrawParticle& Particle : Particles )
		{
			if ( Particle.Emitter != EmitterIndex || Particle.Size <= 0.0f || Particle.Color.a == 0 ) continue;
			if ( ++Count > 64 ) break;
			// この粒子から伸ばす雷のワールド終点座標。
			const FVector3 TargetWorldEnd = bUseDynamicTarget ? Simulation.TargetPosition : ( Particle.Position + EndOffset );
			NEffectPath::BuildLightning( Emitter.Path, Seed + static_cast<uint32>( EmitterIndex ) * 40503u + Particle.Serial * 7919u, Particle.Age, Particle.Position, TargetWorldEnd, Particle.Size, Particle.Color, Geometry );
			for ( FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) for ( FEffectPathGeometry::FPoint& Point : Strip.Points ) { Point.AgeSeconds = Particle.Age; Point.NormalizedAge = Particle.NormalizedAge; }
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture, 3.0f, 0.18f, Emitter.Material.bEnabled ? 1.0f : 0.0f );
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture, 1.0f, 1.0f, 0.0f );
		}
	}

	// Tree Lightningの描画対象を、エミッター順・生成順で先頭4粒子に固定する識別値。
	struct FTreeLightningParticleKey
	{
		// 粒子を所有するエミッター番号。
		int32 Emitter = INDEX_NONE;
		// 同じエミッター内で粒子を識別する生成番号。
		uint32 Serial = 0;
	};

	// 全Tree Lightningエミッター共通の上限を適用し、成長・明滅の形を材質帯へ記録する。
	void RecordTreeLightning( FDrawContext& Draw, const FParticleSimulation& Simulation, const TArray<NParticle::FDrawParticle>& Particles, const TArray<FTreeLightningParticleKey>& Selected, int32 EmitterIndex, int32 Texture, int32 MaskTexture, int32 DistortionTexture, uint32 Seed )
	{
		// このエミッターが持つ固定形状と表示設定。
		const FParticleEmitterDefinition& Emitter = Simulation.Definition->Emitters[EmitterIndex];
		if ( Emitter.Path.HasInvalidUseTarget() ) return;
		// 定義と再生状態の両方で樹状雷の動的終点が有効か。
		const bool bUseDynamicTreeTarget = Emitter.Path.bUseTarget && Simulation.bHasTarget;
		if ( bUseDynamicTreeTarget && !IsFinitePathPosition( Simulation.TargetPosition ) ) return;
		// エフェクト空間で指定された終点をワールド方向へ変換する。
		const FVector3 EndOffset = Simulation.Transform.Rotation.Rotate( Emitter.Path.End * NParticle::GetEffectScale( Simulation ) );
		// Tree Lightningが作る全幹・枝の作業領域。
		thread_local FEffectPathGeometry Geometry;
		for ( const NParticle::FDrawParticle& Particle : Particles )
		{
			if ( Particle.Emitter != EmitterIndex ) continue;
			// 先頭4本の固定選択に入らない粒子は、明滅で空いた枠へ後から入れない。
			bool bSelected = false;
			for ( const FTreeLightningParticleKey& Key : Selected ) if ( Key.Emitter == Particle.Emitter && Key.Serial == Particle.Serial ) { bSelected = true; break; }
			if ( !bSelected || Particle.Size <= 0.0f || Particle.Color.a == 0 ) continue;
			// この粒子から伸ばす樹状雷のワールド終点座標。
			const FVector3 TreeTargetWorldEnd = bUseDynamicTreeTarget ? Simulation.TargetPosition : ( Particle.Position + EndOffset );
			NEffectPath::BuildTreeLightning( Emitter.Path, Seed + static_cast<uint32>( EmitterIndex ) * 40503u + Particle.Serial * 7919u, Particle.Age, Particle.Position, TreeTargetWorldEnd, Particle.Size, Particle.Color, Geometry );
			// 全点で元粒子の秒齢と寿命割合を共有し、子枝だけ曲線時刻が変わらないようにする。
			for ( FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) for ( FEffectPathGeometry::FPoint& Point : Strip.Points ) { Point.AgeSeconds = Particle.Age; Point.NormalizedAge = Particle.NormalizedAge; }
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture, 3.0f, 0.18f, Emitter.Material.bEnabled ? 1.0f : 0.0f );
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) RecordPathStrip( Draw, Strip, Texture, MaskTexture, Emitter, DistortionTexture, 1.0f, 1.0f, 0.0f );
		}
	}
}

//=============================================================================
// 組み込みの丸の画像
//   中心が 1、ふちが 0 の、なめらかな落ち方 (1 - r)^2 の形のアルファ。色は白。TGA（32bit、左上から）。
//   アルファ・加算のどちらも、画像のアルファが効く（DxLib の加算も、画像のアルファを掛ける: ピクセルの確認で分かったこと）。
//=============================================================================
TArray<uint8> NEffect::MakeSoftDotImage()
{
	TArray<uint8> Bytes;
	const auto Put8 = [&]( uint32 Value ) { Bytes.Add( static_cast<uint8>( Value & 0xFFu ) ); };
	const auto Put16 = [&]( uint32 Value )
	{
		Put8( Value );
		Put8( Value >> 8 );
	};
	// ヘッダー 18 バイト: ID 長 0 / カラーマップなし / 無圧縮のカラー(2) / カラーマップ指定 5 バイト(0) / 原点 (0, 0) / 幅 / 高さ / 32bit / 記述子（アルファ 8 ビット + 上から下）
	Put8( 0 );
	Put8( 0 );
	Put8( 2 );
	for ( int32 i = 0; i < 5; ++i ) Put8( 0 );
	Put16( 0 );
	Put16( 0 );
	Put16( SoftDotSize );
	Put16( SoftDotSize );
	Put8( 32 );
	Put8( 8 | 0x20 );
	const float32 Center = static_cast<float32>( SoftDotSize - 1 ) * 0.5f;
	for ( int32 Y = 0; Y < SoftDotSize; ++Y )
	{
		for ( int32 X = 0; X < SoftDotSize; ++X )
		{
			const float32 Dx = ( static_cast<float32>( X ) - Center ) / ( Center + 0.5f );
			const float32 Dy = ( static_cast<float32>( Y ) - Center ) / ( Center + 0.5f );
			const float32 Distance = NMath::Sqrt( Dx * Dx + Dy * Dy );
			const float32 Falloff = Distance >= 1.0f ? 0.0f : ( 1.0f - Distance ) * ( 1.0f - Distance );
			const uint32 Alpha = static_cast<uint32>( std::lround( Falloff * 255.0f ) );
			Put8( 255 );     // B
			Put8( 255 );     // G
			Put8( 255 );     // R
			Put8( Alpha );   // A
		}
	}
	return Bytes;
}

TArray<uint8> NEffect::MakeRibbonImage()
{
	// 幅方向だけが柔らかく消え、長さ方向へ繰り返しても継ぎ目が出ない TGA。
	TArray<uint8> Bytes;
	Bytes.SetNum( 18 + 64 * 2 * 4 );
	FMemory::Memset( Bytes.GetData(), 0, static_cast<usize>( Bytes.Num() ) );
	Bytes[2] = 2;
	Bytes[12] = 2;
	Bytes[14] = 64;
	Bytes[16] = 32;
	Bytes[17] = 8 | 0x20;
	for ( int32 Y = 0; Y < 64; ++Y )
	{
		for ( int32 X = 0; X < 2; ++X )
		{
			// 画像の縦方向が帯の幅に対応する。横は経路に沿って繰り返す。
			const float32 Across = NMath::Abs( ( Y + 0.5f ) / 32.0f - 1.0f );
			// 色の三成分と不透明度の先頭。
			const int32 Offset = 18 + ( Y * 2 + X ) * 4;
			Bytes[Offset] = Bytes[Offset + 1] = Bytes[Offset + 2] = 255;
			Bytes[Offset + 3] = static_cast<uint8>( std::lround( NMath::Pow( 1.0f - Across, 0.7f ) * 255.0f ) );
		}
	}
	return Bytes;
}

//=============================================================================
// FParticleEffectBackend
//=============================================================================
bool FParticleEffectBackend::CanPlay( const FString& Path ) const
{
	return Path.EndsWith( ".effect.json", ESearchCase::IgnoreCase );
}

TSharedPtr<IEffectPlayback> FParticleEffectBackend::Play( const FString& Path, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World )
{
	if ( Assets == nullptr || !CanPlay( Path ) ) return nullptr;
	const TSharedPtr<CEffectAsset> Asset = Assets->LoadEffect( Path );
	if ( !Asset.IsValid() ) return nullptr;

	const TSharedPtr<const FParticleEffectDefinition>& Definition = Asset->GetDefinitionShared();
	return NEffect::CreateParticlePlayback( Definition, Assets, Params, World, Path );
}

TSharedPtr<IEffectPlayback> NEffect::CreateParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World, const FString& Label )
{
	if ( Assets == nullptr || !Definition.IsValid() ) return nullptr;
	// エミッターごとの従来画像を再生中保持する配列。
	TArray<TSharedPtr<CTexture>> Textures;
	// 材質を使うエミッターの消失マスクを保持する配列。
	TArray<TSharedPtr<CTexture>> MaskTextures;
	// コマ送りの時間軸と、再生開始時に作ったregionを保持する配列。
	TArray<FEffectFlipbookFrames> FlipbookFrames;
	// 材質付き四角形・帯・雷が使う歪み画像を再生期間保持する配列。
	TArray<TSharedPtr<CTexture>> DistortionTextures;
	for ( const FParticleEmitterDefinition& Emitter : Definition->Emitters )
	{
		// このmesh定義が再生時に形状として使えるか。
		const bool bMeshCompatible = Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && Emitter.Mesh.IsValidSettings() && Emitter.Path.Kind == FEffectPathDefinition::EKind::None && !Emitter.Flipbook.bEnabled;
		// meshが無効または他経路と競合するときは不要な画像を読まない。
		const bool bMeshDefinitionCompatible = Emitter.Mesh.IsValidSettings() && ( Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::None || bMeshCompatible );
		// Flipbook非対応経路にも使う従来texture。
		TSharedPtr<CTexture> Texture;
		// spriteに有効なコマ送り設定があるか。
		const bool bFlipbookForQuad = bMeshDefinitionCompatible && Emitter.Path.Kind == FEffectPathDefinition::EKind::None && Emitter.Flipbook.bEnabled;
		if ( bMeshDefinitionCompatible && !bFlipbookForQuad && !Emitter.Texture.IsEmpty() )
		{
			Texture = Assets->LoadTexture( Emitter.Texture );
			if ( !Texture.IsValid() ) LOG( LogAsset, Warning, "Effect '%s': the image '%s' of emitter '%s' could not be loaded (a soft dot is used)", *Label, *Emitter.Texture, *Emitter.Name );
		}
		if ( bMeshDefinitionCompatible && !bFlipbookForQuad && !Texture.IsValid() && Emitter.Path.Kind == FEffectPathDefinition::EKind::None ) Texture = GetSoftDot( Assets );
		if ( bMeshDefinitionCompatible && !Texture.IsValid() && Emitter.Path.Kind != FEffectPathDefinition::EKind::None )
		{
			// 帯の進行方向には途切れず、幅だけが柔らかく消える画像。
			const TArray<uint8> Image = NEffect::MakeRibbonImage();
			Texture = Assets->CreateTextureFromMemory( "effect:ribbon", Image.GetData(), static_cast<usize>( Image.Num() ) );
		}
		Textures.Add( Texture );
		// Flipbook使用時だけ作る、再生単位の固定画像列。
		FEffectFlipbookFrames PreparedFrames;
		if ( bFlipbookForQuad )
		{
			// 画像作成に失敗した理由をログへ渡す文字列。
			FString Error;
			if ( !NEffectFlipbook::BuildFrames( Emitter.Flipbook, *Assets, PreparedFrames, &Error ) ) LOG( LogAsset, Warning, "エフェクト '%s' のエミッター '%s' のコマ送り画像を準備できません。四角形は描画しません (%s)", *Label, *Emitter.Name, *Error );
		}
		FlipbookFrames.Add( PreparedFrames );
		// マスクを再生期間保持する材質用texture。
		TSharedPtr<CTexture> MaskTexture;
		if ( bMeshDefinitionCompatible && Emitter.Material.bEnabled && !Emitter.Material.MaskTexture.IsEmpty() )
		{
			MaskTexture = Assets->LoadTexture( Emitter.Material.MaskTexture );
			if ( !MaskTexture.IsValid() ) LOG( LogAsset, Warning, "エフェクト '%s' のエミッター '%s' の材質マスク画像 '%s' を読み込めません。組み込みマスクを使います", *Label, *Emitter.Name, *Emitter.Material.MaskTexture );
		}
		MaskTextures.Add( MaskTexture );
		// 材質が有効な対応経路だけ、正の強さで必要なゆがみ画像を起動時に読む。
		TSharedPtr<CTexture> DistortionTexture;
		// 対応pathの材質でnoise画像が必要な条件か。
		const bool bDistortionForPath = bMeshDefinitionCompatible && ( Emitter.Path.Kind == FEffectPathDefinition::EKind::None || Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon || Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning || Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ) && Emitter.Material.bEnabled && NeedsDistortionTexture( Emitter.Material.Distortion );
		if ( bDistortionForPath )
		{
			if ( Emitter.Material.Distortion.IsValidSettings() ) DistortionTexture = Assets->LoadTexture( Emitter.Material.Distortion.Texture );
			if ( DistortionTexture.IsValid() && !DistortionTexture->IsReady() ) DistortionTexture.Reset();
		}
		DistortionTextures.Add( DistortionTexture );
	}
	return MakeShared<FParticlePlayback>( Definition, Textures, Params, World, MaskTextures, FlipbookFrames, DistortionTextures );
}

//=============================================================================
// FParticlePlayback
//=============================================================================
FParticlePlayback::FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World )
	: FParticlePlayback( Definition, InTextures, Params, World, TArray<TSharedPtr<CTexture>>() )
{
}

FParticlePlayback::FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures )
	: FParticlePlayback( Definition, InTextures, Params, World, InMaskTextures, TArray<FEffectFlipbookFrames>() )
{
}

FParticlePlayback::FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures, const TArray<FEffectFlipbookFrames>& InFlipbookFrames )
	: FParticlePlayback( Definition, InTextures, Params, World, InMaskTextures, InFlipbookFrames, TArray<TSharedPtr<CTexture>>() )
{
}

FParticlePlayback::FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures, const TArray<FEffectFlipbookFrames>& InFlipbookFrames, const TArray<TSharedPtr<CTexture>>& InDistortionTextures )
	: Textures( InTextures ), MaskTextures( InMaskTextures ), FlipbookFrames( InFlipbookFrames ), DistortionTextures( InDistortionTextures ), PathSeed( Params.Seed != 0 ? Params.Seed : ( Definition.IsValid() ? Definition->Seed : 1u ) )
{
	Simulation.Transform = World;
	// 再生開始時点で準備済みの画像だけを固定し、後からreadyになっても採用しない。
	for ( TSharedPtr<CTexture>& Texture : DistortionTextures ) if ( Texture.IsValid() && !Texture->IsReady() ) Texture.Reset();
	TSharedPtr<const FParticleEffectDefinition> Snapshot; // 再生中に呼び出し側の編集を混ぜない定義の写し。
	if ( Definition.IsValid() ) Snapshot = MakeShared<FParticleEffectDefinition>( *Definition );
	NParticle::Start( Simulation, Snapshot, Params.Seed, Params.Scale, Params.Speed, Params.Loop );
	MeshMaskHandles.Reserve( Snapshot.IsValid() ? Snapshot->Emitters.Num() : 0 );
	MeshTextureHandles.Reserve( Snapshot.IsValid() ? Snapshot->Emitters.Num() : 0 );
	if ( Snapshot.IsValid() ) for ( int32 Index = 0; Index < Snapshot->Emitters.Num(); ++Index )
	{
		// Mesh以外のemitterへ追加ready照会を行わない。
		const FParticleEmitterDefinition& Emitter = Snapshot->Emitters[Index];
		const bool bMeshCompatible = Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && Emitter.Mesh.IsValidSettings() && Emitter.Path.Kind == FEffectPathDefinition::EKind::None && !Emitter.Flipbook.bEnabled;
		const TSharedPtr<CTexture>& MaskTexture = MaskTextures.IsValidIndex( Index ) ? MaskTextures[Index] : TSharedPtr<CTexture>();
		const TSharedPtr<CTexture>& MainTexture = Textures.IsValidIndex( Index ) ? Textures[Index] : TSharedPtr<CTexture>();
		MeshMaskHandles.Add( bMeshCompatible && MaskTexture.IsValid() && MaskTexture->IsReady() ? MaskTexture->GetHandle() : -1 );
		MeshTextureHandles.Add( bMeshCompatible && MainTexture.IsValid() && MainTexture->IsReady() ? MainTexture->GetHandle() : -1 );
	}
	if ( Snapshot.IsValid() )
	{
		MeshGeometries.Reserve( Snapshot->Emitters.Num() );
		for ( const FParticleEmitterDefinition& Emitter : Snapshot->Emitters )
		{
			// public constructor経由でも再生開始時にmeshを固定する。
			FEffectMeshGeometry Geometry;
			if ( Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && Emitter.Mesh.IsValidSettings() && Emitter.Path.Kind == FEffectPathDefinition::EKind::None && !Emitter.Flipbook.bEnabled ) NEffectMesh::BuildGeometry( Emitter.Mesh, Geometry );
			MeshGeometries.Add( MoveTemp( Geometry ) );
		}
	}
	if ( Snapshot.IsValid() ) for ( int32 Index = 0; Index < Snapshot->Emitters.Num(); ++Index )
	{
		// 正の強さがありながら、再生開始時に画像を持てなかった対応材質。
		const FParticleEmitterDefinition& Emitter = Snapshot->Emitters[Index];
		// このpathと材質でnoiseが必要か。
		const bool bMeshDefinitionCompatible = Emitter.Mesh.IsValidSettings() && ( Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::None || ( Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None && Emitter.Path.Kind == FEffectPathDefinition::EKind::None && !Emitter.Flipbook.bEnabled ) );
		const bool bDistortionPath = bMeshDefinitionCompatible && ( Emitter.Path.Kind == FEffectPathDefinition::EKind::None || Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon || Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning || Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning );
		// 正のStrength設定があり対応pathを使うか。
		const bool bNeedsDistortion = bDistortionPath && Emitter.Material.bEnabled && NeedsDistortionTexture( Emitter.Material.Distortion );
		// 再生snapshotが有効なnoise handleを持つか。
		const bool bHasDistortionTexture = DistortionTextures.IsValidIndex( Index ) && DistortionTextures[Index].IsValid() && DistortionTextures[Index]->GetHandle() >= 0;
		if ( bNeedsDistortion && !bHasDistortionTexture ) LOG( LogAsset, Warning, "エミッター '%s' のUVゆがみ画像を準備できません。ゆがみが必要な粒子は描画しません", *Emitter.Name );
	}
}

void FParticlePlayback::SetTargetPosition( const FVector3& WorldTarget )
{
	Simulation.TargetPosition = WorldTarget;
	Simulation.bHasTarget = true;
}

void FParticlePlayback::ClearTargetPosition()
{
	Simulation.TargetPosition = FVector3::Zero;
	Simulation.bHasTarget = false;
}

void FParticlePlayback::Record( FDrawContext& Draw ) const
{
	if ( !Simulation.Definition.IsValid() || Simulation.GetNumParticles() == 0 ) return;

	// 描画システムから並列に呼ばれるので、作業用の置き場は、スレッドごとに持つ
	thread_local TArray<NParticle::FDrawParticle> Particles;
	NParticle::CollectDrawParticles( Simulation, Particles );

	const FQuaternion& Rotation = Simulation.Transform.Rotation;
	const FVector3 FixedRight = Rotation.Rotate( FVector3::Right );
	const FVector3 FixedUp = Rotation.Rotate( FVector3::Up );

	// 全Tree Lightningの固定表示枠。alphaや成長状態を見る前に生成順で決める。
	thread_local TArray<FTreeLightningParticleKey> TreeLightningSelection;
	TreeLightningSelection.Reset();
	for ( const NParticle::FDrawParticle& Particle : Particles ) if ( Simulation.Definition->Emitters.IsValidIndex( Particle.Emitter ) && Simulation.Definition->Emitters[Particle.Emitter].Path.Kind == FEffectPathDefinition::EKind::TreeLightning )
	{
		if ( TreeLightningSelection.Num() >= 4 ) break;
		// CollectDrawParticlesのemitter順・安定particle順をそのまま採用する。
		FTreeLightningParticleKey Key;
		Key.Emitter = Particle.Emitter;
		Key.Serial = Particle.Serial;
		TreeLightningSelection.Add( Key );
	}
	// alphaとsizeを見る前に固定するmesh粒子の再生全体64枠。
	thread_local TArray<uint8> MeshParticleSelection;
	MeshParticleSelection.Reset();
	MeshParticleSelection.AddZeroed( Particles.Num() );
	// emitter順・Serial順でmesh粒子を先頭から固定する。
	int32 MeshParticleCount = 0;
	for ( int32 ParticleIndex = 0; ParticleIndex < Particles.Num(); ++ParticleIndex )
	{
		// 選択判定に使う生成順particle。
		const NParticle::FDrawParticle& Particle = Particles[ParticleIndex];
		if ( !Simulation.Definition->Emitters.IsValidIndex( Particle.Emitter ) ) continue;
		const FParticleEmitterDefinition& Emitter = Simulation.Definition->Emitters[Particle.Emitter];
		if ( Emitter.Mesh.Kind == FEffectMeshDefinition::EKind::None || !Emitter.Mesh.IsValidSettings() || Emitter.Path.Kind != FEffectPathDefinition::EKind::None || Emitter.Flipbook.bEnabled ) continue;
		if ( MeshParticleCount >= 64 ) break;
		MeshParticleSelection[ParticleIndex] = 1;
		++MeshParticleCount;
	}
	int32 CurrentEmitter = -1;
	for ( int32 ParticleIndex = 0; ParticleIndex < Particles.Num(); ++ParticleIndex )
	{
		// 現在記録する生成順particle。
		const NParticle::FDrawParticle& Particle = Particles[ParticleIndex];
		if ( Particle.Size <= 0.0f || Particle.Color.a == 0 ) continue;
		const FParticleEmitterDefinition& Emitter = Simulation.Definition->Emitters[Particle.Emitter];
		// 壊れたmesh値や排他設定を別の形状へ黙って置き換えない。
		const bool bMeshRequested = Emitter.Mesh.Kind != FEffectMeshDefinition::EKind::None;
		if ( !Emitter.Mesh.IsValidSettings() || ( bMeshRequested && ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None || Emitter.Flipbook.bEnabled ) ) ) continue;
		// Quad経路でコマ送り画像を選ぶか。
		const bool bUseFlipbook = Emitter.Path.Kind == FEffectPathDefinition::EKind::None && Emitter.Flipbook.bEnabled;
		// 粒子時計で決めたコマtexture。未解決なら描画を飛ばす。
		TSharedPtr<CTexture> SelectedFlipbookTexture;
		if ( bUseFlipbook && FlipbookFrames.IsValidIndex( Particle.Emitter ) )
		{
			// 粒子時計から時間軸の段を求め、元クリップ順の派生画像へ移す。
			const FEffectFlipbookFrames& Prepared = FlipbookFrames[Particle.Emitter];
			const int32 Step = NEffectFlipbook::EvaluateStep( Emitter.Flipbook, Prepared.Timeline, Particle.Age, Particle.NormalizedAge );
			const int32 SourceFrame = Prepared.Timeline.StepSource.IsValidIndex( Step ) ? Prepared.Timeline.StepSource[Step] : INDEX_NONE;
			if ( Prepared.Frames.IsValidIndex( SourceFrame ) ) SelectedFlipbookTexture = Prepared.Frames[SourceFrame];
		}
		const TSharedPtr<CTexture>& Texture = bUseFlipbook ? SelectedFlipbookTexture : ( Textures.IsValidIndex( Particle.Emitter ) ? Textures[Particle.Emitter] : TSharedPtr<CTexture>() );
		if ( !Texture.IsValid() || Texture->GetHandle() < 0 ) continue;

		if ( Particle.Emitter != CurrentEmitter )
		{
			CurrentEmitter = Particle.Emitter;
			Draw.Blend( ToDrawBlend( Emitter.Blend ), 255 );
			// 再生が所有する材質マスクを、同じエミッターの経路記録へ渡す。
			const TSharedPtr<CTexture>& MaskTexture = MaskTextures.IsValidIndex( CurrentEmitter ) ? MaskTextures[CurrentEmitter] : TSharedPtr<CTexture>();
			const int32 MaskHandle = MaskTexture.IsValid() ? MaskTexture->GetHandle() : -1;
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Ribbon ) RecordRibbon( Draw, Particles, CurrentEmitter, Emitter, Texture->GetHandle(), MaskHandle, DistortionTextures.IsValidIndex( CurrentEmitter ) && DistortionTextures[CurrentEmitter].IsValid() ? DistortionTextures[CurrentEmitter]->GetHandle() : -1, NParticle::GetEffectScale( Simulation ) );
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::Lightning ) RecordLightning( Draw, Simulation, Particles, CurrentEmitter, Texture->GetHandle(), MaskHandle, DistortionTextures.IsValidIndex( CurrentEmitter ) && DistortionTextures[CurrentEmitter].IsValid() ? DistortionTextures[CurrentEmitter]->GetHandle() : -1, PathSeed );
			if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ) RecordTreeLightning( Draw, Simulation, Particles, TreeLightningSelection, CurrentEmitter, Texture->GetHandle(), MaskHandle, DistortionTextures.IsValidIndex( CurrentEmitter ) && DistortionTextures[CurrentEmitter].IsValid() ? DistortionTextures[CurrentEmitter]->GetHandle() : -1, PathSeed );
		}
		if ( Emitter.Path.Kind != FEffectPathDefinition::EKind::None ) continue;
		if ( bMeshRequested && ( Emitter.Flipbook.bEnabled || !Emitter.Mesh.IsValidSettings() ) ) continue;
		if ( bMeshRequested )
		{
			if ( !MeshParticleSelection.IsValidIndex( ParticleIndex ) || MeshParticleSelection[ParticleIndex] == 0 || !MeshGeometries.IsValidIndex( Particle.Emitter ) ) continue;
			const FEffectMeshGeometry& Geometry = MeshGeometries[Particle.Emitter];
			const int32 MeshTextureHandle = MeshTextureHandles.IsValidIndex( Particle.Emitter ) ? MeshTextureHandles[Particle.Emitter] : -1;
			if ( Geometry.Vertices.IsEmpty() || Geometry.Indices.IsEmpty() || MeshTextureHandle < 0 ) continue;
			// 描画呼出し間で使い回す粒子固有のワールド頂点。
			thread_local TArray<FMeshVertex> WorldVertices;
			WorldVertices.SetNum( Geometry.Vertices.Num() );
			// 粒子回転、固定形状回転、effect回転を契約順に合成する。
			const FQuaternion SpinRotation = FQuaternion::FromAxisAngle( FVector3::Forward, Particle.Angle );
			const FVector3 MeshRadians = Emitter.Mesh.RotationDegrees * 0.01745329251994329577f;
			const FQuaternion MeshRotation = FQuaternion::EulerToQuat( MeshRadians );
			const FQuaternion MeshWorldRotation = Simulation.Transform.Rotation * MeshRotation * SpinRotation;
			// 元形状の全頂点をコピーして粒子位置・寸法だけを反映する。
			for ( int32 VertexIndex = 0; VertexIndex < Geometry.Vertices.Num(); ++VertexIndex )
			{
				// 書き換え先のparticle vertex。
				FMeshVertex& Vertex = WorldVertices[VertexIndex];
				// immutableな元頂点を参照し、共有geometryには触れない。
				const FMeshVertex& SourceVertex = Geometry.Vertices[VertexIndex];
				const FVector3 LocalPosition( SourceVertex.Position[0] * Particle.Size, SourceVertex.Position[1] * Particle.Size, SourceVertex.Position[2] * Particle.Size );
				const FVector3 WorldPosition = Particle.Position + MeshWorldRotation.Rotate( LocalPosition );
				const FVector3 LocalNormal( SourceVertex.Normal[0], SourceVertex.Normal[1], SourceVertex.Normal[2] );
				const FVector3 WorldNormal = MeshWorldRotation.Rotate( LocalNormal );
				Vertex = SourceVertex;
				Vertex.Position[0] = WorldPosition.x;
				Vertex.Position[1] = WorldPosition.y;
				Vertex.Position[2] = WorldPosition.z;
				Vertex.Normal[0] = WorldNormal.x;
				Vertex.Normal[1] = WorldNormal.y;
				Vertex.Normal[2] = WorldNormal.z;
				Vertex.Color[0] = Particle.Color.r;
				Vertex.Color[1] = Particle.Color.g;
				Vertex.Color[2] = Particle.Color.b;
				Vertex.Color[3] = Particle.Color.a;
			}
			// 再生開始時に固定したnoiseとmask handleだけをmesh命令へ渡す。
			const int32 MeshMaskHandle = MeshMaskHandles.IsValidIndex( Particle.Emitter ) ? MeshMaskHandles[Particle.Emitter] : -1;
			const TSharedPtr<CTexture>& MeshDistortionTexture = DistortionTextures.IsValidIndex( Particle.Emitter ) ? DistortionTextures[Particle.Emitter] : TSharedPtr<CTexture>();
			const int32 MeshDistortionHandle = MeshDistortionTexture.IsValid() ? MeshDistortionTexture->GetHandle() : -1;
			// 材質・transform検証で失敗した粒子はmeshもquadも記録しない。
			FDrawMaterialMeshData MeshData;
			if ( !BuildMaterialMeshData( Emitter, Particle, MeshTextureHandle, MeshMaskHandle, MeshDistortionHandle, MeshData ) ) continue;
			MeshData.NumVertices = WorldVertices.Num();
			MeshData.NumIndices = Geometry.Indices.Num();
			Draw.MaterialMesh3D( MeshData, WorldVertices.GetData(), Geometry.Indices.GetData() );
			continue;
		}

		FDrawQuad3DData Data;
		Data.Pos[0] = Particle.Position.x;
		Data.Pos[1] = Particle.Position.y;
		Data.Pos[2] = Particle.Position.z;
		Data.Width = Particle.Size;
		Data.Height = Particle.Size;
		Data.Handle = Texture->GetHandle();
		if ( bUseFlipbook && !Emitter.Material.bEnabled )
		{
			// legacy DrawPolygon3Dは派生グラフ領域を補正しないため、準備済みUVを使う。
			const float32 ( &UvBounds )[4] = Texture->GetPolygonUvBounds();
			Data.U0 = UvBounds[0]; Data.V0 = UvBounds[1]; Data.U1 = UvBounds[2]; Data.V1 = UvBounds[3];
		}
		Data.Orient = static_cast<int32>( ToQuadOrient( Emitter.Orient ) );
		if ( Emitter.Orient == EParticleOrient::Fixed )
		{
			// 板の向きに粒子の回転を反映する。材質と従来描画で同じ向きを使う。
			const float32 C = NMath::Cos( Particle.Angle ), S = NMath::Sin( Particle.Angle );
			const FVector3 Right = FixedRight * C + FixedUp * S;
			const FVector3 Up = FixedUp * C - FixedRight * S;
			Data.Right[0] = Right.x;
			Data.Right[1] = Right.y;
			Data.Right[2] = Right.z;
			Data.Up[0] = Up.x;
			Data.Up[1] = Up.y;
			Data.Up[2] = Up.z;
		}
		else if ( Emitter.Orient == EParticleOrient::Camera ) Data.Angle = Particle.Angle;
		if ( Emitter.Material.bEnabled )
		{
			// 材質シェーダーへは元の色を渡し、従来の加算用前掛けを重ねない。
			Data.Color.R = Particle.Color.r;
			Data.Color.G = Particle.Color.g;
			Data.Color.B = Particle.Color.b;
			Data.Color.A = Particle.Color.a;
			// その粒子と曲線を評価した材質描画の値。
			FDrawMaterialQuadData MaterialData;
			MaterialData.Quad = Data;
			// この再生が保持しているマスク。未指定・読み込み失敗は組み込みへ渡す。
			const TSharedPtr<CTexture>& MaskTexture = MaskTextures.IsValidIndex( Particle.Emitter ) ? MaskTextures[Particle.Emitter] : TSharedPtr<CTexture>();
			MaterialData.MaskTexture = MaskTexture.IsValid() ? MaskTexture->GetHandle() : -1;
			MaterialData.UVScale[0] = NMath::IsFinite( Emitter.Material.UVScale.x ) && Emitter.Material.UVScale.x > 0.0f ? Emitter.Material.UVScale.x : 1.0f;
			MaterialData.UVScale[1] = NMath::IsFinite( Emitter.Material.UVScale.y ) && Emitter.Material.UVScale.y > 0.0f ? Emitter.Material.UVScale.y : 1.0f;
			MaterialData.MaskScale = NMath::IsFinite( Emitter.Material.MaskScale ) && Emitter.Material.MaskScale > 0.0f ? Emitter.Material.MaskScale : 1.0f;
			MaterialData.Dissolve = Emitter.Material.Dissolve.Evaluate( Particle.NormalizedAge, 0.0f );
			MaterialData.Emission = NMath::Clamp( Emitter.Material.Emission.Evaluate( Particle.NormalizedAge, 1.0f ), 0.0f, 32.0f );
			MaterialData.EdgeWidth = NMath::IsFinite( Emitter.Material.EdgeWidth ) ? NMath::Clamp( Emitter.Material.EdgeWidth, 0.0f, 1.0f ) : 0.08f;
			MaterialData.EdgeEmission = NMath::IsFinite( Emitter.Material.EdgeEmission ) ? NMath::Clamp( Emitter.Material.EdgeEmission, 0.0f, 32.0f ) : 2.0f;
			MaterialData.EdgeColor = { Emitter.Material.EdgeColor.r, Emitter.Material.EdgeColor.g, Emitter.Material.EdgeColor.b, Emitter.Material.EdgeColor.a };
			MaterialData.bDissolve = Emitter.Material.bDissolve ? 1 : 0;
			// 秒齢とスクロール速度の積。double の剰余で長時間後も小数部だけにする。
			const double Age = NMath::IsFinite( Particle.Age ) ? static_cast<double>( Particle.Age ) : 0.0;
			// 横方向へ一周させた UV 移動量。
			const double ScrollU = NMath::IsFinite( Emitter.Material.UVScroll.x ) ? NMath::Fmod( Age * Emitter.Material.UVScroll.x, 1.0 ) : 0.0;
			// 縦方向へ一周させた UV 移動量。
			const double ScrollV = NMath::IsFinite( Emitter.Material.UVScroll.y ) ? NMath::Fmod( Age * Emitter.Material.UVScroll.y, 1.0 ) : 0.0;
			MaterialData.UVOffset[0] = static_cast<float32>( ScrollU < 0.0 ? ScrollU + 1.0 : ScrollU );
			MaterialData.UVOffset[1] = static_cast<float32>( ScrollV < 0.0 ? ScrollV + 1.0 : ScrollV );
			// ゆがみは四角形材質だけで評価し、0なら旧命令へ戻してbyte互換を保つ。
			const FEffectUVDistortionDefinition& DistortionDefinition = Emitter.Material.Distortion;
			if ( DistortionDefinition.bEnabled )
			{
				if ( !DistortionDefinition.IsValidSettings() || !NMath::IsFinite( Particle.Age ) || !NMath::IsFinite( Particle.NormalizedAge ) ) continue;
				const float32 Strength = DistortionDefinition.Strength.Evaluate( Particle.NormalizedAge, 0.05f );
				if ( !NMath::IsFinite( Strength ) || Strength < 0.0f || Strength > 1.0f ) continue;
				if ( Strength > 0.0f )
				{
					// 再生開始時に保持できなかった画像では、通常画像へ黙って切り替えない。
					const TSharedPtr<CTexture>& DistortionTexture = DistortionTextures.IsValidIndex( Particle.Emitter ) ? DistortionTextures[Particle.Emitter] : TSharedPtr<CTexture>();
					if ( !DistortionTexture.IsValid() || DistortionTexture->GetHandle() < 0 ) continue;
					// 粒子秒齢と速度の積をdouble剰余し、長時間後も小数位相だけを渡す。
					const double DistortionAge = static_cast<double>( Particle.Age );
					const double DistortionScrollU = NMath::Fmod( DistortionAge * DistortionDefinition.UVScroll.x, 1.0 );
					const double DistortionScrollV = NMath::Fmod( DistortionAge * DistortionDefinition.UVScroll.y, 1.0 );
					const double WrappedU = DistortionScrollU < 0.0 ? DistortionScrollU + 1.0 : DistortionScrollU;
					const double WrappedV = DistortionScrollV < 0.0 ? DistortionScrollV + 1.0 : DistortionScrollV;
					// 0..1へ丸めた移動量と、画像・強さの独立設定。
					FDrawDistortedMaterialQuadData DistortedData;
					DistortedData.Material = MaterialData;
					DistortedData.DistortionTexture = DistortionTexture->GetHandle();
					DistortedData.Strength = Strength;
					DistortedData.UVScale[0] = DistortionDefinition.UVScale.x; DistortedData.UVScale[1] = DistortionDefinition.UVScale.y;
					DistortedData.UVOffset[0] = static_cast<float32>( WrappedU ); DistortedData.UVOffset[1] = static_cast<float32>( WrappedV );
					if ( DistortedData.UVOffset[0] >= 1.0f ) DistortedData.UVOffset[0] = 0.0f;
					if ( DistortedData.UVOffset[1] >= 1.0f ) DistortedData.UVOffset[1] = 0.0f;
					Draw.DistortedMaterialQuad3D( DistortedData );
					continue;
				}
			}
			Draw.MaterialQuad3D( MaterialData );
			continue;
		}
		if ( Emitter.Blend == EParticleBlend::Add )
		{
			// 加算は、暗くすると消える（アルファでは消えない）
			const uint32 A = Particle.Color.a;
			Data.Color.R = static_cast<uint8>( ( Particle.Color.r * A + 127 ) / 255 );
			Data.Color.G = static_cast<uint8>( ( Particle.Color.g * A + 127 ) / 255 );
			Data.Color.B = static_cast<uint8>( ( Particle.Color.b * A + 127 ) / 255 );
			Data.Color.A = 255;
		}
		else
		{
			Data.Color.R = Particle.Color.r;
			Data.Color.G = Particle.Color.g;
			Data.Color.B = Particle.Color.b;
			Data.Color.A = Particle.Color.a;
		}
		Draw.Quad3D( Data );
	}
}

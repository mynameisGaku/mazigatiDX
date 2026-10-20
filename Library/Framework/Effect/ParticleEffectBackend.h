#pragma once
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Effect/EffectFlipbookFrames.h"
#include "Library/Framework/Effect/EffectMeshGeometry.h"
#include "Library/Framework/Effect/ParticleSimulation.h"

class CTexture;

//=============================================================================
// 内蔵のパーティクルのバックエンド（.effect.json を扱う）
//   CAssetManager::LoadEffect で定義を読み、ParticleSimulation で動かす。Record は四角形・連続する帯・分岐する雷・固定meshを描画命令へ記録する。
//   画像はエミッターの Texture。省略時は四角形用の丸、または幅方向だけが柔らかく消える帯を共有して使う。
//
//   EffectBackend.h の窓口の、ひとつの実装。ふつうは FEffectBackendRegistry 経由で使う（直接使うなら FParticlePlayback を作る）。
//
//   描き方の約束:
//   - 旧非材質の加算命令はRGBへalphaを掛ける。材質付き四角形・帯・meshは生RGBAを渡し、blend stateがalphaを適用する。
//   - 粒子は、エミッターの順に、生まれた順に描く（カメラからの距離では並べない。透明な粒子が重なると、見え方が、描く順に依る）。
	//   - 既存の四角形・帯・雷命令は DxLib の深度設定に従う。材質四角形は深度比較を行い、深度を書き込まない。
//=============================================================================
class FParticleEffectBackend : public IEffectBackend
{
public:
	const char* GetName() const override { return "Particle"; }
	bool CanPlay( const FString& Path ) const override;
	TSharedPtr<IEffectPlayback> Play( const FString& Path, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World ) override;
};

//-----------------------------------------------------------------------------
// FParticlePlayback
//   粒子のエフェクト 1 つの再生。FParticleSimulation と、描くための画像を持つ。
//-----------------------------------------------------------------------------
class FParticlePlayback : public IEffectPlayback
{
public:
	// Textures: エミッターごとの画像（nullptr の項目は、組み込みの丸）。定義と同じ数
	FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World );
	// 材質マスクを再生中に共有保持する。空・読み込み失敗の項目は描画側の組み込み模様を使う。
	FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures );
	// 材質マスクとコマ画像を再生期間保持する。
	FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures, const TArray<FEffectFlipbookFrames>& InFlipbookFrames );
	// 材質マスク・コマ画像・ゆがみ画像を再生期間共有保持する。
	FParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, const TArray<TSharedPtr<CTexture>>& InTextures, const FEffectPlayParams& Params, const FTransform& World, const TArray<TSharedPtr<CTexture>>& InMaskTextures, const TArray<FEffectFlipbookFrames>& InFlipbookFrames, const TArray<TSharedPtr<CTexture>>& InDistortionTextures );

	void SetTransform( const FTransform& World ) override { Simulation.Transform = World; }
	void Update( float32 DeltaTime ) override { NParticle::Advance( Simulation, DeltaTime ); }
	void Record( FDrawContext& Draw ) const override;
	void Stop( bool bImmediate ) override { NParticle::Stop( Simulation, bImmediate ); }
	bool IsFinished() const override { return Simulation.IsFinished(); }
	int32 GetNumParticles() const override { return Simulation.GetNumParticles(); }

	const FParticleSimulation& GetSimulation() const { return Simulation; }

private:
	FParticleSimulation Simulation;
	TArray<TSharedPtr<CTexture>> Textures;
	// 材質マスク画像をエミッター番号で引き、再生終了まで解放しない。
	TArray<TSharedPtr<CTexture>> MaskTextures;
	// エミッターごとの固定済み時間軸とコマ画像。
	TArray<FEffectFlipbookFrames> FlipbookFrames;
	// 材質付き四角形・帯・雷・meshが使うゆがみ画像を再生終了まで保持する。
	TArray<TSharedPtr<CTexture>> DistortionTextures;
	// mesh命令のmask handleを再生開始時に固定する。
	TArray<int32> MeshMaskHandles;
	// mesh命令のsource texture handleを再生開始時に固定する。
	TArray<int32> MeshTextureHandles;
	// emitterごとに再生開始時に作った変更しないCPU形状。
	TArray<FEffectMeshGeometry> MeshGeometries;
	// 再生指定のシードを保持し、描画順や粒子更新の回数に雷の形を依存させない。
	uint32 PathSeed = 1;
};

namespace NEffect
{
	// 定義・アセット管理・再生設定から画像と材質マスクを保持する再生を作る。必須入力が無ければ空を返す。
	TSharedPtr<IEffectPlayback> CreateParticlePlayback( const TSharedPtr<const FParticleEffectDefinition>& Definition, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World, const FString& Label = FString() );

	// 組み込みの、ふちがぼやけた丸の画像（TGA の中身。64 x 64、色は白、アルファが中心から縁へ (1 - 距離)^2 で落ちる。
	// DxLib の加算も画像のアルファを使うので、アルファ・加算のどちらにも、この 1 枚を使う）
	TArray<uint8> MakeSoftDotImage();
	// 長さ方向は途切れず、幅方向だけを柔らかく消す帯の画像。TGA、2 x 64。
	TArray<uint8> MakeRibbonImage();
}

// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Color.h"
#include "Library/Math/Transform.h"

class FDrawContext;
class CAssetManager;

//=============================================================================
// エフェクトの窓口（バックエンド）
//   エフェクトを「再生する」仕組みを、インターフェースにしてある。ワールドの Effect 部品も、ゲームのコードも、ここだけを知る。
//   中身を差し替えられる: 内蔵の簡易パーティクル（ParticleEffectBackend.h。.effect.json）と、外部のエフェクトライブラリ（Effekseer など）を、
//   同じ窓口の、別の実装として登録できる（拡張子などで、どちらが扱うかを決める）。
//
//   // 再生する（登録された最初の、扱えるバックエンド）
//   TSharedPtr<IEffectPlayback> Playback = FEffectBackendRegistry::Global().Play( "Effects/Fire.effect.json", Assets, Params, FTransform( Position ) );
//   Playback->SetTransform( ... );   // 動くものに付けるなら、毎フレーム
//   Playback->Update( DeltaTime );   // 毎フレーム 1 回
//   Playback->Record( Draw );        // 描画コマンドに記録する（const。描画システムから、並列に呼ばれても安全）
//   Playback->Stop( false );         // 出すのをやめる（残りは、自然に消える）
//
//   ふつうは、自分で呼ばずに、CEffectComponent（EffectComponent.h）か、ワールドの Effect 部品に任せる。
//
//   外部のライブラリを足すときの注意: Record は、描くものを FDrawContext に記録する関数。ライブラリが自分で描く（DxLib を直接呼ぶ）種類のものは、
//   描画コマンドを 1 つ足して、IDrawExecutor が、そのライブラリを呼ぶ形にする（Framework は DxLib を呼べないため）。
//=============================================================================
// 再生の指定
struct FEffectPlayParams
{
	float32 Scale = 1.0f;        // 大きさの倍率（エフェクトの拡大に、さらに掛かる）
	float32 Speed = 1.0f;        // 時間の進み方
	uint32 Seed = 0;             // 乱数のシード。0 ならエフェクトのシード
	int32 Loop = -1;             // -1 = エフェクトの指定のまま / 0 = ループしない / 1 = ループする
};

// 再生中のエフェクト 1 つ
class IEffectPlayback
{
public:
	virtual ~IEffectPlayback() = default;

	// エフェクトの、ワールドでの位置・向き・大きさ
	virtual void SetTransform( const FTransform& World ) = 0;
	// 雷などの動的終点（ワールド座標）を指定する
	virtual void SetTargetPosition( const FVector3& WorldTarget ) { (void)WorldTarget; }
	// 動的終点の指定を解除し、定義側の終点へ戻す
	virtual void ClearTargetPosition() {}
	// 時間を進める（毎フレーム 1 回）
	virtual void Update( float32 DeltaTime ) = 0;
	// 描くものを記録する。状態を変えない（描画システムが、並列に呼んでよい）
	virtual void Record( FDrawContext& Draw ) const = 0;
	// 出すのをやめる。bImmediate なら、いま出ているものも消す
	virtual void Stop( bool bImmediate ) = 0;
	// 終わった（もう何も出ず、何も残っていない）
	virtual bool IsFinished() const = 0;
	// 画面に出ている粒子などの数（確認・表示用。分からなければ 0）
	virtual int32 GetNumParticles() const { return 0; }
};

class IEffectBackend
{
public:
	virtual ~IEffectBackend() = default;

	virtual const char* GetName() const = 0;
	// このファイル（アセットのルートからの相対）を扱えるか（拡張子など。読み込みはしない）
	virtual bool CanPlay( const FString& Path ) const = 0;
	// 再生を始める。読み込めなければ nullptr。Assets は、ファイルや画像の読み込み用（動いていなければ nullptr）
	virtual TSharedPtr<IEffectPlayback> Play( const FString& Path, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World ) = 0;
};

//-----------------------------------------------------------------------------
// FEffectBackendRegistry
//   バックエンドの一覧。先に登録したものから、扱えるかを聞く。
//   Global() には、内蔵のパーティクルが入っている。外部のライブラリは、使う側が Add する。
//-----------------------------------------------------------------------------
class FEffectBackendRegistry
{
public:
	// 同じ名前があれば置き換える（順番は、最初の登録のまま）
	void Add( const TSharedPtr<IEffectBackend>& Backend );
	bool Remove( const FString& Name );
	int32 Num() const { return Backends.Num(); }
	// そのファイルを扱える、最初のバックエンド（無ければ nullptr）
	IEffectBackend* Find( const FString& Path ) const;
	TSharedPtr<IEffectPlayback> Play( const FString& Path, CAssetManager* Assets, const FEffectPlayParams& Params, const FTransform& World ) const;

	// アプリ全体で使う、内蔵のものが入った一覧
	static FEffectBackendRegistry& Global();
	// 内蔵のもの（パーティクル）だけが入った、新しい一覧
	static FEffectBackendRegistry CreateWithBuiltins();

private:
	TArray<TSharedPtr<IEffectBackend>> Backends;
};

namespace NEffect
{
	// 内蔵のバックエンド（パーティクル）を登録する
	void RegisterBuiltinBackends( FEffectBackendRegistry& Registry );
}

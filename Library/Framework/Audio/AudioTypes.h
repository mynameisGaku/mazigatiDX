#pragma once
#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// オーディオの共通の型と、純粋な計算（DxLib・アセット・シーンには依存しない）
//   フェードの形・距離による減衰・左右の定位などは、同じ入力なら必ず同じ結果になる関数にしてあるので、
//   音を鳴らさずに確かめられる。
//=============================================================================

// 音の種類ごとの音量の調整先（バス）。Master は全部に掛かる
enum class EAudioBus : uint8
{
	Master,
	Music,     // BGM
	Sfx,       // 効果音
	Ambient,   // 環境音
	Voice,     // 声・セリフ
	Ui,        // メニューなどの音
	Count,
};

const char* ToString( EAudioBus Bus );
// 名前（Master / Music / Sfx / Ambient / Voice / Ui。大文字小文字は区別しない）から。知らない名前は false（Out は変えない）
bool TryParse( const FString& Text, EAudioBus& Out );

// フェードの形（0 -> 1 の変化の仕方）
enum class EFadeCurve : uint8
{
	Linear,       // 直線
	EaseIn,       // 最初ゆっくり
	EaseOut,      // 最後ゆっくり
	SmoothStep,   // 両端がゆっくり
	EqualPower,   // 2 つの音を入れ替える（クロスフェード）とき、合計のパワーが一定になる形（上がる側が sin、下がる側が cos）
};

// 距離による減衰の形
enum class EAudioRolloff : uint8
{
	Linear,    // MinDistance で 1、MaxDistance で 0 の直線
	Inverse,   // 距離に反比例（近くで急に、遠くでゆっくり減る）。MaxDistance で 0 になるよう端を整える
	Squared,   // (1 - 正規化した距離) の 2 乗（遠くで早く聞こえなくなる）
};

// 同時に鳴らせる数を超えたときの扱い
enum class EAudioOverflow : uint8
{
	StopOldest,   // いちばん古いものを止めて、新しいものを鳴らす
	IgnoreNew,    // 新しいものを鳴らさない
};

//-----------------------------------------------------------------------------
// FSoundHandle
//   鳴らしている音 1 つを指す印。鳴り終わった・止めたあとで、同じ場所が別の音に使われても、古い印は無効のまま
//   （世代で見分ける）。無効な印を操作しても何も起きない。
//-----------------------------------------------------------------------------
struct FSoundHandle
{
	uint32 Id = 0;

	FORCEINLINE bool IsValid() const { return Id != 0; }
	friend FORCEINLINE bool operator==( const FSoundHandle& A, const FSoundHandle& B ) { return A.Id == B.Id; }
	friend FORCEINLINE bool operator!=( const FSoundHandle& A, const FSoundHandle& B ) { return A.Id != B.Id; }
};

//-----------------------------------------------------------------------------
// FAudioFade
//   値を、時間をかけて目標へ動かす。Value が現在の値。
//-----------------------------------------------------------------------------
struct FAudioFade
{
	float32 Value = 1.0f;

	// 値を V にして、フェードを止める
	void Reset( float32 V );
	// 現在の値から Target へ、Seconds 秒かけて動かす（0 以下なら即座に）
	void FadeTo( float32 Target, float32 Seconds, EFadeCurve Curve = EFadeCurve::Linear );
	void Step( float32 DeltaSeconds );

	FORCEINLINE bool IsFading() const { return Elapsed < Duration; }
	FORCEINLINE float32 GetTarget() const { return To; }

private:
	float32 From = 1.0f;
	float32 To = 1.0f;
	float32 Elapsed = 0.0f;
	float32 Duration = 0.0f;
	EFadeCurve Curve = EFadeCurve::Linear;
};

//-----------------------------------------------------------------------------
// 再生の指定
//-----------------------------------------------------------------------------
struct FSoundPlayParams
{
	float32 Volume = 1.0f;             // 0..（1 = 元の大きさ）
	float32 Pitch = 1.0f;              // 周波数の倍率（2 で 1 オクターブ上）
	float32 Pan = 0.0f;                // -1（左）..1（右）
	bool bLoop = false;
	EAudioBus Bus = EAudioBus::Sfx;
	float32 FadeInTime = 0.0f;         // 音量を 0 から上げていく時間（秒）
	EFadeCurve FadeInCurve = EFadeCurve::Linear;
	float32 StartTime = 0.0f;          // 音の途中から始める（秒）
	int32 Priority = 0;                // 大きいほど優先。同時に鳴らせる数を超えたとき、低いものから止められる
	bool bPausable = true;             // false なら、全体の一時停止（Pause）でも止まらない（メニュー音など）

	// ばらつき（鳴らすたびに、±この範囲でランダムに変える。足音や銃声を毎回少し違う音にする）
	float32 VolumeVariation = 0.0f;    // 音量の倍率を 1 ± この値の範囲で
	float32 PitchVariation = 0.0f;     // ピッチの倍率を 1 ± この値の範囲で

	// 3D の位置（bSpatial が true のとき、リスナーとの距離で音量が、左右の位置でパンが決まる）
	bool bSpatial = false;
	FVector3 Position = FVector3::Zero;
	float32 MinDistance = 1.0f;        // この距離までは減衰しない
	float32 MaxDistance = 30.0f;       // この距離で聞こえなくなる
	EAudioRolloff Rolloff = EAudioRolloff::Linear;
	float32 SpatialPanStrength = 1.0f; // 左右の定位の強さ（0..1）

	// 数の制限
	int32 MaxInstances = 0;            // 同じ音を同時に鳴らせる数。0 なら制限しない
	EAudioOverflow Overflow = EAudioOverflow::StopOldest;
	float32 MinInterval = 0.0f;        // 同じ音を鳴らす最短の間隔（秒）。連打で音が潰れるのを防ぐ。0 なら制限しない
};

// リスナー（聞き手）の位置と向き
struct FAudioListener
{
	FVector3 Position = FVector3::Zero;
	FVector3 Forward = FVector3::Forward;
	FVector3 Up = FVector3::Up;
};

namespace NAudio
{
	// 進行 t（0..1）に対する、変化の割合（0..1）。bRising は値が上がる向きか（EqualPower だけ、上がる側と下がる側で形が違う）
	float32 ShapeFade( EFadeCurve Curve, float32 T, bool bRising );

	// 距離 Distance での減衰（0..1）。Min 以下は 1、Max 以上は 0
	float32 RolloffGain( EAudioRolloff Rolloff, float32 Distance, float32 MinDistance, float32 MaxDistance );

	// リスナーから見た音源の左右の位置から、パン（-1..1）を求める。ToSource は音源へのベクトル、Right はリスナーの右向き
	float32 SpatialPan( const FVector3& ToSource, const FVector3& Right, float32 Strength );

	// リスナーの右向き（左手系: Up x Forward）。向きが不正なら +X
	FVector3 ListenerRight( const FAudioListener& Listener );

	// 音量 0..1 を、デシベルにして 100 倍した値に（DxLib の SetVolumeSoundMem 用）。0 以下は MinDecibel100
	constexpr int32 MinDecibel100 = -10000;
	int32 VolumeToDecibel100( float32 Volume );
}

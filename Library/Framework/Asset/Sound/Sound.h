#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Framework/Asset/Asset.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

//=============================================================================
// CSound
//   音声データ（WAV / OGG / MP3 など）。CAssetSubsystem::LoadSound で読み込み、CAudioSubsystem で鳴らす。
//
//   TSharedPtr<CSound> Jump = Assets->LoadSound( "Sound/Jump.wav" );                              // 効果音（メモリに展開）
//   TSharedPtr<CSound> Theme = Assets->LoadSound( "Sound/Theme.ogg", ESoundLoadMode::Stream );    // BGM（再生しながら読む）
//   Audio->Play( Jump );
//
//   同じ音の同時再生のために、再生ごとに「ボイス」（バックエンドが再生に使うハンドル）を貸し出す。
//   ボイスは使い終わったら返され、次の再生に使い回される（再生のたびに作り直さない）。
//   Stream の音は、同時に鳴らせる数が限られることがある（できる数はバックエンドが決める。借りられなければ AcquireVoice が InvalidHandle を返す）。
//=============================================================================
class CSound : public CAsset
{
public:
	CSound( const FString& InPath, int32 InHandle, ESoundLoadMode InMode, bool bInAsync );
	~CSound() override;

	// 元のハンドル（普通は直接使わない。再生には AcquireVoice で借りたものを使う）
	FORCEINLINE int32 GetHandle() const { return Handle; }
	FORCEINLINE ESoundLoadMode GetMode() const { return Mode; }
	FORCEINLINE bool IsStream() const { return Mode == ESoundLoadMode::Stream; }

	// 音の長さ（秒）。読み込み中・不明なら 0
	float32 GetLength() const;

	// 音の情報（長さ・サンプルレート・チャンネル数など。分からない項目は 0）。読み込み中・バックエンドが答えられないなら false
	bool GetInfo( FSoundInfo& OutInfo ) const;

	// ループして鳴らすときの区間（秒）。再生位置が EndSeconds に届くと StartSeconds へ戻る。EndSeconds が 0 以下なら音の最後まで。
	// この後に始まるループ再生から使われる（すでに鳴っているものは変わらない）。イントロつきの BGM（頭は 1 度だけ、あとは繰り返す）に使う
	void SetLoopRegion( float32 StartSeconds, float32 EndSeconds );
	// 区間の指定をやめる（音の全体を繰り返す）
	void ClearLoopRegion();
	// SetLoopRegion / ClearLoopRegion を呼んだことがあるか（呼んだなら、鳴らすたびにバックエンドへ伝える）
	FORCEINLINE bool IsLoopRegionSet() const { return bLoopRegionSet; }
	FORCEINLINE float32 GetLoopStart() const { return LoopStartSeconds; }
	FORCEINLINE float32 GetLoopEnd() const { return LoopEndSeconds; }

	bool IsReady() const override;
	FString Describe() const override;
	// Memory は展開後の大きさ（16bit ステレオとして）、Stream は読み込み用の領域
	int64 GetMemoryBytes() const override;

	//-------------------------------------------------------------------------
	// ボイス（CAudioSubsystem が使う）
	//-------------------------------------------------------------------------
	// 再生用のハンドルを借りる。借りられなければ InvalidHandle（Stream で使用中、複製できない、など）
	int32 AcquireVoice();
	void ReleaseVoice( int32 Voice );

	FORCEINLINE int32 GetNumVoicesInUse() const { return NumVoicesInUse; }
	FORCEINLINE int32 GetNumIdleVoices() const { return IdleVoices.Num(); }

	// 使い回しのために取っておく、空きボイスの最大数（これを超えて返されたものは破棄する）
	static constexpr int32 MaxIdleVoices = 8;

private:
	int32 Handle;
	ESoundLoadMode Mode;
	bool bAsync;
	mutable float32 Length = 0.0f;
	mutable FSoundInfo Info;
	mutable bool bInfoRead = false;
	bool bLoopRegionSet = false;
	float32 LoopStartSeconds = 0.0f;
	float32 LoopEndSeconds = 0.0f;
	mutable int64 MemoryBytes = -1;

	TArray<int32> IdleVoices;
	int32 NumVoicesInUse = 0;
	bool bStreamBusy = false;
};

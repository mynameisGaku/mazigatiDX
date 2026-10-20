#pragma once
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// IAudioBackend
//   サウンドの再生を操作する、プラットフォーム層のインターフェース。
//   フレームワーク側（CAudioSystem）はこれだけを知り、DxLib などの実装には依存しない。
//   実装: CDxLibAudioBackend（DxLib）。確認用に、呼ばれた内容を記録するだけの実装を作ってもよい。
//
//   - 「ボイス」は、再生中の音 1 つ分の入れ物（IAssetBackend::LoadSound / DuplicateSound で得たハンドル）。
//     同じ音を重ねて鳴らすときは、別々のボイスを使う
//   - 音量は 0..1 の振幅（1 = 元の大きさ）、パンは -1（左）..1（右）、ピッチは周波数の倍率（1 = 元の高さ。2 で 1 オクターブ上）
//   - すべての関数はメインスレッドだけから呼ばれる
//=============================================================================
class IAudioBackend
{
public:
	virtual ~IAudioBackend() = default;

	// 再生を始める。bFromStart が false なら、止めた位置から再開する。始められなければ false
	virtual bool Play( int32 Voice, bool bLoop, bool bFromStart ) = 0;
	// 止める（位置は覚えているので、Play( ..., false ) で再開できる）
	virtual void Stop( int32 Voice ) = 0;
	// 再生中か（Stop で止めたものは false。ループしない音が最後まで鳴ったあとも false）
	virtual bool IsPlaying( int32 Voice ) = 0;

	virtual void SetVolume( int32 Voice, float32 Volume ) = 0;
	virtual void SetPan( int32 Voice, float32 Pan ) = 0;
	virtual void SetPitch( int32 Voice, float32 Pitch ) = 0;

	// 再生位置（秒）。再生中でなくても取れる
	virtual float32 GetPlayPosition( int32 Voice ) = 0;
	// 再生位置を変える（次に Play( ..., false ) するときの位置。再生中は無視されることがある）
	virtual void SetPlayPosition( int32 Voice, float32 Seconds ) = 0;
};

// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"

//=============================================================================
// ISoundBackend
//   サウンド（読み込み・複製・長さ・情報・PCM との行き来）を扱う窓口。音を使う側（CSoundLoader / CPcmSoundLoader / CSound）が知る
//   すべての関数はメインスレッドだけから呼ばれる（バックエンドがスレッドセーフとは限らない）。実装は IAssetBackend を通して持つ
//=============================================================================
class ISoundBackend
{
public:
	virtual ~ISoundBackend() = default;

	//-------------------------------------------------------------------------
	// サウンド
	//-------------------------------------------------------------------------
	virtual int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) = 0;
	// 同じデータを共有する、別々に再生できるハンドルを作る（Memory で読み込んだものだけ）
	virtual int32 DuplicateSound( int32 Source ) = 0;
	virtual void DeleteSound( int32 Sound ) = 0;
	// 音の長さ（秒）。読み込み中・不明なら負
	virtual float32 GetSoundLength( int32 Sound ) = 0;
	// 音の情報（長さ・サンプル数・サンプルレート・チャンネル数。分からない項目は 0）。読み込み中・番号が不正・未対応なら false で、Out は変えない
	virtual bool GetSoundInfo( int32 /*Sound*/, FSoundInfo& /*Out*/ ) { return false; }
	// 生の音（PCM）から、メモリに展開した音を作る。作れなければ負（NAsset::InvalidHandle）。使い終わったら DeleteSound。
	// 対応するチャンネル数・サンプルレートは実装による（対応しない形は、未対応として負を返す）
	virtual int32 CreateSoundFromPcm( const FPcmBuffer& /*Pcm*/ ) { return -1; }
	// 音のファイル（Path は絶対パス。WAV のほか、実装が読める形式）を、生の音（PCM）に読み出す。読めなければ false で、Out は変えない。
	// WAV は、フレームワークが自分で読める（NWavFile）ので、実装が対応していなくてもよい
	virtual bool DecodeSoundFile( const FString& /*Path*/, FPcmBuffer& /*Out*/ ) { return false; }
};

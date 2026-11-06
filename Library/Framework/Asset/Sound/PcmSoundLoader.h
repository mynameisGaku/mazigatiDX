// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Sound/Sound.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"

class ISoundBackend;

//=============================================================================
// CPcmSoundLoader
//   音を「作る・読み出す・書き出す」役割だけを持つクラス（登録とキャッシュの管理は CAssetSubsystem、鳴らすのは CAudioSubsystem）。
//   バックエンドは作るときに渡す（検査では偽のバックエンドを渡せる）。パスは絶対パス（資産のルートからの解決は呼ぶ側）。
//
//   CPcmSoundLoader Loader( NAssetService::GetBackend() );
//   FPcmBuffer Beep;  Beep.SampleRate = 22050;  for ( ... ) { Beep.Samples.Add( sinf( ... ) ); }
//   TSharedPtr<CSound> Sound = Loader.CreateFromPcm( "Beep", Beep );       // 計算で作った効果音を、鳴らせる音にする
//   FPcmBuffer Pcm;  Loader.Decode( FullPath, Pcm );                         // ファイルの音を、生の音に読み出す
//   CPcmSoundLoader::SaveWav( Pcm, FullPath );                                  // 16 ビットの WAV に書く
//
//   - すべてゲームスレッドから使う（バックエンドの呼び出しを含むため）。SaveWav だけはバックエンドを使わない
//   - 作った音は CAssetSubsystem のキャッシュには入らない（同じ名前でも別の音。持ち主が共有で持つ）
//=============================================================================
class CPcmSoundLoader
{
public:
	explicit CPcmSoundLoader( ISoundBackend* InBackend );

	// 生の音（PCM）から、メモリに展開した音を作る。Name は表示のためだけ。作れなければ nullptr（PCM が不正、バックエンドが未対応の形、など）
	TSharedPtr<CSound> CreateFromPcm( const FString& Name, const FPcmBuffer& Pcm ) const;

	// 音のファイル（絶対パス）を、生の音（PCM）に読み出す。WAV は NWavFile で読み、
	// 読めなければ（WAV 以外の形式も）バックエンドに任せる。読めなければ false で、Out は変えない
	bool Decode( const FString& FullPath, FPcmBuffer& OutPcm ) const;

	// 生の音（PCM）を、16 ビットの WAV ファイル（絶対パス。フォルダは作らない）に保存する
	static bool SaveWav( const FPcmBuffer& Pcm, const FString& FullPath );

private:
	ISoundBackend* Backend = nullptr;
};

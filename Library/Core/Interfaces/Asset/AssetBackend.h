// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Asset/AssetBackendTypes.h"
#include "Library/Core/Interfaces/Asset/FontBackend.h"
#include "Library/Core/Interfaces/Asset/ModelBackend.h"
#include "Library/Core/Interfaces/Asset/RenderResourceBackend.h"
#include "Library/Core/Interfaces/Asset/SoundBackend.h"
#include "Library/Core/Interfaces/Asset/TextureBackend.h"

//=============================================================================
// アセット（画像・モデル・フォント・サウンド）の読み込みと破棄を行う、プラットフォーム層のインターフェース
//   フレームワーク側（CAssetSubsystem など）はこれだけを知り、DxLib などの実装には依存しない。
//   実装: CDxLibAssetBackendSubsystem（DxLib）。確認用に、読み込んだふりをするだけの実装を作ってもよい。
//
//   - 資源は int32 のハンドルで表す。負の値（InvalidHandle）は無効
//   - すべての関数はメインスレッドだけから呼ばれる（DxLib はスレッドセーフではない）
//
//   役割ごとの窓口（ITextureBackend・IModelBackend・IRenderResourceBackend・IFontBackend・ISoundBackend）を、まとめて持つ。
//   実装（CDxLibAssetBackendSubsystem など）は IAssetBackend を継承する。使う側は、必要な窓口だけを知ればよい
//   （たとえば CTextureLoader は ITextureBackend だけ）。ほかのバックエンドで一部の窓口だけを実装するときも、この分け方に沿う。
//=============================================================================
class IAssetBackend : public ITextureBackend, public IModelBackend, public IRenderResourceBackend, public IFontBackend, public ISoundBackend
{
public:
	~IAssetBackend() override = default;

	//-------------------------------------------------------------------------
	// メモリ
	//-------------------------------------------------------------------------
	virtual FBackendMemoryInfo GetMemoryInfo() = 0;
	// モデルが使っているメモリの推定（頂点・テクスチャなど。バイト）
	virtual int64 EstimateModelBytes( int32 Model ) = 0;
	// サウンドが使っているメモリの推定（Memory は展開後の大きさ、Stream は読み込み用の領域。バイト）
	virtual int64 EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode ) = 0;

	//-------------------------------------------------------------------------
	// 非同期の読み込み
	//-------------------------------------------------------------------------
	// bAsync で読み込んだハンドルが、まだ読み込み中なら true（完了・失敗・同期の読み込みなら false）
	virtual bool IsLoading( int32 Handle ) = 0;
	// 読み込み中の数
	virtual int32 GetNumLoading() = 0;
};

// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/Font/FontTypes.h"
#include "Library/Framework/Asset/Model/ModelCollisionTypes.h"
#include "Library/Framework/Asset/Model/ModelMeshTypes.h"
#include "Library/Framework/Asset/Model/ModelStructureTypes.h"
#include "Library/Framework/Asset/Sound/SoundTypes.h"
#include "Library/Framework/Asset/Model/StaticModelSurfaceData.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IShader.h"
#include "Library/Framework/Render/Types/MeshTypes.h"
#include "Library/Framework/Render/Types/RenderTargetTypes.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// アセットのバックエンドの窓口（ITextureBackend など）が共有する、ハンドルと値の型
//   資源は int32 のハンドルで表す。負の値（InvalidHandle）は無効
//=============================================================================
namespace NAsset
{
	constexpr int32 InvalidHandle = -1;
}

// サウンドの読み込み方
enum class ESoundLoadMode : uint8
{
	Memory,   // 全部をメモリに展開する。応答が速く、同じ音の同時再生ができる（効果音向け）
	Stream,   // 再生しながらファイルから読む。メモリを食わない代わりに、同じ音の同時再生ができないことがある（できる数はバックエンドが決める。BGM 向け）
};

// フォントの指定
struct FFontDesc
{
	FString Face;               // フォント名（"MS Gothic" など）。空なら実装の既定フォント
	int32 Size = 24;            // 文字の高さ（ピクセル）
	int32 Thickness = 6;        // 太さ（0..9）
	int32 EdgeSize = 0;         // 0 より大きいと縁取りをつける（ピクセル）
	bool bItalic = false;
	EFontAntialias Antialias = EFontAntialias::Standard;   // 文字のなめらかさ
	int32 LetterSpacing = 0;    // 字間（ピクセル。全部の文字の幅に足す。負の値で詰める）
	int32 LineHeight = -1;      // 行の送り幅（ピクセル。次の行の同じ位置までの距離）。-1 はフォントの既定（1 行の文字の高さ）。0 以上を指定すると、その値
};

// モデルを MV1 ファイルに保存するときの指定
enum class EModelSaveType : uint8
{
	Normal,      // メッシュとアニメーションの両方
	MeshOnly,    // メッシュだけ（アニメーションは別のファイル）
	AnimOnly,    // アニメーションだけ（メッシュのないモーション専用ファイル）
};

struct FModelSaveOptions
{
	EModelSaveType Type = EModelSaveType::Normal;
	// true なら小さくなる保存（精度を落として、ファイルを小さくする）。false なら精度を落とさない（大きい）。既定は true
	bool bCompress = true;
};

// プラットフォームが持っている資源の全体の状況（0 は不明）
struct FBackendMemoryInfo
{
	int64 VideoMemoryTotal = 0;    // ビデオメモリの容量（バイト）
	int64 VideoMemoryUsed = 0;     // ビデオメモリの使用量
	int64 ProcessWorkingSet = 0;   // このプロセスが使っている物理メモリ
	int32 NumTextureHandles = 0;   // 生きているハンドルの数（アセット以外が作ったものも含む）
	int32 NumModelHandles = 0;
	int32 NumSoundHandles = 0;
	int32 NumFontHandles = 0;
};

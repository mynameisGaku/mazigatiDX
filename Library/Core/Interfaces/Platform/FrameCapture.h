// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Math/Color.h"

//=============================================================================
// 画面の画素を受け取る窓口（スクリーンショット・GIF の記録のため）
//   プラットフォームは、フレームを描き終えて画面に出す直前に、受け取り手（IFrameCaptureSink）が欲しいと言ったフレームだけ、
//   画面の画素を読んで渡す。プラットフォームは PNG や GIF を知らず、受け取り手は DxLib などの実装を知らない。
//
//   IPlatform::SetFrameCaptureSink( &Recorder );   // 外すときは nullptr
//=============================================================================

// どの時点の画面か
enum class EFrameCaptureStage : uint8
{
	// ゲームの絵だけ（デバッグ用の UI を重ねる前）
	Game,
	// デバッグ用の UI（ImGui）まで重ねた、画面に出るとおりの絵
	WithDebugUI,
};

// 1 フレーム分の画面の画素（借りているだけ。OnFrame の中でだけ有効。左上から行の順、a はいつも 255）
struct FFramePixels
{
	int32 Width = 0;
	int32 Height = 0;
	const FColor32* Pixels = nullptr;
	// 何番目のフレームか（IPlatform::GetFrameCount と同じ数え方で、いま描き終えたフレーム）
	uint64 FrameIndex = 0;
	EFrameCaptureStage Stage = EFrameCaptureStage::WithDebugUI;
};

class IFrameCaptureSink
{
public:
	virtual ~IFrameCaptureSink() = default;

	// このフレームの、この時点の画素が要るか（要らないときは、画素を読まない。読むのは重いので）
	virtual bool WantsFrame( uint64 FrameIndex, EFrameCaptureStage Stage ) const = 0;

	// 画素を受け取る（WantsFrame が true だったときだけ。1 フレームに、Game → WithDebugUI の順で最大 2 回。メインスレッド）
	virtual void OnFrame( const FFramePixels& Frame ) = 0;
};

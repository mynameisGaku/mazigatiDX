// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Animation/AnimationTimeline.h"
#include "Library/Math/Matrix4x4.h"

namespace NRootMotion
{
	// 指定した再生位置間の完全な親基準姿勢差分を求める。
	// Sample は 0..ClipLength の時間を受け、affine（平行移動・回転・拡大を含み遠近変形を含まない）親基準姿勢を返す。
	// 無効な入力、sample失敗、特異または非finiteな行列では失敗し、OutDelta を変更しない。
	// 時刻と長さは同じ単位を使い、拡大率を含む affine 差分を返す。
	// Once は P(to) * Inverse(P(from))、Loop は位相と差分周回を合成し、PingPong は折返し姿勢だけを比較する。
	// 成功時は Delta * ExtendedFrom == ExtendedTo。Loop周回はdouble整数精度を保つ範囲に制限する。
	bool ExtractDelta( float64 FromPlayhead, float64 ToPlayhead, float32 ClipLength, EAnimLoopMode Mode, const TCallable<bool( float32, FMatrix4x4& )>& Sample, FMatrix4x4& OutDelta );
}

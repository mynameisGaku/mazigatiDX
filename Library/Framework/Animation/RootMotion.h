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

	// scaleを含まないproper rigid行列から移動と回転を取り出す。
	// 軸長二乗と軸同士の内積は1.0e-4以内、行列式は1.0から4.0e-4以内を許容する。m03/m13/m23は厳密に0、m33は1.0e-4以内をaffine値へ戻す。
	// scale・shear・反転・projective・非finiteは失敗し、両出力を保持する。
	bool TryExtractRigidMotion( const FMatrix4x4& Delta, FVector3& OutTranslation, FQuaternion& OutRotation );

	// 共通基準行列を使って差分をactor空間へ移す。
	// 有限affineのSourceDeltaと、有限・可逆・正向きaffineのReferenceToActorBasisを受け、SourceDelta * Basis == Basis * Convertedを満たすproper rigid差分を返す。
	// 失敗時は両出力を保持する。projective成分m03/m13/m23は厳密に拒否し、m33の1.0e-4以内の丸めだけaffine値へ戻す。
	// 骨差分では固定基準pose・固定parent-to-model・ModelLocalをbasisへ含める。動くparentは対象外。
	bool TryConvertToActorMotion( const FMatrix4x4& SourceDelta, const FMatrix4x4& ReferenceToActorBasis, FVector3& OutTranslation, FQuaternion& OutRotation );
}

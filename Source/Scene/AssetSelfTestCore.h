// SPDX-License-Identifier: NOASSERTION
#pragma once

namespace NAssetTest
{
	// 確認結果を収集する自己検査の判定役。
	struct FChecker;

	// Core の数値の基本（Min / Max / Clamp / IsFinite / TNumericLimits）と、右辺値の受け渡し（MoveTemp / Forward）を、標準ライブラリの値と突き合わせて確かめる。
	void RunCoreNumericTests( FChecker& Checker );
}

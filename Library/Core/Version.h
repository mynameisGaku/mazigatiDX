#pragma once

// SPDX-License-Identifier: MIT

//=============================================================================
// フレームワークの版（セマンティック バージョニング。方針は Docs/VERSIONING.md）
//   v1.0.0 より前（0.x）は、公開した窓口でも、壊す変更が入ることがある（変更は CHANGELOG.md に書く）
//   v1.0.0 からは、窓口（Core/Interfaces）と、開発手順に書いた公開ヘッダーについて、
//   壊す変更はメジャー、機能の追加はマイナー、修正はパッチで版を上げる
//=============================================================================
namespace NVersion
{
	constexpr int Major = 0;
	constexpr int Minor = 9;
	constexpr int Patch = 0;

	// "0.9.0" のように、ログや報告に出す文字列（上の数字と同じにする）
	constexpr const char* String = "0.9.0";

	// 製品の名前（ログや報告の先頭に出す）
	constexpr const char* ProductName = "mazigatiDX";
}

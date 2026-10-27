// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Platform/WindowSettings.h"

//=============================================================================
// NGameMain
//   アプリの入口。ゲームの main から 1 行で呼ぶ。
//   リーク報告・ログ（コンソールとファイル）・窓の設定ファイルの読み込みを済ませてから、CEngine でゲームを動かし、終了時にログを片付ける。
//
//   // Source/main.cpp
//   int main()
//   {
//       return NGameMain::RunGame<CMyGame>();
//   }
//
//   窓の設定は Config/ScreenConfig.json から読む（作業ディレクトリ -> exe のフォルダ -> その親の順に探す）。
//   無い項目や、ファイルが無いときは FOptions::DefaultWindow の値を使う。
//   実行ファイルは /SUBSYSTEM:WINDOWS・/ENTRY:mainCRTStartup でリンクする（Build/mazigatiDX.props が設定する）ので、WinMain ではなく main でよい。
//=============================================================================
namespace NGameMain
{
	// 起動の設定
	struct FOptions
	{
		// 設定ファイルに無い項目と、設定ファイルが読めないときに使う窓の設定
		FWindowSettings DefaultWindow;

		// 窓の設定ファイル
		FString ScreenConfigFile = "Config/ScreenConfig.json";
	};

	// 準備 -> RunEngine -> 後片付けの本体。RunEngine には、設定ファイルを反映した窓の設定が渡され、終了コードを返す。
	// 通常は RunGame を使う。戻り値は終了コード。
	int32 Run( const FOptions& Options, const TCallable<int32( const FWindowSettings& )>& RunEngine );

	// TGameInstance（CGameInstance の派生）を動かす。戻り値は終了コード。
	template<typename TGameInstance>
	int32 RunGame( const FOptions& Options = FOptions() )
	{
		return Run( Options, []( const FWindowSettings& Settings )
					{
						CEngine Engine;
						return Engine.Run<TGameInstance>( Settings );
					} );
	}
}

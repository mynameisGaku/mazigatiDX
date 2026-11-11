// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/UI/Style/UIThemeLoader.h"

//=============================================================================
// CUIThemeDocument: テーマのファイル（.gxuitheme）を読み、ファイルが変わったら読み直す（ホットリロード）
//
//   CUIThemeDocument Document;
//   Document.Load( Assets, "UI/Themes/Blue.gxuitheme", Base );   // Base は、親のテーマ（無くてもよい）
//   Context.SetTheme( Document.GetTheme() );
//   ...毎フレームか、数秒おきに...
//   if ( Document.ReloadIfChanged( Assets ) )
//   {
//       // 読み直した。GetTheme() は、同じテーマのまま、中身だけが新しい（これを親にしている子のテーマにも、そのまま効く）。
//       // もう作ってある UI は、ブラシや色を写して持っているので、作り直す（CUIMarkupDocument::Rebuild など）
//   }
//
// 読み直しに失敗したとき（JSON が壊れている途中など）は、前の中身のまま（GetLastError() に理由）。
// 画像やマテリアルそのものの中身の変更は、見ない（テーマのファイルの文字だけを比べる）。
//=============================================================================
class CUIThemeDocument
{
public:
	// 読み込む。成功したら true（GetTheme が使える）。失敗したら false（GetLastError に理由。前に読めていたテーマは、そのまま）
	bool Load( CAssetSubsystem& Assets, const FString& InPath, const TSharedPtr<FUITheme>& Parent );

	// ファイルの中身が前と違えば、読み直す。読み直して成功したときだけ true
	bool ReloadIfChanged( CAssetSubsystem& Assets );

	const TSharedPtr<FUITheme>& GetTheme() const { return Theme; }
	const FString& GetPath() const { return Path; }
	const FString& GetLastError() const { return LastError; }

private:
	FString Path;
	FString LoadedText;
	TSharedPtr<FUITheme> Theme;
	FString LastError;
};

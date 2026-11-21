// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// ERenderLook
//   描画の見た目の水準。公開 API・命令の順序・状態・座標・深度の意味は、どちらでも同じで、変わるのは、色の再現・画像の縮小・縁・影・ポスト処理などの「見た目」だけ。
//   Compat   互換見た目: 従来（DxLib）の式を再現する水準。命令の順序・状態・座標・深度・合成の意味を DxLib と画素で比べる検査は、この水準で行う。
//   Standard 標準見た目: DX12 が、DxLib では出せない綺麗な描画をする水準（画像のミップマップなど。R7b のルックデブで増やしていく）。
//   いまの既定は Compat（標準見た目の機能が揃って、検査を移してから、既定を Standard に替える。ロードマップの「完全互換」の判定）。
//   DX12 の起動オプション --dx12-look=compat | standard で選ぶ（DxLib・Null は、見た目の水準を持たず、いつも従来の見た目）。
//=============================================================================
enum class ERenderLook : uint8
{
	Compat,
	Standard,
};

namespace NRenderLook
{
	// 起動オプションの文字列（"compat" / "standard"。大文字小文字は問わない）から、水準を読む。読めなければ false（OutLook は変えない）
	bool TryParse( const char* Text, ERenderLook& OutLook );

	// 水準の名前（ログ・画面の表示用）
	const char* ToString( ERenderLook Look );
}

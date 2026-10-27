#include "Library/Framework/Engine/GameMain.h"
#include "Source/Application/GameApplication.h"

// アプリの入口。窓の設定は Config/ScreenConfig.json から読み込まれる（無ければ既定値）。
// 実行ファイルは Build/mazigatiDX.props の設定で /ENTRY:mainCRTStartup にリンクされるので、WinMain ではなく main でよい
int main()
{
	return NGameMain::RunGame<CGameApplication>();
}

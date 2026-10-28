#include "Library/Framework/Engine/GameMain.h"
#include "Source/Application/SamplesApplication.h"

// アプリの入口。窓の設定は Config/ScreenConfig.json から読み込まれる（無ければ既定値）
int main()
{
	return NGameMain::RunGame<CSamplesApplication>();
}

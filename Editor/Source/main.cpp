#include "Library/Framework/Engine/GameMain.h"
#include "Source/Application/EditorApplication.h"

// エディタ専用の実行ファイルの入口。ウィンドウの設定は Config/ScreenConfig.json から読み込まれる（無ければ既定値）
int main()
{
	return NGameMain::RunGame<CEditorApplication>();
}

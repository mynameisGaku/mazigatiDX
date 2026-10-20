#pragma once
#include "Library/Core/Interfaces/Inspector/IPropertyInspector.h"

class CScene;
class CGameObject;
class FSubsystemCollection;

//=============================================================================
// FImGuiPropertyInspector
//   IPropertyInspector の ImGui 実装。編集できる項目は ImGui のウィジェットで直接書き換える。
//=============================================================================
class FImGuiPropertyInspector : public IPropertyInspector
{
public:
	void Text( const char* Label, const FString& Value ) override;
	bool Bool( const char* Label, bool& Value ) override;
	bool Int( const char* Label, int32& Value ) override;
	bool Float( const char* Label, float32& Value ) override;
	bool Vector3( const char* Label, FVector3& Value ) override;
};

//=============================================================================
// DebugUI
//   フレームワークのオブジェクトを ImGui のツリーで表示する（ImGui のウィンドウ内で呼ぶ）
//=============================================================================
namespace NDebugUI
{
	// シーンのオブジェクトを親子階層で表示する。各オブジェクトの下に
	// プロパティ・コンポーネント（とそのプロパティ）・子オブジェクトが自動で並ぶ
	void DrawSceneHierarchy( CScene& Scene );

	// オブジェクト 1 つ分（子も再帰的に表示する）
	void DrawGameObject( CGameObject& Object );

	// サブシステムの一覧（各サブシステムのプロパティも表示する）
	void DrawSubsystems( const char* Label, const FSubsystemCollection& Subsystems );
}

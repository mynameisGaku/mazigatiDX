#pragma once
#include "Library/Core/Types/CompilerMacros.h"
#include <type_traits>
#include <typeinfo>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Object/Object.h"

class FSubsystemCollection;

//=============================================================================
// CSubsystem
//   所有者（エンジン・ゲームインスタンス・シーン）と同じ寿命で自動生成されるオブジェクトの基底。
//   直接継承せず、CAppSubsystem / CGameInstanceSubsystem / CSceneSubsystem のどれかを継承する。
//
//   - 所有者の生成時に、登録済みのサブシステムがすべて生成され Initialize される
//   - 所有者の破棄時に、Initialize と逆の順番で Deinitialize される
//   - ITickable も継承すると毎フレーム Tick される
//=============================================================================
class CSubsystem : public CObject
{
public:
	// 他のサブシステムに依存するなら、ここで Collection.InitializeDependency<T>() を呼ぶ
	virtual void Initialize( FSubsystemCollection& Collection );
	virtual void Deinitialize();
};

//=============================================================================
// サブシステムの登録
//   サブシステムの .cpp に REGISTER_SUBSYSTEM( クラス名 ) を 1 行書くだけで、
//   所有者の生成時に自動で生成されるようになる（ヘッダーには書かない）。
//
//   // MySubsystem.cpp
//   REGISTER_SUBSYSTEM( CMySubsystem )
//
//   ※ クラス名は名前空間なしで書ける名前にする（## で変数名を作るため）
//=============================================================================
namespace NSubsystemRegistry
{
	template<typename TBase>
	struct TEntry
	{
		const std::type_info* Type;
		TUniquePtr<TBase> ( *Factory )();
	};

	// TBase ( CAppSubsystem など ) ごとの登録一覧
	template<typename TBase>
	FORCEINLINE TArray<TEntry<TBase>>& GetEntries()
	{
		static TArray<TEntry<TBase>> Entries;
		return Entries;
	}

	template<typename T>
	bool Register();
}

#define REGISTER_SUBSYSTEM( ClassName ) \
	static const bool GSubsystemRegistered_##ClassName = NSubsystemRegistry::Register<ClassName>();

#include "Subsystem.inl"

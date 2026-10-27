#pragma once
#include "Library/Core/Types/CompilerMacros.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Subsystem/Subsystem.h"

//=============================================================================
// FSubsystemCollection
//   1 つの所有者に属するサブシステムの集まり。所有者がメンバとして持つ。
//
//   Collection.Initialize<CSceneSubsystem>( Scene );   // 登録済みの CSceneSubsystem 派生をすべて生成
//   Collection.Tick( DeltaTime, ETickGroup::PreUpdate );
//   CPhysicsSubsystem* Physics = Collection.Get<CPhysicsSubsystem>();
//   IInput* Input = Collection.Get<IInput>();           // インターフェースでも取得できる
//   Collection.Deinitialize();
//=============================================================================
class FSubsystemCollection
{
public:
	FSubsystemCollection() = default;
	FORCEINLINE ~FSubsystemCollection() { Deinitialize(); }

	FSubsystemCollection( const FSubsystemCollection& ) = delete;
	FSubsystemCollection& operator=( const FSubsystemCollection& ) = delete;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	// TBase に登録されているサブシステムを生成して Initialize する（順番はクラス名順）
	template<typename TBase, typename TOuter>
	void Initialize( TOuter& Outer );

	// Initialize と逆の順番で Deinitialize して破棄する
	void Deinitialize();

	// ログとプロファイラーに出す、このコレクションの持ち主の名前（"App" / "GameInstance" / シーン名）
	void SetLabel( const FString& InLabel ) { Label = InLabel; }
	const FString& GetLabel() const { return Label; }

	// Initialize の中から呼ぶ。T を先に Initialize させて返す（無ければ nullptr）
	template<typename T>
	FORCEINLINE T* InitializeDependency()
	{
		return dynamic_cast<T*>( InitializeMatching( []( CSubsystem* Subsystem ) { return dynamic_cast<T*>( Subsystem ) != nullptr; } ) );
	}

	//-------------------------------------------------------------------------
	// 参照
	//-------------------------------------------------------------------------
	// 初期化済みで T（クラスでもインターフェースでもよい）にキャストできる最初のもの
	template<typename T>
	FORCEINLINE T* Get() const
	{
		for ( CSubsystem* Subsystem : InitializedSubsystems )
		{
			if ( T* Casted = dynamic_cast<T*>( Subsystem ) ) return Casted;
		}
		return nullptr;
	}

	// 初期化順
	FORCEINLINE const TArray<CSubsystem*>& GetAll() const { return InitializedSubsystems; }

	//-------------------------------------------------------------------------
	// 更新
	//-------------------------------------------------------------------------
	// ITickable を継承しているもののうち、Group に属するものを初期化順に Tick する
	void Tick( float32 DeltaTime, ETickGroup Group );

private:
	FString Label;

	enum class EState : uint8
	{
		Uninitialized,
		Initializing,
		Initialized,
	};

	struct FEntry
	{
		TUniquePtr<CSubsystem> Subsystem;
		EState State;
	};

	void InitializeAll();
	// ShouldCreateSubsystem が false で生成しなかったときのログ
	void NotifySkipped( const CSubsystem& Subsystem ) const;
	CSubsystem* InitializeEntry( int32 Index );
	CSubsystem* InitializeMatching( bool ( *Predicate )( CSubsystem* ) );

private:
	TArray<FEntry> Entries;
	TArray<CSubsystem*> InitializedSubsystems;
	TArray<ITickable*> Tickables;
};

#include "SubsystemCollection.inl"

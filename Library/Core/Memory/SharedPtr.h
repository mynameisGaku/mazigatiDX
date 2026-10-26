#pragma once
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Types/CompilerMacros.h"
#include <new>
#include <type_traits>
#include <utility>

#include "Library/Core/Collection/CollectionCommon.h"
#include "Library/Core/Collection/Hash.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Core/Types/Utility.h"

template<typename T> class TSharedPtr;
template<typename T> class TWeakPtr;
template<typename T> class TSharedFromThis;

//=============================================================================
// 参照カウント（内部実装）
//   SharedCount: TSharedPtr の数。0 でオブジェクトを破棄
//   WeakCount  : TWeakPtr の数 + ( SharedCount > 0 ? 1 : 0 )。0 で制御ブロックを破棄
//   カウント操作はアトミックなので、別スレッドの TSharedPtr が同じ対象を共有してよい。
//=============================================================================
namespace NSharedPtrDetail
{
	class FRefController
	{
	public:
		FRefController() = default;
		FRefController( const FRefController& ) = delete;
		FRefController& operator=( const FRefController& ) = delete;

		FORCEINLINE void AddShared() noexcept { SharedCount.FetchAdd( 1, EMemoryOrder::Relaxed ); }
		FORCEINLINE void AddWeak() noexcept { WeakCount.FetchAdd( 1, EMemoryOrder::Relaxed ); }

		// SharedCount が 0 でなければ +1 して true（TWeakPtr::Pin 用）
		FORCEINLINE bool TryAddShared() noexcept
		{
			int32 Count = SharedCount.Load( EMemoryOrder::Relaxed );
			while ( Count > 0 )
			{
				if ( SharedCount.CompareExchangeWeak( Count, Count + 1, EMemoryOrder::AcquireRelease, EMemoryOrder::Relaxed ) ) return true;
			}
			return false;
		}

		FORCEINLINE void ReleaseShared() noexcept
		{
			if ( SharedCount.FetchSub( 1, EMemoryOrder::AcquireRelease ) == 1 )
			{
				DestroyObject();
				ReleaseWeak();
			}
		}

		FORCEINLINE void ReleaseWeak() noexcept
		{
			if ( WeakCount.FetchSub( 1, EMemoryOrder::AcquireRelease ) == 1 )
			{
				DestroySelf();
			}
		}

		FORCEINLINE int32 GetSharedCount() const noexcept { return SharedCount.Load( EMemoryOrder::Relaxed ); }

	protected:
		virtual ~FRefController() = default;
		virtual void DestroyObject() noexcept = 0;
		virtual void DestroySelf() noexcept { delete this; }

	private:
		TAtomic<int32> SharedCount { 1 };
		TAtomic<int32> WeakCount { 1 };
	};

	// 生ポインタ + デリータを持つ制御ブロック
	template<typename T, typename Deleter>
	class TRefControllerWithDeleter final : public FRefController
	{
	public:
		FORCEINLINE TRefControllerWithDeleter( T* InObject, Deleter InDeleter ) : Object( InObject ), Delete( MoveTemp( InDeleter ) ) {}

	private:
		void DestroyObject() noexcept override { Delete( Object ); }

		T*      Object;
		Deleter Delete;  // 関数ポインタやラムダも使えるようメンバで持つ
	};

	// オブジェクトを制御ブロック内に直接持つ（MakeShared 用。確保 1 回）
	template<typename T>
	class TRefControllerInline final : public FRefController
	{
	public:
		template<typename... Args>
		FORCEINLINE explicit TRefControllerInline( Args&&... args )
		{
			new ( Storage ) T( Forward<Args>( args )... );
		}

		FORCEINLINE T* GetObjectPtr() noexcept { return std::launder( reinterpret_cast<T*>( Storage ) ); }

	private:
		void DestroyObject() noexcept override { GetObjectPtr()->~T(); }

		alignas( T ) unsigned char Storage[sizeof( T )];
	};

	// T が TSharedFromThis を継承していれば、内部の弱参照を設定する
	template<typename U>
	FORCEINLINE auto EnableSharedFromThis( FRefController* Controller, U* Object, int ) -> decltype( Object->InternalSetWeakThis( Controller, Object ), void() )
	{
		if ( Object ) Object->InternalSetWeakThis( Controller, Object );
	}
	FORCEINLINE void EnableSharedFromThis( FRefController*, const volatile void*, long ) {}

	struct FStaticCastTag {};
	struct FConstCastTag {};
	struct FDynamicCastTag {};
}

//=============================================================================
// TSharedPtr<T>
//   参照カウント式の共有スマートポインタ。最後の TSharedPtr が消えると自動で delete。
//
//   TSharedPtr<CTexture> Tex = MakeShared<CTexture>( "a.png" );
//   TSharedPtr<CTexture> Copy = Tex;            // 共有（カウント +1）
//   TWeakPtr<CTexture>   Weak = Tex;            // 弱参照（カウントに含まれない）
//   if ( TSharedPtr<CTexture> Pinned = Weak.Pin() ) ...
//=============================================================================
template<typename T>
class TSharedPtr
{
	using FRefController = NSharedPtrDetail::FRefController;

	template<typename> friend class TSharedPtr;
	template<typename> friend class TWeakPtr;
	template<typename U, typename... Args> friend TSharedPtr<U> MakeShared( Args&&... args );

public:
	using ElementType = T;

	//-------------------------------------------------------------------------
	// 生成・破棄
	//-------------------------------------------------------------------------
	TSharedPtr() noexcept = default;
	FORCEINLINE TSharedPtr( decltype( nullptr ) ) noexcept {}

	// 生ポインタの所有権を受け取る（delete で破棄）
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	explicit TSharedPtr( U* InObject )
		: TSharedPtr( InObject, TDefaultDelete<U>() )
	{}

	// 生ポインタの所有権を受け取る（Deleter( Ptr ) で破棄）
	template<typename U, typename Deleter, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TSharedPtr( U* InObject, Deleter InDeleter )
	{
		if ( !InObject ) return;
		Object     = InObject;
		Controller = new NSharedPtrDetail::TRefControllerWithDeleter<U, Deleter>( InObject, MoveTemp( InDeleter ) );
		NSharedPtrDetail::EnableSharedFromThis( Controller, InObject, 0 );
	}

	// TUniquePtr から所有権を移す
	template<typename U, typename E, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	TSharedPtr( TUniquePtr<U, E>&& Unique )
		: TSharedPtr( Unique.Get(), Unique.GetDeleter() )
	{
		(void)Unique.Release();
	}

	FORCEINLINE TSharedPtr( const TSharedPtr& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		if ( Controller ) Controller->AddShared();
	}

	FORCEINLINE TSharedPtr( TSharedPtr&& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		Other.Object     = nullptr;
		Other.Controller = nullptr;
	}

	// TSharedPtr<Derived> -> TSharedPtr<Base>
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TSharedPtr( const TSharedPtr<U>& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		if ( Controller ) Controller->AddShared();
	}

	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TSharedPtr( TSharedPtr<U>&& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		Other.Object     = nullptr;
		Other.Controller = nullptr;
	}

	// エイリアシング: Other と寿命を共有しつつ、別のポインタ（メンバなど）を指す
	template<typename U>
	FORCEINLINE TSharedPtr( const TSharedPtr<U>& Other, T* InObject ) noexcept : Object( InObject ), Controller( Other.Controller )
	{
		if ( Controller ) Controller->AddShared();
	}

	FORCEINLINE ~TSharedPtr()
	{
		if ( Controller ) Controller->ReleaseShared();
	}

	FORCEINLINE TSharedPtr& operator=( const TSharedPtr& Other ) noexcept
	{
		TSharedPtr( Other ).SwapWith( *this );
		return *this;
	}
	FORCEINLINE TSharedPtr& operator=( TSharedPtr&& Other ) noexcept
	{
		TSharedPtr( MoveTemp( Other ) ).SwapWith( *this );
		return *this;
	}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TSharedPtr& operator=( const TSharedPtr<U>& Other ) noexcept
	{
		TSharedPtr( Other ).SwapWith( *this );
		return *this;
	}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TSharedPtr& operator=( TSharedPtr<U>&& Other ) noexcept
	{
		TSharedPtr( MoveTemp( Other ) ).SwapWith( *this );
		return *this;
	}
	FORCEINLINE TSharedPtr& operator=( decltype( nullptr ) ) noexcept
	{
		Reset();
		return *this;
	}

	//-------------------------------------------------------------------------
	// 操作
	//-------------------------------------------------------------------------
	FORCEINLINE void Reset() noexcept { TSharedPtr().SwapWith( *this ); }

	FORCEINLINE T* Get() const noexcept { return Object; }
	FORCEINLINE bool IsValid() const noexcept { return Object != nullptr; }
	FORCEINLINE explicit operator bool() const noexcept { return Object != nullptr; }

	FORCEINLINE T& operator*() const { return *Object; }
	FORCEINLINE T* operator->() const noexcept { return Object; }

	// このオブジェクトを共有している TSharedPtr の数
	FORCEINLINE int32 GetSharedReferenceCount() const noexcept { return Controller ? Controller->GetSharedCount() : 0; }
	FORCEINLINE bool IsUnique() const noexcept { return GetSharedReferenceCount() == 1; }

	//-------------------------------------------------------------------------
	// 比較
	//-------------------------------------------------------------------------
	template<typename U>
	bool operator==( const TSharedPtr<U>& Other ) const noexcept { return Object == Other.Get(); }
	template<typename U>
	bool operator!=( const TSharedPtr<U>& Other ) const noexcept { return Object != Other.Get(); }
	bool operator==( decltype( nullptr ) ) const noexcept { return Object == nullptr; }
	bool operator!=( decltype( nullptr ) ) const noexcept { return Object != nullptr; }

	friend FORCEINLINE uint32 GetTypeHash( const TSharedPtr& P ) { return GetTypeHash( P.Object ); }

	//-------------------------------------------------------------------------
	// キャスト用（StaticCastSharedPtr などから使う）
	//-------------------------------------------------------------------------
	template<typename U>
	TSharedPtr( const TSharedPtr<U>& Other, NSharedPtrDetail::FStaticCastTag ) noexcept
		: TSharedPtr( Other, static_cast<T*>( Other.Object ) ) {}

	template<typename U>
	TSharedPtr( const TSharedPtr<U>& Other, NSharedPtrDetail::FConstCastTag ) noexcept
		: TSharedPtr( Other, const_cast<T*>( Other.Object ) ) {}

	template<typename U>
	FORCEINLINE TSharedPtr( const TSharedPtr<U>& Other, NSharedPtrDetail::FDynamicCastTag ) noexcept
	{
		if ( T* Casted = dynamic_cast<T*>( Other.Object ) )
		{
			Object     = Casted;
			Controller = Other.Controller;
			if ( Controller ) Controller->AddShared();
		}
	}

private:
	// 既に +1 済みの制御ブロックを受け取る
	FORCEINLINE TSharedPtr( T* InObject, FRefController* InController, int ) noexcept : Object( InObject ), Controller( InController ) {}

	FORCEINLINE void SwapWith( TSharedPtr& Other ) noexcept
	{
		T* TempObject = Object;
		Object = Other.Object;
		Other.Object = TempObject;

		FRefController* TempController = Controller;
		Controller = Other.Controller;
		Other.Controller = TempController;
	}

	T*              Object     = nullptr;
	FRefController* Controller = nullptr;
};

//=============================================================================
// TWeakPtr<T>
//   TSharedPtr の弱参照。オブジェクトの寿命を延ばさない（循環参照の回避に使う）。
//   使うときは Pin() で TSharedPtr を得る（破棄済みなら空）。
//=============================================================================
template<typename T>
class TWeakPtr
{
	using FRefController = NSharedPtrDetail::FRefController;

	template<typename> friend class TWeakPtr;
	template<typename> friend class TSharedFromThis;

public:
	using ElementType = T;

	TWeakPtr() noexcept = default;
	FORCEINLINE TWeakPtr( decltype( nullptr ) ) noexcept {}

	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TWeakPtr( const TSharedPtr<U>& Shared ) noexcept : Object( Shared.Object ), Controller( Shared.Controller )
	{
		if ( Controller ) Controller->AddWeak();
	}

	FORCEINLINE TWeakPtr( const TWeakPtr& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		if ( Controller ) Controller->AddWeak();
	}

	FORCEINLINE TWeakPtr( TWeakPtr&& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		Other.Object     = nullptr;
		Other.Controller = nullptr;
	}

	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TWeakPtr( const TWeakPtr<U>& Other ) noexcept : Object( Other.Object ), Controller( Other.Controller )
	{
		if ( Controller ) Controller->AddWeak();
	}

	FORCEINLINE ~TWeakPtr()
	{
		if ( Controller ) Controller->ReleaseWeak();
	}

	FORCEINLINE TWeakPtr& operator=( const TWeakPtr& Other ) noexcept
	{
		TWeakPtr( Other ).SwapWith( *this );
		return *this;
	}
	FORCEINLINE TWeakPtr& operator=( TWeakPtr&& Other ) noexcept
	{
		TWeakPtr( MoveTemp( Other ) ).SwapWith( *this );
		return *this;
	}
	template<typename U, typename = std::enable_if_t<std::is_convertible_v<U*, T*>>>
	FORCEINLINE TWeakPtr& operator=( const TSharedPtr<U>& Shared ) noexcept
	{
		TWeakPtr( Shared ).SwapWith( *this );
		return *this;
	}
	FORCEINLINE TWeakPtr& operator=( decltype( nullptr ) ) noexcept
	{
		Reset();
		return *this;
	}

	// 生きていれば TSharedPtr を返す（破棄済みなら空）
	FORCEINLINE TSharedPtr<T> Pin() const noexcept
	{
		if ( Controller && Controller->TryAddShared() ) return TSharedPtr<T>( Object, Controller, 0 );
		return TSharedPtr<T>();
	}

	// 参照先がまだ生きているか（別スレッドで破棄され得る場合は Pin() の結果で判定すること）
	FORCEINLINE bool IsValid() const noexcept { return Controller && Controller->GetSharedCount() > 0; }

	FORCEINLINE void Reset() noexcept { TWeakPtr().SwapWith( *this ); }

	// 同じオブジェクトを指しているか（破棄済みでも比較できる）
	template<typename U>
	FORCEINLINE bool HasSameObject( const U* Other ) const noexcept { return Object == Other; }

private:
	FORCEINLINE TWeakPtr( T* InObject, FRefController* InController ) noexcept : Object( InObject ), Controller( InController )
	{
		if ( Controller ) Controller->AddWeak();
	}

	FORCEINLINE void SwapWith( TWeakPtr& Other ) noexcept
	{
		T* TempObject = Object;
		Object = Other.Object;
		Other.Object = TempObject;

		FRefController* TempController = Controller;
		Controller = Other.Controller;
		Other.Controller = TempController;
	}

	T*              Object     = nullptr;
	FRefController* Controller = nullptr;
};

//=============================================================================
// TSharedFromThis<T>
//   これを継承したクラスは、メンバ関数内で自分の TSharedPtr を得られる。
//   ※ TSharedPtr で管理されているオブジェクトでのみ有効（コンストラクタ内では不可）
//
//   class CEnemy : public TSharedFromThis<CEnemy>
//   {
//       void Register() { Manager.Add( AsShared() ); }
//   };
//=============================================================================
template<typename T>
class TSharedFromThis
{
public:
	FORCEINLINE TSharedPtr<T> AsShared()
	{
		TSharedPtr<T> Shared = WeakThis.Pin();
		COLLECTION_CHECK( Shared.IsValid(), "AsShared() called on an object not owned by TSharedPtr" );
		return Shared;
	}
	FORCEINLINE TSharedPtr<const T> AsShared() const
	{
		TSharedPtr<const T> Shared = TWeakPtr<const T>( WeakThis ).Pin();
		COLLECTION_CHECK( Shared.IsValid(), "AsShared() called on an object not owned by TSharedPtr" );
		return Shared;
	}

	FORCEINLINE TWeakPtr<T> AsWeak() { return WeakThis; }

	// 内部用（TSharedPtr の生成時に呼ばれる）
	template<typename U>
	FORCEINLINE void InternalSetWeakThis( NSharedPtrDetail::FRefController* Controller, U* Object ) const
	{
		if ( !WeakThis.IsValid() ) WeakThis = TWeakPtr<T>( static_cast<T*>( Object ), Controller );
	}

protected:
	TSharedFromThis() = default;
	TSharedFromThis( const TSharedFromThis& ) {}               // コピーしても弱参照は引き継がない
	TSharedFromThis& operator=( const TSharedFromThis& ) { return *this; }
	~TSharedFromThis() = default;

private:
	mutable TWeakPtr<T> WeakThis;
};

//=============================================================================
// MakeShared（オブジェクトと参照カウントを 1 回の確保で作る）
//   auto P = MakeShared<CEnemy>( Args... );
//=============================================================================
template<typename T, typename... Args>
FORCEINLINE TSharedPtr<T> MakeShared( Args&&... args )
{
	auto* Controller = new NSharedPtrDetail::TRefControllerInline<T>( Forward<Args>( args )... );
	T* Object = Controller->GetObjectPtr();
	NSharedPtrDetail::EnableSharedFromThis( Controller, Object, 0 );
	return TSharedPtr<T>( Object, Controller, 0 );
}

//=============================================================================
// キャスト
//=============================================================================
template<typename To, typename From>
FORCEINLINE TSharedPtr<To> StaticCastSharedPtr( const TSharedPtr<From>& Ptr ) { return TSharedPtr<To>( Ptr, NSharedPtrDetail::FStaticCastTag() ); }

template<typename To, typename From>
FORCEINLINE TSharedPtr<To> ConstCastSharedPtr( const TSharedPtr<From>& Ptr ) { return TSharedPtr<To>( Ptr, NSharedPtrDetail::FConstCastTag() ); }

// 失敗すると空
template<typename To, typename From>
FORCEINLINE TSharedPtr<To> DynamicCastSharedPtr( const TSharedPtr<From>& Ptr ) { return TSharedPtr<To>( Ptr, NSharedPtrDetail::FDynamicCastTag() ); }

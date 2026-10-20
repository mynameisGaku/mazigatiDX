#pragma once
#include <climits>
#include <memory>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/CompilerMacros.h"
#include "Library/Framework/Ecs/EcsWorld.h"
#include "Library/Framework/Ecs/SystemParams.h"
#include "Library/Framework/Ecs/TransformWorld.h"
#include "Library/Framework/Object/GameObject.h"
#include "Library/Framework/Render/DrawContext.h"

//=============================================================================
// システム（ECS の「処理」）。スレッドもスケジュールも書かずに、並列に動く。
//
//   Scene.AddSystem( "Motion.Angular",
//       []( float32 DeltaTime, FTransformAccess& Transform, const FAngularMotion& Motion )
//       {
//           Transform.SetLocalRotation( ... );
//       } );
//
//   - 関数の引数の型が、そのまま「何を読み書きするか」の宣言になる。エンジンはそこから、
//       1) 引数の必須コンポーネント（T& / const T&）をすべて持ち、除外（TWithout<...>）を持たないオブジェクトを探し
//          （一番数が少ない型からDense配列を直に回すので速い）
//       2) それらを細かい塊に分けて、ジョブシステムのワーカーで並列に処理し
//       3) 同じデータを書き込む（または、書き込むものを読む）システム同士は、登録順に 1 つずつ、
//          互いに触らないシステム同士は、同時に実行する
//     を自動で行う。ワーカーの数がいくつでも（0 でも）結果は完全に同じ。
//   - ポインタ引数（T* / const T*）にすると「持っていればポインタ、無ければ nullptr」が渡る（任意コンポーネント）。
//   - NEcs::TWithout<A, B> を引数に置くと、A または B を持つオブジェクトを対象から外せる。
//   - 関数の中でしてよいのは、引数で渡されたものの読み書きだけ。引数に無いデータ・ほかのオブジェクト・サブシステム・DxLib・
//     SpawnObject などには触れない（並列に動くので、ぶつかる）。オブジェクトの破棄は FEntityRef::RequestDestroy()
//
//   実行の位置: PreUpdate = シーンの OnTick の後、各オブジェクトの Tick の前 / PostUpdate = 各オブジェクトの Tick の後
//
// 即時クエリ（Scene.ForEach）
//   システムの登録なしで、その場で条件に合うオブジェクトを回したいときに使う（OnTick やイベント処理の中など）。
//   ラムダの引数から自動で絞り込まれ、bool を返すラムダなら false で途中終了できる。
//
//   Scene.ForEach( []( CGameObject& Obj, FHealth& Health, const FShield* Shield, NEcs::TWithout<FInvincible> )
//   {
//       Health.Value -= Shield ? 5.0f : 10.0f;
//       if ( Health.Value <= 0.0f ) Obj.Destroy();
//   } );
//
// 描画システム（AddDrawSystem）
//   描画も同じ仕組みで並列にできる。ただし DxLib は複数のスレッドから呼べないので、描画システムは DxLib を呼ばずに
//   「何を描くか」を FDrawContext に記録するだけにして、記録されたものを、メインスレッドが順番に DxLib へ流す（IDrawExecutor）。
//
//   Scene.AddDrawSystem( "Marker.Draw", []( FDrawContext& Draw, const FTransformView& Transform, const FMarker& Marker )
//   {
//       const FVector3 Position = Transform.GetWorldPosition();
//       Draw.Box( X - 4, Y - 4, X + 4, Y + 4, Marker.Color );
//   } );
//=============================================================================
namespace NEcs
{
	enum class EPhase : uint8
	{
		PreUpdate,
		PostUpdate,
		Draw,   // 描画システム（AddDrawSystem で登録する）
	};

	// 1 つの塊の大きさ（2 のべき乗）。システム 1 つの対象は、これ以下の塊に分けて、ワーカーが取り合う
	constexpr int32 DrawBatchShift = 10;
	constexpr int32 EntitiesPerBatch = 1 << DrawBatchShift;

	// 1 つのシステムが触るデータ（型のキーと、書き込むか）
	struct FAccess
	{
		const void* Key;
		bool bWrite;
	};

	// 描画システムが、1 つのエンティティについて記録したコマンドの範囲（その塊のバッファの中のワードの [Begin, End)）
	struct FDrawRange
	{
		uint32 Begin = 0;
		uint32 End = 0;
		int32 Entity = INDEX_NONE;   // 記録したエンティティ（構造が変わって Dense の並びが変わっても、取り違えないため）
	};

	// 型を消したシステム 1 つ
	class ISystemRunner
	{
	public:
		virtual ~ISystemRunner() = default;

		virtual void GetAccess( std::vector<FAccess>& Out ) const = 0;
		// 実行の前にメインスレッドで呼ぶ。入れ物を見つけ、回す対象の数を返す。
		// MaxObjectId より後に生成されたオブジェクト（今フレームはまだ Tick されないもの）は対象にしない
		virtual int32 Prepare( FEcsWorld& World, FTransformWorld& Transforms, float32 DeltaTime, uint64 MaxObjectId ) = 0;
		// 対象の [Begin, End) を処理する（複数のスレッドから、重ならない範囲で呼ばれる）
		virtual void RunRange( int32 Begin, int32 End ) = 0;

		// 描画システムだけ: 直前の実行で、このエンティティについて記録されたコマンド（無ければ false）。メインスレッドから仮想関数を介さず引く
		FORCEINLINE bool GetDrawSpan( int32 Entity, FDrawSpan& Out ) const
		{
			const int32 Index = Driver != nullptr ? Driver->IndexOf( Entity ) : Entity;
			if ( static_cast<uint32>( Index ) >= static_cast<uint32>( NumPrepared ) ) return false;

			const FDrawRange& Range = Ranges[static_cast<size_t>( Index )];
			if ( Range.Entity != Entity || Range.End <= Range.Begin ) return false;   // 記録していない（または、記録のあとで構造が変わった）

			const FDrawBuffer& Buffer = Buffers[static_cast<size_t>( static_cast<uint32>( Index ) >> DrawBatchShift )];
			Out.Words = Buffer.Words.data() + Range.Begin;
			Out.NumWords = static_cast<int32>( Range.End - Range.Begin );
			return true;
		}

	protected:
		IComponentStorage* Driver = nullptr;
		int32 NumPrepared = 0;
		std::vector<FDrawBuffer> Buffers;   // 描画システムだけ: 塊ごと
		std::vector<FDrawRange> Ranges;     // 描画システムだけ: 回した対象（Driver の Dense の位置）ごと
	};

	//-------------------------------------------------------------------------
	// 引数の種類の判定
	//-------------------------------------------------------------------------
	template<typename TArg> using TBare = std::remove_cv_t<std::remove_reference_t<TArg>>;
	template<typename TArg> using TPointee = std::remove_cv_t<std::remove_pointer_t<std::remove_reference_t<TArg>>>;

	template<typename T>
	struct TIsWithout : std::false_type
	{
		static FORCEINLINE uint64 Mask() { return 0ull; }
		static FORCEINLINE bool AllIn64() { return true; }
		static FORCEINLINE bool AnyPresent( const FEcsWorld& ) { return false; }
		static FORCEINLINE bool HasAny( const FEcsWorld&, int32 ) { return false; }
	};

	template<typename... TExcluded>
	struct TIsWithout<TWithout<TExcluded...>> : std::true_type
	{
		static FORCEINLINE uint64 Mask() { return ( TypeBit<std::remove_cv_t<TExcluded>>() | ... | 0ull ); }
		static FORCEINLINE bool AllIn64() { return ( ( TypeBit<std::remove_cv_t<TExcluded>>() != 0ull ) && ... ); }
		static FORCEINLINE bool HasAny( const FEcsWorld& World, int32 Entity ) { return ( World.HasData<std::remove_cv_t<TExcluded>>( Entity ) || ... ); }
	};

	template<typename TArg> constexpr bool IsDeltaArg = std::is_same_v<TBare<TArg>, float32>;
	template<typename TArg> constexpr bool IsEntityArg = std::is_same_v<TBare<TArg>, FEntityRef>;
	template<typename TArg> constexpr bool IsObjectRefArg = std::is_same_v<TBare<TArg>, CGameObject> && std::is_reference_v<TArg>;
	template<typename TArg> constexpr bool IsObjectPtrArg = std::is_pointer_v<std::remove_reference_t<TArg>> && std::is_same_v<TPointee<TArg>, CGameObject>;
	template<typename TArg> constexpr bool IsTransformArg = std::is_same_v<TBare<TArg>, FTransformAccess>;
	template<typename TArg> constexpr bool IsDrawArg = std::is_same_v<TBare<TArg>, FDrawContext>;
	template<typename TArg> constexpr bool IsViewArg = std::is_same_v<TBare<TArg>, FTransformView>;
	template<typename TArg> constexpr bool IsWithoutArg = TIsWithout<TBare<TArg>>::value;
	template<typename TArg> constexpr bool IsOptionalComponentArg = std::is_pointer_v<std::remove_reference_t<TArg>> && !IsObjectPtrArg<TArg>;
	template<typename TArg> constexpr bool IsRequiredComponentArg =
		!IsDeltaArg<TArg> && !IsEntityArg<TArg> && !IsObjectRefArg<TArg> && !IsObjectPtrArg<TArg> &&
		!IsTransformArg<TArg> && !IsDrawArg<TArg> && !IsViewArg<TArg> && !IsWithoutArg<TArg> && !IsOptionalComponentArg<TArg>;
	template<typename TArg> constexpr bool IsComponentArg = IsRequiredComponentArg<TArg> || IsOptionalComponentArg<TArg>;

	template<typename TArg> constexpr bool IsWriteRefArg = std::is_reference_v<TArg> && !std::is_const_v<std::remove_reference_t<TArg>>;
	template<typename TArg> constexpr bool IsWritePtrArg = std::is_pointer_v<std::remove_reference_t<TArg>> && !std::is_const_v<std::remove_pointer_t<std::remove_reference_t<TArg>>>;
	template<typename TArg> constexpr bool IsWriteArg = IsOptionalComponentArg<TArg> ? IsWritePtrArg<TArg> : IsWriteRefArg<TArg>;

	template<typename TArg>
	using TComponentType = std::conditional_t<IsOptionalComponentArg<TArg>, TPointee<TArg>, TBare<TArg>>;

	template<typename TArg>
	struct TStoragePointer
	{
		using Type = std::conditional_t<IsComponentArg<TArg>, TComponentStorage<TComponentType<TArg>>*, std::nullptr_t>;
	};

	//-------------------------------------------------------------------------
	// 型付きのシステム
	//   bDraw = 描画システム（読むだけ。出力は FDrawContext に記録され、エンティティごとの範囲が残る）
	//-------------------------------------------------------------------------
	template<bool bDraw, typename TFunction, typename... TArgs>
	class TSystemRunner final : public ISystemRunner
	{
		static_assert( ( ( !IsRequiredComponentArg<TArgs> || std::is_reference_v<TArgs> ) && ... ), "A system's required component parameters must be references (T& or const T&), or pointers (T* / const T*) for optional components." );
		static_assert( ( ( !IsTransformArg<TArgs> || std::is_reference_v<TArgs> ) && ... ), "FTransformAccess must be taken by reference." );
		static_assert( ( ( !IsDrawArg<TArgs> || ( std::is_lvalue_reference_v<TArgs> && !std::is_const_v<std::remove_reference_t<TArgs>> ) ) && ... ),
					   "FDrawContext must be taken as FDrawContext&." );
		static_assert( ( ( !IsViewArg<TArgs> || ( std::is_lvalue_reference_v<TArgs> && std::is_const_v<std::remove_reference_t<TArgs>> ) ) && ... ),
					   "FTransformView must be taken as const FTransformView&." );
		static_assert( bDraw || !( ( IsDrawArg<TArgs> || IsViewArg<TArgs> ) || ... ), "FDrawContext / FTransformView can only be used by draw systems (AddDrawSystem) or Scene.ForEach." );
		static_assert( !bDraw || ( ( !IsDeltaArg<TArgs> && !IsEntityArg<TArgs> && !IsTransformArg<TArgs> && !IsObjectRefArg<TArgs> && !IsObjectPtrArg<TArgs> ) && ... ),
					   "A draw system cannot take DeltaTime / FEntityRef / CGameObject / FTransformAccess (read the transform with const FTransformView&)." );
		static_assert( !bDraw || ( ( !IsComponentArg<TArgs> || !IsWriteArg<TArgs> ) && ... ), "A draw system can only read components (const T& or const T*)." );
		static_assert( !bDraw || ( IsDrawArg<TArgs> || ... ), "A draw system needs a FDrawContext& parameter." );

		static constexpr bool bUsesTransform = ( IsTransformArg<TArgs> || ... ) || ( IsViewArg<TArgs> || ... );
		static constexpr bool bWritesTransform = ( ( IsTransformArg<TArgs> && IsWriteRefArg<TArgs> ) || ... );
		static constexpr size_t NumRequired = ( ( IsRequiredComponentArg<TArgs> ? 1u : 0u ) + ... + 0u );
		static constexpr bool bHasWithout = ( IsWithoutArg<TArgs> || ... );

	public:
		explicit TSystemRunner( TFunction InFunction ) : Function( std::move( InFunction ) ) {}

		void GetAccess( std::vector<FAccess>& Out ) const override
		{
			( AddAccess<TArgs>( Out ), ... );
		}

		int32 Prepare( FEcsWorld& InWorld, FTransformWorld& InTransforms, float32 InDeltaTime, uint64 InMaxObjectId ) override
		{
			World = &InWorld;
			Records = InWorld.GetRecords();
			Transforms = &InTransforms;
			DeltaTime = InDeltaTime;
			MaxObjectId = InMaxObjectId;
			Driver = nullptr;
			RequiredMask = 0ull;
			ExcludedMask = 0ull;
			bFastMaskCheck = true;

			// 必要なコンポーネントのうち、一番数が少ない入れ物から回す（持っていないものは、回るあいだにビットマスクで弾かれる）
			int32 Best = INT_MAX;
			PrepareImpl( Best, std::index_sequence_for<TArgs...>{} );
			const int32 Count = Driver != nullptr ? Driver->Num() : ( NumRequired == 0 ? InWorld.GetNumEntitySlots() : 0 );
			NumPrepared = Count;

			if constexpr ( bWritesTransform )
			{
				if ( Count > 0 ) InTransforms.NotifySystemWrite();
			}

			if constexpr ( bDraw )
			{
				// 塊ごとの記録先と、エンティティごとの範囲を用意する（確保した領域は使い回し、書き込みはワーカー側で行う）
				const size_t NumBatches = static_cast<size_t>( ( Count + EntitiesPerBatch - 1 ) >> DrawBatchShift );
				if ( Buffers.size() < NumBatches ) Buffers.resize( NumBatches );
				for ( size_t i = 0; i < NumBatches; ++i ) Buffers[i].Clear();
				if ( Ranges.size() < static_cast<size_t>( Count ) ) Ranges.resize( static_cast<size_t>( Count ) );
			}
			return Count;
		}

		void RunRange( int32 Begin, int32 End ) override
		{
			const int32* Entities = Driver != nullptr ? Driver->GetEntities() : nullptr;
			if constexpr ( bDraw )
			{
				// Begin は塊の先頭（EntitiesPerBatch の倍数）。塊ごとに別のバッファなので、ほかのスレッドとぶつからない
				FDrawContext Context( Buffers[static_cast<size_t>( static_cast<uint32>( Begin ) >> DrawBatchShift )] );
				if ( Entities != nullptr )
				{
					for ( int32 i = Begin; i < End; ++i ) RunEntity( Entities[i], i, &Context, std::index_sequence_for<TArgs...>{} );
				}
				else
				{
					for ( int32 i = Begin; i < End; ++i ) RunEntity( i, i, &Context, std::index_sequence_for<TArgs...>{} );
				}
			}
			else
			{
				if ( Entities != nullptr )
				{
					for ( int32 i = Begin; i < End; ++i ) RunEntity( Entities[i], i, nullptr, std::index_sequence_for<TArgs...>{} );
				}
				else
				{
					for ( int32 i = Begin; i < End; ++i ) RunEntity( i, i, nullptr, std::index_sequence_for<TArgs...>{} );
				}
			}
		}

	private:
		template<typename TArg>
		static void AddAccess( std::vector<FAccess>& Out )
		{
			if constexpr ( IsComponentArg<TArg> ) Out.push_back( { TypeKey<TComponentType<TArg>>(), IsWriteArg<TArg> } );
			else if constexpr ( IsTransformArg<TArg> ) Out.push_back( { TypeKey<FTransformAccess>(), IsWriteRefArg<TArg> } );
			else if constexpr ( IsViewArg<TArg> ) Out.push_back( { TypeKey<FTransformAccess>(), false } );
		}

		template<size_t... Is>
		void PrepareImpl( int32& Best, std::index_sequence<Is...> )
		{
			( BindStorage<Is>( Best ), ... );
		}

		template<size_t I>
		void BindStorage( int32& Best )
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsRequiredComponentArg<TArg> )
			{
				using TComp = TBare<TArg>;
				TComponentStorage<TComp>& Storage = World->GetStorage<TComp>();
				std::get<I>( Storages ) = &Storage;
				const uint64 Bit = TypeBit<TComp>();
				RequiredMask |= Bit;
				if ( Bit == 0ull ) bFastMaskCheck = false;
				if ( Storage.Num() < Best )
				{
					Best = Storage.Num();
					Driver = &Storage;
				}
			}
			else if constexpr ( IsOptionalComponentArg<TArg> )
			{
				using TComp = TPointee<TArg>;
				std::get<I>( Storages ) = IsWriteArg<TArg> ? &World->GetStorage<TComp>() : World->FindStorage<TComp>();
			}
			else if constexpr ( IsWithoutArg<TArg> )
			{
				ExcludedMask |= TIsWithout<TBare<TArg>>::Mask();
				if ( !TIsWithout<TBare<TArg>>::AllIn64() ) bFastMaskCheck = false;
			}
		}

		// TypeId >= 64 のコンポーネントが含まれる場合のフォールバック判定
		template<size_t I>
		bool HasArgSlow( int32 Entity ) const
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsRequiredComponentArg<TArg> ) return std::get<I>( Storages )->Has( Entity );
			else if constexpr ( IsWithoutArg<TArg> ) return !TIsWithout<TBare<TArg>>::HasAny( *World, Entity );
			else return true;
		}

		template<size_t I>
		FORCEINLINE decltype( auto ) Resolve( [[maybe_unused]] int32 Entity, [[maybe_unused]] int32 Index,
											  [[maybe_unused]] const FEcsWorld::FEntityRecord& Record,
											  [[maybe_unused]] FTransformAccess& Access, [[maybe_unused]] FTransformView& View,
											  [[maybe_unused]] FDrawContext* Context )
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsDeltaArg<TArg> ) return DeltaTime;
			else if constexpr ( IsEntityArg<TArg> ) return FEntityRef( Entity, World );
			else if constexpr ( IsObjectRefArg<TArg> ) return *Record.Object;
			else if constexpr ( IsObjectPtrArg<TArg> ) return Record.Object;
			else if constexpr ( IsTransformArg<TArg> ) return ( Access );
			else if constexpr ( IsViewArg<TArg> ) return ( View );
			else if constexpr ( IsDrawArg<TArg> ) return ( *Context );
			else if constexpr ( IsWithoutArg<TArg> ) return TBare<TArg>{};
			else if constexpr ( IsOptionalComponentArg<TArg> )
			{
				auto* Storage = std::get<I>( Storages );
				if ( Storage == nullptr ) return static_cast<TComponentType<TArg>*>( nullptr );
				const uint64 Bit = TypeBit<TComponentType<TArg>>();
				if ( Bit != 0ull ) return ( Record.ComponentMask & Bit ) != 0ull ? &Storage->GetUnchecked( Entity ) : nullptr;
				return Storage->Find( Entity );
			}
			else if constexpr ( NumRequired == 1 )
			{
				// 必須コンポーネントが 1 つだけなら、その入れ物が必ず Driver なので Sparse を引かずに Dense[Index] を直接読む
				return std::get<I>( Storages )->GetDenseAt( Index );
			}
			else
			{
				return std::get<I>( Storages )->GetUnchecked( Entity );
			}
		}

		template<size_t... Is>
		FORCEINLINE void RunEntity( int32 Entity, [[maybe_unused]] int32 Index, [[maybe_unused]] FDrawContext* Context, std::index_sequence<Is...> )
		{
			const FEcsWorld::FEntityRecord& Record = Records[static_cast<size_t>( Entity )];
			if constexpr ( NumRequired == 0 )
			{
				if ( !Record.bAlive )
				{
					if constexpr ( bDraw ) Ranges[static_cast<size_t>( Index )].Entity = INDEX_NONE;
					return;
				}
			}

			if constexpr ( bDraw )
			{
				// オブジェクトの Draw と同じ条件: 破棄が予約されていない・見えている
				if ( Record.bPendingDestroy || !Record.bVisible )
				{
					Ranges[static_cast<size_t>( Index )].Entity = INDEX_NONE;
					return;
				}
			}
			else
			{
				// オブジェクトの Tick と同じ条件: 破棄が予約されていない・Tick が有効・今フレームに生成されたものではない
				if ( Record.bPendingDestroy || !Record.bTickEnabled || Record.ObjectId > MaxObjectId ) return;
			}

			if constexpr ( NumRequired > 1 || bHasWithout )
			{
				if ( ( ( Record.ComponentMask & RequiredMask ) != RequiredMask ) || ( ( Record.ComponentMask & ExcludedMask ) != 0ull ) )
				{
					if constexpr ( bDraw ) Ranges[static_cast<size_t>( Index )].Entity = INDEX_NONE;
					return;
				}
				if ( !bFastMaskCheck && !( HasArgSlow<Is>( Entity ) && ... ) )
				{
					if constexpr ( bDraw ) Ranges[static_cast<size_t>( Index )].Entity = INDEX_NONE;
					return;
				}
			}

			FTransformAccess Access;
			FTransformView View;
			if constexpr ( bUsesTransform )
			{
				FTransformNode* Node = Record.TransformNode;
				if ( Node == nullptr )
				{
					if constexpr ( bDraw ) Ranges[static_cast<size_t>( Index )].Entity = INDEX_NONE;
					return;
				}
				Access = FTransformAccess( Node, Transforms, Record.TransformSlot );
				View = FTransformView( Node );
			}

			if constexpr ( bDraw )
			{
				const uint32 First = static_cast<uint32>( Context->GetNumWords() );
				Function( Resolve<Is>( Entity, Index, Record, Access, View, Context )... );
				Ranges[static_cast<size_t>( Index )] = FDrawRange{ First, static_cast<uint32>( Context->GetNumWords() ), Entity };
			}
			else
			{
				Function( Resolve<Is>( Entity, Index, Record, Access, View, Context )... );
			}
		}

	private:
		TFunction Function;
		FEcsWorld* World = nullptr;
		const FEcsWorld::FEntityRecord* Records = nullptr;
		FTransformWorld* Transforms = nullptr;
		float32 DeltaTime = 0.0f;
		uint64 MaxObjectId = 0;
		uint64 RequiredMask = 0ull;
		uint64 ExcludedMask = 0ull;
		bool bFastMaskCheck = true;
		std::tuple<typename TStoragePointer<TArgs>::Type...> Storages {};
	};

	//-------------------------------------------------------------------------
	// 即時クエリ（ForEach）の実行器
	//-------------------------------------------------------------------------
	template<typename TFunction, typename... TArgs>
	struct TForEachRunner
	{
		static_assert( !( IsDrawArg<TArgs> || ... ), "FDrawContext can only be used in AddDrawSystem." );
		static_assert( !( IsDeltaArg<TArgs> || ... ), "Scene.ForEach does not take DeltaTime (capture it in the lambda if needed)." );

		static constexpr bool bUsesTransform = ( IsTransformArg<TArgs> || ... ) || ( IsViewArg<TArgs> || ... );
		static constexpr bool bWritesTransform = ( ( IsTransformArg<TArgs> && IsWriteRefArg<TArgs> ) || ... );
		static constexpr bool bUsesView = ( IsViewArg<TArgs> || ... );
		static constexpr bool bUsesEntityRef = ( IsEntityArg<TArgs> || ... );
		static constexpr size_t NumRequired = ( ( IsRequiredComponentArg<TArgs> ? 1u : 0u ) + ... + 0u );
		static constexpr bool bHasWithout = ( IsWithoutArg<TArgs> || ... );

		using TStorageTuple = std::tuple<typename TStoragePointer<TArgs>::Type...>;

		static int32 Run( FEcsWorld& World, FTransformWorld& Transforms, TFunction&& Function )
		{
			TStorageTuple Storages{};
			IComponentStorage* Driver = nullptr;
			uint64 RequiredMask = 0ull;
			uint64 ExcludedMask = 0ull;
			bool bFastMaskCheck = true;
			int32 Best = INT_MAX;

			if ( !BindAll( World, Storages, Driver, RequiredMask, ExcludedMask, bFastMaskCheck, Best, std::index_sequence_for<TArgs...>{} ) )
			{
				return 0;
			}

			const int32 Count = Driver != nullptr ? Driver->Num() : ( NumRequired == 0 ? World.GetNumEntitySlots() : 0 );
			if ( Count <= 0 ) return 0;

			if constexpr ( bWritesTransform ) Transforms.NotifySystemWrite();

			// ループの途中で構造が変わらない前提の高速走査。ただし CGameObject::Destroy() は bPendingDestroy を立てるだけなので安全
			const int32* Entities = Driver != nullptr ? Driver->GetEntities() : nullptr;
			int32 NumVisited = 0;
			for ( int32 i = 0; i < Count; ++i )
			{
				const int32 Entity = Entities != nullptr ? Entities[i] : i;
				const FEcsWorld::FEntityRecord& Record = World.GetRecord( Entity );
				if ( !Record.bAlive || Record.bPendingDestroy ) continue;

				if constexpr ( NumRequired > 1 || bHasWithout )
				{
					if ( ( ( Record.ComponentMask & RequiredMask ) != RequiredMask ) || ( ( Record.ComponentMask & ExcludedMask ) != 0ull ) ) continue;
					if ( !bFastMaskCheck && !HasAllSlow( World, Storages, Entity, std::index_sequence_for<TArgs...>{} ) ) continue;
				}

				FTransformAccess Access;
				FTransformView View;
				if constexpr ( bUsesTransform )
				{
					FTransformNode* Node = Record.TransformNode;
					if ( Node == nullptr ) continue;
					if constexpr ( bUsesView ) (void)Transforms.GetWorld( Record.TransformSlot );
					Access = FTransformAccess( Node, &Transforms, Record.TransformSlot );
					View = FTransformView( Node );
				}

				++NumVisited;
				if ( !InvokeOne( World, Storages, Function, Entity, i, Record, Access, View, std::index_sequence_for<TArgs...>{} ) ) break;
			}

			if constexpr ( bWritesTransform ) Transforms.FlushDeferredDirty();
			if constexpr ( bUsesEntityRef ) World.FlushDeferred();
			return NumVisited;
		}

	private:
		template<size_t... Is>
		static bool BindAll( FEcsWorld& World, TStorageTuple& Storages, IComponentStorage*& Driver,
							 uint64& RequiredMask, uint64& ExcludedMask, bool& bFastMaskCheck, int32& Best, std::index_sequence<Is...> )
		{
			return ( BindOne<Is>( World, Storages, Driver, RequiredMask, ExcludedMask, bFastMaskCheck, Best ) && ... );
		}

		template<size_t I>
		static bool BindOne( FEcsWorld& World, TStorageTuple& Storages, IComponentStorage*& Driver,
							 uint64& RequiredMask, uint64& ExcludedMask, bool& bFastMaskCheck, int32& Best )
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsRequiredComponentArg<TArg> )
			{
				using TComp = TBare<TArg>;
				TComponentStorage<TComp>* Storage = World.FindStorage<TComp>();
				if ( Storage == nullptr || Storage->Num() == 0 ) return false;
				std::get<I>( Storages ) = Storage;
				const uint64 Bit = TypeBit<TComp>();
				RequiredMask |= Bit;
				if ( Bit == 0ull ) bFastMaskCheck = false;
				if ( Storage->Num() < Best )
				{
					Best = Storage->Num();
					Driver = Storage;
				}
			}
			else if constexpr ( IsOptionalComponentArg<TArg> )
			{
				using TComp = TPointee<TArg>;
				std::get<I>( Storages ) = World.FindStorage<TComp>();
			}
			else if constexpr ( IsWithoutArg<TArg> )
			{
				ExcludedMask |= TIsWithout<TBare<TArg>>::Mask();
				if ( !TIsWithout<TBare<TArg>>::AllIn64() ) bFastMaskCheck = false;
			}
			return true;
		}

		template<size_t... Is>
		static bool HasAllSlow( const FEcsWorld& World, const TStorageTuple& Storages, int32 Entity, std::index_sequence<Is...> )
		{
			return ( HasOneSlow<Is>( World, Storages, Entity ) && ... );
		}

		template<size_t I>
		static bool HasOneSlow( const FEcsWorld& World, const TStorageTuple& Storages, int32 Entity )
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsRequiredComponentArg<TArg> ) return std::get<I>( Storages )->Has( Entity );
			else if constexpr ( IsWithoutArg<TArg> ) return !TIsWithout<TBare<TArg>>::HasAny( World, Entity );
			else return true;
		}

		template<size_t I>
		static FORCEINLINE decltype( auto ) ResolveOne( FEcsWorld& World, TStorageTuple& Storages, int32 Entity, int32 Index,
														const FEcsWorld::FEntityRecord& Record, FTransformAccess& Access, FTransformView& View )
		{
			using TArg = std::tuple_element_t<I, std::tuple<TArgs...>>;
			if constexpr ( IsEntityArg<TArg> ) return FEntityRef( Entity, &World );
			else if constexpr ( IsObjectRefArg<TArg> ) return *Record.Object;
			else if constexpr ( IsObjectPtrArg<TArg> ) return Record.Object;
			else if constexpr ( IsTransformArg<TArg> ) return ( Access );
			else if constexpr ( IsViewArg<TArg> ) return ( View );
			else if constexpr ( IsWithoutArg<TArg> ) return TBare<TArg>{};
			else if constexpr ( IsOptionalComponentArg<TArg> )
			{
				auto* Storage = std::get<I>( Storages );
				if ( Storage == nullptr ) return static_cast<TComponentType<TArg>*>( nullptr );
				const uint64 Bit = TypeBit<TComponentType<TArg>>();
				if ( Bit != 0ull ) return ( Record.ComponentMask & Bit ) != 0ull ? &Storage->GetUnchecked( Entity ) : nullptr;
				return Storage->Find( Entity );
			}
			else if constexpr ( NumRequired == 1 )
			{
				return std::get<I>( Storages )->GetDenseAt( Index );
			}
			else
			{
				return std::get<I>( Storages )->GetUnchecked( Entity );
			}
		}

		template<size_t... Is>
		static FORCEINLINE bool InvokeOne( FEcsWorld& World, TStorageTuple& Storages, TFunction& Function, int32 Entity, int32 Index,
										   const FEcsWorld::FEntityRecord& Record, FTransformAccess& Access, FTransformView& View, std::index_sequence<Is...> )
		{
			using TReturn = std::invoke_result_t<TFunction&, decltype( ResolveOne<Is>( World, Storages, Entity, Index, Record, Access, View ) )...>;
			if constexpr ( std::is_same_v<TReturn, bool> )
			{
				return Function( ResolveOne<Is>( World, Storages, Entity, Index, Record, Access, View )... );
			}
			else
			{
				Function( ResolveOne<Is>( World, Storages, Entity, Index, Record, Access, View )... );
				return true;
			}
		}
	};

	// ラムダや関数ポインタの引数の型を取り出して、型付きのシステム / ForEach を動かす
	template<typename T>
	struct TFunctionTraits : TFunctionTraits<decltype( &T::operator() )>
	{
	};

	template<typename TClass, typename TReturn, typename... TArgs>
	struct TFunctionTraits<TReturn ( TClass::* )( TArgs... ) const>
	{
		template<bool bDraw, typename TFunction>
		static std::unique_ptr<ISystemRunner> Make( TFunction&& Function )
		{
			return std::make_unique<TSystemRunner<bDraw, std::decay_t<TFunction>, TArgs...>>( std::forward<TFunction>( Function ) );
		}

		template<typename TFunction>
		static int32 RunForEach( FEcsWorld& World, FTransformWorld& Transforms, TFunction&& Function )
		{
			return TForEachRunner<TFunction, TArgs...>::Run( World, Transforms, std::forward<TFunction>( Function ) );
		}
	};

	template<typename TClass, typename TReturn, typename... TArgs>
	struct TFunctionTraits<TReturn ( TClass::* )( TArgs... )>
	{
		template<bool bDraw, typename TFunction>
		static std::unique_ptr<ISystemRunner> Make( TFunction&& Function )
		{
			return std::make_unique<TSystemRunner<bDraw, std::decay_t<TFunction>, TArgs...>>( std::forward<TFunction>( Function ) );
		}

		template<typename TFunction>
		static int32 RunForEach( FEcsWorld& World, FTransformWorld& Transforms, TFunction&& Function )
		{
			return TForEachRunner<TFunction, TArgs...>::Run( World, Transforms, std::forward<TFunction>( Function ) );
		}
	};

	template<typename TReturn, typename... TArgs>
	struct TFunctionTraits<TReturn ( * )( TArgs... )>
	{
		template<bool bDraw, typename TFunction>
		static std::unique_ptr<ISystemRunner> Make( TFunction&& Function )
		{
			return std::make_unique<TSystemRunner<bDraw, std::decay_t<TFunction>, TArgs...>>( std::forward<TFunction>( Function ) );
		}

		template<typename TFunction>
		static int32 RunForEach( FEcsWorld& World, FTransformWorld& Transforms, TFunction&& Function )
		{
			return TForEachRunner<TFunction, TArgs...>::Run( World, Transforms, std::forward<TFunction>( Function ) );
		}
	};

	// 条件に合う生きているエンティティをその場で走査し、処理した件数を返す
	template<typename TFunction>
	FORCEINLINE int32 ForEach( FEcsWorld& World, FTransformWorld& Transforms, TFunction&& Function )
	{
		return TFunctionTraits<std::decay_t<TFunction>>::RunForEach( World, Transforms, std::forward<TFunction>( Function ) );
	}

	// 指定したすべてのコンポーネントを持つ生きているエンティティの数を返す
	template<typename TFirst, typename... TRest>
	int32 CountEntities( const FEcsWorld& World )
	{
		const TComponentStorage<TFirst>* FirstStorage = World.FindStorage<TFirst>();
		if ( FirstStorage == nullptr || FirstStorage->Num() == 0 ) return 0;
		if ( !( ( World.FindStorage<TRest>() != nullptr && World.FindStorage<TRest>()->Num() > 0 ) && ... ) ) return 0;

		const IComponentStorage* Driver = FirstStorage;
		( ( World.FindStorage<TRest>()->Num() < Driver->Num() ? ( Driver = World.FindStorage<TRest>(), 0 ) : 0 ), ... );

		const uint64 Mask = TypeBit<TFirst>() | ( TypeBit<TRest>() | ... | 0ull );
		const bool bFast = ( TypeBit<TFirst>() != 0ull ) && ( ( TypeBit<TRest>() != 0ull ) && ... );
		const int32* Entities = Driver->GetEntities();
		const int32 Num = Driver->Num();
		const FEcsWorld::FEntityRecord* Records = World.GetRecords();
		int32 Count = 0;
		for ( int32 i = 0; i < Num; ++i )
		{
			const int32 Entity = Entities[i];
			const FEcsWorld::FEntityRecord& Record = Records[static_cast<size_t>( Entity )];
			if ( !Record.bAlive || Record.bPendingDestroy ) continue;
			if ( ( Record.ComponentMask & Mask ) != Mask ) continue;
			if ( !bFast && !( World.HasData<TFirst>( Entity ) && ( World.HasData<TRest>( Entity ) && ... ) ) ) continue;
			++Count;
		}
		return Count;
	}

	//=========================================================================
	// FSystemScheduler
	//   シーンごとに 1 つ。システムを持ち、フェーズごとに実行する。
	//   システム同士の「読み書きの衝突」から、実行の段（stage）を自動で決める:
	//     登録順に見て、先に登録された衝突するシステムより必ず後の段に置く（衝突 = 同じ型を、どちらかが書き込む）。
	//     同じ段のシステムは、互いに触るデータが無いので、同時に（さらに各システムは細かく分けて）並列に実行する。
	//   結果は、登録順に 1 つずつ実行したものと同じになる（衝突しない順序の入れ替えしか行わない）。
	//=========================================================================
	class FSystemScheduler
	{
	public:
		// 更新のシステム（PreUpdate / PostUpdate）を登録する。描画システムは AddDraw
		template<typename TFunction>
		void Add( EPhase Phase, const char* Name, TFunction&& Function )
		{
			COLLECTION_CHECK( Phase != EPhase::Draw, "Draw systems are registered with AddDraw" );
			if ( Phase == EPhase::Draw ) return;
			AddInternal<false>( Phase, Name, std::forward<TFunction>( Function ) );
		}

		// 描画システムを登録する（読むだけ。FDrawContext& が必要）
		template<typename TFunction>
		void AddDraw( const char* Name, TFunction&& Function )
		{
			AddInternal<true>( EPhase::Draw, Name, std::forward<TFunction>( Function ) );
		}

		// フェーズのシステムを、段の順に実行する。Jobs が null（またはワーカーがいない）なら、呼び出したスレッドだけで、同じ順序で。
		// MaxObjectId: これより後に生成されたオブジェクトは対象にしない（今フレームはまだ Tick されないため。描画では使わない）
		// Draw フェーズは、実行の前にワールド値を最新にする。出力は ReplayDraw で取り出す
		void Run( EPhase Phase, float32 DeltaTime, uint64 MaxObjectId, FEcsWorld& World, FTransformWorld& Transforms, IJobSystem* Jobs );

		//---------------------------------------------------------------------
		// 描画システムの出力
		//---------------------------------------------------------------------
		FORCEINLINE bool HasDrawSystems() const { return !DrawSystems.empty(); }
		// 直前の Run( Draw ) でこのエンティティについて記録されたコマンドを、描画システムの登録順に Executor へ渡す（メインスレッドから）
		FORCEINLINE void ReplayDraw( int32 Entity, IDrawExecutor& Executor ) const
		{
			for ( const ISystemRunner* Runner : ActiveDrawRunners )
			{
				FDrawSpan Span;
				if ( Runner->GetDrawSpan( Entity, Span ) ) Executor.Execute( Span );
			}
		}

		//---------------------------------------------------------------------
		// 確認用
		//---------------------------------------------------------------------
		int32 GetNumSystems() const { return static_cast<int32>( Systems.size() ); }
		// 登録順の Index 番目のシステムの情報（コンソールなどの表示用）
		const char* GetSystemName( int32 Index ) const { return Systems[static_cast<size_t>( Index )].Name; }
		EPhase GetSystemPhase( int32 Index ) const { return Systems[static_cast<size_t>( Index )].Phase; }
		int32 GetSystemStage( int32 Index ) const { return Systems[static_cast<size_t>( Index )].Stage; }
		// Name の段（見つからなければ INDEX_NONE）
		int32 GetStage( const char* Name ) const;
		int32 GetNumStages( EPhase Phase ) const;

	private:
		struct FSystem
		{
			const char* Name = "System";
			EPhase Phase = EPhase::PreUpdate;
			std::vector<FAccess> Access;
			std::unique_ptr<ISystemRunner> Runner;
			int32 Stage = 0;
		};

		template<bool bDraw, typename TFunction>
		void AddInternal( EPhase Phase, const char* Name, TFunction&& Function )
		{
			FSystem System;
			System.Name = Name;
			System.Phase = Phase;
			System.Runner = TFunctionTraits<std::decay_t<TFunction>>::template Make<bDraw>( std::forward<TFunction>( Function ) );
			System.Runner->GetAccess( System.Access );
			AddSystem( std::move( System ) );
		}

		void AddSystem( FSystem&& System );
		static bool Conflicts( const FSystem& A, const FSystem& B );

	private:
		std::vector<FSystem> Systems;                        // 登録順
		std::vector<int32> DrawSystems;                      // 描画システムの Systems の中の位置（登録順）
		std::vector<const ISystemRunner*> ActiveDrawRunners; // 直前の Run( Draw ) で対象が 1 つ以上あった描画システム（登録順）
	};
}

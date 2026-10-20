#pragma once

// Subsystem.h の大きなテンプレート関数の定義（インライン展開しても速くならないのでクラス定義から分離）
// Subsystem.h の末尾から include される。直接 include しない

template<typename T>
bool NSubsystemRegistry::Register()
{
	using TBase = typename T::FSubsystemBase;
	static_assert( std::is_base_of_v<TBase, T>, "REGISTER_SUBSYSTEM: invalid subsystem class" );
	static_assert( !std::is_abstract_v<T>, "REGISTER_SUBSYSTEM: abstract classes cannot be registered" );
	static_assert( std::is_default_constructible_v<T>, "REGISTER_SUBSYSTEM: subsystem must be default constructible" );

	TArray<TEntry<TBase>>& Entries = GetEntries<TBase>();
	if ( Entries.ContainsByPredicate( []( const TEntry<TBase>& Entry ) { return *Entry.Type == typeid( T ); } ) ) return false;

	Entries.Add( { &typeid( T ), []() -> TUniquePtr<TBase> { return MakeUnique<T>(); } } );
	return true;
}
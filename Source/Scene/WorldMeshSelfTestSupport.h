#pragma once
#include <cmath>
#include <set>
#include <string>

#include "Library/Core/Collection/Map.h"
#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Framework/World/WorldValidate.h"
#include "WorldSelfTestSupport.h"

//=============================================================================
// 編集メッシュの自己確認（WorldMeshSelfTest*.cpp）が共有する道具
//=============================================================================
namespace NWorldTest
{
	// 編集メッシュの純粋な部分（データ・基本の形・描く形・JSON・検証。WorldMeshSelfTestData.cpp）
	void RunMeshDataTests( NAssetTest::FChecker& C );
	// 編集メッシュの操作（選択・押し出し・削除・つなぐ・割る・材質・足す・取り込み。WorldMeshSelfTestOps.cpp）
	void RunMeshOpsTests( NAssetTest::FChecker& C );
	// 編集メッシュのエディタの部分（作る・モデルから変える・始める / 終わる・選ぶ・操作・ギズモ。WorldMeshSelfTestEditor.cpp）
	void RunMeshEditorTests( NAssetTest::FChecker& C );
}

namespace NWorldMeshTest
{
	inline NlohmannJson ParseJsonText( const char* Text )
	{
		return NlohmannJson::parse( Text, nullptr, false );
	}

	// 読み取り専用の JSON の窓口。無いキー・範囲外の番号・型の違いでも落ちず、null として扱う
	//   （const の nlohmann::json の operator[] は、無いキーで assert になり、確認が止まって見えるため。WorldSelfTestLogic.cpp の FJ と同じ）
	class FJson
	{
	public:
		FJson( const NlohmannJson& InJson ) : Json( &InJson ) {}

		FJson operator[]( const char* Key ) const
		{
			if ( !Json->is_object() ) return FJson( Null() );
			const auto It = Json->find( Key );
			return It != Json->end() ? FJson( *It ) : FJson( Null() );
		}
		FJson operator[]( int Index ) const { return Json->is_array() && Index >= 0 && static_cast<size_t>( Index ) < Json->size() ? FJson( ( *Json )[static_cast<size_t>( Index )] ) : FJson( Null() ); }

		template<typename T>
		bool operator==( const T& Value ) const { return *Json == NlohmannJson( Value ); }

		size_t size() const { return Json->size(); }
		bool empty() const { return Json->empty(); }
		bool contains( const char* Key ) const { return Json->is_object() && Json->contains( Key ); }

	private:
		static const NlohmannJson& Null()
		{
			static const NlohmannJson Value;
			return Value;
		}
		const NlohmannJson* Json;
	};

	// 偽のアセットの窓口（登録したファイルだけがある）
	class FFakeQuery final : public IWorldAssetQuery
	{
	public:
		std::set<std::string> Existing;
		bool Exists( const FString& Path ) const override { return Existing.count( std::string( *Path ) ) > 0; }
	};

	// 閉じた形か: 使える面の、向きのある辺（A -> B）が、ちょうど 1 回ずつ、逆向き（B -> A）も 1 回ずつ出てくる
	inline bool IsClosed( const FEditMesh& Mesh )
	{
		TMap<int64, int32> Edges;
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			const TArray<int32>& Indices = Mesh.Faces[Face].Indices;
			for ( int32 i = 0; i < Indices.Num(); ++i ) ++Edges.FindOrAdd( ( static_cast<int64>( Indices[i] ) << 32 ) | static_cast<int64>( Indices[( i + 1 ) % Indices.Num()] ) );
		}
		if ( Edges.IsEmpty() ) return false;
		for ( const auto& Pair : Edges )
		{
			const int32 A = static_cast<int32>( Pair.Key >> 32 ), B = static_cast<int32>( Pair.Key & 0xFFFFFFFF );
			const int32* Reverse = Edges.Find( ( static_cast<int64>( B ) << 32 ) | static_cast<int64>( A ) );
			if ( Pair.Value != 1 || Reverse == nullptr || *Reverse != 1 ) return false;
		}
		return true;
	}

	// すべての面が、形の中心（箱の中心）から外を向いているか（凸な形で確かめる）
	inline bool FacesLookOutward( const FEditMesh& Mesh )
	{
		// 使われている頂点の平均（凸な形の、内側の点）から見て、外を向いているか
		TArray<bool> Used;
		Used.Init( false, Mesh.GetNumVertices() );
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			for ( const int32 Index : Mesh.Faces[Face].Indices ) Used[Index] = true;
		}
		FVector3 Sum = FVector3::Zero;
		int32 Count = 0;
		for ( int32 i = 0; i < Used.Num(); ++i )
		{
			if ( !Used[i] ) continue;
			Sum = Sum + Mesh.Vertices[i];
			++Count;
		}
		if ( Count == 0 ) return false;
		const FVector3 Center = Sum / static_cast<float32>( Count );
		for ( int32 Face = 0; Face < Mesh.GetNumFaces(); ++Face )
		{
			if ( !Mesh.IsFaceValid( Face ) ) continue;
			if ( FVector3::Dot( Mesh.ComputeFaceNormal( Face ), Mesh.ComputeFaceCenter( Face ) - Center ) <= 0.0f ) return false;
		}
		return true;
	}
}

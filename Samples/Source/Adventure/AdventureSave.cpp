#include "AdventureSave.h"

#include "Library/Math/MathUtils.h"
#include "Source/Adventure/AdventureState.h"

namespace
{
	constexpr int32 Version = 1;

	NlohmannJson ToArray( const TArray<int32>& Ids )
	{
		NlohmannJson Json = NlohmannJson::array();
		for ( const int32 Id : Ids )
		{
			Json.push_back( Id );
		}
		return Json;
	}

	// 整数の配列を読む。整数でないものは、飛ばす
	void ReadIds( const NlohmannJson& Json, const char* Key, TArray<int32>& Out )
	{
		Out.Reset();
		if ( !Json.contains( Key ) || !Json[Key].is_array() )
		{
			return;
		}
		for ( const NlohmannJson& Entry : Json[Key] )
		{
			if ( Entry.is_number_integer() && Entry.get<int32>() > 0 )
			{
				Out.Add( Entry.get<int32>() );
			}
		}
	}

	bool SameIds( const TArray<int32>& A, const TArray<int32>& B )
	{
		if ( A.Num() != B.Num() )
		{
			return false;
		}
		for ( int32 Index = 0; Index < A.Num(); ++Index )
		{
			if ( A[Index] != B[Index] )
			{
				return false;
			}
		}
		return true;
	}
}

FAdventureSave FAdventureSave::Capture( const FAdventureState& State )
{
	FAdventureSave Save;
	Save.Hp = State.Hp;
	Save.Potions = State.Potions;
	Save.Keys = State.Keys;
	Save.Gems = State.Gems;
	Save.bGateOpen = State.bGateOpen;
	Save.Position = State.PlayerPosition;
	Save.OpenedChests = State.OpenedChests;
	Save.DefeatedEnemies = State.DefeatedEnemies;
	Save.PickedUp = State.PickedUp;
	return Save;
}

void FAdventureSave::Apply( FAdventureState& State ) const
{
	State.Hp = Hp;
	State.Potions = Potions;
	State.Keys = Keys;
	State.Gems = Gems;
	State.bGateOpen = bGateOpen;
	State.OpenedChests = OpenedChests;
	State.DefeatedEnemies = DefeatedEnemies;
	State.PickedUp = PickedUp;
	State.bPlayerDead = false;
	State.bWon = false;
}

NlohmannJson FAdventureSave::ToJson() const
{
	NlohmannJson Json;
	Json["Version"] = Version;
	Json["Hp"] = Hp;
	Json["Potions"] = Potions;
	Json["Keys"] = Keys;
	Json["Gems"] = Gems;
	Json["GateOpen"] = bGateOpen;
	Json["Position"] = NlohmannJson::array( { Position.x, Position.y, Position.z } );
	Json["OpenedChests"] = ToArray( OpenedChests );
	Json["DefeatedEnemies"] = ToArray( DefeatedEnemies );
	Json["PickedUp"] = ToArray( PickedUp );
	return Json;
}

bool FAdventureSave::FromJson( const NlohmannJson& Json, FAdventureSave& Out )
{
	if ( !Json.is_object() || !Json.contains( "Hp" ) )
	{
		return false;
	}
	FAdventureSave Result;
	// 形の悪い値は、安全な範囲へ直す
	Result.Hp = NMath::Clamp( Json.value( "Hp", FAdventureState::MaxHp ), 1, FAdventureState::MaxHp );
	Result.Potions = NMath::Clamp( Json.value( "Potions", 0 ), 0, FAdventureState::MaxPotions );
	Result.Keys = NMath::Clamp( Json.value( "Keys", 0 ), 0, 9 );
	Result.Gems = NMath::Clamp( Json.value( "Gems", 0 ), 0, 999 );
	Result.bGateOpen = Json.value( "GateOpen", false );
	if ( Json.contains( "Position" ) && Json["Position"].is_array() && Json["Position"].size() >= 3 )
	{
		const NlohmannJson& P = Json["Position"];
		if ( P[0].is_number() && P[1].is_number() && P[2].is_number() )
		{
			Result.Position = FVector3( NMath::Clamp( P[0].get<float32>(), -30.0f, 30.0f ), 0.0f, NMath::Clamp( P[2].get<float32>(), -30.0f, 30.0f ) );
		}
	}
	ReadIds( Json, "OpenedChests", Result.OpenedChests );
	ReadIds( Json, "DefeatedEnemies", Result.DefeatedEnemies );
	ReadIds( Json, "PickedUp", Result.PickedUp );
	Out = Result;
	return true;
}

bool FAdventureSave::IsSameAs( const FAdventureSave& Other ) const
{
	return Hp == Other.Hp && Potions == Other.Potions && Keys == Other.Keys && Gems == Other.Gems && bGateOpen == Other.bGateOpen
		&& NMath::IsNearlyEqual( Position.x, Other.Position.x, 0.001f ) && NMath::IsNearlyEqual( Position.z, Other.Position.z, 0.001f )
		&& SameIds( OpenedChests, Other.OpenedChests ) && SameIds( DefeatedEnemies, Other.DefeatedEnemies ) && SameIds( PickedUp, Other.PickedUp );
}

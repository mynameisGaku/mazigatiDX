#include "WorldDefinitionJson.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Math/MathUtils.h"

namespace NWorldDefinitionJson
{
	//-------------------------------------------------------------------------
	//-------------------------------------------------------------------------
	// 部品
	//-------------------------------------------------------------------------
	NlohmannJson ModelToJson( const FWorldModel& Model )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Model.Path;
		if ( !Model.Animations.IsEmpty() )
		{
			Json["Animations"] = Model.Animations;
		}
		if ( !Model.Clip.IsEmpty() )
		{
			Json["Clip"] = Model.Clip;
		}
		if ( Model.Speed != 1.0f )
		{
			Json["Speed"] = NJson::Round4( Model.Speed );
		}
		if ( Model.MaterialEffect.IsSet() )
		{
			Json["MaterialEffect"] = Model.MaterialEffect.GetValue().ToJson();
		}
		return Json;
	}

	FWorldModel ModelFromJson( const NlohmannJson& Json )
	{
		FWorldModel Model;
		Model.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Model.Animations = NJson::GetOr<FString>( Json, "Animations", FString() );
		Model.Clip = NJson::GetOr<FString>( Json, "Clip", FString() );
		Model.Speed = NJson::GetOr<float32>( Json, "Speed", 1.0f );
		if ( Json.contains( "MaterialEffect" ) )
		{
			// nullや配列も設定済みbindingとして残し、Validateへ渡す。
			FWorldModelMaterialEffect MaterialEffect;
			FWorldModelMaterialEffect::FromJson( Json["MaterialEffect"], MaterialEffect );
			Model.MaterialEffect = MoveTemp( MaterialEffect );
		}
		return Model;
	}

	NlohmannJson PrefabToJson( const FWorldPrefab& Prefab )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Prefab.Path;
		return Json;
	}

	FWorldPrefab PrefabFromJson( const NlohmannJson& Json )
	{
		FWorldPrefab Prefab;
		Prefab.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		return Prefab;
	}

	NlohmannJson BillboardToJson( const FWorldBillboard& Board )
	{
		NlohmannJson Json = NlohmannJson::object();
		Json["Texture"] = Board.Texture;
		if ( Board.Size.x != 1.0f || Board.Size.y != 1.0f )
		{
			Json["Size"] = Vector2ToJson( Board.Size );
		}
		if ( Board.Pivot.x != 0.5f || Board.Pivot.y != 0.5f )
		{
			Json["Pivot"] = Vector2ToJson( Board.Pivot );
		}
		if ( Board.Mode != EWorldBillboardMode::Camera )
		{
			Json["Mode"] = ToString( Board.Mode );
		}
		const FColor32 White( 255, 255, 255, 255 );
		if ( Board.Tint.r != White.r || Board.Tint.g != White.g || Board.Tint.b != White.b || Board.Tint.a != White.a )
		{
			Json["Tint"] = NWorld::FormatColor( Board.Tint );
		}
		if ( Board.bUseRect )
		{
			Json["Rect"] = NlohmannJson::array( { Board.RectX, Board.RectY, Board.RectWidth, Board.RectHeight } );
		}
		if ( Board.bFlipX )
		{
			Json["FlipX"] = true;
		}
		if ( Board.bFlipY )
		{
			Json["FlipY"] = true;
		}
		return Json;
	}

	FWorldBillboard BillboardFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where )
	{
		FWorldBillboard Board;
		Board.Texture = NJson::GetOr<FString>( Json, "Texture", FString() );
		Board.Size = ReadVector2( Json, "Size", Board.Size );
		Board.Pivot = ReadVector2( Json, "Pivot", Board.Pivot );
		if ( Json.contains( "Mode" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Mode", FString() );
			if ( !TryParse( Text, Board.Mode ) )
			{
				Warn( Warnings, Where + ": unknown billboard mode '" + Text + "' (using Camera)" );
			}
		}
		if ( Json.contains( "Tint" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Tint", FString() );
			if ( !NWorld::TryParseColor( Text, Board.Tint ) )
			{
				Warn( Warnings, Where + ": bad tint color '" + Text + "' (using white)" );
			}
		}
		if ( Json.contains( "Rect" ) && IsNumberArray( Json["Rect"], 4 ) )
		{
			Board.bUseRect = true;
			Board.RectX = Json["Rect"][0].get<int32>();
			Board.RectY = Json["Rect"][1].get<int32>();
			Board.RectWidth = Json["Rect"][2].get<int32>();
			Board.RectHeight = Json["Rect"][3].get<int32>();
		}
		Board.bFlipX = NJson::GetOr<bool>( Json, "FlipX", false );
		Board.bFlipY = NJson::GetOr<bool>( Json, "FlipY", false );
		return Board;
	}

	NlohmannJson SoundToJson( const FWorldSound& Sound )
	{
		const FWorldSound Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Sound.Path;
		if ( Sound.Bus != Default.Bus )
		{
			Json["Bus"] = Sound.Bus;
		}
		if ( Sound.Volume != Default.Volume )
		{
			Json["Volume"] = NJson::Round4( Sound.Volume );
		}
		if ( Sound.Pitch != Default.Pitch )
		{
			Json["Pitch"] = NJson::Round4( Sound.Pitch );
		}
		if ( Sound.bLoop != Default.bLoop )
		{
			Json["Loop"] = Sound.bLoop;
		}
		if ( Sound.bAutoPlay != Default.bAutoPlay )
		{
			Json["AutoPlay"] = Sound.bAutoPlay;
		}
		if ( Sound.MinDistance != Default.MinDistance )
		{
			Json["MinDistance"] = NJson::Round4( Sound.MinDistance );
		}
		if ( Sound.MaxDistance != Default.MaxDistance )
		{
			Json["MaxDistance"] = NJson::Round4( Sound.MaxDistance );
		}
		return Json;
	}

	NlohmannJson EffectToJson( const FWorldEffect& Effect )
	{
		const FWorldEffect Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Path"] = Effect.Path;
		if ( Effect.bAutoPlay != Default.bAutoPlay )
		{
			Json["AutoPlay"] = Effect.bAutoPlay;
		}
		if ( Effect.Scale != Default.Scale )
		{
			Json["Scale"] = NJson::Round4( Effect.Scale );
		}
		if ( Effect.Speed != Default.Speed )
		{
			Json["Speed"] = NJson::Round4( Effect.Speed );
		}
		if ( Effect.Seed != Default.Seed )
		{
			Json["Seed"] = Effect.Seed;
		}
		if ( Effect.Loop >= 0 )
		{
			Json["Loop"] = Effect.Loop != 0;
		}
		if ( Effect.Target != WorldNoId )
		{
			Json["Target"] = Effect.Target;
		}
		if ( Effect.TargetOffset != FVector3::Zero )
		{
			Json["TargetOffset"] = NJson::ToJson( Effect.TargetOffset );
		}
		return Json;
	}

	FWorldEffect EffectFromJson( const NlohmannJson& Json )
	{
		FWorldEffect Effect;
		Effect.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Effect.bAutoPlay = NJson::GetOr<bool>( Json, "AutoPlay", Effect.bAutoPlay );
		Effect.Scale = NJson::GetOr<float32>( Json, "Scale", Effect.Scale );
		Effect.Speed = NJson::GetOr<float32>( Json, "Speed", Effect.Speed );
		Effect.Seed = NJson::GetOr<int32>( Json, "Seed", Effect.Seed );
		if ( Json.contains( "Loop" ) && Json["Loop"].is_boolean() )
		{
			Effect.Loop = Json["Loop"].get<bool>() ? 1 : 0;
		}
		if ( Json.contains( "Target" ) )
		{
			const NlohmannJson& Target = Json["Target"];
			if ( Target.is_number_unsigned() )
			{
				Effect.Target = Target.get<uint64>() <= static_cast<uint64>( TNumericLimits<int32>::Max() ) ? static_cast<int32>( Target.get<uint64>() ) : -1;
			}
			else if ( Target.is_number_integer() && Target.get<int64>() >= TNumericLimits<int32>::Min() && Target.get<int64>() <= TNumericLimits<int32>::Max() )
			{
				Effect.Target = static_cast<int32>( Target.get<int64>() );
			}
			else
			{
				Effect.Target = -1;
			}
		}
		if ( Json.contains( "TargetOffset" ) )
		{
			const NlohmannJson& Offset = Json["TargetOffset"];
			if ( Offset.is_array() && Offset.size() == 3 && Offset[0].is_number() && Offset[1].is_number() && Offset[2].is_number() )
			{
				Effect.TargetOffset = FVector3( Offset[0].get<float32>(), Offset[1].get<float32>(), Offset[2].get<float32>() );
			}
			else
			{
				Effect.TargetOffset = FVector3( TNumericLimits<float32>::QuietNaN(), 0.0f, 0.0f );
			}
		}
		return Effect;
	}

	FWorldSound SoundFromJson( const NlohmannJson& Json )
	{
		FWorldSound Sound;
		Sound.Path = NJson::GetOr<FString>( Json, "Path", FString() );
		Sound.Bus = NJson::GetOr<FString>( Json, "Bus", Sound.Bus );
		Sound.Volume = NJson::GetOr<float32>( Json, "Volume", Sound.Volume );
		Sound.Pitch = NJson::GetOr<float32>( Json, "Pitch", Sound.Pitch );
		Sound.bLoop = NJson::GetOr<bool>( Json, "Loop", Sound.bLoop );
		Sound.bAutoPlay = NJson::GetOr<bool>( Json, "AutoPlay", Sound.bAutoPlay );
		Sound.MinDistance = NJson::GetOr<float32>( Json, "MinDistance", Sound.MinDistance );
		Sound.MaxDistance = NJson::GetOr<float32>( Json, "MaxDistance", Sound.MaxDistance );
		return Sound;
	}

	NlohmannJson ColliderToJson( const FWorldCollider& Collider )
	{
		const FWorldCollider Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Shape"] = ToString( Collider.Shape );
		// 使う項目だけ書く（形に関係のない項目は、既定値のまま）
		if ( Collider.Shape == EWorldColliderShape::Box )
		{
			Json["Size"] = NJson::ToJson( Collider.Size );
		}
		else if ( Collider.Shape != EWorldColliderShape::Mesh )
		{
			Json["Radius"] = NJson::Round4( Collider.Radius );
		}
		if ( Collider.Shape == EWorldColliderShape::Capsule )
		{
			Json["Height"] = NJson::Round4( Collider.Height );
		}
		if ( Collider.Shape == EWorldColliderShape::Mesh && !Collider.Mesh.IsEmpty() )
		{
			Json["Mesh"] = Collider.Mesh;
		}
		if ( Collider.Shape != EWorldColliderShape::Mesh && !IsNearlyEqual( Collider.Offset, Default.Offset ) )
		{
			Json["Offset"] = NJson::ToJson( Collider.Offset );
		}
		if ( Collider.bTrigger )
		{
			Json["Trigger"] = true;
		}
		return Json;
	}

	FWorldCollider ColliderFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, const FString& Where )
	{
		FWorldCollider Collider;
		if ( Json.contains( "Shape" ) )
		{
			const FString Text = NJson::GetOr<FString>( Json, "Shape", FString() );
			if ( !TryParse( Text, Collider.Shape ) )
			{
				Warn( Warnings, Where + ": unknown collider shape '" + Text + "' (using Box)" );
			}
		}
		Collider.Size = NJson::ReadVector3( Json, "Size", Collider.Size );
		Collider.Radius = NJson::GetOr<float32>( Json, "Radius", Collider.Radius );
		Collider.Height = NJson::GetOr<float32>( Json, "Height", Collider.Height );
		Collider.Offset = NJson::ReadVector3( Json, "Offset", Collider.Offset );
		Collider.Mesh = NJson::GetOr<FString>( Json, "Mesh", FString() );
		Collider.bTrigger = NJson::GetOr<bool>( Json, "Trigger", false );
		return Collider;
	}
}

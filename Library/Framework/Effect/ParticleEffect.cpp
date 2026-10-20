#include "ParticleEffect.h"

#include <algorithm>
#include <cmath>

#include "Library/Framework/World/WorldDefinition.h"
#include "Library/Json/JsonHelpers.h"

//=============================================================================
// 粒子のエフェクトの定義ファイル（JSON、拡張子は .effect.json）の書式
//
//   {
//     "Version": 1,
//     "Name": "Campfire",
//     "Duration": 0,                      // エミッターが動く秒。0 = 止めるまで続く
//     "Loop": true,                       // Duration のあと、最初から繰り返す
//     "Seed": 1,                          // 乱数のシード
//     "Emitters": [
//       {
//         "Name": "Flames",
//         "Texture": "Textures/Spark.png", // 省略なら、ふちがぼやけた丸
//         "Blend": "Add",                  // "Alpha" / "Add"
//         "Space": "World",                // "World" / "Local"
//         "Orient": "Camera",              // "Camera" / "CameraY" / "Fixed"
//         "Path": { "Kind": "Lightning", "End": [0, 3, 0], "Segments": 32, "Branches": 4, "Jitter": 0.3, "BranchLength": 0.45, "RefreshHz": 12, "UVRepeat": 1, "BreakDistance": 2 },
//         "MaxParticles": 200,
//         "Delay": 0,
//         "Rate": 30,                      // 1 秒に出す数
//         "Bursts": [ { "Time": 0, "Count": 10 } ],
//         "Lifetime": [0.5, 1.0],          // 数 1 つでも書ける（最小 = 最大）。範囲は、生まれるときに、どれかが選ばれる
//         "Speed": [1, 2],
//         "Shape": { "Type": "Cone", "Radius": 0.1, "Angle": 15, "Size": [1, 1, 1] },   // Type: "Cone" / "Sphere" / "Box"
//         "Direction": [0, 1, 0],
//         "Gravity": [0, -2, 0],
//         "Drag": 0,
//         "Size": { "Start": [0.2, 0.3], "End": 0 },
//         "Color": { "Start": "#FFFFFFFF", "End": "#FFFFFF00" },
//         "Angle": [0, 360],               // 生まれたときの回転（度）
//         "Spin": [-90, 90]                // 回る速さ（度 / 秒）
//       }
//     ]
//   }
//
//   既定値と同じ項目は書き出さない。知らない項目は無視する（書き戻すと、なくなる）。
//=============================================================================
namespace
{
	void Warn( TArray<FString>* Warnings, const FString& Message )
	{
		if ( Warnings != nullptr ) Warnings->Add( Message );
	}

	// 4 バイトずつ・値のバイトを混ぜる（FNV-1a）
	struct FHasher
	{
		uint64 Hash = 1469598103934665603ull;

		template<typename T>
		void Value( const T& Item )
		{
			const uint8* Ptr = reinterpret_cast<const uint8*>( &Item );
			for ( usize i = 0; i < sizeof( T ); ++i )
			{
				Hash ^= Ptr[i];
				Hash *= 1099511628211ull;
			}
		}
		void String( const FString& Text )
		{
			for ( int32 i = 0; i < Text.Len(); ++i ) Value( static_cast<uint32>( Text[i] ) );
			Value( 0xFFFFFFFFu );   // 区切り（"ab" + "c" と "a" + "bc" を区別する）
		}
		void Range( const FParticleRange& R )
		{
			Value( R.Min );
			Value( R.Max );
		}
		void Vector( const FVector3& V )
		{
			Value( V.x );
			Value( V.y );
			Value( V.z );
		}
		void Color( const FColor32& C )
		{
			Value( C.r );
			Value( C.g );
			Value( C.b );
			Value( C.a );
		}
	};

	// 範囲: 最小 = 最大なら、数 1 つで書く
	NlohmannJson RangeToJson( const FParticleRange& Range )
	{
		// 保存と編集履歴で同じ変換を使うため、小数を丸めて元の値を失わないようにする。
		if ( Range.Min == Range.Max ) return Range.Min;
		return NlohmannJson::array( { Range.Min, Range.Max } );
	}

	// 数 1 つ、または [最小, 最大]。読めなければ Default（OutWarnings に書く）
	FParticleRange ReadRange( const NlohmannJson& Object, const char* Key, const FParticleRange& Default, TArray<FString>* Warnings, const FString& Where )
	{
		if ( !Object.is_object() ) return Default;
		const auto It = Object.find( Key );
		if ( It == Object.end() || It->is_null() ) return Default;
		try
		{
			if ( It->is_number() )
			{
				const float32 Value = It->get<float32>();
				return { Value, Value };
			}
			if ( It->is_array() && It->size() >= 2 ) return { ( *It )[0].get<float32>(), ( *It )[1].get<float32>() };
		}
		catch ( const nlohmann::json::exception& )
		{
		}
		Warn( Warnings, Where + ": '" + FString( Key ) + "' is not a number or [min, max] (using the default)" );
		return Default;
	}

	bool IsDefault( const FParticleRange& A, const FParticleRange& B ) { return A.Min == B.Min && A.Max == B.Max; }
	bool IsDefault( const FVector3& A, const FVector3& B ) { return A.x == B.x && A.y == B.y && A.z == B.z; }
	bool IsDefault( const FColor32& A, const FColor32& B ) { return A.r == B.r && A.g == B.g && A.b == B.b && A.a == B.a; }

	NlohmannJson EmitterToJson( const FParticleEmitterDefinition& Emitter )
	{
		const FParticleEmitterDefinition Default;
		NlohmannJson Json = NlohmannJson::object();
		Json["Name"] = Emitter.Name;
		if ( !Emitter.Texture.IsEmpty() ) Json["Texture"] = Emitter.Texture;
		if ( Emitter.Blend != Default.Blend ) Json["Blend"] = ToString( Emitter.Blend );
		if ( Emitter.Space != Default.Space ) Json["Space"] = ToString( Emitter.Space );
		if ( Emitter.Orient != Default.Orient ) Json["Orient"] = ToString( Emitter.Orient );
		if ( Emitter.Path.ComputeHash() != Default.Path.ComputeHash() ) Json["Path"] = Emitter.Path.ToJson();
		if ( Emitter.Material.ComputeHash() != Default.Material.ComputeHash() ) Json["Material"] = Emitter.Material.ToJson();
		// 既定の倍率は省略し、以前の JSON 形を保つ。
		if ( Emitter.Appearance.ComputeHash() != Default.Appearance.ComputeHash() ) Json["Appearance"] = Emitter.Appearance.ToJson();
		// 全項目が既定の Flipbook は省略して旧 JSON を保つ。
		if ( !Emitter.Flipbook.IsDefault() ) Json["Flipbook"] = Emitter.Flipbook.ToJson();
		if ( Emitter.MaxParticles != Default.MaxParticles ) Json["MaxParticles"] = Emitter.MaxParticles;
		if ( Emitter.Delay != Default.Delay ) Json["Delay"] = Emitter.Delay;
		if ( Emitter.Rate != Default.Rate ) Json["Rate"] = Emitter.Rate;
		if ( !Emitter.Bursts.IsEmpty() )
		{
			NlohmannJson Bursts = NlohmannJson::array();
			for ( const FParticleBurst& Burst : Emitter.Bursts )
			{
				NlohmannJson Item = NlohmannJson::object();
				Item["Time"] = Burst.Time;
				Item["Count"] = Burst.Count;
				Bursts.push_back( Item );
			}
			Json["Bursts"] = Bursts;
		}
		if ( !IsDefault( Emitter.Lifetime, Default.Lifetime ) ) Json["Lifetime"] = RangeToJson( Emitter.Lifetime );
		if ( !IsDefault( Emitter.Speed, Default.Speed ) ) Json["Speed"] = RangeToJson( Emitter.Speed );

		NlohmannJson Shape = NlohmannJson::object();
		if ( Emitter.Shape != Default.Shape ) Shape["Type"] = ToString( Emitter.Shape );
		if ( Emitter.ShapeRadius != Default.ShapeRadius ) Shape["Radius"] = Emitter.ShapeRadius;
		if ( Emitter.ConeAngle != Default.ConeAngle ) Shape["Angle"] = Emitter.ConeAngle;
		if ( !IsDefault( Emitter.BoxSize, Default.BoxSize ) ) Shape["Size"] = NlohmannJson::array( { Emitter.BoxSize.x, Emitter.BoxSize.y, Emitter.BoxSize.z } );
		if ( !Shape.empty() ) Json["Shape"] = Shape;
		if ( !IsDefault( Emitter.Direction, Default.Direction ) ) Json["Direction"] = NlohmannJson::array( { Emitter.Direction.x, Emitter.Direction.y, Emitter.Direction.z } );
		if ( !IsDefault( Emitter.Gravity, Default.Gravity ) ) Json["Gravity"] = NlohmannJson::array( { Emitter.Gravity.x, Emitter.Gravity.y, Emitter.Gravity.z } );
		if ( Emitter.Drag != Default.Drag ) Json["Drag"] = Emitter.Drag;

		NlohmannJson Size = NlohmannJson::object();
		if ( !IsDefault( Emitter.StartSize, Default.StartSize ) ) Size["Start"] = RangeToJson( Emitter.StartSize );
		if ( !IsDefault( Emitter.EndSize, Default.EndSize ) ) Size["End"] = RangeToJson( Emitter.EndSize );
		if ( !Size.empty() ) Json["Size"] = Size;
		NlohmannJson Color = NlohmannJson::object();
		if ( !IsDefault( Emitter.StartColor, Default.StartColor ) ) Color["Start"] = NWorld::FormatColor( Emitter.StartColor );
		if ( !IsDefault( Emitter.EndColor, Default.EndColor ) ) Color["End"] = NWorld::FormatColor( Emitter.EndColor );
		if ( !Color.empty() ) Json["Color"] = Color;
		if ( !IsDefault( Emitter.StartAngle, Default.StartAngle ) ) Json["Angle"] = RangeToJson( Emitter.StartAngle );
		if ( !IsDefault( Emitter.Spin, Default.Spin ) ) Json["Spin"] = RangeToJson( Emitter.Spin );
		return Json;
	}

	template<typename TEnum>
	void ReadEnum( const NlohmannJson& Object, const char* Key, TEnum& Out, TArray<FString>* Warnings, const FString& Where )
	{
		if ( !Object.contains( Key ) ) return;
		const FString Text = NJson::GetOr<FString>( Object, Key, FString() );
		if ( !TryParse( Text, Out ) ) Warn( Warnings, Where + ": unknown " + FString( Key ) + " '" + Text + "' (using the default)" );
	}

	void ReadColor( const NlohmannJson& Object, const char* Key, FColor32& Out, TArray<FString>* Warnings, const FString& Where )
	{
		if ( !Object.contains( Key ) ) return;
		const FString Text = NJson::GetOr<FString>( Object, Key, FString() );
		if ( !NWorld::TryParseColor( Text, Out ) ) Warn( Warnings, Where + ": '" + FString( Key ) + "' is not a colour like #RRGGBB or #RRGGBBAA (using the default)" );
	}

	FParticleEmitterDefinition EmitterFromJson( const NlohmannJson& Json, TArray<FString>* Warnings, int32 Index )
	{
		FParticleEmitterDefinition Emitter;
		const FString Where = FString::Printf( "emitter #%d", Index );
		if ( !Json.is_object() )
		{
			Warn( Warnings, Where + " is not an object (using the default emitter)" );
			return Emitter;
		}
		Emitter.Name = NJson::GetOr<FString>( Json, "Name", Emitter.Name );
		Emitter.Texture = NJson::GetOr<FString>( Json, "Texture", FString() );
		ReadEnum( Json, "Blend", Emitter.Blend, Warnings, Where );
		ReadEnum( Json, "Space", Emitter.Space, Warnings, Where );
		ReadEnum( Json, "Orient", Emitter.Orient, Warnings, Where );
		if ( Json.contains( "Path" ) )
		{
			// 表示設定の読み込みで見つかった項目を、元のエミッター番号と一緒に報告する。
			TArray<FString> PathWarnings;
			if ( !FEffectPathDefinition::FromJson( Json["Path"], Emitter.Path, &PathWarnings ) ) Warn( Warnings, Where + ": Path はオブジェクトにしてください（既定値を使います）" );
			// 表示設定内の警告を、呼び出し元の一覧へ渡す。
			for ( const FString& Message : PathWarnings ) Warn( Warnings, Where + ": " + Message );
		}
		if ( Json.contains( "Material" ) )
		{
			// 材質の読み込み時に見つかった警告を、元のエミッター番号と一緒に報告する。
			TArray<FString> MaterialWarnings;
			if ( !FEffectMaterialDefinition::FromJson( Json["Material"], Emitter.Material, &MaterialWarnings ) )
			{
				Emitter.Material.Dissolve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
				Warn( Warnings, Where + ": Material はオブジェクトにしてください（Validate で修正してください）" );
			}
			for ( const FString& Message : MaterialWarnings ) Warn( Warnings, Where + ": Material: " + Message );
		}
		if ( Json.contains( "Appearance" ) )
		{
			// 表示倍率の読み込み警告を元のエミッター番号と一緒に報告する。
			TArray<FString> AppearanceWarnings;
			if ( !FEffectAppearanceDefinition::FromJson( Json["Appearance"], Emitter.Appearance, &AppearanceWarnings ) )
			{
				Emitter.Appearance.Size.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( 0xFFu );
				Warn( Warnings, Where + ": Appearance はオブジェクトにしてください（Validate で修正してください）" );
			}
			for ( const FString& Message : AppearanceWarnings ) Warn( Warnings, Where + ": Appearance: " + Message );
		}
		if ( Json.contains( "Flipbook" ) )
		{
			// Flipbookの不正値を既定化せず、emitter番号付きの警告と検証状態へ渡す。
			TArray<FString> FlipbookWarnings;
			FEffectFlipbookDefinition::FromJson( Json["Flipbook"], Emitter.Flipbook, &FlipbookWarnings );
			for ( const FString& Message : FlipbookWarnings ) Warn( Warnings, Where + ": Flipbook: " + Message );
		}
		Emitter.MaxParticles = NJson::GetOr<int32>( Json, "MaxParticles", Emitter.MaxParticles );
		Emitter.Delay = NJson::GetOr<float32>( Json, "Delay", Emitter.Delay );
		Emitter.Rate = NJson::GetOr<float32>( Json, "Rate", Emitter.Rate );
		if ( Json.contains( "Bursts" ) && Json["Bursts"].is_array() )
		{
			for ( const NlohmannJson& Item : Json["Bursts"] )
			{
				if ( !Item.is_object() )
				{
					Warn( Warnings, Where + ": a burst that is not an object was skipped" );
					continue;
				}
				FParticleBurst Burst;
				Burst.Time = NJson::GetOr<float32>( Item, "Time", 0.0f );
				Burst.Count = NJson::GetOr<int32>( Item, "Count", Burst.Count );
				Emitter.Bursts.Add( Burst );
			}
			Emitter.SortBursts();
		}
		else if ( Json.contains( "Bursts" ) ) Warn( Warnings, Where + ": 'Bursts' is not an array" );
		Emitter.Lifetime = ReadRange( Json, "Lifetime", Emitter.Lifetime, Warnings, Where );
		Emitter.Speed = ReadRange( Json, "Speed", Emitter.Speed, Warnings, Where );
		if ( Json.contains( "Shape" ) && Json["Shape"].is_object() )
		{
			const NlohmannJson& Shape = Json["Shape"];
			ReadEnum( Shape, "Type", Emitter.Shape, Warnings, Where );
			Emitter.ShapeRadius = NJson::GetOr<float32>( Shape, "Radius", Emitter.ShapeRadius );
			Emitter.ConeAngle = NJson::GetOr<float32>( Shape, "Angle", Emitter.ConeAngle );
			Emitter.BoxSize = NJson::ReadVector3( Shape, "Size", Emitter.BoxSize );
		}
		else if ( Json.contains( "Shape" ) ) Warn( Warnings, Where + ": 'Shape' is not an object (using the default)" );
		Emitter.Direction = NJson::ReadVector3( Json, "Direction", Emitter.Direction );
		Emitter.Gravity = NJson::ReadVector3( Json, "Gravity", Emitter.Gravity );
		Emitter.Drag = NJson::GetOr<float32>( Json, "Drag", Emitter.Drag );
		if ( Json.contains( "Size" ) && Json["Size"].is_object() )
		{
			Emitter.StartSize = ReadRange( Json["Size"], "Start", Emitter.StartSize, Warnings, Where );
			Emitter.EndSize = ReadRange( Json["Size"], "End", Emitter.EndSize, Warnings, Where );
		}
		else if ( Json.contains( "Size" ) ) Warn( Warnings, Where + ": 'Size' is not an object (using the default)" );
		if ( Json.contains( "Color" ) && Json["Color"].is_object() )
		{
			ReadColor( Json["Color"], "Start", Emitter.StartColor, Warnings, Where );
			ReadColor( Json["Color"], "End", Emitter.EndColor, Warnings, Where );
		}
		else if ( Json.contains( "Color" ) ) Warn( Warnings, Where + ": 'Color' is not an object (using the default)" );
		Emitter.StartAngle = ReadRange( Json, "Angle", Emitter.StartAngle, Warnings, Where );
		Emitter.Spin = ReadRange( Json, "Spin", Emitter.Spin, Warnings, Where );
		return Emitter;
	}

	bool IsFinite( float32 Value ) { return std::isfinite( Value ); }
	bool IsFinite( const FVector3& V ) { return IsFinite( V.x ) && IsFinite( V.y ) && IsFinite( V.z ); }
}

//=============================================================================
// 種類の名前
//=============================================================================
const char* ToString( EParticleShape Shape )
{
	switch ( Shape )
	{
	case EParticleShape::Cone: return "Cone";
	case EParticleShape::Sphere: return "Sphere";
	case EParticleShape::Box: return "Box";
	}
	return "Cone";
}

const char* ToString( EParticleBlend Blend )
{
	return Blend == EParticleBlend::Add ? "Add" : "Alpha";
}

const char* ToString( EParticleSpace Space )
{
	return Space == EParticleSpace::Local ? "Local" : "World";
}

const char* ToString( EParticleOrient Orient )
{
	switch ( Orient )
	{
	case EParticleOrient::Camera: return "Camera";
	case EParticleOrient::CameraY: return "CameraY";
	case EParticleOrient::Fixed: return "Fixed";
	}
	return "Camera";
}

bool TryParse( const FString& Text, EParticleShape& Out )
{
	for ( const EParticleShape Shape : { EParticleShape::Cone, EParticleShape::Sphere, EParticleShape::Box } )
	{
		if ( Text.Equals( FString( ToString( Shape ) ), ESearchCase::IgnoreCase ) )
		{
			Out = Shape;
			return true;
		}
	}
	return false;
}

bool TryParse( const FString& Text, EParticleBlend& Out )
{
	for ( const EParticleBlend Blend : { EParticleBlend::Alpha, EParticleBlend::Add } )
	{
		if ( Text.Equals( FString( ToString( Blend ) ), ESearchCase::IgnoreCase ) )
		{
			Out = Blend;
			return true;
		}
	}
	return false;
}

bool TryParse( const FString& Text, EParticleSpace& Out )
{
	for ( const EParticleSpace Space : { EParticleSpace::World, EParticleSpace::Local } )
	{
		if ( Text.Equals( FString( ToString( Space ) ), ESearchCase::IgnoreCase ) )
		{
			Out = Space;
			return true;
		}
	}
	return false;
}

bool TryParse( const FString& Text, EParticleOrient& Out )
{
	for ( const EParticleOrient Orient : { EParticleOrient::Camera, EParticleOrient::CameraY, EParticleOrient::Fixed } )
	{
		if ( Text.Equals( FString( ToString( Orient ) ), ESearchCase::IgnoreCase ) )
		{
			Out = Orient;
			return true;
		}
	}
	return false;
}

//=============================================================================
// FParticleEmitterDefinition
//=============================================================================
void FParticleEmitterDefinition::SortBursts()
{
	Bursts.StableSort( []( const FParticleBurst& A, const FParticleBurst& B ) { return A.Time < B.Time; } );
}

//=============================================================================
// FParticleEffectDefinition
//=============================================================================
NlohmannJson FParticleEffectDefinition::ToJson() const
{
	NlohmannJson Json = NlohmannJson::object();
	Json["Version"] = Version;
	if ( !Name.IsEmpty() ) Json["Name"] = Name;
	if ( Duration != 0.0f ) Json["Duration"] = Duration;
	if ( !bLoop ) Json["Loop"] = false;
	if ( Seed != 1 ) Json["Seed"] = Seed;
	NlohmannJson Array = NlohmannJson::array();
	for ( const FParticleEmitterDefinition& Emitter : Emitters ) Array.push_back( EmitterToJson( Emitter ) );
	Json["Emitters"] = Array;
	return Json;
}

bool FParticleEffectDefinition::FromJson( const NlohmannJson& Json, FParticleEffectDefinition& OutDefinition, TArray<FString>* OutWarnings )
{
	if ( !Json.is_object() ) return false;
	FParticleEffectDefinition Effect;
	Effect.Version = NJson::GetOr<int32>( Json, "Version", ParticleEffectFormatVersion );
	if ( Effect.Version > ParticleEffectFormatVersion ) Warn( OutWarnings, FString::Printf( "the effect is version %d, newer than this program's version %d", Effect.Version, ParticleEffectFormatVersion ) );
	Effect.Name = NJson::GetOr<FString>( Json, "Name", FString() );
	Effect.Duration = NJson::GetOr<float32>( Json, "Duration", 0.0f );
	Effect.bLoop = NJson::GetOr<bool>( Json, "Loop", true );
	Effect.Seed = NJson::GetOr<uint32>( Json, "Seed", 1u );
	if ( Json.contains( "Emitters" ) && Json["Emitters"].is_array() )
	{
		int32 Index = 0;
		for ( const NlohmannJson& Item : Json["Emitters"] ) Effect.Emitters.Add( EmitterFromJson( Item, OutWarnings, Index++ ) );
	}
	else if ( Json.contains( "Emitters" ) ) Warn( OutWarnings, "'Emitters' is not an array" );
	OutDefinition = std::move( Effect );
	return true;
}

TArray<FParticleIssue> FParticleEffectDefinition::Validate() const
{
	TArray<FParticleIssue> Issues;
	const auto Add = [&]( bool bError, int32 Emitter, const FString& Message )
	{
		FParticleIssue Issue;
		Issue.bError = bError;
		Issue.Emitter = Emitter;
		Issue.Message = Message;
		Issues.Add( Issue );
	};
	// Tree Lightningの全emitterが要求するparticle枠の合計。
	int64 TreeParticleBudget = 0;
	for ( const FParticleEmitterDefinition& Emitter : Emitters ) if ( Emitter.Path.Kind == FEffectPathDefinition::EKind::TreeLightning ) TreeParticleBudget += std::max( 0, Emitter.MaxParticles );
	if ( TreeParticleBudget > 4 ) Add( false, -1, "Tree Lightningは負荷を抑えるため、全エミッター合計で先頭4粒子まで描画します" );

	if ( Emitters.IsEmpty() ) Add( false, -1, "the effect has no emitters (nothing is shown)" );
	if ( Emitters.Num() > MaxEmitters ) Add( true, -1, FString::Printf( "the effect has %d emitters (the limit is %d)", Emitters.Num(), MaxEmitters ) );
	if ( !IsFinite( Duration ) || Duration < 0.0f ) Add( true, -1, "the duration must be 0 (until stopped) or more" );

	for ( int32 i = 0; i < Emitters.Num(); ++i )
	{
		const FParticleEmitterDefinition& E = Emitters[i];
		// 帯と雷の各項目も、エミッターのエラーとして保存前に確認する。
		for ( const FString& Message : E.Path.Validate() ) Add( true, i, Message );
		// 材質設定は表示方式に関わらず検証する。
		for ( const FString& Message : E.Material.Validate() ) Add( true, i, Message );
		// UV歪みは材質が無効でも値を、Material.Validate経由で検証する。
		if ( E.Material.Distortion.bEnabled && !E.Material.bEnabled ) Add( false, i, "UV歪みを適用するには材質を有効にしてください。設定値は保持します" );
		// 寿命に沿った倍率曲線は保存前に値域と順序を検証する。
		for ( const FString& Message : E.Appearance.Validate() ) Add( true, i, FString( "Appearance: " ) + Message );
		// コマ再生の保存値は無効中も検証し、帯や雷での未対応だけを警告する。
		for ( const FString& Message : E.Flipbook.Validate() ) Add( true, i, FString( "Flipbook: " ) + Message );
		if ( E.Flipbook.bEnabled && E.Path.Kind != FEffectPathDefinition::EKind::None ) Add( false, i, "Flipbook は四角形粒子にだけ適用され、帯と雷の再生では無視します" );
		// RibbonのUV位相はshaderへ連続floatで渡すため、寿命全体の移動量を制限する。
		if ( E.Material.bEnabled && E.Path.Kind == FEffectPathDefinition::EKind::Ribbon && std::isfinite( E.Lifetime.Min ) && std::isfinite( E.Lifetime.Max ) && E.Lifetime.Min > 0.0f && E.Lifetime.Max > 0.0f && std::isfinite( E.Material.UVScroll.x ) && std::isfinite( E.Material.UVScroll.y ) )
		{
			// 並べ替え前後を問わない最長秒数。
			const double MaximumLifetime = std::max( static_cast<double>( E.Lifetime.Min ), static_cast<double>( E.Lifetime.Max ) );
			// 一番大きいUV速度。
			const double MaximumScroll = std::max( std::fabs( static_cast<double>( E.Material.UVScroll.x ) ), std::fabs( static_cast<double>( E.Material.UVScroll.y ) ) );
			if ( MaximumLifetime * MaximumScroll > 1.0e6 ) Add( true, i, "帯の材質では寿命×UV速度を 1000000 以下にしてください" );
		}
		if ( E.Material.bEnabled && E.Material.Distortion.bEnabled && E.Path.Kind == FEffectPathDefinition::EKind::Ribbon && std::isfinite( E.Lifetime.Min ) && std::isfinite( E.Lifetime.Max ) && E.Lifetime.Min > 0.0f && E.Lifetime.Max > 0.0f && std::isfinite( E.Material.Distortion.UVScroll.x ) && std::isfinite( E.Material.Distortion.UVScroll.y ) )
		{
			// UV位相の計算に使う最長寿命。
			const double MaximumDistortionLifetime = std::max( static_cast<double>( E.Lifetime.Min ), static_cast<double>( E.Lifetime.Max ) );
			// 横縦のnoise速度で大きい方。
			const double MaximumDistortionScroll = std::max( std::fabs( static_cast<double>( E.Material.Distortion.UVScroll.x ) ), std::fabs( static_cast<double>( E.Material.Distortion.UVScroll.y ) ) );
			if ( MaximumDistortionLifetime * MaximumDistortionScroll > 1.0e6 ) Add( true, i, "帯のUV歪みでは寿命×noise速度を 1000000 以下にしてください" );
		}
		if ( E.Path.Kind == FEffectPathDefinition::EKind::Lightning && E.MaxParticles > 64 ) Add( false, i, "雷は負荷を抑えるため、先頭の 64 個まで描画します" );
		if ( E.MaxParticles < 1 || E.MaxParticles > MaxParticlesPerEmitter ) Add( true, i, FString::Printf( "MaxParticles must be between 1 and %d", MaxParticlesPerEmitter ) );
		if ( !IsFinite( E.Delay ) || E.Delay < 0.0f ) Add( true, i, "the delay must not be negative" );
		if ( !IsFinite( E.Rate ) || E.Rate < 0.0f ) Add( true, i, "the rate must not be negative" );
		for ( const FParticleBurst& Burst : E.Bursts )
		{
			if ( !IsFinite( Burst.Time ) || Burst.Time < 0.0f ) Add( true, i, "a burst time must not be negative" );
			if ( Burst.Count < 0 ) Add( true, i, "a burst count must not be negative" );
		}
		const auto CheckRange = [&]( const FParticleRange& Range, const char* What, bool bMustBePositive )
		{
			if ( !IsFinite( Range.Min ) || !IsFinite( Range.Max ) )
			{
				Add( true, i, FString( What ) + " has a value that is not a finite number" );
				return;
			}
			if ( Range.Min > Range.Max ) Add( false, i, FString( What ) + ": the minimum is larger than the maximum (they are swapped)" );
			if ( bMustBePositive && std::min( Range.Min, Range.Max ) <= 0.0f ) Add( true, i, FString( What ) + " must be greater than 0" );
		};
		CheckRange( E.Lifetime, "the lifetime", true );
		CheckRange( E.Speed, "the speed", false );
		CheckRange( E.StartSize, "the start size", false );
		CheckRange( E.EndSize, "the end size", false );
		CheckRange( E.StartAngle, "the start angle", false );
		CheckRange( E.Spin, "the spin", false );
		if ( std::min( E.Speed.Min, E.Speed.Max ) < 0.0f ) Add( true, i, "the speed must not be negative" );
		if ( std::min( E.StartSize.Min, E.StartSize.Max ) < 0.0f || std::min( E.EndSize.Min, E.EndSize.Max ) < 0.0f ) Add( true, i, "a size must not be negative" );
		if ( !IsFinite( E.ShapeRadius ) || E.ShapeRadius < 0.0f ) Add( true, i, "the shape radius must not be negative" );
		if ( !IsFinite( E.ConeAngle ) || E.ConeAngle < 0.0f || E.ConeAngle > 180.0f ) Add( true, i, "the cone angle must be between 0 and 180 degrees" );
		if ( !IsFinite( E.BoxSize ) || E.BoxSize.x < 0.0f || E.BoxSize.y < 0.0f || E.BoxSize.z < 0.0f ) Add( true, i, "the box size must not be negative" );
		if ( !IsFinite( E.Direction ) || E.Direction.LengthSquared() < 1.0e-12f ) Add( true, i, "the direction must not be zero" );
		if ( !IsFinite( E.Gravity ) ) Add( true, i, "the gravity is not a finite number" );
		if ( !IsFinite( E.Drag ) || E.Drag < 0.0f ) Add( true, i, "the drag must not be negative" );
		if ( E.Rate > 0.0f && E.MaxParticles >= 1 && E.Rate * std::max( E.Lifetime.Max, E.Lifetime.Min ) > static_cast<float32>( E.MaxParticles ) * 1.01f )
		{
			Add( false, i, FString::Printf( "at this rate and lifetime more than %d particles would be alive at once (the extra ones are not emitted)", E.MaxParticles ) );
		}
		if ( E.Rate == 0.0f && E.Bursts.IsEmpty() ) Add( false, i, "the emitter has no rate and no bursts (nothing is emitted)" );
	}
	return Issues;
}

bool FParticleEffectDefinition::HasErrors( const TArray<FParticleIssue>& Issues )
{
	for ( const FParticleIssue& Issue : Issues )
	{
		if ( Issue.bError ) return true;
	}
	return false;
}

uint64 FParticleEffectDefinition::ComputeHash() const
{
	FHasher Hasher;
	Hasher.Value( Duration );
	Hasher.Value( bLoop );
	Hasher.Value( Seed );
	Hasher.Value( Emitters.Num() );
	for ( const FParticleEmitterDefinition& E : Emitters )
	{
		Hasher.String( E.Name );
		Hasher.String( E.Texture );
		Hasher.Value( static_cast<uint8>( E.Blend ) );
		Hasher.Value( static_cast<uint8>( E.Space ) );
		Hasher.Value( static_cast<uint8>( E.Orient ) );
		Hasher.Value( E.Path.ComputeHash() );
		Hasher.Value( E.Material.ComputeHash() );
		// 既定の Appearance は混ぜず、旧エフェクトの hash を保つ。
		// 比較に使う既定倍率。
		const FEffectAppearanceDefinition DefaultAppearance;
		if ( E.Appearance.ComputeHash() != DefaultAppearance.ComputeHash() )
		{
			Hasher.Value( uint32( 0x41505052u ) );
			Hasher.Value( E.Appearance.ComputeHash() );
		}
		// 既定の Flipbook は混ぜず、format 1 の旧 hash を保つ。
		if ( !E.Flipbook.IsDefault() )
		{
			Hasher.Value( uint32( 0x464C4950u ) );
			Hasher.Value( E.Flipbook.ComputeHash() );
		}
		Hasher.Value( E.MaxParticles );
		Hasher.Value( E.Delay );
		Hasher.Value( E.Rate );
		Hasher.Value( E.Bursts.Num() );
		for ( const FParticleBurst& Burst : E.Bursts )
		{
			Hasher.Value( Burst.Time );
			Hasher.Value( Burst.Count );
		}
		Hasher.Range( E.Lifetime );
		Hasher.Range( E.Speed );
		Hasher.Value( static_cast<uint8>( E.Shape ) );
		Hasher.Value( E.ShapeRadius );
		Hasher.Value( E.ConeAngle );
		Hasher.Vector( E.BoxSize );
		Hasher.Vector( E.Direction );
		Hasher.Vector( E.Gravity );
		Hasher.Value( E.Drag );
		Hasher.Range( E.StartSize );
		Hasher.Range( E.EndSize );
		Hasher.Color( E.StartColor );
		Hasher.Color( E.EndColor );
		Hasher.Range( E.StartAngle );
		Hasher.Range( E.Spin );
	}
	return Hasher.Hash;
}

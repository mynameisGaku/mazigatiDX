#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Optional.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/World/Mesh/EditMesh.h"
#include "Library/Json/JsonObject.h"
#include "Library/Math/Color.h"
#include "Library/Math/Transform.h"
#include "Library/Math/Vector2.h"
#include "Library/Math/Vector3.h"

//=============================================================================
// ワールド（.world）の定義
//   レベル（置き物の集まり）を表す、純粋なデータ。DxLib・シーン・物理・ImGui を使わない。
//   JSON と相互に変換できる（書式は WorldDefinition.cpp の先頭）。シーンへの設置は WorldSpawner.h、編集の操作は WorldEdit.h、
//   検証は WorldValidate.h、当たり判定を付けるかどうかの決定は WorldCollision.h。
//
//   考え方:
//   - オブジェクトは平らな配列。階層は Parent（親の Id）で表す。Id は安定（名前を変えても、並べ替えても、消しても、同じ Id は二度と使われない）
//     なので、コードやほかのオブジェクトからの参照は Id でする。名前は一意でなくてよい。
//   - 1 つのオブジェクトが持てる部品（Model / Billboard / Sound / Effect / Collider / Mesh / Prefab）は、種類ごとに 1 つまで。複数要るときは、子オブジェクトにする。
//     知らない種類の部品や、知らない項目は、JSON のまま保存して、書き戻しで失われない（新しいデータを古いエディタが壊さない）。
//   - タグ（文字列）とプロパティ（名前と値）は、どのオブジェクトにも付けられる。コードは、タグでオブジェクトを探し、
//     プロパティを読む（空のオブジェクトにタグを付けておけば、そこへコードの生成物を結び付けられる）。
//   - 角度は度、座標は左手系・Y 上（Library/Math と同じ）。
//
//   FWorldDefinition World;
//   FWorldDefinition::FromJson( Json, World );        // ファイルからは、CAssetManager::LoadWorld（WorldAsset.h）
//   NlohmannJson Saved = World.ToJson();               // 編集は NWorldEdit（WorldEdit.h）を使う
//=============================================================================

constexpr int32 WorldFormatVersion = 1;
constexpr int32 WorldNoId = 0;   // 「親がいない」「無効な Id」を表す Id
constexpr int32 WorldMaxPrefabDepth = 8;   // 入れ子（Prefab 部品）の深さの上限。これより深いものは、置かない（循環の最後の砦）

//-----------------------------------------------------------------------------
// 種類
//-----------------------------------------------------------------------------
// ビルボード（常に平らな画像）の向き
enum class EWorldBillboardMode : uint8
{
	Camera,    // 常にカメラの正面を向く（画面に対して平ら）
	CameraY,   // Y 軸だけ回ってカメラの方を向く（地面に立った看板・草・木のような使い方。傾かない）
	World,     // オブジェクトの向きのまま（回転・拡大もオブジェクトに従う。床の模様・壁の絵のような使い方）
};

enum class EWorldColliderShape : uint8
{
	Box,       // Size = 各辺の長さ
	Sphere,    // Radius
	Capsule,   // Radius と Height（Y 軸方向。Height は半球を含む全体の高さ）
	Mesh,      // 三角形そのもの（静的な床・壁・地形用）。Mesh のファイル、無ければ、同じオブジェクトの Mesh 部品（編集メッシュ）、それも無ければ、Model のファイルから作る
};

// オブジェクトごとの、当たり判定を付けるかどうかの指定
enum class EWorldCollisionMode : uint8
{
	Auto,   // ワールドのタグ規則と既定に従う
	On,     // 付ける（タグ規則より優先）
	Off,    // 付けない（タグ規則より優先）
};

enum class EWorldPropType : uint8
{
	Bool,
	Int,
	Float,
	String,
	Vector3,
	Asset,    // アセットのパス（ルートからの相対）。検証でファイルがあるか確かめられる
	Object,   // ほかのオブジェクトへの参照（Id）。検証で存在を確かめられる
};

const char* ToString( EWorldBillboardMode Mode );
const char* ToString( EWorldColliderShape Shape );
const char* ToString( EWorldCollisionMode Mode );
const char* ToString( EWorldPropType Type );
// 大文字小文字は区別しない。知らない名前は false（Out は変えない）
bool TryParse( const FString& Text, EWorldBillboardMode& Out );
bool TryParse( const FString& Text, EWorldColliderShape& Out );
bool TryParse( const FString& Text, EWorldCollisionMode& Out );

//-----------------------------------------------------------------------------
// FWorldProp
//   オブジェクトのプロパティ 1 つ。Type に応じた値のメンバだけが意味を持つ（String と Asset は StringValue）。
//   JSON では、bool / 整数 / 小数 / 文字列 / [x, y, z] は、そのままの値。Asset は { "Asset": "パス" }、Object は { "Object": Id }。
//-----------------------------------------------------------------------------
struct FWorldProp
{
	FString Name;
	EWorldPropType Type = EWorldPropType::String;
	bool bValue = false;
	int64 IntValue = 0;
	float64 FloatValue = 0.0;
	FString StringValue;
	FVector3 VectorValue = FVector3::Zero;
	int32 ObjectId = WorldNoId;

	static FWorldProp MakeBool( const FString& InName, bool bInValue );
	static FWorldProp MakeInt( const FString& InName, int64 InValue );
	static FWorldProp MakeFloat( const FString& InName, float64 InValue );
	static FWorldProp MakeString( const FString& InName, const FString& InValue );
	static FWorldProp MakeVector3( const FString& InName, const FVector3& InValue );
	static FWorldProp MakeAsset( const FString& InName, const FString& InPath );
	static FWorldProp MakeObject( const FString& InName, int32 InObjectId );

	// 値だけの JSON（上の書式）
	NlohmannJson ValueToJson() const;
	// JSON の値から。作れない形（null・長さの違う配列など）なら false
	static bool FromJsonValue( const FString& InName, const NlohmannJson& Json, FWorldProp& OutProp );

	// 人が読む形（インスペクターやログ用）
	FString ToDisplayString() const;
};

//-----------------------------------------------------------------------------
// 部品
//-----------------------------------------------------------------------------
// 位置・回転（度。Pitch, Yaw, Roll）・拡大
struct FWorldTransform
{
	FVector3 Position = FVector3::Zero;
	FVector3 RotationDegrees = FVector3::Zero;
	FVector3 Scale = FVector3::One;

	FTransform ToTransform() const;
	static FWorldTransform FromTransform( const FTransform& Transform );
	bool IsIdentity() const;
};

// 3D モデル
struct FWorldModel
{
	FString Path;                 // モデルのファイル（.mv1 / .fbx など。アセットのルートからの相対）
	FString Animations;           // アニメーション集（.modelanim.json）。空なら、モデルのファイルに入っているアニメーション
	FString Clip;                 // 最初に再生するクリップの名前。空なら再生しない
	float32 Speed = 1.0f;         // 再生の速さ
};

// 画像（板）
struct FWorldBillboard
{
	FString Texture;                                 // 画像のファイル
	FVector2 Size = FVector2( 1.0f, 1.0f );          // 世界での大きさ（幅 x 高さ）。オブジェクトの拡大が掛かる
	FVector2 Pivot = FVector2( 0.5f, 0.5f );         // 画像の中の、オブジェクトの位置に来る点（0..1。左上が 0, 0）
	EWorldBillboardMode Mode = EWorldBillboardMode::Camera;
	FColor32 Tint = FColor32( 255, 255, 255, 255 );  // 色（A は不透明度）
	bool bUseRect = false;                           // true なら、画像の一部だけ（スプライトシートの 1 コマなど）
	int32 RectX = 0;
	int32 RectY = 0;
	int32 RectWidth = 0;
	int32 RectHeight = 0;
	bool bFlipX = false;
	bool bFlipY = false;
};

// 環境音
struct FWorldSound
{
	FString Path;                 // 音のファイル
	FString Bus = "Ambient";      // Master / Music / Sfx / Ambient / Voice / Ui
	float32 Volume = 1.0f;
	float32 Pitch = 1.0f;
	bool bLoop = true;
	bool bAutoPlay = true;        // シーンに入ったときに鳴らす
	float32 MinDistance = 1.0f;   // これより近いと、最大の音量
	float32 MaxDistance = 25.0f;  // これより遠いと、聞こえない
};

// エフェクト（炎・煙・火花・魔法の光など。Effect/EffectBackend.h のバックエンドが再生する。内蔵は .effect.json のパーティクル）
struct FWorldEffect
{
	FString Path;                 // エフェクトのファイル
	bool bAutoPlay = true;        // シーンに入ったときに始める
	float32 Scale = 1.0f;         // 大きさの倍率（オブジェクトの拡大にさらに掛かる）
	float32 Speed = 1.0f;         // 時間の進み方
	int32 Seed = 0;               // 乱数のシード（0 ならエフェクトのシード）
	int32 Loop = -1;              // -1 = エフェクトの指定のまま / 0 = ループしない / 1 = ループする
};

// 当たり判定の形。付けるかどうかは、オブジェクトの Collision とワールドのタグ規則で決まる（WorldCollision.h）
struct FWorldCollider
{
	EWorldColliderShape Shape = EWorldColliderShape::Box;
	FVector3 Size = FVector3::One;     // Box: 各辺の長さ
	float32 Radius = 0.5f;             // Sphere / Capsule
	float32 Height = 2.0f;             // Capsule: 半球を含む全体の高さ
	FVector3 Offset = FVector3::Zero;  // 形の中心の、オブジェクトからのずれ（オブジェクトのローカル。Mesh では使わない: モデルの座標のまま）
	FString Mesh;                      // Mesh: 三角形を取るモデルのファイル。空なら、同じオブジェクトの Mesh 部品（あれば）、無ければ Model のファイル
	bool bTrigger = false;             // true なら、ぶつからず、重なりだけを知らせる
};

// 入れ子のワールド（プレハブ）。別の .world を、このオブジェクトの子として置く
//   置くときは、その .world のオブジェクトが、このオブジェクトの子になる（このオブジェクトの位置・向き・大きさに従う）。
//   中のタグ・結び付け（FWorldBindings）・部品は、ふつうに働く。当たり判定のタグ規則は、中のワールドのものが使われる。
struct FWorldPrefab
{
	FString Path;                 // 置く .world のファイル（アセットのルートからの相対）
};

// 知らない種類の部品（JSON のまま保存する）
struct FWorldRawComponent
{
	FString Type;
	NlohmannJson Data;
};

//-----------------------------------------------------------------------------
// FWorldObject
//-----------------------------------------------------------------------------
struct FWorldObject
{
	int32 Id = WorldNoId;             // ワールドの中で一意（1 以上）。コードからの参照用
	FString Name;
	int32 Parent = WorldNoId;         // 親の Id。WorldNoId なら最上位
	bool bEnabled = true;             // false なら、実行時に置かない（子も）。データには残る
	TArray<FString> Tags;
	TArray<FWorldProp> Props;         // 名前の順に並べて保つ（SetProp が保つ）
	FWorldTransform Transform;        // 親からの相対
	EWorldCollisionMode Collision = EWorldCollisionMode::Auto;

	TOptional<FWorldModel> Model;
	TOptional<FWorldBillboard> Billboard;
	TOptional<FWorldSound> Sound;
	TOptional<FWorldEffect> Effect;
	TOptional<FWorldCollider> Collider;
	TOptional<FEditMesh> Mesh;        // 編集メッシュ（レベル専用の形。Mesh/EditMesh.h）
	TOptional<FWorldPrefab> Prefab;   // 入れ子のワールド（別の .world を、子として置く）
	TArray<FWorldRawComponent> RawComponents;
	NlohmannJson Extra;               // 知らない項目（書き戻すとき、そのまま出す）

	// タグは大文字小文字を区別する（コードの文字列定数と同じ綴りで探すため）
	bool HasTag( const FString& Tag ) const;
	bool AddTag( const FString& Tag );     // 空・前後の空白だけ・すでにある場合は false
	bool RemoveTag( const FString& Tag );

	const FWorldProp* FindProp( const FString& PropName ) const;
	// 同じ名前があれば置き換える。名前の順を保つ
	void SetProp( const FWorldProp& Prop );
	bool RemoveProp( const FString& PropName );

	const FWorldRawComponent* FindRawComponent( const FString& Type ) const;

	// 付いている部品の名前（"Model" "Billboard" "Sound" "Effect" "Collider" "Mesh" "Prefab" と、知らない種類）
	TArray<FString> GetComponentNames() const;
};

//-----------------------------------------------------------------------------
// FWorldDefinition
//-----------------------------------------------------------------------------
// 当たり判定のタグ規則。Tag を持つオブジェクトの当たり判定を、付ける / 付けない
struct FWorldCollisionRule
{
	FString Tag;
	bool bCollide = false;
};

struct FWorldSettings
{
	// 上から順に見て、最初に一致した規則だけが使われる（オブジェクトごとの指定が先）
	TArray<FWorldCollisionRule> CollisionRules;
};

struct FWorldDefinition
{
	int32 Version = WorldFormatVersion;
	FString Name;
	FWorldSettings Settings;
	// 次に割り当てる Id。オブジェクトを消しても戻らない（古い Id が別のオブジェクトを指さないように）
	int32 NextId = 1;
	TArray<FWorldObject> Objects;
	NlohmannJson Extra;               // 知らない項目

	// 新しい Id を 1 つ割り当てる（NextId を進める）
	int32 AllocateId();

	// Id から探す。無ければ INDEX_NONE / nullptr
	int32 FindIndexById( int32 Id ) const;
	FWorldObject* FindObject( int32 Id );
	const FWorldObject* FindObject( int32 Id ) const;
	// 最初に見つかった、その名前（大文字小文字を区別する）のオブジェクト
	const FWorldObject* FindObjectByName( const FString& ObjectName ) const;

	NlohmannJson ToJson() const;
	// JSON から。オブジェクトでなければ false。足りない項目は既定値、知らない項目は Extra に残る。
	// Id が無い・重複しているオブジェクトには新しい Id を振る（直した内容は OutWarnings に書く）。
	// OutWarnings は省略できる
	static bool FromJson( const NlohmannJson& Json, FWorldDefinition& OutDefinition, TArray<FString>* OutWarnings = nullptr );
};

namespace NWorld
{
	// アセットのパスを、比べられる形にする（区切りは / 、大文字小文字は区別しない。Windows のファイルの扱いに合わせる）
	FString NormalizeAssetPath( const FString& Path );

	// "#RRGGBB" / "#RRGGBBAA"（# は省略可）の色。読めなければ false
	bool TryParseColor( const FString& Text, FColor32& OutColor );
	// A が 255 なら "#RRGGBB"、そうでなければ "#RRGGBBAA"
	FString FormatColor( const FColor32& Color );
}

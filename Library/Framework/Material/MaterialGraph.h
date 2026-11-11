// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/MaterialTypes.h"

//=============================================================================
// マテリアルのグラフ（ノードと線）
//   ノードは 1 つの値を出す（出力は 1 つ）。入力（ピン）には、別のノードの出力をつなぐ。つないでいないピンは、そのピンの既定の数（または、UV）になる。
//   出力ピン（ドメイン・陰影ごとに決まる。NMaterialNodes::GetOutputPins）にも、ノードをつなぐ。
//   グラフは、見た目の定義だけ。HLSL にする・評価するのは、MaterialCompiler（中間表現を作る）。
//
//   FMaterialGraph Graph;
//   const int32 Base = Graph.AddNode( EMaterialNodeKind::VectorParameter, "Tint", FVector4( 1, 0.5f, 0, 1 ) );
//   const int32 Uv = Graph.AddNode( EMaterialNodeKind::TexCoord );
//   const int32 Mask = Graph.AddNode( EMaterialNodeKind::Mask, "x" );
//   Graph.Connect( Mask, 0, Uv );
//   const int32 Color = Graph.AddNode( EMaterialNodeKind::Multiply );
//   Graph.Connect( Color, 0, Base );
//   Graph.Connect( Color, 1, Mask );
//   Graph.SetOutput( "Color", Color );
//=============================================================================
enum class EMaterialNodeKind : uint8
{
	// 値・パラメーター（名前で、外から変えられる）
	Constant,           // Value（Float4。使う成分は、ValueComponents）
	ScalarParameter,    // Name = パラメーターの名前、Value.x = 既定
	VectorParameter,    // Name、Value = 既定（bColor が true なら、色として扱う）
	ParameterCollectionScalar,   // ParameterCollection と Name（コレクションの中の名前）、Value.x = 既定（コレクションに無いとき）
	ParameterCollectionVector,
	TextureSample,      // Name = 画像のパラメーターの名前、TexturePath = 既定の画像、入力: UV（つながなければ UV）。出力は Float4

	// ドメインの入力
	TexCoord,           // UV（Float2。UI は矩形の左上が (0, 0)、右下が (1, 1)）
	Time,               // 秒（Float）
	Size,               // UI・Particle: 矩形の大きさ（画素）/ PostProcess: 画面の大きさ（Float2）
	HoverAmount,        // UI: ポインターが乗っている（0〜1）
	PressAmount,        // UI: 押されている（0〜1）
	Value01,            // UI: 部品の値（スライダー・プログレスバーの 0〜1）
	Enabled,            // UI: 使える（1）/ 使えない（0）
	VertexColor,        // 頂点（UI は、ブラシに掛ける色。Particle は、粒子の色）（Float4）
	WorldPosition,      // Surface（Float3）
	WorldNormal,        // Surface（Float3）
	CameraVector,       // Surface: 表面からカメラへの向き（長さ 1）（Float3）
	SceneColor,         // PostProcess: シーンの色（Float4）。入力: UV（つながなければ、画面の UV）
	SceneDepth,         // PostProcess: シーンの深度（カメラからの距離に直したもの。Float）。入力: UV
	ParticleLife,       // Particle: 粒子の寿命の位置（生まれたときが 0、消えるときが 1）（Float）
	ParticleAge,        // Particle: 粒子が生まれてからの秒（Float）
	ParticleRandom,     // Particle: 粒子ごとに決まった、0〜1 のばらばらな数（生まれてから消えるまで、変わらない）（Float）

	// 算術（2 つ: A, B）
	Add,
	Subtract,
	Multiply,
	Divide,
	Min,
	Max,
	Power,
	Mod,                // A を B で割った余り（負の数でも 0 以上）
	Step,               // A（しきい値）以上なら 1、未満なら 0（B を A と比べる）
	Dot,                // 内積（Float を出す）
	Distance,           // 距離（Float を出す）

	// 3 つ
	Lerp,               // A と B の間（Alpha）
	Clamp,              // Value を Min と Max に収める
	SmoothStep,         // Min から Max へ、なめらかに 0 → 1（Value）

	// 1 つ
	Abs,
	Saturate,           // 0〜1 に収める
	OneMinus,           // 1 - 値
	Negate,
	Sin,
	Cos,
	Frac,
	Floor,
	Ceil,
	Sqrt,
	Length,             // ベクトルの長さ（Float を出す）
	Normalize,

	// 組み立て・便利なノード
	Append,             // A, B, C, D を並べて、1 つのベクトルにする（C, D は、つながなければ無し。合計 4 成分まで）
	Mask,               // 成分を取り出す（Name = "x"・"xy"・"rgb"・"a" など。1〜4 文字）
	Panner,             // UV を、時間とともにずらす（Value = 1 秒あたりのずれ (x, y)）。入力: UV
	Tiling,             // UV を、繰り返す（Value = (横の倍率, 縦の倍率, 横のずれ, 縦のずれ)）。入力: UV
	Noise,              // 値のノイズ（0〜1）。入力: UV, Scale
	Checker,            // 市松模様（0 か 1）。入力: UV, Count
	Fresnel,            // 縁ほど 1 に近い（Surface）。入力: Exponent

	// Substrate 風の万能モデル（Surface の陰影が Substrate のとき）
	SubstrateSlab,      // 1 つの層。入力: DiffuseColor ほか。出力は Slab
	SubstrateMix,       // 2 つの層を、Mix で混ぜる。入力: A, B（Slab）, Mix

	Count
};

// ピンが求める型。Any は、つながれた値の型のまま（算術）
enum class EMaterialPinExpect : uint8
{
	Any,
	Float,
	Float2,
	Float3,
	Slab
};

struct FMaterialPinInfo
{
	const char* Name = "";
	float32 Default = 0.0f;                    // つながないときの数（Float。必要なら、成分に広げる）
	EMaterialPinExpect Expect = EMaterialPinExpect::Any;
	bool bUvDefault = false;                   // つながないと、UV になる
	bool bOptional = false;                    // つながなくてよい（Append の C・D）
};

struct FMaterialNodeInfo
{
	EMaterialNodeKind Kind = EMaterialNodeKind::Constant;
	const char* Name = "";                     // JSON・エディタの名前
	const char* Category = "";                 // エディタの一覧の分類
	int32 NumPins = 0;
	FMaterialPinInfo Pins[11];
	uint32 DomainMask = 0xF;                   // 使えるドメイン（1 << EMaterialDomain）
};

// 出力ピン（マテリアルの出口）
struct FMaterialOutputPin
{
	const char* Name = "";
	EMaterialValueType Type = EMaterialValueType::Float3;
	float32 Default = 0.0f;                    // つながないときの数（成分に広げる）
	bool bDefaultIsSceneColor = false;         // つながないと、シーンの色になる（PostProcess の Color）
};

namespace NMaterialNodes
{
	const FMaterialNodeInfo& GetInfo( EMaterialNodeKind Kind );
	// 名前から種類（JSON・エディタ）
	bool FindKind( const FString& Name, EMaterialNodeKind& OutKind );
	bool IsAvailable( EMaterialNodeKind Kind, EMaterialDomain Domain );
	// ドメイン・陰影ごとの出力ピン
	TArray<FMaterialOutputPin> GetOutputPins( EMaterialDomain Domain, EMaterialShading Shading );
}

struct FMaterialNode
{
	int32 Id = -1;
	EMaterialNodeKind Kind = EMaterialNodeKind::Constant;
	FString Name;                              // パラメーター・画像の名前 / Mask の成分
	FString ParameterCollection;                        // ParameterCollectionScalar / ParameterCollectionVector: コレクションの名前
	FString TexturePath;                       // TextureSample: 既定の画像のパス
	FVector4 Value = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	int32 ValueComponents = 1;                 // Constant・VectorParameter: 使う成分（1〜4）
	bool bColor = false;                       // VectorParameter: 色（エディタの見せ方）
	FVector2 Position = FVector2( 0.0f, 0.0f ); // エディタでの位置
	TArray<int32> Inputs;                      // ピンごとの、つないだノードの番号（-1 は、つなぎなし）
	TArray<float32> PinDefaults;               // ピンごとの、つないでいないときの数（空なら、ピンの既定）
};

struct FMaterialOutputLink
{
	FString Pin;
	int32 SourceNode = -1;
};

class FMaterialGraph
{
public:
	TArray<FMaterialNode> Nodes;
	TArray<FMaterialOutputLink> Outputs;

	// ノードを足す。番号を返す（以後、変わらない。消しても、番号は詰めない）
	int32 AddNode( EMaterialNodeKind Kind, const FString& Name = FString(), const FVector4& Value = FVector4( 0.0f, 0.0f, 0.0f, 0.0f ), const FVector2& Position = FVector2( 0.0f, 0.0f ) );
	// ノードを消す（そのノードへの線も、外す）。無ければ false
	bool RemoveNode( int32 NodeId );
	// ノードのピン Pin に、ノード SourceId の出力をつなぐ（-1 で外す）。ノード・ピンが無いと false。自分自身には、つなげない
	bool Connect( int32 NodeId, int32 Pin, int32 SourceId );
	// 出力ピンに、ノードをつなぐ（-1 で外す）。つなぎ直すと、置き換える
	void SetOutput( const FString& Pin, int32 SourceId );
	int32 GetOutput( const FString& Pin ) const;

	FMaterialNode* FindNode( int32 NodeId );
	const FMaterialNode* FindNode( int32 NodeId ) const;

private:
	int32 NextId = 1;
	friend class FMaterialDefinition;
	friend struct FMaterialJson;
};

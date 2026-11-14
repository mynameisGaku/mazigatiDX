// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Material/MaterialCompiler.h"
#include "Library/Framework/Material/MaterialSubstrate.h"

//=============================================================================
// NMaterialGraphCompiler
//   NMaterialCompiler::Compile の中身（Material の中だけで使う）。グラフを、出力からたどって、命令にする。
//   - MaterialGraphCompiler.cpp          : 全体の流れ・診断・命令を足す・パラメーターと画像の置き場・ピンの値・出力
//   - MaterialGraphCompilerNodes.cpp     : ノードの種類ごとのコンパイル（CompileKind）
//   - MaterialGraphCompilerSubstrate.cpp : Substrate の層（通り抜ける割合・DefaultLit の層・Slab の出力）
//=============================================================================
namespace NMaterialGraphCompiler
{
	constexpr int32 NumSlabFields = NMaterialSlabField::Num;
	constexpr int32 MaxSlabLeaves = NMaterialSubstrate::MaxSlabs;

	EMaterialValueType SlabFieldType( int32 Field );

	// 1 つのノードの、コンパイルの結果
	struct FResult
	{
		int32 Value = -1;                      // 命令の番号（Slab のときは -1）
		EMaterialValueType Type = EMaterialValueType::Float;
		bool bSlab = false;
		int32 Fields[NumSlabFields];           // 値を混ぜた、1 つの層（簡略版と、出力の値）
		// 陰影を別々に計算する層（フルの評価）。SubstrateMix は、値を混ぜずに、層を並べて、重みを掛ける。SubstrateLayer は、下の層に、上の層を通り抜ける割合を掛ける
		int32 NumLeaves = 0;
		int32 LeafFields[MaxSlabLeaves][NumSlabFields];
		int32 LeafWeights[MaxSlabLeaves];
		int32 LeafThroughputs[MaxSlabLeaves];    // 上に重なった層を通り抜ける割合（Float3。-1 は、1）
		int32 LeafDiffuseScales[MaxSlabLeaves];  // 層の中で散らばる割合（Float3。-1 は、1）
		int32 Pass = -1;                         // この層（の束）の全体を、光が通り抜ける割合（Float3。-1 は、まだ計算していない）
		bool bDone = false;
		bool bVisiting = false;

		FResult()
		{
			for ( int32 Index = 0; Index < NumSlabFields; ++Index )
			{
				Fields[Index] = -1;
			}
			for ( int32 Leaf = 0; Leaf < MaxSlabLeaves; ++Leaf )
			{
				LeafWeights[Leaf] = -1;
				LeafThroughputs[Leaf] = -1;
				LeafDiffuseScales[Leaf] = -1;
				for ( int32 Index = 0; Index < NumSlabFields; ++Index )
				{
					LeafFields[Leaf][Index] = -1;
				}
			}
		}

		// Fields を、重み Weight の、ただ 1 つの層にする
		void SetSingleLeaf( int32 Weight )
		{
			NumLeaves = 1;
			LeafWeights[0] = Weight;
			LeafThroughputs[0] = -1;
			LeafDiffuseScales[0] = -1;
			for ( int32 Index = 0; Index < NumSlabFields; ++Index )
			{
				LeafFields[0][Index] = Fields[Index];
			}
		}
	};

	class FCompiler
	{
	public:
		FCompiler( const FMaterialDefinition& InDefinition, const FMaterialCapabilities& InCapabilities );
		FMaterialProgram Run();

	private:
		// ---- 診断 ----
		void Report( EMaterialSeverity Severity, const FString& Message, int32 NodeId = -1 );
		void Finish();
		FMaterialProgram RunCustom();

		// ---- 命令を足す ----
		int32 Emit( EMaterialOp Op, EMaterialValueType Type, int32 Node, int32 A = -1, int32 B = -1, int32 C = -1, uint8 Sub = 0 );
		EMaterialValueType TypeOf( int32 Value ) const;
		int32 NumOf( int32 Value ) const;
		int32 EmitConst( const FVector4& Value, int32 NumComponents, int32 Node );
		int32 EmitScalar( float32 Value, int32 Node );
		// 失敗したときの代わりの値（0）。コンパイルは、続ける
		int32 Fallback( int32 Node, EMaterialValueType Type = EMaterialValueType::Float );
		int32 Splat( int32 Value, int32 Components, int32 Node );
		int32 MaskTo( int32 Value, int32 Components, int32 Node );
		// 値を、Components 成分に合わせる: 同じならそのまま、Float なら広げる、大きければ切り詰める。小さいベクトルは、エラー
		int32 Coerce( int32 Value, int32 Components, int32 Node, const char* What );
		// 2 つの値の型を揃える（Float は、広がる）。揃えられなければ、エラー
		bool Unify( int32& A, int32& B, int32 Node, const char* What );

		// ---- パラメーター・画像の置き場 ----
		int32 FindOrAddParam( const FString& Name, const FString& ParameterCollection, EMaterialValueType Type, const FVector4& Default, bool bColor, int32 Node );
		int32 AddTextureSlot( const FString& Name, const FString& Path, EMaterialTextureSource Source );
		int32 TextureSlotFor( const FString& Name, const FString& Path, int32 Node );

		// ---- ピンの値 ----
		float32 PinDefault( const FMaterialNode& Node, int32 Pin ) const;
		int32 UvValue( int32 Node );
		// ノード Node のピン Pin の値。つながれていれば、そのノードの値。なければ、既定の数（UV の指定のピンは、UV）。求められる型に合わせる
		int32 PinValue( const FMaterialNode& Node, int32 Pin, bool* bOutConnected = nullptr );

		// ---- ノード ----
		FResult ScalarResult( int32 Value ) const;
		int32 EmitUnary( EMaterialUnary Operation, int32 Input, int32 Node );
		int32 EmitBinary( EMaterialBinary Operation, int32 A, int32 B, int32 Node );
		int32 EmitInput( EMaterialInput Input, EMaterialValueType Type, int32 Node );
		FResult CompileNode( int32 NodeId );
		FResult CompileKind( const FMaterialNode& Node );
		// A と B の間（Alpha は Float。A・B がベクトルなら、広げる）
		int32 EmitLerp( int32 A, int32 B, int32 Alpha, int32 Node );
		// 掛け算。Value が -1（= 1）なら、Factor そのまま
		int32 MultiplyOptional( int32 Value, int32 Factor, int32 Node );
		// 層（の束）を、光が通り抜ける割合（Float3）: 表面で反射しなかった分（1 - フレネル）で、金属は通さない。
		// 見る向きで決まるので、面の法線とカメラの向きから、命令にする（MaterialSubstrate の、環境の映り込みのフレネルと同じ式）
		int32 EnsurePass( FResult& Slab, int32 Node );
		// DefaultLit を、フルのモデルの、層 1 つにする（出力の値から。F0 は、Specular の 8%）
		void AddDefaultLitSlab();
		// Slab のピン（A・B）の層
		FResult SlabInput( const FMaterialNode& Node, int32 Pin );

		// ---- 出力 ----
		void CompileOutput( const FMaterialOutputPin& Pin );
		void CompileSlabOutput( int32 Link );

		const FMaterialDefinition& Definition;
		const FMaterialGraph& Graph;
		FMaterialCapabilities Capabilities;
		FMaterialProgram Program;
		TMap<int32, FResult> Results;
		int32 CachedUv = -1;
	};
}

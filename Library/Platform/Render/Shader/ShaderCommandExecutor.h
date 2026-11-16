// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"

class CGeometryCommandExecutor;

//=============================================================================
// CShaderCommandExecutor
//   シェーダーの命令（UseShader / ShaderConstants / ShaderTexture / ShaderOutputTarget / PrimitiveShader3D / ShaderRect / ShaderQuad3D）を、
//   共通の描画命令から解釈する。置いたシェーダー・定数・画像の状態をこの 1 回の実行（Span）の間だけ持ち、描くときは、IShaderDrawSource へ、
//   シェーダーの番号・定数・画像・頂点を渡す（描画 API を知らない）。深度・表裏・カメラ・ワールド行列は、3D の実行側（CGeometryCommandExecutor）のものを使う。
//   書式・検査は、DxLib の実装（DxLibShader.cpp）と同じ（壊れた命令・使えないスロットは、実行しない）。
//=============================================================================
class CShaderCommandExecutor
{
public:
	enum class EResult : uint8
	{
		NotHandled, // シェーダーの命令ではない。
		Executed,   // 状態を更新した、または描いた（描けなかったときも、命令としては読めた）。
		Rejected,   // 壊れた命令・シェーダーが無い・描けなかった。
	};

	// Geometry は借用。Source は nullptr でもよい（シェーダーを使えない実行先。すべて Rejected になる）。
	CShaderCommandExecutor( CGeometryCommandExecutor& InGeometry, IShaderDrawSource* InSource );

	// 描く前に呼ぶ。描き先（描画先のパス・書く描画先の形式）を整えて、Request に入れる。false なら、描かない。
	void SetPrepare( TCallable<bool( FShaderDrawRequest& )> InPrepare ) { Prepare = MoveTemp( InPrepare ); }
	// ShaderOutputTarget で置いた、出力 Index（1〜7）の描画先の番号（置いていなければ -1）。
	int32 GetOutputTarget( int32 Index ) const { return Index >= 1 && Index < NDrawShader::NumOutputTargets ? OutputTargets[Index] : -1; }
	// 出力の描画先を、1 つでも置いているか。
	bool HasOutputTargets() const;
	// シェーダーの命令か。
	static bool IsShaderCommand( EDrawCommand Type );
	// 1 命令を検査して実行する。bAlphaBlend は、いまの合成（通常のアルファか、上書きか）。
	EResult Execute( const FDrawCommandView& View, bool bAlphaBlend );
	// 組み込みのシェーダー（Builtin / BuiltinPixel）で描く。置いてある利用者のシェーダー・定数・画像は使わない（Request に、State・頂点・定数・画像を入れておく）。bTwoSided なら、両面を描く。描く前の準備（SetPrepare）は行う。
	bool DrawBuiltIn( FShaderDrawRequest& Request, bool bTwoSided );

private:
	struct FStoredConstants
	{
		int32 Stage = 0;
		int32 Slot = 0;
		TArray<uint8> Bytes;
	};
	// 置いてある状態を、描画の値に入れて、描く
	bool Draw( FShaderDrawRequest& Request, bool bAlphaBlend, bool bTwoSided = false );

	CGeometryCommandExecutor& Geometry;
	IShaderDrawSource* Source;
	TCallable<bool( FShaderDrawRequest& )> Prepare;   // 描く前の準備（任意）。
	int32 OutputTargets[NDrawShader::NumOutputTargets] = { -1, -1, -1, -1, -1, -1, -1, -1 };   // 出力ごとの描画先の番号。
	int32 VertexShader = -1;                 // 置いた頂点シェーダーの番号。
	int32 PixelShader = -1;                  // 置いたピクセルシェーダーの番号。
	TArray<FStoredConstants> Constants;      // 置いた定数（段と番号ごと）。
	int32 Textures[NDrawShader::NumTextureStages] = { -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 };   // 段ごとの画像の番号。
};

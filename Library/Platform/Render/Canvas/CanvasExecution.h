// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IFontDrawSource.h"
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderTargetDrawSource.h"
#include "Library/Core/Interfaces/Render/IShaderDrawSource.h"
#include "Library/Core/Interfaces/Render/IStaticModelSource.h"
#include "Library/Core/Interfaces/Render/ITextureDrawSource.h"
#include "Library/Core/Render/DrawSpan.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Platform/Render/Shader/MaterialEffectCommandExecutor.h"
#include "Library/Platform/Render/Shader/ShaderCommandExecutor.h"
#include "Library/Core/Types/Numeric.h"

// 2D の命令の座標を、実行の前に確かめる（不正な値を図形生成器へ渡さない）。
namespace NCanvasValidation
{
	// 不正な座標で図形生成器へ過大な値を渡さない。
	inline bool Valid( float32 Value )
	{
		return NMath::IsFinite( Value ) && NMath::Abs( Value ) <= 1000000.0f;
	}
	// 頂点列を読む前に全座標を検査する。
	inline bool ValidPoints( const float32* Points, int32 Count )
	{
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			if ( !Valid( Points[Index] ) )
			{
				return false;
			}
		}
		return true;
	}
	// 円や角の丸みを、何分割で描くか（半径に応じた既定。指定があれば、それを使う）。
	inline int32 GetSegments( float32 Radius, int32 Requested )
	{
		return Requested > 0 ? NMath::Clamp( Requested, 3, 512 ) : NMath::Clamp( static_cast<int32>( NMath::Ceil( NMath::Sqrt( NMath::Max( Radius, 0.0f ) ) * 6 ) ), 12, 512 );
	}
}

// 2D の描画に掛かる変換（Transform2D）。点 (x, y) を (x * XX + y * YX + TX, x * XY + y * YY + TY) へ移す。
struct FCanvasTransform
{
	float32 XX = 1, XY = 0, YX = 0, YY = 1, TX = 0, TY = 0;
	bool bIdentity = true; // 変換なし。矩形や円を、そのままの形で描ける。

	void Apply( float32& X, float32& Y ) const
	{
		const float32 OldX = X; // 両方の式で、変換前の値を使う。
		X = OldX * XX + Y * YX + TX;
		Y = OldX * XY + Y * YY + TY;
	}
	// 線の太さに掛ける倍率（面積の拡大率の平方根）。
	float32 GetScale() const
	{
		return bIdentity ? 1.0f : NMath::Sqrt( NMath::Abs( XX * YY - XY * YX ) );
	}
};

// マスク（PushMask）の 1 つ分。マスクにできない画像（描画先でない）のときは、View.Binding が 0 で、マスクなしと同じ。
struct FCanvasMaskState
{
	FTextureDrawView View; // マスクの画像。
	int32 Channel = 0; // 値にする成分（EDrawMaskChannel と同じ並び）。
	bool bInvert = false; // 値が小さいところほど濃く描くか。
};

// 描画先（レンダーターゲット）を替える前の状態。戻すときに使う。
struct FCanvasTargetScope
{
	FRenderTargetDrawView Target; // この段の描画先（主画面なら無効）。
	FCanvasTransform Transform; // 替える前の2Dの変換。
	int32 ClipDepth = 0; // 替える前のクリップの段数。
	bool bShadow = false; // 影の絵への描画か（終わるとき、影を落とす状態を戻す）。
	TArray<FCanvasMaskState> MaskStack; // 替える前のマスクの並び（替えた先は、マスクなしから始まる）。
};

// このSpanだけの、2Dの描画の状態（明るさ・不透明度・合成・変換・マスクなど）。
struct FCanvasDrawState
{
	bool bLinearFilter = false; // 画像の既定は最近傍。
	bool bExplicitFilter = false; // 明示した補間を使えない実行先では画像を省略する。
	bool bAlphaBlend = false; // 3Dへ渡す合成方式。
	FDrawRgb Bright; // このSpanだけの乗算色。
	int32 Alpha = 255; // このSpanだけの不透明度。
	int32 AddColorValue[3] = { 0, 0, 0 }; // このSpanだけの、描く色へ足す値（赤・緑・青）。平らな図形だけに効く。
	bool bAlphaTest = false; // アルファテストをするか。
	EDrawCompare AlphaTestMode = EDrawCompare::Always; // アルファテストの比べ方。
	int32 AlphaTestReference = 0; // アルファテストの基準値。
	int32 ClipDepth = 0; // 終了時に必ず戻すクリップ段数。
	FCanvasTransform Transform; // このSpanだけの2Dの変換。
	TArray<FCanvasMaskState> MaskStack; // 積んだマスク。最後が、いまのマスク（外側のマスクに重ねず、替わる）。

	// 色と現在の描画状態を合成する。
	FColor32 MakeColor( FDrawRgb Value ) const;
	// 平らな図形の色（描く色に、AddColor の値を足す。DxLibと同じく、明るさの乗算のあとに足す）。
	FColor32 MakeShapeColor( FDrawRgb Value ) const;
	// アルファテストを、あるアルファが通るか。
	bool AlphaTestPasses( int32 Value ) const;
	// 画素ごとにアルファが違う図（画像・文字）が、1画素でも通りうるか（0から、いまの不透明度まで）。
	bool AlphaTestMayPass() const;
};

// 描画先（画像・影の絵・複数の出力）の積みと、描かない範囲の深さ。
struct FCanvasTargetState
{
	TArray<FCanvasTargetScope> TargetStack; // 積んだ描画先。最後が、いまの描画先。
	FRenderTargetDrawView CurrentTarget; // いまの描画先（主画面なら無効）。
	bool bMrtActive = false; // 複数の出力（MRT）のパスの中か。ShaderOutputTarget で出力を置いて、シェーダーで描くときに、パスを開き直す。
	TArray<uint64> MrtSignature; // いまのMRTのパスの、色の描画先の並び。
	int32 SkipTargetDepth = 0; // 未対応の描画先（レンダーターゲット）へ描く範囲の深さ。この中は描かない。
	int32 SkipShadowDepth = 0; // 描けないシャドウマップへ描く範囲の深さ。この中は描かない（画面へ描いてしまわないように）。
	bool bShadowPassActive = false; // 影の絵へ描いているか（BeginShadowMap〜EndShadowMap）。
};

// 2Dを描けない描画先（浮動小数点の描画先・複数の出力のパス）で使う、何も描かないキャンバス。
class CCanvasNullCanvas2D : public IRenderCanvas2D
{
public:
	void Box( float32, float32, float32, float32, FColor32, bool, float32 ) override {}
	void Line( float32, float32, float32, float32, FColor32, float32 ) override {}
	void Circle( float32, float32, float32, FColor32, bool, float32, int32 ) override {}
	void Triangle( const float32 ( & )[6], FColor32, bool, float32 ) override {}
	void Quad( const float32 ( & )[8], FColor32, bool, float32 ) override {}
	void Text( float32, float32, FColor32, const char*, const char* ) override {}
	void Image( const FTextureDrawView&, const float32 ( & )[8], const float32 ( & )[4], FColor32 ) override {}
	void PushClip( int32, int32, int32, int32 ) override {}
	void PopClip() override {}
	void SetAntiAlias( bool ) override {}
};

// 1 回の Execute が持つ、借りた実行先と、描画の状態。役割ごとのクラス（描画先の積み・図形・画像・文字・画像の処理・状態の命令・3D の命令）が、これを共有する。
struct FCanvasExecution
{
	// 借りた実行先は、実行が終わるまで所有側が生存させる。Span は、この実行の命令列。
	FCanvasExecution( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels, IFontDrawSource* InFonts, IRenderTargetDrawSource* InTargets, IShaderDrawSource* InShaders, const FDrawSpan& InSpan, IRenderCanvas2D* InCanvas );
	// 対応していない命令は、その命令だけを飛ばして、続きを描く（警告を出す）。
	static void Unsupported( const FDrawCommandView& View, const char* Reason );
	// 登録済みの画像を借りる。無い・大きさ0なら false。
	bool GetImage( int32 Handle, FTextureDrawView& Image );
	// いまのマスクを、キャンバスへ反映する（無ければ、マスクなしに）。実行先が使えなければ、マスクなしで描く。
	void ApplyMask();

	IRenderSurface& Surface; // 借用する描画先。
	ITextureDrawSource* Textures; // 借用する登録画像。nullptrなら画像を描かない。
	IGeometryBufferSource* Buffers; // 同期の命令解釈にだけ借用する形状。
	IStaticModelSource* Models; // 同期実行中だけ借りる静的モデル。
	IFontDrawSource* Fonts; // 借用するフォントの表。nullptrなら既定の文字で描く。
	IRenderTargetDrawSource* Targets; // 借用する描画先の表。nullptrなら描画先へ描けない。
	IShaderDrawSource* Shaders; // 借用するシェーダーの実行先。nullptrならシェーダーの命令は飛ばす。
	const FDrawSpan& Span; // この実行の命令列。
	IRenderCanvas2D* Canvas; // いまの 2D の実行先（描画先を替えると、取り直す）。
	CCanvasNullCanvas2D NullCanvas; // 2Dを描けない描画先では、これを使う（何も描かない）。
	CGeometryCommandExecutor Geometry; // このSpanだけの3D状態。
	CShaderCommandExecutor ShaderExecutor; // このSpanだけのシェーダーの状態。
	CMaterialEffectCommandExecutor MaterialExecutor; // 材質付きの3D描画（組み込みのシェーダーで描く）。
	FCanvasDrawState State; // 2Dの描画の状態。
	FCanvasTargetState TargetState; // 描画先の積み。
	bool bStopped = false; // 壊れた命令以降を描かない印。
};

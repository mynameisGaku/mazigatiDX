// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/UI/Core/UITypes.h"
#include "Library/Math/Color.h"

//=============================================================================
// FUIBrush
//   部品の「見た目」の指定。部品が何をどう描くかは、ブラシで差し替えられる（色・画像・9 分割・好きな描き方・それらの重ね）。
//   ブラシは、FDrawContext（コマンドリスト）へ、既存の描画命令を記録するだけ。DxLib も、実行先も知らない。
//
//   FUIBrush::MakeSolid( FColor32( 40, 44, 56 ), 8.0f )                                     // 角丸の面
//   FUIBrush::MakeImage( Texture, EUIImageMode::Fill )                                       // 画像を、覆うように
//   FUIBrush::MakeNineSlice( Texture, FUIMargin( 12.0f ) )                                   // 9 分割（角は拡大せず、辺と中央を伸ばす）
//   FUIBrush::MakeCallback( []( FDrawContext& CommandList, const FUIRect& Rect, float32 Opacity ) { ... } )   // 自由に描く
//   FUIBrush::MakeLayered( { Back, Frame, Icon } )                                           // 重ねる（先のものが奥）
//   FUIBrush::MakeMaterial( GlowMaterial )                                                   // マテリアル（UI ドメイン。シェーダーで、うにょうにょ・ビカビカ）
//=============================================================================

// 画像の貼り方
enum class EUIImageMode : uint8
{
	Stretch,    // 矩形いっぱいに引き伸ばす（縦横比は保たない）
	Fit,        // 縦横比を保ち、矩形に収まる最大の大きさで、中央に置く（余白ができる）
	Fill,       // 縦横比を保ち、矩形を覆う大きさで、はみ出す部分を切り落とす
	Tile,       // 元の大きさで、繰り返し並べる
	NineSlice   // 9 分割。四隅は元の大きさ、四辺は一方向に、中央は両方向に伸ばす
};

enum class EUIBrushKind : uint8
{
	None,      // 何も描かない
	Solid,     // 単色の面（角丸・枠つき）
	Image,     // 画像
	Callback,  // 呼び出し側が、自由に記録する
	Layered,   // 複数のブラシを重ねる
	Material   // マテリアル（UI ドメインの CMaterial を、矩形に描く）
};

struct FUIBrush
{
	// 描く関数（Callback）: コマンドリスト・矩形・不透明度（0〜1。親から掛け合わせたもの）
	using FPaintFunction = TCallable<void( FDrawContext&, const FUIRect&, float32 )>;

	EUIBrushKind Kind = EUIBrushKind::None;

	// Solid: 面の色 / Image: 画像に掛ける色（乗算。白なら元の色）。アルファは不透明度に掛かる
	FColor32 Color = FColor32( 255, 255, 255, 255 );
	// Solid: 角の丸め（半径。画素。矩形の短辺の半分までに収まる）。0 なら四角
	float32 CornerRadius = 0.0f;
	// Solid: 枠（太さが 0 より大きく、アルファが 0 でないときだけ描く。面の上に描く）
	FColor32 OutlineColor = FColor32( 255, 255, 255, 0 );
	float32 OutlineThickness = 0.0f;

	// Image: 画像と、その大きさ（画素。9 分割・縦横比・タイルの計算に使う）、貼り方、9 分割の余白（元の画像の画素）
	TSharedPtr<IImage> Image;
	FVector2 ImageSize = FVector2( 0.0f, 0.0f );
	EUIImageMode ImageMode = EUIImageMode::Stretch;
	FUIMargin NineSliceMargin;

	FPaintFunction Paint;                       // Callback
	TArray<TSharedPtr<FUIBrush>> Layers;        // Layered（先のものが奥）
	// Material: 描くマテリアル。色（Color）は、マテリアルの出力に掛ける（白なら、そのまま。アルファは、不透明度に掛かる）。
	//   マテリアルの入力（HoverAmount・PressAmount・Value01・Enabled）は、このブラシを描く部品の状態（CUIWidget::PaintRecursive が決める）。時間は、CMaterialSubsystem の時間
	TSharedPtr<CMaterial> Material;

	// 何も描かないブラシか（None、色のアルファが 0 の面など、見えないものも含む）
	bool IsEmpty() const;

	static FUIBrush MakeNone();
	static FUIBrush MakeSolid( const FColor32& InColor, float32 InCornerRadius = 0.0f );
	static FUIBrush MakeSolidWithOutline( const FColor32& InColor, float32 InCornerRadius, const FColor32& InOutlineColor, float32 InOutlineThickness );
	// 画像。ImageSize は、画像の大きさ（画素）。CTexture なら GetWidth() / GetHeight()
	static FUIBrush MakeImage( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, EUIImageMode InMode = EUIImageMode::Stretch, const FColor32& InTint = FColor32( 255, 255, 255, 255 ) );
	// 9 分割の画像。余白は、元の画像の画素（左・上・右・下）
	static FUIBrush MakeNineSlice( const TSharedPtr<IImage>& InImage, const FVector2& InImageSize, const FUIMargin& InMargin, const FColor32& InTint = FColor32( 255, 255, 255, 255 ) );
	static FUIBrush MakeCallback( const FPaintFunction& InPaint );
	static FUIBrush MakeLayered( const TArray<FUIBrush>& InLayers );
	// UI ドメインの（または Particle の）マテリアル。それ以外のドメインのマテリアルは、描かれない
	static FUIBrush MakeMaterial( const TSharedPtr<CMaterial>& InMaterial, const FColor32& InTint = FColor32( 255, 255, 255, 255 ) );
};

// マテリアルのブラシへ渡す、部品の状態
struct FUIMaterialState
{
	float32 Hover = 0.0f;
	float32 Press = 0.0f;
	float32 Value = 0.0f;
	float32 Enabled = 1.0f;
};

// この範囲（描くとき）の間、マテリアルのブラシに渡す部品の状態を決める（スレッドごと。範囲を出ると、前の状態へ戻る）。CUIWidget が、自分の背景と OnPaint を描くときに使う
class FUIMaterialStateScope
{
public:
	explicit FUIMaterialStateScope( const FUIMaterialState& State );
	~FUIMaterialStateScope();
	FUIMaterialStateScope( const FUIMaterialStateScope& ) = delete;
	FUIMaterialStateScope& operator=( const FUIMaterialStateScope& ) = delete;

private:
	FUIMaterialState Previous;
};

//=============================================================================
// NUIBrush
//=============================================================================
namespace NUIBrush
{
	// ブラシを、矩形 Rect に描く命令を、CommandList へ記録する（Opacity は 0〜1。範囲の外は収める）。
	//   - 描く前に、ブレンド（アルファ）と、画像に掛ける色（Tint）を、この関数が設定する。描いたあとの状態は、呼び出し側が戻す
	//     （部品の木をまとめて描く CUIWidget::Paint は、最後に、ブレンドと色を元に戻す）。
	//   - 矩形の大きさが 0 以下・不透明度が 0・何も描かないブラシなら、何も記録しない。
	void Draw( FDrawContext& CommandList, const FUIBrush& Brush, const FUIRect& Rect, float32 Opacity = 1.0f );

	// 色のアルファと不透明度を掛けた、0〜255 の値
	int32 ToAlpha( const FColor32& Color, float32 Opacity );
}

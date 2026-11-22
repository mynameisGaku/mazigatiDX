// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Framework/Render/Draw/DrawContext2DImages.h"

// 描画先（レンダーターゲット）の切り替え・画像の処理（フィルター・混ぜる）・マスク
class FDrawContextTargets : public FDrawContext2DImages
{
public:
	explicit FDrawContextTargets( FDrawBuffer& InBuffer ) : FDrawContext2DImages( InBuffer ) {}

	//-------------------------------------------------------------------------
	// 描画先（レンダーターゲット）
	//-------------------------------------------------------------------------
	// 描画先を、描き込める画像（CRenderTarget）に替える（SetDrawScreen）。PopTarget か FTargetScope で、1 つ前の描画先へ戻す（入れ子にできる）。
	// 替えた先は、クリップ範囲なし・2D の変換なしから始まり、戻すと、替える前の範囲・変換に戻る。ブレンドなどの状態と、3D のカメラは、替えても変わらない。
	// 画像が無効（nullptr・無効なハンドル）なら、何も記録せず false を返す（対の PopTarget も呼ばないこと）。
	// 描画先にできない画像（ふつうの画像）を渡すと、実行のときに、対の PopTarget まで、描く命令を飛ばす。
	// そのオブジェクトの分が終わると、Pop が足りなくても、始める前の描画先に戻る
	bool PushTarget( FImageHandle Target );

	template<typename TImage>
	FORCEINLINE bool PushTarget( const TImage& Target )
	{
		return PushTarget( ToHandle( Target ) );
	}

	// 1 つ前の描画先へ戻す。PushTarget の数より多く呼んでも、何も起きない
	void PopTarget();

	// いまの描画先の全体を、色（アルファも）で塗りつぶす（ClearDrawScreen）。クリップ範囲・ブレンドには関係なく、全体を、その色そのままにする。
	// 既定は、透明な黒（アルファを持つ描画先を、透明にする）。アルファを持たない描画先は、不透明な色になる
	void Clear( const FColor32& Color = FColor32( 0, 0, 0, 0 ) );

	// いまの描画先の矩形 (X0, Y0)-(X1, Y1)（右と下の端は含まない）を、画像 Dest へ写す（GetDrawScreenGraph）。
	// Dest は、矩形と同じ大きさ（幅・高さが同じ）の、描き込める画像（CRenderTarget）にする。大きさが違う・矩形が描画先の外にはみ出す場合は、実行のときに、何もしない。
	// 幅・高さが 0 以下、絶対値が 1e7 を超える座標、無効な画像は、何も記録しない
	void CopyScreen( int32 X0, int32 Y0, int32 X1, int32 Y1, FImageHandle Dest );

	template<typename TImage>
	FORCEINLINE void CopyScreen( int32 X0, int32 Y0, int32 X1, int32 Y1, const TImage& Dest )
	{
		CopyScreen( X0, Y0, X1, Y1, ToHandle( Dest ) );
	}

	//-------------------------------------------------------------------------
	// 画像の処理（フィルター・混ぜる）
	//   どれも、記録した順に実行される（描いたあとの画像に掛けたり、掛けた画像を、そのあと描いたりできる）。
	//   描画先・クリップ・ブレンド・明るさ・2D の変換には、影響されず、変えもしない（いまの描画先とも関係なく、渡した画像だけを扱う）。
	//   画像は、実行のときまで、持ち主が手放さないこと。実行のとき、DxLib が受け付けない画像（描画先にできない画像など）には、何もしない。
	//   画像・設定が無効（nullptr・無効なハンドル・GradientMap のマップが無効・矩形の大きさが 0 以下）なら、何も記録せず false を返す。
	//   存在しないハンドルや、画像の外の矩形を、そのまま記録したときも、実行のときは、何も起きない（落ちない）
	//-------------------------------------------------------------------------
	// 画像 Target に、フィルターを、その場で掛ける（GraphFilter）。Target は、描き込める画像（CRenderTarget）か、画素から作った画像（CDynamicTexture）。
	// 画像を切り出したもの（SplitTexture など）にも使える（元の画像の、その範囲だけが変わる）。変換先の大きさが決まる種類（DownScale・BicubicScale・Lanczos3Scale）は、次の ApplyFilter を使う
	bool ApplyFilter( FImageHandle Target, const FGraphFilter& Filter );

	template<typename TImage>
	FORCEINLINE bool ApplyFilter( const TImage& Target, const FGraphFilter& Filter )
	{
		return ApplyFilter( ToHandle( Target ), Filter );
	}

	// Source にフィルターを掛けた結果を、画像 Dest へ出す（GraphFilterBlt）。Source は変わらない。Dest は、描き込める画像（CRenderTarget）でも、ふつうの画像（CDynamicTexture）でもよい。
	// 大きさが違うとき、拡大・縮小はされず、左上から重なる分だけが出る（DownScale・BicubicScale・Lanczos3Scale は、決めた大きさに変えて、左上へ出す）。
	// bAlphaBlend が true なら、結果を、Dest の中身にアルファブレンドで重ねる（false なら、置き換える）。重ねるとき、アルファを持つ Dest のアルファは、元の値と二重に掛かる（描画先への描画と同じ）
	bool ApplyFilter( FImageHandle Source, FImageHandle Dest, const FGraphFilter& Filter, bool bAlphaBlend = false );

	template<typename TSource, typename TDest>
	FORCEINLINE bool ApplyFilter( const TSource& Source, const TDest& Dest, const FGraphFilter& Filter, bool bAlphaBlend = false )
	{
		return ApplyFilter( ToHandle( Source ), ToHandle( Dest ), Filter, bAlphaBlend );
	}

	// Source の矩形（SrcX, SrcY から SrcW x SrcH。右と下の端は含まない）にフィルターを掛けた結果を、Dest の (DestX, DestY) から出す（GraphFilterRectBlt）。
	// 範囲が画像の外へはみ出す分は、DxLib が受け付けず、何もしないことがある（画像の中に収める）
	bool ApplyFilterRect( FImageHandle Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, FImageHandle Dest, int32 DestX, int32 DestY, const FGraphFilter& Filter, bool bAlphaBlend = false );

	template<typename TSource, typename TDest>
	FORCEINLINE bool ApplyFilterRect( const TSource& Source, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, const TDest& Dest, int32 DestX, int32 DestY, const FGraphFilter& Filter, bool bAlphaBlend = false )
	{
		return ApplyFilterRect( ToHandle( Source ), SrcX, SrcY, SrcW, SrcH, ToHandle( Dest ), DestX, DestY, Filter, bAlphaBlend );
	}

	// 2 枚の画像 Base と Overlay を混ぜて、画像 Dest へ出す（GraphBlendBlt。Dest が Base と同じなら、その場で、GraphBlend）。Base・Overlay は変わらない（Dest が Base のときは、Base が変わる）。
	// Dest は、画像（CRenderTarget・CDynamicTexture など）。大きさは、Base と同じにする（違うと、DxLib の結果になる）。混ぜ方は FGraphBlend
	bool BlendImages( FImageHandle Base, FImageHandle Overlay, FImageHandle Dest, const FGraphBlend& Mode );

	template<typename TBase, typename TOverlay, typename TDest>
	FORCEINLINE bool BlendImages( const TBase& Base, const TOverlay& Overlay, const TDest& Dest, const FGraphBlend& Mode )
	{
		return BlendImages( ToHandle( Base ), ToHandle( Overlay ), ToHandle( Dest ), Mode );
	}

	//-------------------------------------------------------------------------
	// マスク
	//-------------------------------------------------------------------------
	// 以降の描画を、マスクの画像の値で薄める（SetMaskScreenGraph）。PopMask か FMaskScope で、1 つ前のマスクへ戻す（入れ子にできるが、内側のマスクが、外側のマスクに替わる。重ねはしない）。
	// Mask は、アルファを持つ描画先（CRenderTarget。bAlpha が true）。ふつうの画像は使えない（実行のときに、マスクなしで描かれる）。マスクに描く図形・画像・文字は、ふつうの描画（PushTarget）で作る。
	// Channel は、マスクの値にする成分（アルファ・赤・緑・青）。bInvert が false なら、値が大きいところほど濃く描かれる（0 で描かれず、255 でそのまま。ふつうの「白いところが見える」）。true なら逆。
	// 値は段階的に効く（0 と 255 のあいだは、ブレンドの強さになる）。マスクの画像の外（描画先のほうが大きい）は、値が 255 として扱われる。
	// 替えた先の描画先（PushTarget）は、マスクなしから始まり、戻すと、替える前のマスクに戻る。そのオブジェクトの分が終わると、マスクは解除される。
	// マスクは、画面（描画先）の左上を合わせて使う。画像が無効なら、何も記録せず false を返す（対の PopMask も呼ばないこと）
	bool PushMask( FImageHandle Mask, EDrawMaskChannel Channel = EDrawMaskChannel::Alpha, bool bInvert = false );

	template<typename TImage>
	FORCEINLINE bool PushMask( const TImage& Mask, EDrawMaskChannel Channel = EDrawMaskChannel::Alpha, bool bInvert = false )
	{
		return PushMask( ToHandle( Mask ), Channel, bInvert );
	}

	// 1 つ前のマスクへ戻す（無ければ、マスクなしに）。PushMask の数より多く呼んでも、何も起きない
	void PopMask();
};

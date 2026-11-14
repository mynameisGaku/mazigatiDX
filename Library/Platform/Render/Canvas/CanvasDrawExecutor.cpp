// SPDX-License-Identifier: MIT
#include "CanvasDrawExecutor.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Platform/System/BackendSupport.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 不正な座標で図形生成器へ過大な値を渡さない。
	bool Valid( float32 Value )
	{
		return NMath::IsFinite( Value ) && NMath::Abs( Value ) <= 1000000.0f;
	}
	// 頂点列を読む前に全座標を検査する。
	bool ValidPoints( const float32* Points, int32 Count )
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

	// 円や角の丸みを、何分割で描くか（半径に応じた既定。指定があれば、それを使う）。
	int32 GetSegments( float32 Radius, int32 Requested )
	{
		return Requested > 0 ? NMath::Clamp( Requested, 3, 512 ) : NMath::Clamp( static_cast<int32>( NMath::Ceil( NMath::Sqrt( NMath::Max( Radius, 0.0f ) ) * 6 ) ), 12, 512 );
	}

	// 未対応の命令の名前（警告に出す）。
	FString CommandName( EDrawCommand Type )
	{
		return FString::Printf( "Draw command %u", static_cast<unsigned>( Type ) );
	}
}

CCanvasDrawExecutor::CCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels )
	: Surface( InSurface ), Models( InModels ), Buffers( InBuffers ), Textures( InTextures )
{
}

void CCanvasDrawExecutor::Execute( const FDrawSpan& Span )
{
	IRenderCanvas2D* Canvas = Surface.GetCanvas2D(); // フレーム内だけ借りる。
	if ( !Canvas || !Span.Words || Span.NumWords <= 0 )
	{
		return;
	}
	CGeometryCommandExecutor Geometry( Surface.GetGeometry3D(), Textures, Buffers, Models ); // このSpanだけの3D状態。
	bool bLinearFilter = false; // 画像の既定は最近傍。
	bool bExplicitFilter = false; // 明示した補間を使えない実行先では画像を省略する。
	bool bAlphaBlend = false; // 3Dへ渡す合成方式。
	FDrawRgb Bright; // このSpanだけの乗算色。
	int32 Alpha = 255; // このSpanだけの不透明度。
	int32 ClipDepth = 0; // 終了時に必ず戻すクリップ段数。
	int32 SkipTargetDepth = 0; // 未対応の描画先（レンダーターゲット）へ描く範囲の深さ。この中は描かない。
	int32 SkipShadowDepth = 0; // シャドウマップへ描く範囲の深さ。この中は描かない（画面へ描いてしまわないように）。
	bool bStopped = false; // 壊れた命令以降を描かない印。
	FCanvasTransform Transform; // このSpanだけの2Dの変換。
	const auto Color = [&]( FDrawRgb Value ) // 色と現在の描画状態を合成する。
	{
		return FColor32( static_cast<uint8>( Value.R * Bright.R / 255 ), static_cast<uint8>( Value.G * Bright.G / 255 ),
			static_cast<uint8>( Value.B * Bright.B / 255 ), static_cast<uint8>( Alpha ) );
	};
	const auto Unsupported = [&]( const FDrawCommandView& View, const char* Reason ) // 対応していない命令は、その命令だけを飛ばして、続きを描く。
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", *CommandName( View.Type ), Reason );
	};
	// 凸な点列（XYの順）を、変換してから描く。
	const auto DrawPolygon = [&]( const float32* Points, int32 Count, FColor32 Value, bool bFill, float32 Thickness )
	{
		if ( Count < 3 || Count > 512 || !ValidPoints( Points, Count * 2 ) || !Valid( Thickness ) || Thickness <= 0 )
		{
			return;
		}
		TArray<float32> Moved; // 変換後の点列。
		Moved.SetNum( Count * 2 );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			float32 X = Points[Index * 2], Y = Points[Index * 2 + 1]; // 変換する点。
			Transform.Apply( X, Y );
			Moved[Index * 2] = X;
			Moved[Index * 2 + 1] = Y;
		}
		if ( !ValidPoints( Moved.GetData(), Count * 2 ) )
		{
			return;
		}
		const float32 Width = Thickness * Transform.GetScale(); // 変換の拡大を、線の太さにも掛ける。
		if ( Count == 3 )
		{
			const float32 Three[6] = { Moved[0], Moved[1], Moved[2], Moved[3], Moved[4], Moved[5] };
			Canvas->Triangle( Three, Value, bFill, Width );
		}
		else if ( Count == 4 )
		{
			const float32 Four[8] = { Moved[0], Moved[1], Moved[2], Moved[3], Moved[4], Moved[5], Moved[6], Moved[7] };
			Canvas->Quad( Four, Value, bFill, Width );
		}
		else
		{
			Canvas->Polygon( Moved.GetData(), Count, Value, bFill, Width );
		}
	};
	// 楕円（半径が同じなら円）を、点列にして描く。
	const auto DrawEllipse = [&]( float32 X, float32 Y, float32 Rx, float32 Ry, FColor32 Value, bool bFill, float32 Thickness, int32 Segments )
	{
		if ( !Valid( X ) || !Valid( Y ) || !Valid( Rx ) || !Valid( Ry ) || Rx <= 0 || Ry <= 0 )
		{
			return;
		}
		const int32 Count = GetSegments( NMath::Max( Rx, Ry ), Segments ); // 大きいほうの半径で分割を決める。
		TArray<float32> Points; // 輪郭は閉じる点を重複させない。
		Points.SetNum( Count * 2 );
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			const float32 Angle = Index * 6.28318530718f / Count; // 一周を等分する角度。
			Points[Index * 2] = X + Rx * NMath::Cos( Angle );
			Points[Index * 2 + 1] = Y + Ry * NMath::Sin( Angle );
		}
		DrawPolygon( Points.GetData(), Count, Value, bFill, Thickness );
	};
	// 登録済みの画像を借りる。無い・大きさ0なら false。
	const auto GetImage = [&]( int32 Handle, FTextureDrawView& Image )
	{
		return Textures && Textures->GetTextureDrawView( Handle, Image ) && Image.Width > 0 && Image.Height > 0;
	};
	// 画像の中の矩形を、UV にする。画像の外へ出る矩形は false。
	const auto SourceUV = [&]( const FTextureDrawView& Image, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, float32 ( &UV )[4] )
	{
		if ( SrcX < 0 || SrcY < 0 || SrcW <= 0 || SrcH <= 0 || static_cast<int64>( SrcX ) + SrcW > Image.Width || static_cast<int64>( SrcY ) + SrcH > Image.Height )
		{
			return false;
		}
		UV[0] = static_cast<float32>( SrcX ) / Image.Width;
		UV[1] = static_cast<float32>( SrcY ) / Image.Height;
		UV[2] = static_cast<float32>( SrcX + SrcW ) / Image.Width;
		UV[3] = static_cast<float32>( SrcY + SrcH ) / Image.Height;
		return true;
	};
	// 反転の指定を、UV の入れ替えにする（位置はそのまま）。
	const auto FlipUV = [&]( const FDrawCommandView& View, float32 ( &UV )[4] )
	{
		if ( View.Has( FDrawFlag::FlipX ) )
		{
			const float32 Old = UV[0]; // 左右を交換する。
			UV[0] = UV[2];
			UV[2] = Old;
		}
		if ( View.Has( FDrawFlag::FlipY ) )
		{
			const float32 Old = UV[1]; // 上下を交換する。
			UV[1] = UV[3];
			UV[3] = Old;
		}
	};
	// 画像を、4点（左上・右上・右下・左下）へ、変換してから貼る。
	const auto DrawImage = [&]( const FTextureDrawView& Image, const float32 ( &Points )[8], const float32 ( &UV )[4] )
	{
		float32 Moved[8]; // 変換後の4点。
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			float32 X = Points[Index * 2], Y = Points[Index * 2 + 1]; // 変換する点。
			Transform.Apply( X, Y );
			Moved[Index * 2] = X;
			Moved[Index * 2 + 1] = Y;
		}
		if ( !ValidPoints( Points, 8 ) || !ValidPoints( Moved, 8 ) )
		{
			return;
		}
		if ( !Canvas->SetTextureFilter( bLinearFilter ) && bExplicitFilter )
		{
			return;
		}
		Canvas->Image( Image, Moved, UV, Color( FDrawRgb() ) );
	};
	// 画像の中の矩形を、基準点まわりに拡大・回転して描く（Sprite / SpriteRect / SpritePivot の共通）。
	// 反転は、基準点を軸にする（反転しても、基準点は (X, Y) のまま）。
	const auto DrawSprite = [&]( const FDrawCommandView& View, const FTextureDrawView& Image, float32 X, float32 Y, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 Angle,
								 int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH )
	{
		float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
		if ( !SourceUV( Image, SrcX, SrcY, SrcW, SrcH, UV ) || !Valid( Angle ) || !Valid( ScaleX ) || !Valid( ScaleY ) || !Valid( PivotX ) || !Valid( PivotY ) )
		{
			return;
		}
		const float32 SignX = View.Has( FDrawFlag::FlipX ) ? -1.0f : 1.0f, SignY = View.Has( FDrawFlag::FlipY ) ? -1.0f : 1.0f; // 基準点を軸にした反転。
		const float32 Left = ( 0 - PivotX ) * ScaleX * SignX, Right = ( SrcW - PivotX ) * ScaleX * SignX; // 基準点からの、左右の端。
		const float32 Top = ( 0 - PivotY ) * ScaleY * SignY, Bottom = ( SrcH - PivotY ) * ScaleY * SignY; // 基準点からの、上下の端。
		const float32 Cos = NMath::Cos( Angle ), Sin = NMath::Sin( Angle ); // 画面（Y が下）での回転。正は時計回り。
		const float32 Local[8] = { Left, Top, Right, Top, Right, Bottom, Left, Bottom }; // 画像の左上・右上・右下・左下。
		float32 Points[8]; // 回転して、(X, Y) へ動かした4点。
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			Points[Index * 2] = X + Local[Index * 2] * Cos - Local[Index * 2 + 1] * Sin;
			Points[Index * 2 + 1] = Y + Local[Index * 2] * Sin + Local[Index * 2 + 1] * Cos;
		}
		DrawImage( Image, Points, UV );
	};

	int32 Offset = 0; // 読み終えたword数。
	while ( Offset < Span.NumWords && !bStopped )
	{
		const uint32 Header = Span.Words[Offset]; // 命令の固定ヘッダー。
		const int32 Count = static_cast<int32>( Header >> 16 ); // ヘッダー込みの長さ。
		if ( Count < 1 || Count > Span.NumWords - Offset )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "Malformed command span" );
			break;
		}
		FDrawCommandView View; // 検査済みの範囲だけを読む。
		View.Type = static_cast<EDrawCommand>( Header & 255 );
		View.Flags = static_cast<uint8>( Header >> 8 );
		View.Data = Span.Words + Offset + 1;
		View.NumDataWords = Count - 1;
		Offset += Count;

		// 描けない描画先・シャドウマップへ描く範囲は、対になる終わりまで、まとめて飛ばす（画面へ描いてしまわないように）。
		if ( View.Type == EDrawCommand::PushTarget )
		{
			++SkipTargetDepth;
			Unsupported( View, "Drawing into a render target is skipped" );
			continue;
		}
		if ( View.Type == EDrawCommand::PopTarget )
		{
			SkipTargetDepth = NMath::Max( 0, SkipTargetDepth - 1 );
			continue;
		}
		if ( View.Type == EDrawCommand::BeginShadowMap )
		{
			++SkipShadowDepth;
			Unsupported( View, "Drawing into a shadow map is skipped" );
			continue;
		}
		if ( View.Type == EDrawCommand::EndShadowMap )
		{
			SkipShadowDepth = NMath::Max( 0, SkipShadowDepth - 1 );
			continue;
		}
		if ( SkipTargetDepth > 0 || SkipShadowDepth > 0 )
		{
			continue;
		}

		const bool bFill = View.Has( FDrawFlag::Fill ); // 塗りつぶしか輪郭か。
		Canvas->SetAntiAlias( View.Has( FDrawFlag::AntiAlias ) || View.Type == EDrawCommand::BoxAA || View.Type == EDrawCommand::CircleAA || View.Type == EDrawCommand::LineAA );
		switch ( View.Type )
		{
		case EDrawCommand::Clear:
		{
			if ( !View.CanRead<FDrawClearData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawClearData>(); // 固定部を所有コピーする。
			Surface.RecordClear( FColor32( static_cast<uint8>( NMath::Clamp( Data.R, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.G, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.B, 0, 255 ) ), static_cast<uint8>( NMath::Clamp( Data.A, 0, 255 ) ) ) );
			break;
		}
		case EDrawCommand::Blend:
		{
			if ( !View.CanRead<FDrawBlendData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBlendData>(); // 固定部を所有コピーする。
			EGeometryBlend Mode = EGeometryBlend::Alpha; // 実行先の混ぜ方。
			switch ( static_cast<EDrawBlend>( Data.Mode ) )
			{
			case EDrawBlend::Opaque:
			case EDrawBlend::Alpha:
				break;
			case EDrawBlend::Add:
				Mode = EGeometryBlend::Add;
				break;
			case EDrawBlend::Subtract:
				Mode = EGeometryBlend::Subtract;
				break;
			case EDrawBlend::Multiply:
				Mode = EGeometryBlend::Multiply;
				break;
			case EDrawBlend::PmaAlpha:
				Mode = EGeometryBlend::PremultipliedAlpha;
				break;
			case EDrawBlend::PmaAdd:
				Mode = EGeometryBlend::PremultipliedAdd;
				break;
			case EDrawBlend::PmaSubtract:
				Mode = EGeometryBlend::PremultipliedSubtract;
				break;
			default:
				// 反転などは、まだ無い。通常の合成で描く（色は変わるが、形は出る）。
				NBackendSupport::ReportUnsupported( "DrawCommands", "Blend mode", "Invert / MultiplyAlpha / PmaInvert are drawn with Alpha" );
				break;
			}
			bAlphaBlend = Data.Mode != static_cast<int32>( EDrawBlend::Opaque );
			Alpha = Data.Mode == static_cast<int32>( EDrawBlend::Opaque ) ? 255 : NMath::Clamp( Data.Param, 0, 255 );
			Canvas->SetBlend( Mode );
			Geometry.SetBlend( Mode );
			break;
		}
		case EDrawCommand::CustomBlend:
		{
			if ( !View.CanRead<FDrawCustomBlendData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCustomBlendData>(); // 固定部を所有コピーする。
			Unsupported( View, "Custom blend is drawn with Alpha" );
			bAlphaBlend = true;
			Alpha = NMath::Clamp( Data.Param, 0, 255 );
			Canvas->SetBlend( EGeometryBlend::Alpha );
			Geometry.SetBlend( EGeometryBlend::Alpha );
			break;
		}
		case EDrawCommand::Filter:
		{
			if ( !View.CanRead<FDrawFilterData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawFilterData>(); // 2Dと3Dで同じ設定を使う。
			if ( Data.Mode < 0 || Data.Mode > static_cast<int32>( EDrawFilter::Anisotropic ) )
			{
				bStopped = true;
				break;
			}
			bLinearFilter = Data.Mode >= 1; // 異方性は、線形として扱う。
			bExplicitFilter = true;
			FDrawFilterData Simple = Data; // 3D側は、最近傍か線形だけを受け取る。
			Simple.Mode = bLinearFilter ? 1 : 0;
			FDrawCommandView SimpleView = View; // 値を差し替えた命令。
			SimpleView.Data = reinterpret_cast<const uint32*>( &Simple );
			Geometry.Execute( SimpleView, Bright, Alpha, bAlphaBlend );
			break;
		}
		case EDrawCommand::Bright:
		{
			if ( !View.CanRead<FDrawBrightData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawBrightData>(); // 固定部を所有コピーする。
			Bright = Data.Color;
			break;
		}
		case EDrawCommand::Transform2D:
		{
			if ( !View.CanRead<FDrawMatrixData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawMatrixData>(); // 固定部を所有コピーする。
			if ( !ValidPoints( Data.M, 12 ) )
			{
				bStopped = true;
				break;
			}
			Transform.XX = Data.M[0];
			Transform.XY = Data.M[1];
			Transform.YX = Data.M[3];
			Transform.YY = Data.M[4];
			Transform.TX = Data.M[9];
			Transform.TY = Data.M[10];
			Transform.bIdentity = Transform.XX == 1 && Transform.XY == 0 && Transform.YX == 0 && Transform.YY == 1 && Transform.TX == 0 && Transform.TY == 0;
			break;
		}
		case EDrawCommand::PushClip:
		{
			if ( !View.CanRead<FDrawClipData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawClipData>(); // 固定部を所有コピーする。
			Canvas->PushClip( Data.X0, Data.Y0, NMath::Max( Data.X0, Data.X1 ), NMath::Max( Data.Y0, Data.Y1 ) );
			++ClipDepth;
			break;
		}
		case EDrawCommand::PopClip:
		{
			if ( !View.CanRead<FDrawPopClipData>() )
			{
				bStopped = true;
				break;
			}
			if ( ClipDepth > 0 )
			{
				Canvas->PopClip();
				--ClipDepth;
			}
			break;
		}
		case EDrawCommand::Box:
		case EDrawCommand::BoxAA:
		{
			float32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
			FDrawRgb Value; // 図形の色。
			if ( View.Type == EDrawCommand::Box )
			{
				if ( !View.CanRead<FDrawBoxData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawBoxData>(); // 固定部を所有コピーする。
				X0 = static_cast<float32>( Data.X0 );
				Y0 = static_cast<float32>( Data.Y0 );
				X1 = static_cast<float32>( Data.X1 );
				Y1 = static_cast<float32>( Data.Y1 );
				Thickness = static_cast<float32>( Data.Thickness );
				Value = Data.Color;
			}
			else
			{
				if ( !View.CanRead<FDrawBoxAAData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawBoxAAData>(); // 固定部を所有コピーする。
				X0 = Data.X0;
				Y0 = Data.Y0;
				X1 = Data.X1;
				Y1 = Data.Y1;
				Thickness = Data.Thickness;
				Value = Data.Color;
			}
			if ( !Valid( X0 ) || !Valid( Y0 ) || !Valid( X1 ) || !Valid( Y1 ) || !Valid( Thickness ) || Thickness <= 0 )
			{
				break;
			}
			if ( Transform.bIdentity )
			{
				Canvas->Box( X0, Y0, X1, Y1, Color( Value ), bFill, Thickness );
			}
			else
			{
				const float32 Points[8] = { X0, Y0, X1, Y0, X1, Y1, X0, Y1 }; // 変換すると、矩形でなくなる。
				DrawPolygon( Points, 4, Color( Value ), bFill, Thickness );
			}
			break;
		}
		case EDrawCommand::Line:
		case EDrawCommand::LineAA:
		{
			float32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
			FDrawRgb Value; // 線の色。
			if ( View.Type == EDrawCommand::Line )
			{
				if ( !View.CanRead<FDrawLineData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawLineData>(); // 固定部を所有コピーする。
				X0 = static_cast<float32>( Data.X0 );
				Y0 = static_cast<float32>( Data.Y0 );
				X1 = static_cast<float32>( Data.X1 );
				Y1 = static_cast<float32>( Data.Y1 );
				Thickness = static_cast<float32>( Data.Thickness );
				Value = Data.Color;
			}
			else
			{
				if ( !View.CanRead<FDrawLineAAData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawLineAAData>(); // 固定部を所有コピーする。
				X0 = Data.X0;
				Y0 = Data.Y0;
				X1 = Data.X1;
				Y1 = Data.Y1;
				Thickness = Data.Thickness;
				Value = Data.Color;
			}
			if ( !Valid( X0 ) || !Valid( Y0 ) || !Valid( X1 ) || !Valid( Y1 ) || !Valid( Thickness ) || Thickness <= 0 )
			{
				break;
			}
			Transform.Apply( X0, Y0 );
			Transform.Apply( X1, Y1 );
			if ( Valid( X0 ) && Valid( Y0 ) && Valid( X1 ) && Valid( Y1 ) )
			{
				Canvas->Line( X0, Y0, X1, Y1, Color( Value ), Thickness * Transform.GetScale() );
			}
			break;
		}
		case EDrawCommand::Circle:
		case EDrawCommand::CircleAA:
		{
			float32 X = 0, Y = 0, Radius = 0, Thickness = 1; // 整数と小数の命令を、同じ値へそろえる。
			int32 Segments = 0; // 0 は自動。
			FDrawRgb Value; // 円の色。
			if ( View.Type == EDrawCommand::Circle )
			{
				if ( !View.CanRead<FDrawCircleData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawCircleData>(); // 固定部を所有コピーする。
				X = static_cast<float32>( Data.X );
				Y = static_cast<float32>( Data.Y );
				Radius = static_cast<float32>( Data.Radius );
				Thickness = static_cast<float32>( Data.Thickness );
				Value = Data.Color;
			}
			else
			{
				if ( !View.CanRead<FDrawCircleAAData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawCircleAAData>(); // 固定部を所有コピーする。
				X = Data.X;
				Y = Data.Y;
				Radius = Data.Radius;
				Thickness = Data.Thickness;
				Segments = NMath::Clamp( Data.NumSegments, 0, 4096 );
				Value = Data.Color;
			}
			if ( !Valid( X ) || !Valid( Y ) || !Valid( Radius ) || Radius <= 0 || !Valid( Thickness ) || Thickness <= 0 )
			{
				break;
			}
			if ( Transform.bIdentity )
			{
				Canvas->Circle( X, Y, Radius, Color( Value ), bFill, Thickness, Segments );
			}
			else
			{
				DrawEllipse( X, Y, Radius, Radius, Color( Value ), bFill, Thickness, Segments );
			}
			break;
		}
		case EDrawCommand::Oval:
		{
			if ( !View.CanRead<FDrawOvalData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawOvalData>(); // 固定部を所有コピーする。
			DrawEllipse( Data.X, Data.Y, Data.Rx, Data.Ry, Color( Data.Color ), bFill, Data.Thickness, NMath::Clamp( Data.NumSegments, 0, 4096 ) );
			break;
		}
		case EDrawCommand::RoundBox:
		{
			if ( !View.CanRead<FDrawRoundBoxData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawRoundBoxData>(); // 固定部を所有コピーする。
			const float32 Raw[6] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.Rx, Data.Ry }; // まとめて検査する値。
			if ( !ValidPoints( Raw, 6 ) )
			{
				break;
			}
			const float32 Left = NMath::Min( Data.X0, Data.X1 ), Right = NMath::Max( Data.X0, Data.X1 ), Top = NMath::Min( Data.Y0, Data.Y1 ), Bottom = NMath::Max( Data.Y0, Data.Y1 ); // 向きをそろえた四辺。
			const float32 Rx = NMath::Clamp( Data.Rx, 0.0f, ( Right - Left ) * 0.5f ), Ry = NMath::Clamp( Data.Ry, 0.0f, ( Bottom - Top ) * 0.5f ); // 丸みは、辺の半分まで。
			if ( Right - Left <= 0 || Bottom - Top <= 0 )
			{
				break;
			}
			if ( Rx <= 0 || Ry <= 0 )
			{
				const float32 Points[8] = { Left, Top, Right, Top, Right, Bottom, Left, Bottom }; // 丸みなしは、矩形。
				DrawPolygon( Points, 4, Color( Data.Color ), bFill, Data.Thickness );
				break;
			}
			const int32 PerCorner = NMath::Clamp( GetSegments( NMath::Max( Rx, Ry ), NMath::Clamp( Data.NumSegments, 0, 4096 ) ) / 4, 2, 120 ); // 角 1 つの分割。
			const float32 CenterX[4] = { Right - Rx, Left + Rx, Left + Rx, Right - Rx }, CenterY[4] = { Bottom - Ry, Bottom - Ry, Top + Ry, Top + Ry }; // 右下・左下・左上・右上の、丸みの中心。
			TArray<float32> Points; // 角ごとに、4分の1の円弧を並べる。
			for ( int32 Corner = 0; Corner < 4; ++Corner )
			{
				for ( int32 Index = 0; Index <= PerCorner; ++Index )
				{
					const float32 Angle = ( Corner + static_cast<float32>( Index ) / PerCorner ) * 1.57079632679f; // 角ごとに 90 度ずつ進む。
					Points.Add( CenterX[Corner] + Rx * NMath::Cos( Angle ) );
					Points.Add( CenterY[Corner] + Ry * NMath::Sin( Angle ) );
				}
			}
			DrawPolygon( Points.GetData(), Points.Num() / 2, Color( Data.Color ), bFill, Data.Thickness );
			break;
		}
		case EDrawCommand::Pixel:
		{
			if ( !View.CanRead<FDrawPixelData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawPixelData>(); // 固定部を所有コピーする。
			const float32 X = static_cast<float32>( Data.X ), Y = static_cast<float32>( Data.Y ); // 1画素の矩形の左上。
			const float32 Points[8] = { X, Y, X + 1, Y, X + 1, Y + 1, X, Y + 1 }; // 1画素の矩形。
			DrawPolygon( Points, 4, Color( Data.Color ), true, 1 );
			break;
		}
		case EDrawCommand::Triangle:
		{
			if ( !View.CanRead<FDrawTriangleData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTriangleData>(); // 固定部を所有コピーする。
			const float32 Points[6] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2 }; // XYの並び。
			DrawPolygon( Points, 3, Color( Data.Color ), bFill, Data.Thickness );
			break;
		}
		case EDrawCommand::Quad:
		{
			if ( !View.CanRead<FDrawQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawQuadData>(); // 固定部を所有コピーする。
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2, Data.X3, Data.Y3 }; // XYの並び。
			DrawPolygon( Points, 4, Color( Data.Color ), bFill, Data.Thickness );
			break;
		}
		case EDrawCommand::Text:
		case EDrawCommand::TextFont:
		{
			float32 X = 0, Y = 0; // 文字列の左上。
			int32 Length = 0, Capacity = 0; // 文字数と、命令に含まれる文字の範囲。
			FDrawRgb Value; // 文字の色。
			const char* Text = nullptr; // 命令の中の文字列。
			if ( View.Type == EDrawCommand::Text )
			{
				if ( !View.CanRead<FDrawTextData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawTextData>(); // 固定部を所有コピーする。
				X = static_cast<float32>( Data.X );
				Y = static_cast<float32>( Data.Y );
				Length = Data.Length;
				Value = Data.Color;
				Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
				Text = View.GetTextChars();
			}
			else
			{
				if ( !View.CanRead<FDrawTextFontData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawTextFontData>(); // 固定部を所有コピーする。
				X = static_cast<float32>( Data.X );
				Y = static_cast<float32>( Data.Y );
				Length = Data.Length;
				Value = Data.Color;
				Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
				Text = View.GetFontTextChars();
				// フォントの指定・縁取り・位置揃えは、まだ無い。既定の文字で、左端から描く。
				Unsupported( View, "Fonts, edges and alignment are not supported; the default font is used" );
			}
			if ( Length >= 0 && Length < Capacity && Text[Length] == '\0' )
			{
				Transform.Apply( X, Y ); // 文字は、位置だけを変換する（拡大・回転はしない）。
				if ( Valid( X ) && Valid( Y ) )
				{
					Canvas->Text( X, Y, Color( Value ), Text, Text + Length );
				}
			}
			break;
		}
		case EDrawCommand::Image:
		{
			if ( !View.CanRead<FDrawImageData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !GetImage( Data.Handle, Image ) )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y, Data.X + static_cast<float32>( Image.Width ), Data.Y + static_cast<float32>( Image.Height ), Data.X, Data.Y + static_cast<float32>( Image.Height ) }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::ImageStretch:
		{
			if ( !View.CanRead<FDrawImageStretchData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageStretchData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !GetImage( Data.Handle, Image ) )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::ImageQuad:
		{
			if ( !View.CanRead<FDrawImageQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageQuadData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !GetImage( Data.Handle, Image ) )
			{
				break;
			}
			float32 UV[4] = { 0, 0, 1, 1 }; // 反転前の矩形。
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::ImagePart:
		{
			if ( !View.CanRead<FDrawImagePartData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
			if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
			{
				break;
			}
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X, Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y, Data.X + static_cast<float32>( Data.SrcW ), Data.Y + static_cast<float32>( Data.SrcH ), Data.X, Data.Y + static_cast<float32>( Data.SrcH ) }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::ImagePartStretch:
		{
			if ( !View.CanRead<FDrawImagePartStretchData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartStretchData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
			if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
			{
				break;
			}
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X0, Data.Y0, Data.X1, Data.Y0, Data.X1, Data.Y1, Data.X0, Data.Y1 }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::ImagePartQuad:
		{
			if ( !View.CanRead<FDrawImagePartQuadData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImagePartQuadData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			float32 UV[4] = { 0, 0, 1, 1 }; // 切り出す範囲。
			if ( !GetImage( Data.Handle, Image ) || !SourceUV( Image, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, UV ) )
			{
				break;
			}
			FlipUV( View, UV );
			const float32 Points[8] = { Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3] }; // 描画先の4点。
			DrawImage( Image, Points, UV );
			break;
		}
		case EDrawCommand::Sprite:
		{
			if ( !View.CanRead<FDrawSpriteData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawSpriteData>(); // 中心・拡大率・回転。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( GetImage( Data.Handle, Image ) )
			{
				DrawSprite( View, Image, Data.X, Data.Y, Image.Width * 0.5f, Image.Height * 0.5f, Data.Scale, Data.Scale, Data.Angle, 0, 0, Image.Width, Image.Height );
			}
			break;
		}
		case EDrawCommand::SpriteRect:
		{
			if ( !View.CanRead<FDrawSpriteRectData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawSpriteRectData>(); // 画像の一部分・基準点・拡大率・回転。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( GetImage( Data.Handle, Image ) )
			{
				DrawSprite( View, Image, Data.X, Data.Y, Data.PivotX, Data.PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH );
			}
			break;
		}
		case EDrawCommand::SpritePivot:
		{
			if ( !View.CanRead<FDrawSpritePivotData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawSpritePivotData>(); // 基準点・拡大率・回転。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( GetImage( Data.Handle, Image ) )
			{
				DrawSprite( View, Image, Data.X, Data.Y, Data.PivotX, Data.PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, 0, 0, Image.Width, Image.Height );
			}
			break;
		}
		// まだ描画に反映しない状態。図形は、この状態なしで描く。
		case EDrawCommand::AddColor:
		case EDrawCommand::AlphaTest:
		case EDrawCommand::PushMask:
		case EDrawCommand::PopMask:
		case EDrawCommand::UseShadowMap:
		case EDrawCommand::UseShader:
		case EDrawCommand::ShaderConstants:
		case EDrawCommand::ShaderTexture:
		case EDrawCommand::ShaderOutputTarget:
		case EDrawCommand::ModelAnim:
		case EDrawCommand::ModelAnimFrame:
			Unsupported( View, "The state is ignored" );
			break;
		default:
		{
			const auto Result = Geometry.Execute( View, Bright, Alpha, bAlphaBlend ); // 同じ順序で3Dへ渡す。
			if ( Result == CGeometryCommandExecutor::EResult::Executed )
			{
				break;
			}
			// 描けない命令は、その命令だけを飛ばす（前後の命令は描く）。
			Unsupported( View, Result == CGeometryCommandExecutor::EResult::Rejected ? "The command was rejected (unsupported input or a failed draw) and skipped" : "The command is skipped" );
			break;
		}
		}
	}
	while ( ClipDepth > 0 )
	{
		Canvas->PopClip();
		--ClipDepth;
	}
	Canvas->SetTextureFilter( true ); // 外側のImGui描画の既定へ戻す。
	Canvas->SetAntiAlias( true );
	Canvas->SetBlend( EGeometryBlend::Alpha );
	if ( bStopped )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "Incomplete span", "Malformed payload: remaining commands were skipped" );
	}
}

// SPDX-License-Identifier: MIT
#include "CanvasDrawExecutor.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Platform/Render/Shader/ShaderCommandExecutor.h"
#include "Library/Platform/Render/Shadow/ShadowMapMath.h"
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

	// 2Dを描けない描画先（浮動小数点の描画先・複数の出力のパス）で使う、何も描かないキャンバス。
	class CNullCanvas2D : public IRenderCanvas2D
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

	// 未対応の命令の名前（警告に出す）。
	FString CommandName( EDrawCommand Type )
	{
		return FString::Printf( "Draw command %u", static_cast<unsigned>( Type ) );
	}
}

CCanvasDrawExecutor::CCanvasDrawExecutor( IRenderSurface& InSurface, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels, IFontDrawSource* InFonts, IRenderTargetDrawSource* InTargets, IShaderDrawSource* InShaders )
	: Surface( InSurface ), Models( InModels ), Buffers( InBuffers ), Textures( InTextures ), Fonts( InFonts ), Targets( InTargets ), Shaders( InShaders )
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
	CShaderCommandExecutor ShaderExecutor( Geometry, Shaders ); // このSpanだけのシェーダーの状態。
	Geometry.SetShaderSource( Shaders ); // 影の描画にも、同じ実行先を使う。
	CNullCanvas2D NullCanvas; // 2Dを描けない描画先では、これを使う（何も描かない）。
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
	int32 SkipTargetDepth = 0; // 未対応の描画先（レンダーターゲット）へ描く範囲の深さ。この中は描かない。
	int32 SkipShadowDepth = 0; // 描けないシャドウマップへ描く範囲の深さ。この中は描かない（画面へ描いてしまわないように）。
	bool bShadowPassActive = false; // 影の絵へ描いているか（BeginShadowMap〜EndShadowMap）。
	bool bStopped = false; // 壊れた命令以降を描かない印。
	FCanvasTransform Transform; // このSpanだけの2Dの変換。
	// マスク（PushMask）の 1 つ分。マスクにできない画像（描画先でない）のときは、View.Binding が 0 で、マスクなしと同じ。
	struct FMaskState
	{
		FTextureDrawView View; // マスクの画像。
		int32 Channel = 0; // 値にする成分（EDrawMaskChannel と同じ並び）。
		bool bInvert = false; // 値が小さいところほど濃く描くか。
	};
	TArray<FMaskState> MaskStack; // 積んだマスク。最後が、いまのマスク（外側のマスクに重ねず、替わる）。
	// いまのマスクを、キャンバスへ反映する（無ければ、マスクなしに）。実行先が使えなければ、マスクなしで描く。
	const auto ApplyMask = [&]()
	{
		if ( !Canvas )
		{
			return;
		}
		if ( MaskStack.IsEmpty() || MaskStack[MaskStack.Num() - 1].View.Binding == 0 )
		{
			Canvas->SetMask( nullptr, 0, false );
			return;
		}
		const FMaskState& Top = MaskStack[MaskStack.Num() - 1];
		Canvas->SetMask( &Top.View, Top.Channel, Top.bInvert );
	};
	// 描画先（レンダーターゲット）を替える前の状態。戻すときに使う。
	struct FTargetScope
	{
		FRenderTargetDrawView Target; // この段の描画先（主画面なら無効）。
		FCanvasTransform Transform; // 替える前の2Dの変換。
		int32 ClipDepth = 0; // 替える前のクリップの段数。
		bool bShadow = false; // 影の絵への描画か（終わるとき、影を落とす状態を戻す）。
		TArray<FMaskState> MaskStack; // 替える前のマスクの並び（替えた先は、マスクなしから始まる）。
	};
	TArray<FTargetScope> TargetStack; // 積んだ描画先。最後が、いまの描画先。
	FRenderTargetDrawView CurrentTarget; // いまの描画先（主画面なら無効）。
	// いまの描画先のパスを始める（主画面なら何もしない）。キャンバスと3Dの実行先を取り直す。
	const auto EnterTarget = [&]( const FRenderTargetDrawView& Target )
	{
		bool bEntered = true; // 主画面は、常に成功。
		if ( Target.Color.IsValid() )
		{
			FRenderTargetPassDesc Desc; // すでに描いてある内容を保って、続きを描く。
			Desc.ColorTarget = Target.Color;
			Desc.DepthTarget = Target.Depth;
			Desc.bClearColor = false;
			Desc.bClearDepth = false;
			bEntered = Surface.BeginRenderTargetPass( Desc );
		}
		Canvas = Surface.GetCanvas2D();
		if ( !Canvas )
		{
			Canvas = &NullCanvas; // 浮動小数点の描画先などは、2Dを描けない（3Dとシェーダーだけ）。
		}
		Geometry.SetGeometry( Surface.GetGeometry3D() );
		if ( !MaskStack.IsEmpty() )
		{
			ApplyMask();
		}
		return bEntered;
	};
	// 複数の出力（MRT）のパスの中か。ShaderOutputTarget で出力を置いて、シェーダーで描くときに、パスを開き直す。
	bool bMrtActive = false;
	TArray<uint64> MrtSignature; // いまのMRTのパスの、色の描画先の並び。
	// MRTのパスから、1つの出力のパスへ戻る（描いてある内容は保つ）。
	const auto LeaveMrt = [&]()
	{
		if ( !bMrtActive )
		{
			return;
		}
		bMrtActive = false;
		Surface.EndRenderTargetPass();
		EnterTarget( CurrentTarget );
	};
	// シェーダーで描く前に、描画先のパスを整える。書く描画先の数と形式を、Request に入れる。
	const auto PrepareTargets = [&]( FShaderDrawRequest& Request ) -> bool
	{
		FRenderTargetDrawView Outputs[NDrawShader::NumOutputTargets]; // 出力 1〜7 の描画先。
		int32 Highest = 0; // 置いてある、いちばん大きい出力の番号。
		for ( int32 Index = 1; Index < NDrawShader::NumOutputTargets; ++Index )
		{
			const int32 Handle = ShaderExecutor.GetOutputTarget( Index );
			if ( Handle < 0 )
			{
				continue;
			}
			if ( !Targets || !Targets->GetRenderTargetDrawView( Handle, Outputs[Index] ) )
			{
				NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget (not a render target)", "The output target is skipped" );
				return false; // 描画先として読めない番号は、描かない。
			}
			Highest = Index;
		}
		Request.NumTargets = 1;
		Request.TargetFormats[0] = CurrentTarget.Color.IsValid() ? CurrentTarget.Format : ERenderTargetColorFormat::RGBA8;
		if ( Highest == 0 )
		{
			LeaveMrt();
			return true;
		}
		if ( !CurrentTarget.Color.IsValid() )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget", "The screen cannot have multiple outputs; draw into a render target (PushTarget)" );
			return false;
		}
		TArray<uint64> Signature;
		Signature.Add( CurrentTarget.Color.Value );
		for ( int32 Index = 1; Index <= Highest; ++Index )
		{
			if ( !Outputs[Index].Color.IsValid() )
			{
				return false; // 出力の番号は、抜けなく置く。
			}
			Signature.Add( Outputs[Index].Color.Value );
		}
		bool bSame = bMrtActive && Signature.Num() == MrtSignature.Num();
		for ( int32 Index = 0; bSame && Index < Signature.Num(); ++Index )
		{
			bSame = Signature[Index] == MrtSignature[Index];
		}
		if ( !bSame )
		{
			Surface.EndRenderTargetPass(); // いまのパスを閉じて、すべての出力を束ねたパスを開く（描いてある内容は保つ）。
			TArray<FRenderTargetColorAttachment> Attachments;
			FRenderTargetColorAttachment First;
			First.Target = CurrentTarget.Color;
			First.bClear = false;
			Attachments.Add( First );
			for ( int32 Index = 1; Index <= Highest; ++Index )
			{
				FRenderTargetColorAttachment Attachment;
				Attachment.Target = Outputs[Index].Color;
				Attachment.bClear = false;
				Attachments.Add( Attachment );
			}
			FRenderTargetPassDesc Desc;
			Desc.ColorTargets = TArrayView<const FRenderTargetColorAttachment>( Attachments.GetData(), Attachments.Num() );
			Desc.DepthTarget = CurrentTarget.Depth;
			Desc.bClearColor = false;
			Desc.bClearDepth = false;
			if ( !Surface.BeginRenderTargetPass( Desc ) )
			{
				NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget (pass)", "The pass with multiple render targets could not be started" );
				bMrtActive = false;
				EnterTarget( CurrentTarget );
				return false;
			}
			bMrtActive = true;
			MrtSignature = Signature;
			Canvas = Surface.GetCanvas2D();
			if ( !Canvas )
			{
				Canvas = &NullCanvas;
			}
			Geometry.SetGeometry( Surface.GetGeometry3D() );
		}
		Request.NumTargets = Highest + 1;
		for ( int32 Index = 1; Index <= Highest; ++Index )
		{
			Request.TargetFormats[Index] = Outputs[Index].Format;
		}
		return true;
	};
	ShaderExecutor.SetPrepare( PrepareTargets );
	const auto Color = [&]( FDrawRgb Value ) // 色と現在の描画状態を合成する。
	{
		return FColor32( static_cast<uint8>( Value.R * Bright.R / 255 ), static_cast<uint8>( Value.G * Bright.G / 255 ),
			static_cast<uint8>( Value.B * Bright.B / 255 ), static_cast<uint8>( Alpha ) );
	};
	// 平らな図形の色（描く色に、AddColor の値を足す。DxLibと同じく、明るさの乗算のあとに足す）。
	const auto ShapeColor = [&]( FDrawRgb Value )
	{
		const FColor32 Base = Color( Value );
		return FColor32( static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.r ) + AddColorValue[0], 0, 255 ) ), static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.g ) + AddColorValue[1], 0, 255 ) ),
			static_cast<uint8>( NMath::Clamp( static_cast<int32>( Base.b ) + AddColorValue[2], 0, 255 ) ), Base.a );
	};
	// アルファテストを、あるアルファが通るか。
	const auto AlphaTestPasses = [&]( int32 Value )
	{
		switch ( AlphaTestMode )
		{
		case EDrawCompare::Never:
			return false;
		case EDrawCompare::Less:
			return Value < AlphaTestReference;
		case EDrawCompare::Equal:
			return Value == AlphaTestReference;
		case EDrawCompare::LessEqual:
			return Value <= AlphaTestReference;
		case EDrawCompare::Greater:
			return Value > AlphaTestReference;
		case EDrawCompare::NotEqual:
			return Value != AlphaTestReference;
		case EDrawCompare::GreaterEqual:
			return Value >= AlphaTestReference;
		default:
			return true;
		}
	};
	// 画素ごとにアルファが違う図（画像・文字）が、1画素でも通りうるか（0から、いまの不透明度まで）。
	const auto AlphaTestMayPass = [&]()
	{
		for ( int32 Value = 0; Value <= Alpha; ++Value )
		{
			if ( AlphaTestPasses( Value ) )
			{
				return true;
			}
		}
		return false;
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
		if ( AddColorValue[0] != 0 || AddColorValue[1] != 0 || AddColorValue[2] != 0 )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "AddColor (image / text)", "AddColor is applied to flat shapes only; images and text are drawn without it" );
		}
		Canvas->Image( Image, Moved, UV, Color( FDrawRgb() ) );
	};
	// 画像を、凸な多角形（x, y, u, v の頂点列）へ、変換してから貼る。
	const auto DrawImagePolygon = [&]( const FTextureDrawView& Image, TArray<float32>& Vertices )
	{
		const int32 Count = Vertices.Num() / 4; // 頂点の数。
		if ( Count < 3 || !ValidPoints( Vertices.GetData(), Vertices.Num() ) )
		{
			return;
		}
		for ( int32 Index = 0; Index < Count; ++Index )
		{
			Transform.Apply( Vertices[Index * 4], Vertices[Index * 4 + 1] );
		}
		if ( !ValidPoints( Vertices.GetData(), Vertices.Num() ) )
		{
			return;
		}
		if ( !Canvas->SetTextureFilter( bLinearFilter ) && bExplicitFilter )
		{
			return;
		}
		Canvas->ImagePolygon( Image, Vertices.GetData(), Count, Color( FDrawRgb() ) );
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

	// 画像の処理（フィルター・混ぜ合わせ）。変換先の画像のパスを開き、その矩形へ、組み込みのピクセルシェーダーで描いて、元の描画先へ戻る。描けなければ false。
	// Constants は、シェーダーの定数（int4 を 4 つ。[0..1] = 種類・乗算済みか、[2] = 混ぜる強さ、[8..15] = 引数）。矩形の大きさと、元・先の左上は、ここで入れる。
	const auto RunImageOperation = [&]( EBuiltInPixelShader Pixel, int32 DestHandle, int32 BaseHandle, int32 AuxHandle, bool bBlendToDest, bool bUseRect, int32 SrcX, int32 SrcY, int32 SrcW, int32 SrcH, int32 DestX, int32 DestY, int32 ( &Constants )[16] ) -> bool
	{
		FRenderTargetDrawView Destination; // 変換先の描画先。
		FTextureDrawView BaseImage, AuxImage; // 元の画像と、混ぜる画像（グラデーションマップ）。
		// 読みながら同じ画像へは書けない。変換先は、色だけの RGBA8 の描画先に限る。
		if ( !Shaders || !Targets || BaseHandle == DestHandle || AuxHandle == DestHandle || !Targets->GetRenderTargetDrawView( DestHandle, Destination ) || Destination.Format != ERenderTargetColorFormat::RGBA8 || !GetImage( BaseHandle, BaseImage )
			|| ( AuxHandle >= 0 && !GetImage( AuxHandle, AuxImage ) ) )
		{
			return false;
		}
		int32 X0 = 0, Y0 = 0, X1 = Destination.Width, Y1 = Destination.Height; // 描く矩形（変換先の中）。
		if ( bUseRect )
		{
			if ( SrcX < 0 || SrcY < 0 || SrcW <= 0 || SrcH <= 0 || static_cast<int64>( SrcX ) + SrcW > BaseImage.Width || static_cast<int64>( SrcY ) + SrcH > BaseImage.Height )
			{
				return false;
			}
			X0 = NMath::Max( DestX, 0 );
			Y0 = NMath::Max( DestY, 0 );
			X1 = static_cast<int32>( NMath::Min( static_cast<int64>( DestX ) + SrcW, static_cast<int64>( Destination.Width ) ) );
			Y1 = static_cast<int32>( NMath::Min( static_cast<int64>( DestY ) + SrcH, static_cast<int64>( Destination.Height ) ) );
			if ( X1 <= X0 || Y1 <= Y0 )
			{
				return true; // 変換先の外へ出る矩形は、何も描かない。
			}
		}
		if ( Pixel == EBuiltInPixelShader::ImageFilter )
		{
			Constants[2] = bUseRect ? SrcW : Destination.Width; // 拡大・縮小の変換先の大きさ。
			Constants[3] = bUseRect ? SrcH : Destination.Height;
		}
		Constants[4] = bUseRect ? SrcX : 0; // 元の矩形の左上。
		Constants[5] = bUseRect ? SrcY : 0;
		Constants[6] = bUseRect ? DestX : 0; // 変換先の矩形の左上。
		Constants[7] = bUseRect ? DestY : 0;
		const FRenderTargetDrawView Previous = CurrentTarget; // 戻す先。
		const TArray<FMaskState> SavedMask = MaskStack; // 変換先のパスには、マスクを掛けない。
		MaskStack.Reset();
		if ( Previous.Color.IsValid() )
		{
			Surface.EndRenderTargetPass(); // 入れ子にできないので、いまの描画先のパスを閉じてから、変換先のパスを始める。
		}
		bMrtActive = false;
		if ( !EnterTarget( Destination ) )
		{
			MaskStack = SavedMask;
			EnterTarget( Previous );
			return false;
		}
		bool bDrawn = false; // 描けたか。
		IRenderGeometry3D* Target = Geometry.GetGeometry();
		if ( Target && Target->GetWidth() == Destination.Width && Target->GetHeight() == Destination.Height )
		{
			const float32 Width = static_cast<float32>( Destination.Width ), Height = static_cast<float32>( Destination.Height );
			const float32 Left = static_cast<float32>( X0 ) / Width * 2.0f - 1.0f, Right = static_cast<float32>( X1 ) / Width * 2.0f - 1.0f; // 画素の座標をクリップ座標へ。
			const float32 Top = 1.0f - static_cast<float32>( Y0 ) / Height * 2.0f, Bottom = 1.0f - static_cast<float32>( Y1 ) / Height * 2.0f;
			const float32 PositionsX[4] = { Left, Right, Left, Right }, PositionsY[4] = { Top, Top, Bottom, Bottom };
			FDrawShaderVertex3D Vertices[4];
			for ( int32 Index = 0; Index < 4; ++Index )
			{
				FMemory::Memzero( &Vertices[Index], sizeof( FDrawShaderVertex3D ) );
				Vertices[Index].Position[0] = PositionsX[Index];
				Vertices[Index].Position[1] = PositionsY[Index];
				for ( uint8& Channel : Vertices[Index].Diffuse )
				{
					Channel = 255;
				}
			}
			const uint32 Indices[6] = { 0, 1, 2, 2, 1, 3 };
			FShaderConstantData Constant; // b5 へ置く定数。
			Constant.Stage = static_cast<int32>( EShaderStage::Pixel );
			Constant.Slot = 5;
			Constant.Bytes = TArrayView<const uint8>( reinterpret_cast<const uint8*>( Constants ), sizeof( int32 ) * 16 );
			FShaderDrawRequest Request;
			Request.Builtin = EBuiltInVertexShader::Rect;
			Request.BuiltinPixel = Pixel;
			Request.Topology = EGeometryTopology::Triangles;
			Request.bUseDepth = false;
			Request.State.Culling = EGeometryCulling::None;
			Request.State.bAlphaBlend = bBlendToDest; // false なら、変換先を置き換える。
			Request.NumTargets = 1;
			Request.TargetFormats[0] = ERenderTargetColorFormat::RGBA8;
			Request.Vertices = reinterpret_cast<const uint8*>( Vertices );
			Request.NumVertices = 4;
			Request.Indices = Indices;
			Request.NumIndices = 6;
			Request.Constants = TArrayView<const FShaderConstantData>( &Constant, 1 );
			Request.Textures[0] = BaseHandle;
			Request.Textures[2] = AuxHandle;
			bDrawn = Shaders->DrawShader( Request, *Target );
		}
		Surface.EndRenderTargetPass();
		MaskStack = SavedMask;
		if ( !EnterTarget( Previous ) )
		{
			bStopped = true; // 元の描画先へ戻れない。これ以上は描けない。
		}
		return bDrawn;
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
			FRenderTargetDrawView Next; // 替える先の描画先。
			if ( SkipTargetDepth > 0 || SkipShadowDepth > 0 || !View.CanRead<FDrawTargetData>() || !Targets || !Targets->GetRenderTargetDrawView( View.Read<FDrawTargetData>().Handle, Next ) )
			{
				++SkipTargetDepth;
				Unsupported( View, "Drawing into this render target is skipped" );
				continue;
			}
			// いまの描画先のパスを閉じてから、次の描画先のパスを始める（パスは入れ子にできない）。
			if ( CurrentTarget.Color.IsValid() )
			{
				Surface.EndRenderTargetPass();
				bMrtActive = false;
			}
			FTargetScope Scope; // 戻すときの状態。
			Scope.Target = CurrentTarget;
			Scope.Transform = Transform;
			Scope.ClipDepth = ClipDepth;
			Scope.MaskStack = MaskStack;
			MaskStack.Reset(); // 替えた先は、マスクなしから始まる。
			if ( !EnterTarget( Next ) )
			{
				// 始められなかったら、元の描画先へ戻して、この範囲を飛ばす。
				MaskStack = Scope.MaskStack;
				EnterTarget( CurrentTarget );
				++SkipTargetDepth;
				Unsupported( View, "The render target pass could not be started; drawing into it is skipped" );
				if ( !Canvas )
				{
					bStopped = true;
				}
				continue;
			}
			TargetStack.Add( Scope );
			CurrentTarget = Next;
			Transform = FCanvasTransform(); // 替えた先は、クリップなし・2Dの変換なしから始まる。
			ClipDepth = 0;
			continue;
		}
		if ( View.Type == EDrawCommand::PopTarget )
		{
			if ( SkipTargetDepth > 0 )
			{
				--SkipTargetDepth;
				continue;
			}
			if ( SkipShadowDepth > 0 || TargetStack.IsEmpty() )
			{
				continue;
			}
			while ( ClipDepth > 0 )
			{
				Canvas->PopClip();
				--ClipDepth;
			}
			Surface.EndRenderTargetPass();
			bMrtActive = false;
			const FTargetScope Scope = TargetStack.Pop(); // 替える前の状態。
			CurrentTarget = Scope.Target;
			Transform = Scope.Transform;
			ClipDepth = Scope.ClipDepth;
			MaskStack = Scope.MaskStack;
			if ( !EnterTarget( CurrentTarget ) )
			{
				bStopped = true; // 元の描画先へ戻れない。これ以上は描けない。
			}
			continue;
		}
		if ( View.Type == EDrawCommand::BeginShadowMap )
		{
			// 描けない（入れ子・使えない値・影の絵でない・カメラが無い）ときは、対の EndShadowMap まで、描く命令を飛ばす（画面へ描いてしまわないように）。
			FRenderTargetDrawView Map; // 影の絵（R32Float の色と深度）。
			FMatrix4x4 LightViewProjection;
			FDrawBeginShadowMapData Data;
			IRenderGeometry3D* Target = Geometry.GetGeometry();
			bool bReady = SkipTargetDepth == 0 && SkipShadowDepth == 0 && !bShadowPassActive && View.CanRead<FDrawBeginShadowMapData>() && Shaders && Targets && Target;
			if ( bReady )
			{
				Data = View.Read<FDrawBeginShadowMapData>();
				const FCameraViewport Viewport = { Target->GetWidth(), Target->GetHeight() };
				bReady = Data.Handle >= 0 && Targets->GetRenderTargetDrawView( Data.Handle, Map ) && Map.Format == ERenderTargetColorFormat::R32Float && Map.Depth.IsValid()
					&& NMath::IsFinite( Data.AdjustDepth ) && Data.AdjustDepth >= 0.0f && Data.AdjustDepth < 1.0f
					&& NShadowMapMath::ComputeLightViewProjection( Data, Geometry.GetCamera(), Viewport, LightViewProjection );
			}
			if ( bReady && CurrentTarget.Color.IsValid() )
			{
				Surface.EndRenderTargetPass(); // いまの描画先のパスを閉じてから、影の絵のパスを始める。
				bMrtActive = false;
			}
			if ( bReady )
			{
				FRenderTargetPassDesc Desc; // 影の絵は、いちばん遠い（1）の深度で始める。
				Desc.ColorTarget = Map.Color;
				Desc.DepthTarget = Map.Depth;
				Desc.ClearColor = FColor32( 255, 255, 255, 255 );
				Desc.bClearColor = true;
				Desc.ClearDepth = 1.0f;
				Desc.bClearDepth = true;
				if ( !Surface.BeginRenderTargetPass( Desc ) )
				{
					EnterTarget( CurrentTarget ); // 元の描画先のパスへ戻す。
					bReady = false;
				}
			}
			if ( !bReady )
			{
				++SkipShadowDepth;
				Unsupported( View, "The shadow map pass could not be started; drawing into it is skipped" );
				continue;
			}
			FTargetScope Scope; // 終わったときに戻す状態。
			Scope.Target = CurrentTarget;
			Scope.Transform = Transform;
			Scope.ClipDepth = ClipDepth;
			Scope.bShadow = true;
			Scope.MaskStack = MaskStack;
			MaskStack.Reset();
			TargetStack.Add( Scope );
			CurrentTarget = Map;
			Transform = FCanvasTransform();
			ClipDepth = 0;
			Canvas = Surface.GetCanvas2D();
			if ( !Canvas )
			{
				Canvas = &NullCanvas;
			}
			Geometry.SetGeometry( Surface.GetGeometry3D() );
			Geometry.BeginShadowCasting( LightViewProjection );
			Shaders->SetShadowPass( Data.Handle, LightViewProjection, Data.AdjustDepth, Map.Width, Map.Height );
			bShadowPassActive = true;
			continue;
		}
		if ( View.Type == EDrawCommand::EndShadowMap )
		{
			if ( SkipShadowDepth > 0 )
			{
				--SkipShadowDepth;
				continue;
			}
			if ( bShadowPassActive && !TargetStack.IsEmpty() )
			{
				Surface.EndRenderTargetPass();
				bMrtActive = false;
				Geometry.EndShadowCasting();
				bShadowPassActive = false;
				const FTargetScope Scope = TargetStack.Pop(); // 始める前の状態。
				CurrentTarget = Scope.Target;
				Transform = Scope.Transform;
				ClipDepth = Scope.ClipDepth;
				MaskStack = Scope.MaskStack;
				if ( !EnterTarget( CurrentTarget ) )
				{
					bStopped = true;
				}
			}
			continue;
		}
		// 影の絵へ描いている間は、描画先・カメラ・クリップ・消去などの命令は、使えない（DxLib と同じ。無視する）。
		if ( bShadowPassActive )
		{
			switch ( View.Type )
			{
			case EDrawCommand::PushTarget:
			case EDrawCommand::PopTarget:
			case EDrawCommand::PushCamera:
			case EDrawCommand::PopCamera:
			case EDrawCommand::PushClip:
			case EDrawCommand::PopClip:
			case EDrawCommand::PushMask:
			case EDrawCommand::PopMask:
			case EDrawCommand::Clear:
			case EDrawCommand::ClearDepth:
			case EDrawCommand::CopyScreen:
			case EDrawCommand::ImageFilter:
			case EDrawCommand::ImageBlend:
			case EDrawCommand::UseShadowMap:
			case EDrawCommand::PreviewShadowMap:
			case EDrawCommand::ShaderOutputTarget:
				continue;
			default:
				break;
			}
		}
		if ( SkipTargetDepth > 0 || SkipShadowDepth > 0 )
		{
			continue;
		}

		if ( bAlphaTest )
		{
			// アルファテスト: 平らな図形は、アルファが1つに決まるので、通らなければ丸ごと飛ばす。画像・文字は、通りうる画素が無いときだけ飛ばす（画素ごとの比べは、まだ無い）。
			bool bFlatShape = false;
			bool bImageLike = false;
			switch ( View.Type )
			{
			case EDrawCommand::Box:
			case EDrawCommand::BoxAA:
			case EDrawCommand::Line:
			case EDrawCommand::LineAA:
			case EDrawCommand::Circle:
			case EDrawCommand::CircleAA:
			case EDrawCommand::Oval:
			case EDrawCommand::RoundBox:
			case EDrawCommand::Pixel:
			case EDrawCommand::Triangle:
			case EDrawCommand::Quad:
				bFlatShape = true;
				break;
			case EDrawCommand::Text:
			case EDrawCommand::TextFont:
			case EDrawCommand::TextStyled:
			case EDrawCommand::Image:
			case EDrawCommand::ImageStretch:
			case EDrawCommand::ImageQuad:
			case EDrawCommand::ImagePart:
			case EDrawCommand::ImagePartStretch:
			case EDrawCommand::ImagePartQuad:
			case EDrawCommand::Sprite:
			case EDrawCommand::SpriteRect:
			case EDrawCommand::SpritePivot:
			case EDrawCommand::Tile:
			case EDrawCommand::CircleGauge:
				bImageLike = true;
				break;
			default:
				break;
			}
			if ( bFlatShape && !AlphaTestPasses( Alpha ) )
			{
				continue;
			}
			if ( bImageLike )
			{
				if ( !AlphaTestMayPass() )
				{
					continue;
				}
				NBackendSupport::ReportUnsupported( "DrawCommands", "AlphaTest (image / text)", "AlphaTest is applied to flat shapes only; images and text are drawn without a per-pixel test" );
			}
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
			// 係数の番号は、EDrawBlendFactor / EDrawBlendOp と同じ並び。範囲の外は、通常の合成で描く。
			const auto Factor = [&]( int32 Value ) { return static_cast<EGeometryBlendFactor>( NMath::Clamp( Value, 0, static_cast<int32>( EGeometryBlendFactor::SrcAlphaSaturate ) ) ); };
			const auto Op = [&]( int32 Value ) { return static_cast<EGeometryBlendOp>( NMath::Clamp( Value, 0, static_cast<int32>( EGeometryBlendOp::Max ) ) ); };
			FGeometryCustomBlend Custom; // 利用者が決めた混ぜ方。
			Custom.SrcColor = Factor( Data.SrcColor );
			Custom.DestColor = Factor( Data.DestColor );
			Custom.ColorOp = Op( Data.ColorOp );
			Custom.SrcAlpha = Factor( Data.SrcAlpha );
			Custom.DestAlpha = Factor( Data.DestAlpha );
			Custom.AlphaOp = Op( Data.AlphaOp );
			bAlphaBlend = true;
			Alpha = NMath::Clamp( Data.Param, 0, 255 );
			Canvas->SetCustomBlend( Custom );
			Geometry.SetCustomBlend( Custom );
			Geometry.SetBlend( EGeometryBlend::Custom );
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
			if ( !bFill && ( X0 == X1 || Y0 == Y1 ) )
			{
				// 幅か高さが0の輪郭は、線として描く（DxLibの DrawBox と同じ。格子の線などに使われる）。
				float32 LineX0 = X0, LineY0 = Y0, LineX1 = X1, LineY1 = Y1; // 変換する端点。
				Transform.Apply( LineX0, LineY0 );
				Transform.Apply( LineX1, LineY1 );
				if ( Valid( LineX0 ) && Valid( LineY0 ) && Valid( LineX1 ) && Valid( LineY1 ) )
				{
					Canvas->Line( LineX0, LineY0, LineX1, LineY1, ShapeColor( Value ), Thickness * Transform.GetScale() );
				}
			}
			else if ( Transform.bIdentity )
			{
				Canvas->Box( X0, Y0, X1, Y1, ShapeColor( Value ), bFill, Thickness );
			}
			else
			{
				const float32 Points[8] = { X0, Y0, X1, Y0, X1, Y1, X0, Y1 }; // 変換すると、矩形でなくなる。
				DrawPolygon( Points, 4, ShapeColor( Value ), bFill, Thickness );
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
				Canvas->Line( X0, Y0, X1, Y1, ShapeColor( Value ), Thickness * Transform.GetScale() );
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
				Canvas->Circle( X, Y, Radius, ShapeColor( Value ), bFill, Thickness, Segments );
			}
			else
			{
				DrawEllipse( X, Y, Radius, Radius, ShapeColor( Value ), bFill, Thickness, Segments );
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
			DrawEllipse( Data.X, Data.Y, Data.Rx, Data.Ry, ShapeColor( Data.Color ), bFill, Data.Thickness, NMath::Clamp( Data.NumSegments, 0, 4096 ) );
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
				DrawPolygon( Points, 4, ShapeColor( Data.Color ), bFill, Data.Thickness );
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
			DrawPolygon( Points.GetData(), Points.Num() / 2, ShapeColor( Data.Color ), bFill, Data.Thickness );
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
			DrawPolygon( Points, 4, ShapeColor( Data.Color ), true, 1 );
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
			DrawPolygon( Points, 3, ShapeColor( Data.Color ), bFill, Data.Thickness );
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
			DrawPolygon( Points, 4, ShapeColor( Data.Color ), bFill, Data.Thickness );
			break;
		}
		case EDrawCommand::Text:
		case EDrawCommand::TextFont:
		case EDrawCommand::TextStyled:
		{
			float32 X = 0, Y = 0; // 文字列の位置（Text / TextFont は左上。TextStyled は基準点）。
			float32 ScaleX = 1, ScaleY = 1, Angle = 0, AnchorX = 0, AnchorY = 0; // TextStyled の拡大・回転・基準点。
			int32 Length = 0, Capacity = 0, Font = -1; // 文字数、命令に含まれる文字の範囲、フォントの番号（負は既定）。
			FDrawRgb Value, Edge; // 文字と縁取りの色。
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
			else if ( View.Type == EDrawCommand::TextFont )
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
				Edge = Data.EdgeColor;
				Font = Data.Font;
				Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
				Text = View.GetFontTextChars();
			}
			else
			{
				if ( !View.CanRead<FDrawTextStyledData>() )
				{
					bStopped = true;
					break;
				}
				const auto Data = View.Read<FDrawTextStyledData>(); // 固定部を所有コピーする。
				X = Data.X;
				Y = Data.Y;
				ScaleX = Data.ScaleX;
				ScaleY = Data.ScaleY;
				Angle = Data.Angle;
				AnchorX = Data.AnchorX;
				AnchorY = Data.AnchorY;
				Length = Data.Length;
				Value = Data.Color;
				Edge = Data.EdgeColor;
				Font = Data.Font;
				Capacity = ( View.NumDataWords - static_cast<int32>( sizeof( Data ) / 4 ) ) * 4;
				Text = View.GetStyledTextChars();
				if ( Data.Vertical != 0 )
				{
					Unsupported( View, "Vertical writing is drawn horizontally" );
				}
			}
			if ( Length <= 0 || Length >= Capacity || Text[Length] != '\0' )
			{
				break;
			}
			const float32 Raw[7] = { X, Y, ScaleX, ScaleY, Angle, AnchorX, AnchorY }; // まとめて検査する値。
			if ( !ValidPoints( Raw, 7 ) )
			{
				break;
			}
			FRenderTextFont FontValue; // 文字を画像にするための、フォントの値。
			FTextSize Size; // 位置揃えと基準点に使う、文字列の大きさ。
			if ( !Fonts || !Fonts->GetFontDrawView( Font, FontValue ) || !Fonts->MeasureFontText( Font, Text, Length, Size ) )
			{
				// フォントの表が無い実行先・無効なフォントは、既定の文字で、位置だけを合わせて描く。
				Unsupported( View, "Fonts are not available; the default text is used" );
				Transform.Apply( X, Y );
				if ( Valid( X ) && Valid( Y ) )
				{
					Canvas->Text( X, Y, Color( Value ), Text, Text + Length );
				}
				break;
			}
			if ( View.Type == EDrawCommand::TextFont && ( View.Has( FDrawFlag::AlignCenter ) || View.Has( FDrawFlag::AlignRight ) ) )
			{
				X -= static_cast<float32>( View.Has( FDrawFlag::AlignCenter ) ? Size.Width / 2 : Size.Width ); // X が、文字列の中央 / 右端。
			}
			// 文字の画像の中の点を、基準点まわりに拡大・回転して (X, Y) へ動かし、最後に 2D の変換を掛ける。
			const float32 Cos = NMath::Cos( Angle ), Sin = NMath::Sin( Angle ); // 画面（Y が下）での回転。正は時計回り。
			const float32 PivotX = AnchorX * Size.Width, PivotY = AnchorY * Size.Height; // 基準点（文字列の中の位置）。
			const float32 A0 = ScaleX * Cos, A1 = ScaleX * Sin, A2 = -ScaleY * Sin, A3 = ScaleY * Cos; // 拡大と回転。
			const float32 A4 = X - PivotX * A0 - PivotY * A2, A5 = Y - PivotX * A1 - PivotY * A3; // 基準点が (X, Y) に来る移動。
			const float32 Matrix[6] = { A0 * Transform.XX + A1 * Transform.YX, A0 * Transform.XY + A1 * Transform.YY, A2 * Transform.XX + A3 * Transform.YX, A2 * Transform.XY + A3 * Transform.YY,
				A4 * Transform.XX + A5 * Transform.YX + Transform.TX, A4 * Transform.XY + A5 * Transform.YY + Transform.TY };
			const FColor32 EdgeColor = Color( Edge ); // 縁取りにも、明るさと不透明度を掛ける。
			Canvas->TextFont( FontValue, Matrix, Color( Value ), EdgeColor, Text, Text + Length );
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
		case EDrawCommand::CopyScreen:
		{
			if ( !View.CanRead<FDrawCopyScreenData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCopyScreenData>(); // 固定部を所有コピーする。
			FRenderTargetDrawView Destination; // 写し先の描画先（画像）。
			// DxLibと同じく、画像と矩形の大きさが同じでないと、何もしない。
			if ( !Targets || !Targets->GetRenderTargetDrawView( Data.Handle, Destination ) || Destination.Width != Data.X1 - Data.X0 || Destination.Height != Data.Y1 - Data.Y0 )
			{
				break;
			}
			if ( !Surface.CopyColorRegion( Data.X0, Data.Y0, Data.X1, Data.Y1, Destination.Color ) )
			{
				Unsupported( View, "The screen copy failed (the destination has another pixel format, or the region is outside the screen) and is skipped" );
			}
			break;
		}
		case EDrawCommand::UseShadowMap:
		{
			if ( !View.CanRead<FDrawUseShadowMapData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawUseShadowMapData>(); // 固定部を所有コピーする。
			if ( Data.Slot < 0 || Data.Slot >= NDrawShadowMap::NumSlots )
			{
				break;
			}
			FRenderTargetDrawView Map;
			const bool bUsable = Data.Handle >= 0 && Targets && Targets->GetRenderTargetDrawView( Data.Handle, Map ) && Map.Format == ERenderTargetColorFormat::R32Float;
			Geometry.SetShadowReceiver( Data.Slot, bUsable ? Data.Handle : -1 ); // 使えない番号は、そのスロットを外す。
			break;
		}
		case EDrawCommand::AddColor:
		{
			if ( !View.CanRead<FDrawAddColorData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawAddColorData>(); // 固定部を所有コピーする。
			AddColorValue[0] = NMath::Clamp( Data.R, -255, 255 );
			AddColorValue[1] = NMath::Clamp( Data.G, -255, 255 );
			AddColorValue[2] = NMath::Clamp( Data.B, -255, 255 );
			break;
		}
		case EDrawCommand::AlphaTest:
		{
			if ( !View.CanRead<FDrawAlphaTestData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawAlphaTestData>(); // 固定部を所有コピーする。
			bAlphaTest = Data.Enabled != 0;
			AlphaTestMode = static_cast<EDrawCompare>( NMath::Clamp( Data.Mode, 0, static_cast<int32>( EDrawCompare::Always ) ) );
			AlphaTestReference = NMath::Clamp( Data.Reference, 0, 255 );
			break;
		}
		case EDrawCommand::Tile:
		{
			if ( !View.CanRead<FDrawTileData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawTileData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !GetImage( Data.Handle, Image ) || !Valid( Data.Scale ) || Data.Scale <= 0 || !Valid( Data.Angle ) || Data.X1 <= Data.X0 || Data.Y1 <= Data.Y0 )
			{
				break;
			}
			if ( Data.Angle != 0.0f )
			{
				Unsupported( View, "A tile with an angle is skipped" );
				break;
			}
			// DxLibのDrawTileと同じ並べ方: 拡大した画像の中心を「矩形の中心 - 拡大率 x (Tx, Ty)」に置く。(Tx, Ty) は整数。
			const float32 TileWidth = static_cast<float32>( Image.Width ) * Data.Scale, TileHeight = static_cast<float32>( Image.Height ) * Data.Scale; // 1枚の大きさ。
			const int32 ShiftX = NMath::RoundToInt( ( ( static_cast<float32>( Data.X1 - Data.X0 ) - TileWidth ) * 0.5f - static_cast<float32>( Data.OffsetX ) ) / Data.Scale );
			const int32 ShiftY = NMath::RoundToInt( ( ( static_cast<float32>( Data.Y1 - Data.Y0 ) - TileHeight ) * 0.5f - static_cast<float32>( Data.OffsetY ) ) / Data.Scale );
			const float32 Left = static_cast<float32>( Data.X0 + Data.X1 ) * 0.5f - Data.Scale * static_cast<float32>( ShiftX ) - TileWidth * 0.5f; // 基準になる1枚の左端。
			const float32 Top = static_cast<float32>( Data.Y0 + Data.Y1 ) * 0.5f - Data.Scale * static_cast<float32>( ShiftY ) - TileHeight * 0.5f; // 基準になる1枚の上端。
			const int32 FirstX = static_cast<int32>( NMath::Floor( ( static_cast<float32>( Data.X0 ) - Left ) / TileWidth ) ), LastX = static_cast<int32>( NMath::Ceil( ( static_cast<float32>( Data.X1 ) - Left ) / TileWidth ) );
			const int32 FirstY = static_cast<int32>( NMath::Floor( ( static_cast<float32>( Data.Y0 ) - Top ) / TileHeight ) ), LastY = static_cast<int32>( NMath::Ceil( ( static_cast<float32>( Data.Y1 ) - Top ) / TileHeight ) );
			if ( static_cast<int64>( LastX - FirstX ) * ( LastY - FirstY ) > 4096 )
			{
				Unsupported( View, "A tile with more than 4096 pieces is skipped" );
				break;
			}
			for ( int32 Row = FirstY; Row < LastY; ++Row )
			{
				for ( int32 Column = FirstX; Column < LastX; ++Column )
				{
					const float32 TileX = Left + static_cast<float32>( Column ) * TileWidth, TileY = Top + static_cast<float32>( Row ) * TileHeight; // この1枚の左上。
					const float32 X0 = NMath::Max( TileX, static_cast<float32>( Data.X0 ) ), X1 = NMath::Min( TileX + TileWidth, static_cast<float32>( Data.X1 ) ); // 矩形で切った範囲。
					const float32 Y0 = NMath::Max( TileY, static_cast<float32>( Data.Y0 ) ), Y1 = NMath::Min( TileY + TileHeight, static_cast<float32>( Data.Y1 ) );
					if ( X1 <= X0 || Y1 <= Y0 )
					{
						continue;
					}
					const float32 UV[4] = { ( X0 - TileX ) / TileWidth, ( Y0 - TileY ) / TileHeight, ( X1 - TileX ) / TileWidth, ( Y1 - TileY ) / TileHeight }; // 切った範囲に当たる画像の範囲。
					const float32 Points[8] = { X0, Y0, X1, Y0, X1, Y1, X0, Y1 };
					DrawImage( Image, Points, UV );
				}
			}
			break;
		}
		case EDrawCommand::CircleGauge:
		{
			if ( !View.CanRead<FDrawCircleGaugeData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawCircleGaugeData>(); // 固定部を所有コピーする。
			FTextureDrawView Image; // 所有者から借りた描画用画像。
			if ( !GetImage( Data.Handle, Image ) || !Valid( Data.X ) || !Valid( Data.Y ) || !Valid( Data.Percent ) || !Valid( Data.StartPercent ) || !Valid( Data.Scale ) || Data.Scale <= 0 || Data.Percent <= 0.0f )
			{
				break;
			}
			if ( View.Has( FDrawFlag::FlipX ) || View.Has( FDrawFlag::FlipY ) )
			{
				Unsupported( View, "A flipped circle gauge is skipped" );
				break;
			}
			const float64 Half[2] = { Image.Width * static_cast<float64>( Data.Scale ) * 0.5, Image.Height * static_cast<float64>( Data.Scale ) * 0.5 }; // 画像の半分の幅と高さ（拡大後）。
			// 真上から時計回りの割合 From〜To（0〜1）の扇を、画像の縁までの多角形にして貼る。
			const auto DrawSector = [&]( float64 From, float64 To )
			{
				if ( To <= From )
				{
					return;
				}
				constexpr float64 TwoPi = 6.283185307179586;
				TArray<float32> Vertices; // x, y, u, v の頂点列。
				// 中心から、真上を0として時計回りに Angle だけ進んだ向きが、画像の縁に当たる点。
				const auto AddVertex = [&]( float64 OffsetX, float64 OffsetY )
				{
					Vertices.Add( static_cast<float32>( Data.X + OffsetX ) );
					Vertices.Add( static_cast<float32>( Data.Y + OffsetY ) );
					Vertices.Add( static_cast<float32>( 0.5 + OffsetX / ( Half[0] * 2.0 ) ) );
					Vertices.Add( static_cast<float32>( 0.5 + OffsetY / ( Half[1] * 2.0 ) ) );
				};
				const auto AddEdge = [&]( float64 Fraction )
				{
					const float64 Angle = Fraction * TwoPi; // 真上から時計回りの角度。
					const float64 DirectionX = NMath::Sin( Angle ), DirectionY = -NMath::Cos( Angle ); // 画面の向き（Yは下が正）。
					float64 Length = 1.0e30;
					if ( NMath::Abs( DirectionX ) > 1.0e-12 )
					{
						Length = NMath::Min( Length, Half[0] / NMath::Abs( DirectionX ) );
					}
					if ( NMath::Abs( DirectionY ) > 1.0e-12 )
					{
						Length = NMath::Min( Length, Half[1] / NMath::Abs( DirectionY ) );
					}
					AddVertex( DirectionX * Length, DirectionY * Length );
				};
				AddVertex( 0.0, 0.0 );
				AddEdge( From );
				// 間にある、画像の4つの角（右上・右下・左下・左上の順）。
				const float64 CornerAngle = NMath::Atan2( Half[0], Half[1] ); // 真上から右上の角までの角度。
				const float64 Corners[4][3] = { { CornerAngle, Half[0], -Half[1] }, { 3.141592653589793 - CornerAngle, Half[0], Half[1] }, { 3.141592653589793 + CornerAngle, -Half[0], Half[1] }, { TwoPi - CornerAngle, -Half[0], -Half[1] } };
				for ( const auto& Corner : Corners )
				{
					if ( Corner[0] > From * TwoPi && Corner[0] < To * TwoPi )
					{
						AddVertex( Corner[1], Corner[2] );
					}
				}
				AddEdge( To );
				DrawImagePolygon( Image, Vertices );
			};
			// DxLibと同じ解釈: 描く量が100以上なら全体、描き終わりが100を超えるなら、先頭へ回り込む。
			const float64 Start = NMath::Clamp( static_cast<float64>( Data.StartPercent ), 0.0, 100.0 ), End = Start + static_cast<float64>( Data.Percent );
			if ( Data.Percent >= 100.0f )
			{
				DrawSector( 0.0, 1.0 ); // 全体は、描き始めの位置に関係ない（1回で描く）。
			}
			else if ( End <= 100.0 )
			{
				DrawSector( Start / 100.0, End / 100.0 );
			}
			else
			{
				DrawSector( Start / 100.0, 1.0 );
				DrawSector( 0.0, ( End - 100.0 ) / 100.0 );
			}
			break;
		}
		case EDrawCommand::ImageFilter:
		{
			if ( !View.CanRead<FDrawImageFilterData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageFilterData>(); // 固定部を所有コピーする。
			// DxLibと同じく、使えない種類・画像の番号は、何もしない。
			if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawGraphFilter::FloatColorScale ) || Data.Source < 0 || Data.Dest < 0 || ( Data.Type == static_cast<int32>( EDrawGraphFilter::GradientMap ) && Data.Map < 0 ) )
			{
				break;
			}
			int32 Constants[16] = { Data.Type, Data.Premultiplied, 0, 0, 0, 0, 0, 0, Data.Params[0], Data.Params[1], Data.Params[2], Data.Params[3], Data.Params[4], Data.Params[5], Data.Params[6], Data.Params[7] };
			if ( !RunImageOperation( EBuiltInPixelShader::ImageFilter, Data.Dest, Data.Source, Data.Type == static_cast<int32>( EDrawGraphFilter::GradientMap ) ? Data.Map : -1, Data.Blend != 0, Data.HasRect != 0, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH,
					Data.DestX, Data.DestY, Constants ) )
			{
				Unsupported( View, "The image filter failed (the destination is not an RGBA8 render target, or the source is the destination) and is skipped" );
			}
			break;
		}
		case EDrawCommand::ImageBlend:
		{
			if ( !View.CanRead<FDrawImageBlendData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawImageBlendData>(); // 固定部を所有コピーする。
			if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawGraphBlend::Mask ) || Data.Base < 0 || Data.Blend < 0 || Data.Dest < 0 )
			{
				break;
			}
			int32 Constants[16] = { Data.Type, Data.Premultiplied, NMath::Clamp( Data.Ratio, 0, 255 ), 0, 0, 0, 0, 0, Data.Select[0], Data.Select[1], Data.Select[2], Data.Select[3], 0, 0, 0, 0 };
			if ( !RunImageOperation( EBuiltInPixelShader::ImageBlend, Data.Dest, Data.Base, Data.Blend, false, false, 0, 0, 0, 0, 0, 0, Constants ) )
			{
				Unsupported( View, "The image blend failed (the destination is not an RGBA8 render target, or an image is the destination) and is skipped" );
			}
			break;
		}
		case EDrawCommand::PushMask:
		{
			if ( !View.CanRead<FDrawMaskData>() )
			{
				bStopped = true;
				break;
			}
			const auto Data = View.Read<FDrawMaskData>(); // 固定部を所有コピーする。
			FMaskState Mask;
			FTextureDrawView Image; // マスクの画像。
			// DxLibと同じく、マスクにできない画像は、マスクなしで描く（そのマスクも、1 つに数える）。2Dの描画だけが、マスクで薄まる。
			if ( GetImage( Data.Handle, Image ) )
			{
				Mask.View = Image;
			}
			Mask.Channel = NMath::Clamp( Data.Channel, 0, 3 );
			Mask.bInvert = Data.Invert != 0;
			MaskStack.Add( Mask );
			ApplyMask();
			break;
		}
		case EDrawCommand::PopMask:
		{
			if ( MaskStack.IsEmpty() )
			{
				break;
			}
			MaskStack.Pop();
			ApplyMask();
			break;
		}
		// まだ描画に反映しない状態。図形は、この状態なしで描く。
		case EDrawCommand::ModelAnim:
		case EDrawCommand::ModelAnimFrame:
			Unsupported( View, "The state is ignored" );
			break;
		default:
		{
			if ( CShaderCommandExecutor::IsShaderCommand( View.Type ) )
			{
				// シェーダーの命令。描けなかった命令だけを飛ばして、続きを描く。
				if ( ShaderExecutor.Execute( View, bAlphaBlend ) != CShaderCommandExecutor::EResult::Executed )
				{
					Unsupported( View, "The shader command was rejected (no shader, an unsupported shader or a failed draw) and skipped" );
				}
				if ( View.Type == EDrawCommand::ShaderOutputTarget && !ShaderExecutor.HasOutputTargets() )
				{
					LeaveMrt(); // 出力を外したら、1つの出力のパスへ戻る（そのあとの描画・読み出しのため）。
				}
				break;
			}
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
	// 戻し忘れた描画先を、主画面まで戻す。
	while ( !TargetStack.IsEmpty() && Canvas )
	{
		while ( ClipDepth > 0 )
		{
			Canvas->PopClip();
			--ClipDepth;
		}
		Surface.EndRenderTargetPass();
		bMrtActive = false;
		const FTargetScope Scope = TargetStack.Pop(); // 替える前の状態。
		if ( Scope.bShadow )
		{
			Geometry.EndShadowCasting();
			bShadowPassActive = false;
		}
		CurrentTarget = Scope.Target;
		ClipDepth = Scope.ClipDepth;
		EnterTarget( CurrentTarget );
	}
	if ( !Canvas )
	{
		return;
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

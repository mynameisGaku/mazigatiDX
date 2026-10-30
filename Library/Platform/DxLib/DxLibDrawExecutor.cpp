#include "DxLibDrawExecutor.h"


#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Interfaces/Render/DrawRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialQuad.h"
#include "Library/Core/Interfaces/Render/DrawDistortedMaterialRibbon.h"
#include "Library/Core/Interfaces/Render/DrawMaterialMesh.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Math/MathConstants.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"
#include "Library/Platform/DxLib/DxLibImageOps.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibLighting.h"
#include "Library/Platform/DxLib/DxLibRenderState3D.h"
#include "Library/Platform/DxLib/DxLibRibbonMesh.h"
#include "Library/Platform/DxLib/DxLibShapes3D.h"

REGISTER_SUBSYSTEM( CDxLibDrawExecutor )

namespace
{
	DEFINE_LOG_CATEGORY_STATIC( LogDxLibDraw, Log );

	FORCEINLINE unsigned int ToDxColor( const FDrawRgb& Color )
	{
		return GetColor( Color.R, Color.G, Color.B );
	}

	FORCEINLINE VECTOR ToVector( const float32 ( &P )[3] )
	{
		return VGet( P[0], P[1], P[2] );
	}

	// FDrawMatrixData の 12 個の値（m[0..3][0..2]）から、DxLib の行列を作る（最後の列は 0, 0, 0, 1）
	MATRIX ToMatrix( const float32 ( &M )[12] )
	{
		MATRIX Matrix{};
		for ( int Row = 0; Row < 4; ++Row )
		{
			for ( int Column = 0; Column < 3; ++Column ) Matrix.m[Row][Column] = M[Row * 3 + Column];
			Matrix.m[Row][3] = Row == 3 ? 1.0f : 0.0f;
		}
		return Matrix;
	}

	// FDrawQuad3DData を、画像を貼った 2 枚の三角形として描く。Camera / CameraY は、いまのカメラから向きを決める
	void DrawQuad3D( const FDrawQuad3DData& Data )
	{
		if ( Data.Handle < 0 ) return;

		const VECTOR Position = ToVector( Data.Pos );
		VECTOR Right = ToVector( Data.Right );
		VECTOR Up = ToVector( Data.Up );
		switch ( static_cast<EDrawQuadOrient>( Data.Orient ) )
		{
		case EDrawQuadOrient::Fixed:
			break;
		case EDrawQuadOrient::CameraY:
		{
			// カメラ -> 物の向き（水平だけ）。真上・真下から見ているときは、カメラの横向きを使う
			const VECTOR CameraPosition = GetCameraPosition();
			VECTOR Forward = VGet( Position.x - CameraPosition.x, 0.0f, Position.z - CameraPosition.z );
			Up = VGet( 0.0f, 1.0f, 0.0f );
			if ( VSquareSize( Forward ) < 1.0e-8f ) Right = GetCameraRightVector();
			else
			{
				Forward = VNorm( Forward );
				Right = VCross( Up, Forward );
			}
			break;
		}
		case EDrawQuadOrient::Camera:
		default:
		{
			const VECTOR CameraRight = GetCameraRightVector();
			const VECTOR CameraUp = GetCameraUpVector();
			const float Cos = NMath::Cos( Data.Angle );
			const float Sin = NMath::Sin( Data.Angle );
			// Angle だけ回す。正の向きは、オブジェクトを視線（Forward）の軸まわりに正の Roll で回したときと同じ（右向きが、上へ傾く）
			Right = VAdd( VScale( CameraRight, Cos ), VScale( CameraUp, Sin ) );
			Up = VAdd( VScale( CameraUp, Cos ), VScale( CameraRight, -Sin ) );
			break;
		}
		}

		// 四角形の四隅（画像の左上・右上・左下・右下）。基準点が Pos に来る
		const float Left = -Data.PivotX * Data.Width;
		const float RightEdge = ( 1.0f - Data.PivotX ) * Data.Width;
		const float Top = Data.PivotY * Data.Height;
		const float Bottom = -( 1.0f - Data.PivotY ) * Data.Height;
		const auto Corner = [&]( float A, float B ) { return VAdd( Position, VAdd( VScale( Right, A ), VScale( Up, B ) ) ); };

		const VECTOR Normal = VScale( VNorm( VCross( Right, Up ) ), -1.0f );   // 見ている人の側
		const COLOR_U8 Diffuse = GetColorU8( Data.Color.R, Data.Color.G, Data.Color.B, Data.Color.A );
		const COLOR_U8 Specular = GetColorU8( 0, 0, 0, 0 );
		const auto MakeVertex = [&]( const VECTOR& P, float U, float V )
		{
			VERTEX3D Vertex{};
			Vertex.pos = P;
			Vertex.norm = Normal;
			Vertex.dif = Diffuse;
			Vertex.spc = Specular;
			Vertex.u = U;
			Vertex.v = V;
			return Vertex;
		};
		const VERTEX3D TopLeft = MakeVertex( Corner( Left, Top ), Data.U0, Data.V0 );
		const VERTEX3D TopRight = MakeVertex( Corner( RightEdge, Top ), Data.U1, Data.V0 );
		const VERTEX3D BottomLeft = MakeVertex( Corner( Left, Bottom ), Data.U0, Data.V1 );
		const VERTEX3D BottomRight = MakeVertex( Corner( RightEdge, Bottom ), Data.U1, Data.V1 );
		const VERTEX3D Vertices[6] = { TopLeft, TopRight, BottomLeft, TopRight, BottomRight, BottomLeft };

		// 両面を描く。色の不透明度が効くよう、ブレンドしていなければ、この 1 枚の間だけアルファブレンドにする
		const int OldCulling = GetUseBackCulling();
		int OldBlendMode = DX_BLENDMODE_NOBLEND;
		int OldBlendParam = 0;
		GetDrawBlendMode( &OldBlendMode, &OldBlendParam );
		SetUseBackCulling( DX_CULLING_NONE );
		if ( Data.Color.A < 255 && OldBlendMode == DX_BLENDMODE_NOBLEND ) SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 );
		DrawPolygon3D( Vertices, 2, Data.Handle, TRUE );
		SetDrawBlendMode( OldBlendMode, OldBlendParam );
		SetUseBackCulling( OldCulling );
	}

	// 頂点バッファの三角形を、行列をセットして描く。描いたあとに、行列（WorldMatrix で決めたもの）とカリングを元に戻す。色は、頂点の色
	void DrawMesh3D( const FDrawMeshData& Data, bool bTwoSided )
	{
		if ( Data.VertexBuffer < 0 || Data.IndexBuffer < 0 || Data.NumIndices < 3 || Data.NumVertices < 3 ) return;
		const MATRIX Matrix = ToMatrix( Data.M );
		MATRIX OldMatrix;
		GetTransformToWorldMatrix( &OldMatrix );
		SetTransformToWorld( &Matrix );

		const int OldCulling = GetUseBackCulling();
		if ( bTwoSided ) SetUseBackCulling( DX_CULLING_NONE );
		// 画像なしは、DX_NONE_GRAPH（-1 ではない。-1 だと何も描かれない）
		const int Graph = Data.Texture >= 0 ? Data.Texture : DX_NONE_GRAPH;
		DrawPrimitiveIndexed3D_UseVertexBuffer2( Data.VertexBuffer, Data.IndexBuffer, DX_PRIMTYPE_TRIANGLELIST, 0, 0, Data.NumVertices, Data.StartIndex, Data.NumIndices, Graph, FALSE );
		SetUseBackCulling( OldCulling );
		SetTransformToWorld( &OldMatrix );
	}

	// 旧材質なしRibbonの命令を検査し、共有mesh変換後にDxLibで描く。
	void DrawRibbon3D( const FDrawCommandView& Command )
	{
		constexpr int32 DataWords = sizeof( FDrawRibbonData ) / 4;
		if ( Command.NumDataWords < DataWords ) return;
		const FDrawRibbonData Data = Command.Read<FDrawRibbonData>();
		if ( Data.NumPoints < 2 || Data.NumPoints > FDrawRibbonData::MaxPoints || Data.Texture < -1 || !NMath::IsFinite( Data.UVRepeat ) || Data.UVRepeat <= 0.0f || Data.JoinFlags < 0 || ( Data.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || Data.NumPoints - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Data.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 ) return;
		if ( Command.NumDataWords != DataWords + Data.NumPoints * static_cast<int32>( sizeof( FDrawRibbonData::FPoint ) / 4 ) ) return;
		TArray<FDrawRibbonData::FPoint> Points;
		Points.SetNum( Data.NumPoints );
		FMemory::Memcpy( Points.GetData(), Command.Data + DataWords, static_cast<size_t>( Data.NumPoints ) * sizeof( FDrawRibbonData::FPoint ) );
		for ( int32 Index = 0; Index < Data.NumPoints; ++Index )
		{
			const FDrawRibbonData::FPoint& Point = Points[Index];
			if ( !NMath::IsFinite( Point.Pos[0] ) || !NMath::IsFinite( Point.Pos[1] ) || !NMath::IsFinite( Point.Pos[2] ) || !NMath::IsFinite( Point.Width ) || Point.Width < 0.0f || !NMath::IsFinite( Point.U ) || Point.U < 0.0f || !NMath::IsFinite( Point.U * Data.UVRepeat ) || ( Index > 0 && Point.U < Points[Index - 1].U ) ) return;
		}
		FDxLibRibbonMesh Mesh;
		if ( !NDxLibRibbonMesh::Build( Data, Points.GetData(), true, Mesh ) ) return;
		const int32 OldCulling = GetUseBackCulling();
		int32 OldBlendMode = DX_BLENDMODE_NOBLEND;
		int32 OldBlendParam = 0;
		GetDrawBlendMode( &OldBlendMode, &OldBlendParam );
		MATRIX OldWorld = {};
		GetTransformToWorldMatrix( &OldWorld );
		const MATRIX Identity = MGetIdent();
		SetTransformToWorld( &Identity );
		SetUseBackCulling( DX_CULLING_NONE );
		if ( OldBlendMode == DX_BLENDMODE_NOBLEND ) SetDrawBlendMode( DX_BLENDMODE_ALPHA, 255 );
		DrawPolygonIndexed3D( Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num() / 3, Data.Texture >= 0 ? Data.Texture : DX_NONE_GRAPH, TRUE );
		SetDrawBlendMode( OldBlendMode, OldBlendParam );
		SetUseBackCulling( OldCulling );
		SetTransformToWorld( &OldWorld );
	}
	int ToDxBlendMode( int32 Mode )
	{
		switch ( static_cast<EDrawBlend>( Mode ) )
		{
		case EDrawBlend::Alpha: return DX_BLENDMODE_ALPHA;
		case EDrawBlend::Add: return DX_BLENDMODE_ADD;
		case EDrawBlend::Subtract: return DX_BLENDMODE_SUB;
		case EDrawBlend::Multiply: return DX_BLENDMODE_MUL;
		case EDrawBlend::Invert: return DX_BLENDMODE_INVSRC;
		case EDrawBlend::MultiplyAlpha: return DX_BLENDMODE_MULA;
		case EDrawBlend::PmaAlpha: return DX_BLENDMODE_PMA_ALPHA;
		case EDrawBlend::PmaAdd: return DX_BLENDMODE_PMA_ADD;
		case EDrawBlend::PmaSubtract: return DX_BLENDMODE_PMA_SUB;
		case EDrawBlend::PmaInvert: return DX_BLENDMODE_PMA_INVSRC;
		default: return DX_BLENDMODE_NOBLEND;
		}
	}

	// カスタムブレンドの係数
	int ToDxBlendFactor( int32 Factor )
	{
		switch ( static_cast<EDrawBlendFactor>( Factor ) )
		{
		case EDrawBlendFactor::Zero: return DX_BLEND_ZERO;
		case EDrawBlendFactor::One: return DX_BLEND_ONE;
		case EDrawBlendFactor::SrcColor: return DX_BLEND_SRC_COLOR;
		case EDrawBlendFactor::InvSrcColor: return DX_BLEND_INV_SRC_COLOR;
		case EDrawBlendFactor::SrcAlpha: return DX_BLEND_SRC_ALPHA;
		case EDrawBlendFactor::InvSrcAlpha: return DX_BLEND_INV_SRC_ALPHA;
		case EDrawBlendFactor::DestColor: return DX_BLEND_DEST_COLOR;
		case EDrawBlendFactor::InvDestColor: return DX_BLEND_INV_DEST_COLOR;
		case EDrawBlendFactor::DestAlpha: return DX_BLEND_DEST_ALPHA;
		case EDrawBlendFactor::InvDestAlpha: return DX_BLEND_INV_DEST_ALPHA;
		case EDrawBlendFactor::SrcAlphaSat: return DX_BLEND_SRC_ALPHA_SAT;
		default: return DX_BLEND_ONE;
		}
	}

	// カスタムブレンドの組み合わせ方
	int ToDxBlendOp( int32 Op )
	{
		switch ( static_cast<EDrawBlendOp>( Op ) )
		{
		case EDrawBlendOp::Subtract: return DX_BLENDOP_SUBTRACT;
		case EDrawBlendOp::ReverseSubtract: return DX_BLENDOP_REV_SUBTRACT;
		case EDrawBlendOp::Min: return DX_BLENDOP_MIX;
		case EDrawBlendOp::Max: return DX_BLENDOP_MAX;
		default: return DX_BLENDOP_ADD;
		}
	}

	// 画像を拡大・縮小・回転して描くときの色の決め方
	int ToDxFilter( int32 Filter )
	{
		switch ( static_cast<EDrawFilter>( Filter ) )
		{
		case EDrawFilter::Bilinear: return DX_DRAWMODE_BILINEAR;
		case EDrawFilter::Anisotropic: return DX_DRAWMODE_ANISOTROPIC;
		default: return DX_DRAWMODE_NEAREST;
		}
	}

	// 描画の状態だけを変える命令か（何かを描く命令ではない）。クリップ範囲が空のあいだ、描く命令だけを飛ばすために使う
	bool IsStateCommand( EDrawCommand Type )
	{
		switch ( Type )
		{
		case EDrawCommand::Blend:
		case EDrawCommand::Bright:
		case EDrawCommand::WorldMatrix:
		case EDrawCommand::ModelAnim:
		case EDrawCommand::CustomBlend:
		case EDrawCommand::AddColor:
		case EDrawCommand::AlphaTest:
		case EDrawCommand::Filter:
		case EDrawCommand::PushClip:
		case EDrawCommand::PopClip:
		case EDrawCommand::Transform2D:
		case EDrawCommand::PushTarget:
		case EDrawCommand::PopTarget:
		case EDrawCommand::PushMask:
		case EDrawCommand::PopMask:
		case EDrawCommand::PushCamera:
		case EDrawCommand::PopCamera:
		case EDrawCommand::PushWorldMatrix:
		case EDrawCommand::PopWorldMatrix:
			return true;
		default:
			return FDxLibRenderState3D::IsStateCommand( Type ) || FDxLibLighting::IsStateCommand( Type );
		}
	}

	// 渡された画像だけを扱う命令か（いまの描画先・クリップには関係ないので、クリップ範囲が空のときも、描画先にできない画像へ替えたときも、実行する）
	bool IsImageOperation( EDrawCommand Type )
	{
		return Type == EDrawCommand::ImageFilter || Type == EDrawCommand::ImageBlend;
	}

	// 描画先の全体にかかわる命令か（クリップ範囲に関係なく、実行する）
	bool IsWholeTargetCommand( EDrawCommand Type )
	{
		return Type == EDrawCommand::Clear || Type == EDrawCommand::CopyScreen || Type == EDrawCommand::ClearDepth;
	}

	// PushMask で重ねたマスク 1 つ分
	struct FMaskState
	{
		int Handle = -1;
		int Channel = 0;
		bool bInvert = false;
	};

	// PushCamera で替える前のカメラ（PopCamera で戻す。DxLib から取得できる値）
	struct FCameraFrame
	{
		float Near = 0.0f;
		float Far = 0.0f;
		MATRIX View = {};
		MATRIX Projection = {};
		float CenterX = 0.0f;
		float CenterY = 0.0f;
		float DotAspect = 1.0f;
	};

	// PushTarget で描画先を替える前の状態（PopTarget で戻す）
	struct FTargetFrame
	{
		int PreviousScreen = 0;
		// 替える前の、マスクの並び（替えた先は、マスクなしから始まる）
		TArray<FMaskState> MaskStack;
		RECT PreviousArea = {};
		// 替える前の、クリップの状態
		bool bClipChanged = false;
		RECT OldArea = {};
		TArray<RECT> ClipStack;
		bool bClipEmpty = false;
		// 替える前の、2D の変換（DxLib に取得関数がないので、実行のなかで覚えている）
		bool bTransform2DChanged = false;
		MATRIX Transform2D = {};
		// 替える前の、カメラの光軸が通る画面の点（替えると、替えた先の中心に変わるので、戻すときに戻す）
		float CameraCenterX = 0.0f;
		float CameraCenterY = 0.0f;
	};

	// 2 つの範囲の共通部分（右と下の端は含まない）。重ならなければ、空の範囲（右 <= 左 または 下 <= 上）になる
	RECT IntersectArea( const RECT& A, const RECT& B )
	{
		RECT Result;
		Result.left = NMath::Max( A.left, B.left );
		Result.top = NMath::Max( A.top, B.top );
		Result.right = NMath::Min( A.right, B.right );
		Result.bottom = NMath::Min( A.bottom, B.bottom );
		return Result;
	}

	// 範囲が空か
	bool IsEmptyArea( const RECT& Area )
	{
		return Area.right <= Area.left || Area.bottom <= Area.top;
	}

	// 円・楕円・角の丸みを近似する角数。指定が無ければ（0）、半径（ピクセル）から、折れ線と円弧の誤差が 1/4 ピクセル以下になる数を 4 の倍数で決める（12..256）
	int ResolveSegments( int32 NumSegments, float32 Radius )
	{
		if ( NumSegments > 0 )
		{
			return NumSegments;
		}
		if ( Radius <= 0.25f )
		{
			return 12;
		}
		const float32 HalfAngle = NMath::Acos( 1.0f - 0.25f / Radius );   // 1 辺が張る中心角の半分
		const int32 Count = HalfAngle > 0.0f ? static_cast<int32>( NMath::Ceil( NMath::PI / HalfAngle ) ) : 256;
		return NMath::Clamp( ( Count + 3 ) / 4 * 4, 12, 256 );
	}

	// なめらかでない描き方では、float の座標を四捨五入して int にする（記録のとき、絶対値 1e7 以下に絞ってあるので溢れない）
	FORCEINLINE int ToPixel( float32 Value )
	{
		return NMath::RoundToInt( Value );
	}
}

void CDxLibDrawExecutor::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	Collection.InitializeDependency<IPlatform>();
	// asset managerと同じ寿命のplatform geometry backendを確保する。
	AssetBackend = Collection.InitializeDependency<CDxLibAssetBackend>();
	// platformが有効な間にnative材質描画資源を作る。失敗時は能力がfalseのまま。
	EffectMaterialRenderer.Initialize();
}

// DxLib終了前にnative shaderとbufferなどの所有資源を解放する。
void CDxLibDrawExecutor::Deinitialize()
{
	EffectMaterialRenderer.Deinitialize();
	AssetBackend = nullptr;
	if ( bMaskScreenCreated )
	{
		DeleteMaskScreen();
		bMaskScreenCreated = false;
	}
}

// DxLib のマスクは、マスクスクリーンを作ってからでないと使えない。初めて使うときに 1 度だけ作る
bool CDxLibDrawExecutor::EnsureMaskScreen()
{
	if ( !bMaskScreenCreated && CreateMaskScreen() == 0 )
	{
		bMaskScreenCreated = true;
	}
	return bMaskScreenCreated;
}

// 現在のDirect3D device上で材質描画用shaderが準備済みか返す。
bool CDxLibDrawExecutor::SupportsEffectMaterials() const
{
	return EffectMaterialRenderer.IsAvailable();
}

bool CDxLibDrawExecutor::SupportsEffectMaterialRibbons() const
{
	return EffectMaterialRenderer.IsRibbonAvailable();
}

bool CDxLibDrawExecutor::SupportsEffectUVDistortion() const
{
	return EffectMaterialRenderer.IsUVDistortionAvailable();
}

bool CDxLibDrawExecutor::SupportsEffectRibbonUVDistortion() const
{
	return EffectMaterialRenderer.IsRibbonUVDistortionAvailable();
}

bool CDxLibDrawExecutor::SupportsEffectMaterialMesh() const
{
	return EffectMaterialRenderer.IsMeshAvailable();
}

bool CDxLibDrawExecutor::SupportsModelMaterialEffects() const
{
	return AssetBackend != nullptr && EffectMaterialRenderer.IsMeshAvailable();
}

void CDxLibDrawExecutor::Execute( const FDrawSpan& Span )
{
	// このオブジェクトの分で変えた状態。終わりで、元に戻す（変える前の値は、最初に変えるときに覚える）
	bool bBlendChanged = false;
	bool bWorldMatrixChanged = false;
	// 3D 図形の変換: 実行のなかで覚えている、いまの変換（始めは変換なし）と、PushWorldMatrix で積む前の変換の並び
	MATRIX WorldMatrix = MGetIdent();
	TArray<MATRIX> WorldMatrixStack;
	// 3D の描画状態（深度バッファ・カリング・画像の貼り方・フォグ・異方性）。変えた分を、終わりに戻す
	FDxLibRenderState3D RenderState3D;
	// ライティング（光の計算・全体の環境光・材質・ライト）。変えた分を、終わりに戻す
	FDxLibLighting Lighting;
	bool bBrightChanged = false;
	bool bAddColorChanged = false;
	int OldAddColor[3] = { 0, 0, 0 };
	bool bAlphaTestChanged = false;
	int OldAlphaTestMode = -1;
	int OldAlphaTestParam = 0;
	bool bFilterChanged = false;
	int OldFilter = DX_DRAWMODE_NEAREST;
	bool bTransform2DChanged = false;
	// クリップ: 始める前の範囲と、PushClip で重ねた範囲の並び（一番内側が、いまの範囲）。いまの範囲が空のあいだは、描く命令を飛ばす
	bool bClipChanged = false;
	RECT OldArea = {};
	TArray<RECT> ClipStack;
	bool bClipEmpty = false;
	// 実行のなかで覚えている、いまの 2D の変換（Transform2D で決めたもの）
	MATRIX Transform2DMatrix = MGetIdent();
	// 描画先: PushTarget で替える前の状態の並び（一番内側が、いまの描画先に替える前のもの）と、替えられなかった PushTarget の数
	// （描画先にできない画像を渡された。その対の PopTarget まで、描く命令を飛ばす。飛ばさないと、元の描画先に描いてしまう）
	TArray<FTargetFrame> TargetStack;
	int NumInvalidTargets = 0;
	// マスク: PushMask で重ねた並び（一番内側が、いまのマスク）。マスクの設定を、実行のなかで 1 度でも変えたか。変える前の、マスクを使う設定
	TArray<FMaskState> MaskStack;
	bool bMaskChanged = false;
	int OldUseMask = FALSE;

	// マスクの並びの一番内側を DxLib に反映する（無ければ、マスクなしに）。DxLib のマスクは、描画先とは別の、全体で 1 つの設定
	const auto ApplyMask = [&]()
	{
		if ( !bMaskChanged )
		{
			// 実行の前の値を覚えて、終わりに戻す。DxLib は、何も設定していないときも TRUE と答える（マスクスクリーンも、マスクの画像も無いあいだは、効かない）
			OldUseMask = GetUseMaskScreenFlag() == FALSE ? FALSE : TRUE;
			bMaskChanged = true;
		}
		if ( !MaskStack.IsEmpty() && EnsureMaskScreen() )
		{
			const FMaskState& Mask = MaskStack.Last();
			// マスクにできる画像（アルファを持つ描画先）でないと、DxLib が断る。そのときは、マスクなしで描く
			if ( SetMaskScreenGraph( Mask.Handle ) == 0 )
			{
				SetMaskScreenGraphUseChannel( NDxLibImageOps::ToDxMaskChannel( Mask.Channel ) );
				// DxLib の既定は、マスクの値が 0 のところが描かれる向き。こちらの既定は、値が大きいところほど濃く描かれる向きなので、逆にする
				SetMaskReverseEffectFlag( Mask.bInvert ? FALSE : TRUE );
				SetUseMaskScreenFlag( TRUE );
				return;
			}
		}
		SetUseMaskScreenFlag( FALSE );
		SetMaskScreenGraph( -1 );
	};

	// カメラ: PushCamera で替える前の状態の並び（一番内側が、いまのカメラに替える前のもの）
	TArray<FCameraFrame> CameraStack;

	// カメラを、いまのカメラの値で上書きする（実行のときの描画先の大きさで、光軸が通る画面の点を決める）
	// DxLib は、3D の描画を、頂点バッファに溜めるが、カメラを替えるときに、溜まった分を、替える前のカメラで描き出す（箱・線・球で確かめた）ので、先に描き出す必要はない
	const auto ApplyCamera = [&]( const FDrawCameraData& Data )
	{
		// 描画先の大きさ。描画先が画像（MakeScreen で作った描画先）なら、その画像の大きさ。画面（バックバッファ）なら、画面の大きさ
		// （GetDrawScreenSize は、描画先が画像のときも、画面の大きさを返す）
		int ScreenWidth = 0;
		int ScreenHeight = 0;
		if ( GetGraphSize( GetDrawScreen(), &ScreenWidth, &ScreenHeight ) != 0 )
		{
			GetDrawScreenSize( &ScreenWidth, &ScreenHeight );
		}
		SetCameraNearFar( Data.Near, Data.Far );
		SetCameraDotAspect( Data.HorizontalStretch );
		if ( Data.Orthographic != 0 )
		{
			SetupCamera_Ortho( Data.OrthoHeight );
		}
		else
		{
			SetupCamera_Perspective( Data.FovY );
		}
		SetCameraPositionAndTargetAndUpVec( VGet( Data.Position[0], Data.Position[1], Data.Position[2] ), VGet( Data.Target[0], Data.Target[1], Data.Target[2] ), VGet( Data.Up[0], Data.Up[1], Data.Up[2] ) );
		SetCameraScreenCenter( static_cast<float>( ScreenWidth ) * 0.5f + Data.ScreenOffsetX, static_cast<float>( ScreenHeight ) * 0.5f + Data.ScreenOffsetY );
	};

	// 1 つ前のカメラに戻す（ビュー行列と射影行列で、そのまま戻す）
	const auto RestoreCamera = [&]()
	{
		const FCameraFrame Frame = CameraStack.Pop();
		SetCameraNearFar( Frame.Near, Frame.Far );
		SetCameraDotAspect( Frame.DotAspect );
		SetCameraViewMatrix( Frame.View );
		SetupCamera_ProjectionMatrix( Frame.Projection );
		SetCameraScreenCenter( Frame.CenterX, Frame.CenterY );
	};

	// 1 つ前の描画先に戻す。カメラは、替えても変わらないよう、「替えるとリセットする設定」を一時的に切って替える
	const auto RestoreTarget = [&]()
	{
		const FTargetFrame Frame = TargetStack.Pop();
		const int PreviousReset = GetUseSetDrawScreenSettingReset();
		SetUseSetDrawScreenSettingReset( FALSE );
		SetDrawScreen( Frame.PreviousScreen );
		SetUseSetDrawScreenSettingReset( PreviousReset );
		SetCameraScreenCenter( Frame.CameraCenterX, Frame.CameraCenterY );
		SetDrawArea( Frame.PreviousArea.left, Frame.PreviousArea.top, Frame.PreviousArea.right, Frame.PreviousArea.bottom );
		bClipChanged = Frame.bClipChanged;
		OldArea = Frame.OldArea;
		ClipStack = Frame.ClipStack;
		bClipEmpty = Frame.bClipEmpty;
		if ( Frame.bTransform2DChanged )
		{
			SetTransformTo2D( &Frame.Transform2D );
		}
		else
		{
			ResetTransformTo2D();
		}
		bTransform2DChanged = Frame.bTransform2DChanged;
		Transform2DMatrix = Frame.Transform2D;
		MaskStack = Frame.MaskStack;
		if ( bMaskChanged )
		{
			ApplyMask();
		}
	};

	ForEachDrawCommand( Span, [&]( const FDrawCommandView& Command )
	{
		const int Fill = Command.Has( FDrawFlag::Fill ) ? TRUE : FALSE;

		// 状態の命令（と描画先を替える命令・画像の処理）以外は、クリップ範囲が空のあいだ（描画先の全体にかかわる命令を除く）と、描画先にできない画像へ替えたあいだは、飛ばす
		if ( !IsStateCommand( Command.Type ) && !IsImageOperation( Command.Type ) && ( NumInvalidTargets > 0 || ( bClipEmpty && !IsWholeTargetCommand( Command.Type ) ) ) )
		{
			return;
		}

		// 3D の描画状態の命令
		if ( RenderState3D.Apply( Command ) )
		{
			return;
		}

		// ライティングの命令
		if ( Lighting.Apply( Command ) )
		{
			return;
		}

		switch ( Command.Type )
		{
		case EDrawCommand::Box:
		{
			const FDrawBoxData Data = Command.Read<FDrawBoxData>();
			DrawBox( Data.X0, Data.Y0, Data.X1, Data.Y1, ToDxColor( Data.Color ), Fill, Data.Thickness );
			break;
		}
		case EDrawCommand::Circle:
		{
			const FDrawCircleData Data = Command.Read<FDrawCircleData>();
			DrawCircle( Data.X, Data.Y, Data.Radius, ToDxColor( Data.Color ), Fill, Data.Thickness );
			break;
		}
		case EDrawCommand::Line:
		{
			const FDrawLineData Data = Command.Read<FDrawLineData>();
			DrawLine( Data.X0, Data.Y0, Data.X1, Data.Y1, ToDxColor( Data.Color ), Data.Thickness );
			break;
		}
		case EDrawCommand::Text:
		{
			const FDrawTextData Data = Command.Read<FDrawTextData>();
			DrawString( Data.X, Data.Y, Command.GetTextChars(), ToDxColor( Data.Color ) );
			break;
		}
		case EDrawCommand::Sprite:
		{
			const FDrawSpriteData Data = Command.Read<FDrawSpriteData>();
			DrawRotaGraphF( Data.X, Data.Y, Data.Scale, Data.Angle, Data.Handle, TRUE, Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE, Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE );
			break;
		}
		case EDrawCommand::Blend:
		{
			const FDrawBlendData Data = Command.Read<FDrawBlendData>();
			SetDrawBlendMode( ToDxBlendMode( Data.Mode ), Data.Param );
			bBlendChanged = static_cast<EDrawBlend>( Data.Mode ) != EDrawBlend::Opaque;
			break;
		}
		case EDrawCommand::Line3D:
		{
			const FDrawLine3DData Data = Command.Read<FDrawLine3DData>();
			DrawLine3D( ToVector( Data.P0 ), ToVector( Data.P1 ), ToDxColor( Data.Color ) );
			break;
		}
		case EDrawCommand::Triangle3D:
		{
			const FDrawTriangle3DData Data = Command.Read<FDrawTriangle3DData>();
			DrawTriangle3D( ToVector( Data.P0 ), ToVector( Data.P1 ), ToVector( Data.P2 ), ToDxColor( Data.Color ), Fill );
			break;
		}
		case EDrawCommand::Sphere3D:
		{
			const FDrawSphere3DData Data = Command.Read<FDrawSphere3DData>();
			DrawSphere3D( ToVector( Data.Center ), Data.Radius, Data.Div, ToDxColor( Data.Diffuse ), ToDxColor( Data.Specular ), Fill );
			break;
		}
		case EDrawCommand::Capsule3D:
		{
			const FDrawCapsule3DData Data = Command.Read<FDrawCapsule3DData>();
			DrawCapsule3D( ToVector( Data.P0 ), ToVector( Data.P1 ), Data.Radius, Data.Div, ToDxColor( Data.Diffuse ), ToDxColor( Data.Specular ), Fill );
			break;
		}
		case EDrawCommand::Cube3D:
		{
			const FDrawCube3DData Data = Command.Read<FDrawCube3DData>();
			DrawCube3D( ToVector( Data.Min ), ToVector( Data.Max ), ToDxColor( Data.Diffuse ), ToDxColor( Data.Specular ), Fill );
			break;
		}
		case EDrawCommand::Billboard3D:
		{
			const FDrawBillboard3DData Data = Command.Read<FDrawBillboard3DData>();
			DrawBillboard3D( ToVector( Data.Pos ), 0.5f, 0.5f, Data.Size, Data.Angle, Data.Handle, TRUE, Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE, Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE );
			break;
		}
		case EDrawCommand::WorldMatrix:
		{
			const FDrawMatrixData Data = Command.Read<FDrawMatrixData>();
			WorldMatrix = ToMatrix( Data.M );
			SetTransformToWorld( &WorldMatrix );
			bWorldMatrixChanged = true;
			break;
		}
		case EDrawCommand::PushWorldMatrix:
		{
			if ( !Command.CanRead<FDrawMatrixData>() )
			{
				break;
			}
			// 自分の座標 -> 親の座標（いまの変換）の順に掛ける（行ベクトルの流儀: 先に掛けたものが先に効く）
			WorldMatrixStack.Add( WorldMatrix );
			WorldMatrix = MMult( ToMatrix( Command.Read<FDrawMatrixData>().M ), WorldMatrix );
			SetTransformToWorld( &WorldMatrix );
			bWorldMatrixChanged = true;
			break;
		}
		case EDrawCommand::PopWorldMatrix:
		{
			if ( WorldMatrixStack.IsEmpty() )
			{
				break;
			}
			WorldMatrix = WorldMatrixStack.Pop();
			SetTransformToWorld( &WorldMatrix );
			break;
		}
		case EDrawCommand::ClearDepth:
		{
			// 溜まった 3D を、消す前の深度で先に描き出す（DxLib は、深度バッファを消すときには描き出さない）。範囲の指定は DxLib が無視するので、全体を消す
			RenderVertex();
			ClearDrawScreenZBuffer();
			break;
		}
		case EDrawCommand::Model3D:
		{
			const FDrawModelData Data = Command.Read<FDrawModelData>();
			MV1SetMatrix( Data.Handle, ToMatrix( Data.M ) );
			if ( Data.AnimIndex >= 0 ) MV1SetAttachAnimTime( Data.Handle, Data.AnimIndex, Data.AnimTime );
			MV1DrawModel( Data.Handle );
			break;
		}
		case EDrawCommand::ModelMaterial3D:
		{
			ExecuteModelMaterial( Command );
			break;
		}
		case EDrawCommand::SpriteRect:
		{
			const FDrawSpriteRectData Data = Command.Read<FDrawSpriteRectData>();
			// DxLib の反転は、画像の中だけを反転して、基準点の位置は元のまま。「反転した画像の、同じ点」を基準点にするため、
			// 反転する向きでは基準点を矩形の中で反対側に移す（基準点が表示位置に残り、基準点を軸に反転したように見える）
			const float32 PivotX = Command.Has( FDrawFlag::FlipX ) ? static_cast<float32>( Data.SrcW ) - Data.PivotX : Data.PivotX;
			const float32 PivotY = Command.Has( FDrawFlag::FlipY ) ? static_cast<float32>( Data.SrcH ) - Data.PivotY : Data.PivotY;
			DrawRectRotaGraph3F( Data.X, Data.Y, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, PivotX, PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, Data.Handle, TRUE,
								 Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE, Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE );
			break;
		}
		case EDrawCommand::Bright:
		{
			const FDrawBrightData Data = Command.Read<FDrawBrightData>();
			SetDrawBright( Data.Color.R, Data.Color.G, Data.Color.B );
			bBrightChanged = Data.Color.R != 255 || Data.Color.G != 255 || Data.Color.B != 255;
			break;
		}
		case EDrawCommand::TextFont:
		{
			const FDrawTextFontData Data = Command.Read<FDrawTextFontData>();
			const char* Chars = Command.GetFontTextChars();
			int X = Data.X;
			if ( Command.Has( FDrawFlag::AlignCenter ) || Command.Has( FDrawFlag::AlignRight ) )
			{
				const int Width = Data.Font >= 0 ? GetDrawStringWidthToHandle( Chars, Data.Length, Data.Font ) : GetDrawStringWidth( Chars, Data.Length );
				X -= Command.Has( FDrawFlag::AlignCenter ) ? Width / 2 : Width;
			}
			if ( Data.Font >= 0 ) DrawStringToHandle( X, Data.Y, Chars, ToDxColor( Data.Color ), Data.Font, ToDxColor( Data.EdgeColor ) );
			else DrawString( X, Data.Y, Chars, ToDxColor( Data.Color ) );
			break;
		}
		case EDrawCommand::TextStyled:
		{
			if ( !Command.CanRead<FDrawTextStyledData>() )
			{
				break;
			}
			const FDrawTextStyledData Data = Command.Read<FDrawTextStyledData>();
			const char* Chars = Command.GetStyledTextChars();
			const int Font = Data.Font >= 0 ? Data.Font : GetDefaultFontHandle();
			const int Vertical = Data.Vertical != 0 ? TRUE : FALSE;
			// 基準点は、文字列の大きさに対する割合なので、実行のときに、大きさを測って、文字列の中の位置（ピクセル）にする
			int Width = 0;
			int Height = 0;
			int NumLines = 0;
			GetDrawStringSizeToHandle( &Width, &Height, &NumLines, Chars, Data.Length, Font, Vertical );
			float32 PivotX = Data.AnchorX * static_cast<float32>( Width );
			float32 PivotY = Data.AnchorY * static_cast<float32>( Height );
			if ( Vertical == TRUE )
			{
				// 縦書きの DxLib の基準点の座標は、書く向き（下）が x、列の並ぶ向き（左）が y（実測）。画面の向き（x が右、y が下）で受け取っているので、入れ替えて渡す
				// （拡大率は、縦書きでも、横が ExRateX、縦が ExRateY のまま）
				PivotX = Data.AnchorY * static_cast<float32>( Height );
				PivotY = Data.AnchorX * static_cast<float32>( Width );
			}
			DrawRotaStringFToHandle( Data.X, Data.Y, Data.ScaleX, Data.ScaleY, PivotX, PivotY, Data.Angle, ToDxColor( Data.Color ), Font, ToDxColor( Data.EdgeColor ), Vertical, Chars );
			break;
		}
		case EDrawCommand::ModelAnim:
		{
			const FDrawModelAnimData Data = Command.Read<FDrawModelAnimData>();
			MV1SetAttachAnimTime( Data.Handle, Data.AttachIndex, Data.Time );
			MV1SetAttachAnimBlendRate( Data.Handle, Data.AttachIndex, Data.Rate );
			break;
		}
		case EDrawCommand::Quad3D:
		{
			DrawQuad3D( Command.Read<FDrawQuad3DData>() );
			break;
		}
		case EDrawCommand::MaterialQuad3D:
		{
			// 固定長でない偽造命令を読み取らないよう、一度だけ警告する状態。
			static bool bInvalidMaterialCommandLogged = false;
			if ( Command.NumDataWords != static_cast<int32>( sizeof( FDrawMaterialQuadData ) / 4 ) )
			{
				if ( !bInvalidMaterialCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付き四角形の命令長が不正なため描画を読み飛ばしました" ); bInvalidMaterialCommandLogged = true; }
				break;
			}
			EffectMaterialRenderer.Draw( Command.Read<FDrawMaterialQuadData>() );
			break;
		}
		case EDrawCommand::DistortedMaterialQuad3D:
		{
			// 固定長でない偽造命令を読まない警告状態。
			static bool bInvalidDistortedMaterialCommandLogged = false;
			if ( Command.NumDataWords != static_cast<int32>( sizeof( FDrawDistortedMaterialQuadData ) / 4 ) )
			{
				if ( !bInvalidDistortedMaterialCommandLogged ) { LOG( LogDxLibDraw, Warning, "UVゆがみ付き材質四角形の命令長が不正なため描画を読み飛ばしました" ); bInvalidDistortedMaterialCommandLogged = true; }
				break;
			}
			EffectMaterialRenderer.DrawDistorted( Command.Read<FDrawDistortedMaterialQuadData>() );
			break;
		}
		case EDrawCommand::DistortedMaterialRibbon3D:
		{
			// ゆがみ付き材質帯の壊れたpayloadを一度だけ記録する状態。
			static bool bInvalidDistortedRibbonCommandLogged = false;
			// 固定headerに必要なword数。
			constexpr int32 DataWords = sizeof( FDrawDistortedMaterialRibbonData ) / 4;
			if ( Command.NumDataWords < DataWords )
			{
				if ( !bInvalidDistortedRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "UVゆがみ付き材質帯の命令長が不正なため描画を読み飛ばしました" ); bInvalidDistortedRibbonCommandLogged = true; }
				break;
			}
			// 点数を読む前にbyte列から固定headerを複写する。
			FDrawDistortedMaterialRibbonData Data{};
			FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
			if ( !NDrawDistortedMaterialRibbon::IsValidHeader( Data ) )
			{
				if ( !bInvalidDistortedRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "UVゆがみ付き材質帯の点数または設定値が不正なため描画を読み飛ばしました" ); bInvalidDistortedRibbonCommandLogged = true; }
				break;
			}
			// 最大点数検査後に計算する、完全なpayloadのword数。
			const size_t ExpectedDataWords = static_cast<size_t>( DataWords ) + static_cast<size_t>( Data.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4;
			if ( ExpectedDataWords > static_cast<size_t>( NDrawCommand::MaxWords - 1 ) || Command.NumDataWords != static_cast<int32>( ExpectedDataWords ) )
			{
				if ( !bInvalidDistortedRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "UVゆがみ付き材質帯の点数または命令長が不正なため描画を読み飛ばしました" ); bInvalidDistortedRibbonCommandLogged = true; }
				break;
			}
			// 検証済み長さから作る、命令が所有する点列。
			TArray<FDrawDistortedMaterialRibbonData::FPoint> Points;
			Points.SetNum( Data.Material.Ribbon.NumPoints );
			FMemory::Memcpy( Points.GetData(), Command.Data + DataWords, static_cast<size_t>( Data.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) );
			if ( !NDrawDistortedMaterialRibbon::IsValid( Data, Points.GetData() ) )
			{
				if ( !bInvalidDistortedRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "UVゆがみ付き材質帯の点または反復上限が不正なため描画を読み飛ばしました" ); bInvalidDistortedRibbonCommandLogged = true; }
				break;
			}
			if ( !EffectMaterialRenderer.IsRibbonUVDistortionAvailable() )
			{
				// 未対応描画先の診断を重複させない状態。
				static bool bUnsupportedDistortedRibbonLogged = false;
				if ( !bUnsupportedDistortedRibbonLogged ) { LOG( LogDxLibDraw, Warning, "現在の描画実装では材質帯のUVゆがみを描画できません" ); bUnsupportedDistortedRibbonLogged = true; }
				break;
			}
			EffectMaterialRenderer.DrawDistortedRibbon( Data, Points.GetData() );
			break;
		}
		case EDrawCommand::MaterialRibbon3D:
		{
			// 材質付き帯の壊れたpayloadを一度だけ記録する状態。
			static bool bInvalidMaterialRibbonCommandLogged = false;
			// 固定headerに必要なword数。
			constexpr int32 DataWords = sizeof( FDrawMaterialRibbonData ) / 4;
			if ( Command.NumDataWords < DataWords )
			{
				if ( !bInvalidMaterialRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付き帯の命令長が不正なため描画を読み飛ばしました" ); bInvalidMaterialRibbonCommandLogged = true; }
				break;
			}
			// 点を読む前にbyte列から型付き所有headerへ複写する。
			FDrawMaterialRibbonData Data{};
			FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
			// 検証に使う経路値と材質値。
			const FDrawRibbonData& Ribbon = Data.Ribbon;
			const FDrawMaterialRibbonData::FMaterialSettings& Material = Data.Material;
			if ( Ribbon.NumPoints < 2 || Ribbon.NumPoints > FDrawRibbonData::MaxPoints || Ribbon.Texture < 0 || Material.MaskTexture < -1 || !NMath::IsFinite( Ribbon.UVRepeat ) || Ribbon.UVRepeat <= 0.0f || Ribbon.JoinFlags < 0 || ( Ribbon.JoinFlags & ~( FDrawRibbonData::PreviousPoint | FDrawRibbonData::NextPoint ) ) != 0 || Ribbon.NumPoints - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) - static_cast<int32>( ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 ) < 2 || Command.NumDataWords != DataWords + Ribbon.NumPoints * static_cast<int32>( sizeof( FDrawMaterialRibbonData::FPoint ) / 4 ) )
			{
				if ( !bInvalidMaterialRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付き帯の点数または命令長が不正なため描画を読み飛ばしました" ); bInvalidMaterialRibbonCommandLogged = true; }
				break;
			}
			if ( !NMath::IsFinite( Material.UVScale[0] ) || !NMath::IsFinite( Material.UVScale[1] ) || Material.UVScale[0] <= 0.0f || Material.UVScale[1] <= 0.0f || Material.UVScale[0] > 128.0f || Material.UVScale[1] > 128.0f || !NMath::IsFinite( Material.MaskScale ) || Material.MaskScale <= 0.0f || Material.MaskScale > 128.0f || !NMath::IsFinite( Material.EdgeWidth ) || Material.EdgeWidth < 0.0f || Material.EdgeWidth > 1.0f || !NMath::IsFinite( Material.EdgeEmission ) || Material.EdgeEmission < 0.0f || Material.EdgeEmission > 32.0f || ( Material.bDissolve != 0 && Material.bDissolve != 1 ) || ( Material.DepthTest != 0 && Material.DepthTest != 1 ) || ( Material.DepthWrite != 0 && Material.DepthWrite != 1 ) )
			{
				if ( !bInvalidMaterialRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付き帯の設定値が不正なため描画を読み飛ばしました" ); bInvalidMaterialRibbonCommandLogged = true; }
				break;
			}
			// 命令payloadから作る、通常寿命を持つ所有点列。
			TArray<FDrawMaterialRibbonData::FPoint> Points;
			Points.SetNum( Ribbon.NumPoints );
			FMemory::Memcpy( Points.GetData(), Command.Data + DataWords, static_cast<size_t>( Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
			// 反復上限と点の妥当性を検査する累積区間数。
			int32 TextureSections = 0;
			// 点列全体が描画条件を満たすかを示す結果。
			bool bValidPoints = true;
			// 点の有限値と距離順を検査する番号。
			for ( int32 Index = 0; Index < Ribbon.NumPoints; ++Index )
			{
				// 今回検査する所有済み点。
				const FDrawMaterialRibbonData::FPoint& Point = Points[Index];
				// 画像反復を加えた連続横座標。
				const float32 BaseU = Point.Path.U * Ribbon.UVRepeat;
				if ( !NMath::IsFinite( Point.Path.Pos[0] ) || !NMath::IsFinite( Point.Path.Pos[1] ) || !NMath::IsFinite( Point.Path.Pos[2] ) || !NMath::IsFinite( Point.Path.Width ) || Point.Path.Width < 0.0f || !NMath::IsFinite( Point.Path.U ) || Point.Path.U < 0.0f || !NMath::IsFinite( BaseU ) || !NMath::IsFinite( BaseU * Material.UVScale[0] + Point.UVOffset[0] ) || !NMath::IsFinite( BaseU * Material.MaskScale ) || !NMath::IsFinite( Point.Dissolve ) || Point.Dissolve < 0.0f || Point.Dissolve > 1.0f || !NMath::IsFinite( Point.Emission ) || Point.Emission < 0.0f || Point.Emission > 32.0f || !NMath::IsFinite( Point.UVOffset[0] ) || !NMath::IsFinite( Point.UVOffset[1] ) || NMath::Abs( Point.UVOffset[0] ) > 1.0e6f || NMath::Abs( Point.UVOffset[1] ) > 1.0e6f || ( Index > 0 && Point.Path.U < Points[Index - 1].Path.U ) )
				{
					bValidPoints = false;
					break;
				}
				// ghost補助点を除いた表示区間だけを数える。
				const bool bVisibleSegment = Index > 0 && !( Index == 1 && ( Ribbon.JoinFlags & FDrawRibbonData::PreviousPoint ) != 0 ) && !( Index + 1 == Ribbon.NumPoints && ( Ribbon.JoinFlags & FDrawRibbonData::NextPoint ) != 0 );
				if ( bVisibleSegment )
				{
					// 始点で使う画像反復番号。
					const float64 FirstRepeat = NMath::Floor( static_cast<float64>( Points[Index - 1].Path.U * Ribbon.UVRepeat ) );
					// 終点までに必要な整数反復区間数。
					const float64 Sections = NMath::Max( 1.0, NMath::Ceil( static_cast<float64>( Point.Path.U * Ribbon.UVRepeat ) ) - FirstRepeat );
					if ( !NMath::IsFinite( Sections ) || Sections > static_cast<float64>( FDrawRibbonData::MaxTextureSections - TextureSections ) ) { bValidPoints = false; break; }
					TextureSections += static_cast<int32>( Sections );
				}
			}
			if ( !bValidPoints )
			{
				if ( !bInvalidMaterialRibbonCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付き帯の点または反復上限が不正なため描画を読み飛ばしました" ); bInvalidMaterialRibbonCommandLogged = true; }
				break;
			}
			if ( !EffectMaterialRenderer.IsRibbonAvailable() )
			{
				// 非対応描画先の診断を重複させない状態。
				static bool bUnsupportedMaterialRibbonLogged = false;
				if ( !bUnsupportedMaterialRibbonLogged ) { LOG( LogDxLibDraw, Warning, "現在の描画実装では材質付きの帯を描画できません" ); bUnsupportedMaterialRibbonLogged = true; }
				break;
			}
			EffectMaterialRenderer.DrawRibbon( Data, Points.GetData() );
			break;
		}
		case EDrawCommand::MaterialMesh3D:
		{
			// 固定headerに必要なword数。
			constexpr int32 DataWords = sizeof( FDrawMaterialMeshData ) / 4;
			// 壊れた固定header長を検出したことを記録する状態。
			static bool bInvalidMaterialMeshCommandLogged = false;
			if ( Command.NumDataWords < DataWords )
			{
				if ( !bInvalidMaterialMeshCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付きメッシュの命令headerが短いため描画を読み飛ばしました" ); bInvalidMaterialMeshCommandLogged = true; }
				break;
			}
			// byte列から型付きheaderへ安全に複写する値。
			FDrawMaterialMeshData Data{};
			FMemory::Memcpy( &Data, Command.Data, sizeof( Data ) );
			if ( !NDrawMaterialMesh::IsValidHeader( Data ) )
			{
				if ( !bInvalidMaterialMeshCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付きメッシュの頂点数または設定値が不正なため描画を読み飛ばしました" ); bInvalidMaterialMeshCommandLogged = true; }
				break;
			}
			// 上限確認後に計算する正確なpayload長。
			const size_t VertexWords = static_cast<size_t>( Data.NumVertices ) * sizeof( FMeshVertex ) / 4;
			const size_t IndexWords = static_cast<size_t>( Data.NumIndices );
			const size_t ExpectedDataWords = static_cast<size_t>( DataWords ) + VertexWords + IndexWords;
			if ( ExpectedDataWords > static_cast<size_t>( NDrawCommand::MaxWords - 1 ) || Command.NumDataWords != static_cast<int32>( ExpectedDataWords ) )
			{
				if ( !bInvalidMaterialMeshCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付きメッシュの命令長が不正なため描画を読み飛ばしました" ); bInvalidMaterialMeshCommandLogged = true; }
				break;
			}
			// byte列から寿命を持つ型付き頂点配列へ複写する。
			TArray<FMeshVertex> Vertices;
			Vertices.SetNum( Data.NumVertices );
			FMemory::Memcpy( Vertices.GetData(), Command.Data + DataWords, static_cast<size_t>( Data.NumVertices ) * sizeof( FMeshVertex ) );
			// 頂点配列直後のbyte列から所有index配列へ複写する。
			TArray<uint32> Indices;
			Indices.SetNum( Data.NumIndices );
			FMemory::Memcpy( Indices.GetData(), Command.Data + DataWords + VertexWords, static_cast<size_t>( Data.NumIndices ) * sizeof( uint32 ) );
			if ( !NDrawMaterialMesh::IsValid( Data, Vertices.GetData(), Indices.GetData() ) )
			{
				if ( !bInvalidMaterialMeshCommandLogged ) { LOG( LogDxLibDraw, Warning, "材質付きメッシュの頂点またはindexが不正なため描画を読み飛ばしました" ); bInvalidMaterialMeshCommandLogged = true; }
				break;
			}
			if ( !EffectMaterialRenderer.IsMeshAvailable() )
			{
				// 非対応描画先を一度だけ記録する状態。
				static bool bUnsupportedMaterialMeshLogged = false;
				if ( !bUnsupportedMaterialMeshLogged ) { LOG( LogDxLibDraw, Warning, "現在の描画実装では材質付きメッシュを描画できません" ); bUnsupportedMaterialMeshLogged = true; }
				break;
			}
			EffectMaterialRenderer.DrawMesh( Data, Vertices.GetData(), Indices.GetData(), Command.Has( FDrawFlag::UseRegularCulling ) );
			break;
		}
		case EDrawCommand::Mesh3D:
		{
			DrawMesh3D( Command.Read<FDrawMeshData>(), Command.Has( FDrawFlag::TwoSided ) );
			break;
		}
		case EDrawCommand::Ribbon3D:
		{
			DrawRibbon3D( Command );
			break;
		}
		case EDrawCommand::Pixel3D:
		case EDrawCommand::Cone3D:
		case EDrawCommand::BillboardPivot3D:
		case EDrawCommand::BillboardQuad3D:
		case EDrawCommand::Sprite3D:
		case EDrawCommand::SpriteStretch3D:
		case EDrawCommand::SpritePivot3D:
		case EDrawCommand::Primitive3D:
		{
			NDxLibShapes3D::Execute( Command );
			break;
		}
		case EDrawCommand::Pixel:
		{
			if ( !Command.CanRead<FDrawPixelData>() )
			{
				break;
			}
			const FDrawPixelData Data = Command.Read<FDrawPixelData>();
			DrawPixel( Data.X, Data.Y, ToDxColor( Data.Color ) );
			break;
		}
		case EDrawCommand::Triangle:
		{
			if ( !Command.CanRead<FDrawTriangleData>() )
			{
				break;
			}
			const FDrawTriangleData Data = Command.Read<FDrawTriangleData>();
			if ( Command.Has( FDrawFlag::AntiAlias ) )
			{
				DrawTriangleAA( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2, ToDxColor( Data.Color ), Fill, Data.Thickness );
			}
			else
			{
				DrawTriangle( ToPixel( Data.X0 ), ToPixel( Data.Y0 ), ToPixel( Data.X1 ), ToPixel( Data.Y1 ), ToPixel( Data.X2 ), ToPixel( Data.Y2 ), ToDxColor( Data.Color ), Fill );
			}
			break;
		}
		case EDrawCommand::Quad:
		{
			if ( !Command.CanRead<FDrawQuadData>() )
			{
				break;
			}
			const FDrawQuadData Data = Command.Read<FDrawQuadData>();
			if ( Command.Has( FDrawFlag::AntiAlias ) )
			{
				DrawQuadrangleAA( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.X2, Data.Y2, Data.X3, Data.Y3, ToDxColor( Data.Color ), Fill, Data.Thickness );
			}
			else
			{
				DrawQuadrangle( ToPixel( Data.X0 ), ToPixel( Data.Y0 ), ToPixel( Data.X1 ), ToPixel( Data.Y1 ), ToPixel( Data.X2 ), ToPixel( Data.Y2 ), ToPixel( Data.X3 ), ToPixel( Data.Y3 ), ToDxColor( Data.Color ), Fill );
			}
			break;
		}
		case EDrawCommand::Oval:
		{
			if ( !Command.CanRead<FDrawOvalData>() )
			{
				break;
			}
			const FDrawOvalData Data = Command.Read<FDrawOvalData>();
			if ( Command.Has( FDrawFlag::AntiAlias ) )
			{
				DrawOvalAA( Data.X, Data.Y, Data.Rx, Data.Ry, ResolveSegments( Data.NumSegments, NMath::Max( Data.Rx, Data.Ry ) ), ToDxColor( Data.Color ), Fill, Data.Thickness );
			}
			else
			{
				DrawOval( ToPixel( Data.X ), ToPixel( Data.Y ), ToPixel( Data.Rx ), ToPixel( Data.Ry ), ToDxColor( Data.Color ), Fill, NMath::Max( ToPixel( Data.Thickness ), 1 ) );
			}
			break;
		}
		case EDrawCommand::RoundBox:
		{
			if ( !Command.CanRead<FDrawRoundBoxData>() )
			{
				break;
			}
			const FDrawRoundBoxData Data = Command.Read<FDrawRoundBoxData>();
			if ( Command.Has( FDrawFlag::AntiAlias ) )
			{
				DrawRoundRectAA( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.Rx, Data.Ry, ResolveSegments( Data.NumSegments, NMath::Max( Data.Rx, Data.Ry ) ), ToDxColor( Data.Color ), Fill, Data.Thickness );
			}
			else
			{
				DrawRoundRect( ToPixel( Data.X0 ), ToPixel( Data.Y0 ), ToPixel( Data.X1 ), ToPixel( Data.Y1 ), ToPixel( Data.Rx ), ToPixel( Data.Ry ), ToDxColor( Data.Color ), Fill );
			}
			break;
		}
		case EDrawCommand::BoxAA:
		{
			if ( !Command.CanRead<FDrawBoxAAData>() )
			{
				break;
			}
			const FDrawBoxAAData Data = Command.Read<FDrawBoxAAData>();
			DrawBoxAA( Data.X0, Data.Y0, Data.X1, Data.Y1, ToDxColor( Data.Color ), Fill, Data.Thickness );
			break;
		}
		case EDrawCommand::CircleAA:
		{
			if ( !Command.CanRead<FDrawCircleAAData>() )
			{
				break;
			}
			const FDrawCircleAAData Data = Command.Read<FDrawCircleAAData>();
			DrawCircleAA( Data.X, Data.Y, Data.Radius, ResolveSegments( Data.NumSegments, Data.Radius ), ToDxColor( Data.Color ), Fill, Data.Thickness );
			break;
		}
		case EDrawCommand::LineAA:
		{
			if ( !Command.CanRead<FDrawLineAAData>() )
			{
				break;
			}
			const FDrawLineAAData Data = Command.Read<FDrawLineAAData>();
			DrawLineAA( Data.X0, Data.Y0, Data.X1, Data.Y1, ToDxColor( Data.Color ), Data.Thickness );
			break;
		}
		case EDrawCommand::Image:
		{
			if ( !Command.CanRead<FDrawImageData>() )
			{
				break;
			}
			const FDrawImageData Data = Command.Read<FDrawImageData>();
			const int FlipX = Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE;
			const int FlipY = Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE;
			if ( FlipX || FlipY )
			{
				DrawReverseGraphF( Data.X, Data.Y, Data.Handle, TRUE, FlipX, FlipY );
			}
			else
			{
				DrawGraphF( Data.X, Data.Y, Data.Handle, TRUE );
			}
			break;
		}
		case EDrawCommand::ImageStretch:
		{
			if ( !Command.CanRead<FDrawImageStretchData>() )
			{
				break;
			}
			const FDrawImageStretchData Data = Command.Read<FDrawImageStretchData>();
			DrawExtendGraphF( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.Handle, TRUE );
			break;
		}
		case EDrawCommand::ImageQuad:
		{
			if ( !Command.CanRead<FDrawImageQuadData>() )
			{
				break;
			}
			const FDrawImageQuadData Data = Command.Read<FDrawImageQuadData>();
			DrawModiGraphF( Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3], Data.Handle, TRUE );
			break;
		}
		case EDrawCommand::ImagePart:
		{
			if ( !Command.CanRead<FDrawImagePartData>() )
			{
				break;
			}
			const FDrawImagePartData Data = Command.Read<FDrawImagePartData>();
			DrawRectGraphF( Data.X, Data.Y, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, Data.Handle, TRUE, Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE, Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE );
			break;
		}
		case EDrawCommand::ImagePartStretch:
		{
			if ( !Command.CanRead<FDrawImagePartStretchData>() )
			{
				break;
			}
			const FDrawImagePartStretchData Data = Command.Read<FDrawImagePartStretchData>();
			DrawRectExtendGraphF( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, Data.Handle, TRUE );
			break;
		}
		case EDrawCommand::ImagePartQuad:
		{
			if ( !Command.CanRead<FDrawImagePartQuadData>() )
			{
				break;
			}
			const FDrawImagePartQuadData Data = Command.Read<FDrawImagePartQuadData>();
			DrawRectModiGraphF( Data.X[0], Data.Y[0], Data.X[1], Data.Y[1], Data.X[2], Data.Y[2], Data.X[3], Data.Y[3], Data.SrcX, Data.SrcY, Data.SrcW, Data.SrcH, Data.Handle, TRUE );
			break;
		}
		case EDrawCommand::SpritePivot:
		{
			if ( !Command.CanRead<FDrawSpritePivotData>() )
			{
				break;
			}
			const FDrawSpritePivotData Data = Command.Read<FDrawSpritePivotData>();
			// DxLib の反転は、画像の中だけを反転して、基準点の位置は元のまま。「反転した画像の、同じ点」を基準点にするため、反転する向きでは基準点を画像の中で反対側に移す
			float32 PivotX = Data.PivotX;
			float32 PivotY = Data.PivotY;
			if ( Command.Has( FDrawFlag::FlipX ) || Command.Has( FDrawFlag::FlipY ) )
			{
				float32 Width = 0.0f;
				float32 Height = 0.0f;
				GetGraphSizeF( Data.Handle, &Width, &Height );
				if ( Command.Has( FDrawFlag::FlipX ) )
				{
					PivotX = Width - PivotX;
				}
				if ( Command.Has( FDrawFlag::FlipY ) )
				{
					PivotY = Height - PivotY;
				}
			}
			DrawRotaGraph3F( Data.X, Data.Y, PivotX, PivotY, Data.ScaleX, Data.ScaleY, Data.Angle, Data.Handle, TRUE, Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE, Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE );
			break;
		}
		case EDrawCommand::Tile:
		{
			if ( !Command.CanRead<FDrawTileData>() )
			{
				break;
			}
			const FDrawTileData Data = Command.Read<FDrawTileData>();
			// DxLib の DrawTile は、拡大した画像の中心を「矩形の中心 - 拡大率 x (Tx, Ty)」に置いて並べる（矩形の左上に並びを合わせるものではない）。
			// 使う側が数えやすいよう、画像の左上を矩形の左上に合わせ、OffsetX / OffsetY を、右下向きのずれ（ピクセル）にする。
			// (Tx, Ty) は整数なので、拡大率が 1 でないと、ずれは拡大率の整数倍に丸まる
			int Width = 0;
			int Height = 0;
			GetGraphSize( Data.Handle, &Width, &Height );
			const float32 TileWidth = static_cast<float32>( Width ) * Data.Scale;
			const float32 TileHeight = static_cast<float32>( Height ) * Data.Scale;
			const int ShiftX = NMath::RoundToInt( ( ( static_cast<float32>( Data.X1 - Data.X0 ) - TileWidth ) * 0.5f - static_cast<float32>( Data.OffsetX ) ) / Data.Scale );
			const int ShiftY = NMath::RoundToInt( ( ( static_cast<float32>( Data.Y1 - Data.Y0 ) - TileHeight ) * 0.5f - static_cast<float32>( Data.OffsetY ) ) / Data.Scale );
			DrawTile( Data.X0, Data.Y0, Data.X1, Data.Y1, ShiftX, ShiftY, Data.Scale, Data.Angle, Data.Handle, TRUE );
			break;
		}
		case EDrawCommand::CircleGauge:
		{
			if ( !Command.CanRead<FDrawCircleGaugeData>() )
			{
				break;
			}
			const FDrawCircleGaugeData Data = Command.Read<FDrawCircleGaugeData>();
			const int FlipX = Command.Has( FDrawFlag::FlipX ) ? TRUE : FALSE;
			const int FlipY = Command.Has( FDrawFlag::FlipY ) ? TRUE : FALSE;
			// DxLib の StartPercent は描き始め、Percent は描き終わり（どちらも真上からの位置）。使う側は、Percent を「描く量」として渡すので、描き終わりに直す。
			// 描き終わりが 100 を超えるときは、先頭へ回り込んで描く
			const double End = static_cast<double>( Data.StartPercent ) + static_cast<double>( Data.Percent );
			if ( Data.Percent >= 100.0f )
			{
				// 全体は、描き始めの位置に関係ない（つなぎ目で、縁が重ならないよう、1 回で描く）
				DrawCircleGaugeF( Data.X, Data.Y, 100.0, Data.Handle, 0.0, Data.Scale, FlipX, FlipY );
			}
			else if ( End <= 100.0 )
			{
				DrawCircleGaugeF( Data.X, Data.Y, End, Data.Handle, Data.StartPercent, Data.Scale, FlipX, FlipY );
			}
			else
			{
				DrawCircleGaugeF( Data.X, Data.Y, 100.0, Data.Handle, Data.StartPercent, Data.Scale, FlipX, FlipY );
				DrawCircleGaugeF( Data.X, Data.Y, End - 100.0, Data.Handle, 0.0, Data.Scale, FlipX, FlipY );
			}
			break;
		}
		case EDrawCommand::CustomBlend:
		{
			if ( !Command.CanRead<FDrawCustomBlendData>() )
			{
				break;
			}
			const FDrawCustomBlendData Data = Command.Read<FDrawCustomBlendData>();
			SetDrawCustomBlendMode( TRUE, ToDxBlendFactor( Data.SrcColor ), ToDxBlendFactor( Data.DestColor ), ToDxBlendOp( Data.ColorOp ),
									ToDxBlendFactor( Data.SrcAlpha ), ToDxBlendFactor( Data.DestAlpha ), ToDxBlendOp( Data.AlphaOp ), Data.Param );
			bBlendChanged = true;
			break;
		}
		case EDrawCommand::AddColor:
		{
			if ( !Command.CanRead<FDrawAddColorData>() )
			{
				break;
			}
			const FDrawAddColorData Data = Command.Read<FDrawAddColorData>();
			if ( !bAddColorChanged )
			{
				GetDrawAddColor( &OldAddColor[0], &OldAddColor[1], &OldAddColor[2] );
				bAddColorChanged = true;
			}
			SetDrawAddColor( Data.R, Data.G, Data.B );
			break;
		}
		case EDrawCommand::AlphaTest:
		{
			if ( !Command.CanRead<FDrawAlphaTestData>() )
			{
				break;
			}
			const FDrawAlphaTestData Data = Command.Read<FDrawAlphaTestData>();
			if ( !bAlphaTestChanged )
			{
				GetDrawAlphaTest( &OldAlphaTestMode, &OldAlphaTestParam );
				bAlphaTestChanged = true;
			}
			if ( Data.Enabled == 0 )
			{
				// -1 が、アルファテストをしない（既定）状態
				SetDrawAlphaTest( -1, 0 );
			}
			else if ( static_cast<EDrawCompare>( Data.Mode ) == EDrawCompare::Never )
			{
				// DxLib の DX_CMP_NEVER は、実際には何も止めない（実測）。「アルファ < 0」は、どの画素でも成り立たないので、これで代用する
				SetDrawAlphaTest( DX_CMP_LESS, 0 );
			}
			else
			{
				SetDrawAlphaTest( NDxLibRenderState3D::ToDxCompare( Data.Mode ), Data.Reference );
			}
			break;
		}
		case EDrawCommand::Filter:
		{
			if ( !Command.CanRead<FDrawFilterData>() )
			{
				break;
			}
			const FDrawFilterData Data = Command.Read<FDrawFilterData>();
			if ( !bFilterChanged )
			{
				OldFilter = GetDrawMode();
				bFilterChanged = true;
			}
			SetDrawMode( ToDxFilter( Data.Mode ) );
			break;
		}
		case EDrawCommand::PushClip:
		{
			if ( !Command.CanRead<FDrawClipData>() )
			{
				break;
			}
			const FDrawClipData Data = Command.Read<FDrawClipData>();
			if ( !bClipChanged )
			{
				GetDrawArea( &OldArea );
				bClipChanged = true;
			}
			// 新しい範囲は、いまの範囲（一番内側に重ねた範囲。無ければ、始める前の範囲）との共通部分
			const RECT Requested = { Data.X0, Data.Y0, Data.X1, Data.Y1 };
			const RECT Area = IntersectArea( Requested, ClipStack.IsEmpty() ? OldArea : ClipStack.Last() );
			ClipStack.Add( Area );
			bClipEmpty = IsEmptyArea( Area );
			if ( !bClipEmpty )
			{
				SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			}
			break;
		}
		case EDrawCommand::PopClip:
		{
			if ( ClipStack.IsEmpty() )
			{
				break;
			}
			ClipStack.Pop();
			const RECT& Area = ClipStack.IsEmpty() ? OldArea : ClipStack.Last();
			bClipEmpty = IsEmptyArea( Area );
			if ( !bClipEmpty )
			{
				SetDrawArea( Area.left, Area.top, Area.right, Area.bottom );
			}
			break;
		}
		case EDrawCommand::Transform2D:
		{
			if ( !Command.CanRead<FDrawMatrixData>() )
			{
				break;
			}
			const FDrawMatrixData Data = Command.Read<FDrawMatrixData>();
			const MATRIX Matrix = ToMatrix( Data.M );
			SetTransformTo2D( &Matrix );
			Transform2DMatrix = Matrix;
			bTransform2DChanged = true;
			break;
		}
		case EDrawCommand::PushTarget:
		{
			if ( !Command.CanRead<FDrawTargetData>() )
			{
				break;
			}
			const FDrawTargetData Data = Command.Read<FDrawTargetData>();
			if ( NumInvalidTargets > 0 || Data.Handle < 0 )
			{
				++NumInvalidTargets;
				break;
			}
			FTargetFrame Frame;
			Frame.PreviousScreen = GetDrawScreen();
			GetDrawArea( &Frame.PreviousArea );
			GetCameraScreenCenter( &Frame.CameraCenterX, &Frame.CameraCenterY );
			const int PreviousReset = GetUseSetDrawScreenSettingReset();
			SetUseSetDrawScreenSettingReset( FALSE );
			const int Result = SetDrawScreen( Data.Handle );
			SetUseSetDrawScreenSettingReset( PreviousReset );
			if ( Result != 0 )
			{
				// 描画先にできない画像（DxLib が拒んだ）。描画先は変わっていない
				++NumInvalidTargets;
				break;
			}
			// 替えた先の描画範囲は、その全体
			SetDrawAreaFull();
			Frame.bClipChanged = bClipChanged;
			Frame.OldArea = OldArea;
			Frame.ClipStack = ClipStack;
			Frame.bClipEmpty = bClipEmpty;
			Frame.bTransform2DChanged = bTransform2DChanged;
			Frame.Transform2D = Transform2DMatrix;
			Frame.MaskStack = MaskStack;
			TargetStack.Add( Frame );
			// 替えた先は、クリップなし・2D の変換なしから始まる
			bClipChanged = true;
			GetDrawArea( &OldArea );
			ClipStack.Empty();
			bClipEmpty = false;
			ResetTransformTo2D();
			Transform2DMatrix = MGetIdent();
			bTransform2DChanged = true;
			// マスクも、なしから始まる
			if ( !MaskStack.IsEmpty() )
			{
				MaskStack.Empty();
				ApplyMask();
			}
			break;
		}
		case EDrawCommand::PopTarget:
		{
			if ( NumInvalidTargets > 0 )
			{
				--NumInvalidTargets;
				break;
			}
			if ( TargetStack.IsEmpty() )
			{
				break;
			}
			RestoreTarget();
			break;
		}
		case EDrawCommand::Clear:
		{
			if ( !Command.CanRead<FDrawClearData>() )
			{
				break;
			}
			const FDrawClearData Data = Command.Read<FDrawClearData>();
			// ClearDrawScreen は背景色で消す。背景色を一時的に指定の色にして、終わったら戻す
			int PreviousRed = 0;
			int PreviousGreen = 0;
			int PreviousBlue = 0;
			int PreviousAlpha = 0;
			GetBackgroundColor( &PreviousRed, &PreviousGreen, &PreviousBlue, &PreviousAlpha );
			SetBackgroundColor( NMath::Clamp( Data.R, 0, 255 ), NMath::Clamp( Data.G, 0, 255 ), NMath::Clamp( Data.B, 0, 255 ), NMath::Clamp( Data.A, 0, 255 ) );
			ClearDrawScreen();
			SetBackgroundColor( PreviousRed, PreviousGreen, PreviousBlue, PreviousAlpha );
			break;
		}
		case EDrawCommand::CopyScreen:
		{
			if ( !Command.CanRead<FDrawCopyScreenData>() )
			{
				break;
			}
			const FDrawCopyScreenData Data = Command.Read<FDrawCopyScreenData>();
			// DxLib は、画像と矩形の大きさが同じでないと写せない。違うときは何もしない
			int Width = 0;
			int Height = 0;
			if ( Data.Handle < 0 || GetGraphSize( Data.Handle, &Width, &Height ) != 0 || Width != Data.X1 - Data.X0 || Height != Data.Y1 - Data.Y0 )
			{
				break;
			}
			GetDrawScreenGraph( Data.X0, Data.Y0, Data.X1, Data.Y1, Data.Handle );
			break;
		}
		case EDrawCommand::ImageFilter:
		{
			if ( !Command.CanRead<FDrawImageFilterData>() )
			{
				break;
			}
			NDxLibImageOps::ApplyFilter( Command.Read<FDrawImageFilterData>() );
			break;
		}
		case EDrawCommand::ImageBlend:
		{
			if ( !Command.CanRead<FDrawImageBlendData>() )
			{
				break;
			}
			NDxLibImageOps::BlendImages( Command.Read<FDrawImageBlendData>() );
			break;
		}
		case EDrawCommand::PushMask:
		{
			if ( !Command.CanRead<FDrawMaskData>() )
			{
				break;
			}
			const FDrawMaskData Data = Command.Read<FDrawMaskData>();
			FMaskState State;
			State.Handle = Data.Handle;
			State.Channel = Data.Channel;
			State.bInvert = Data.Invert != 0;
			MaskStack.Add( State );
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
		case EDrawCommand::PushCamera:
		{
			if ( !Command.CanRead<FDrawCameraData>() )
			{
				break;
			}
			FCameraFrame Frame;
			Frame.Near = GetCameraNear();
			Frame.Far = GetCameraFar();
			Frame.View = GetCameraViewMatrix();
			Frame.Projection = GetCameraProjectionMatrix();
			GetCameraScreenCenter( &Frame.CenterX, &Frame.CenterY );
			Frame.DotAspect = GetCameraDotAspect();
			CameraStack.Add( Frame );
			ApplyCamera( Command.Read<FDrawCameraData>() );
			break;
		}
		case EDrawCommand::PopCamera:
		{
			if ( CameraStack.IsEmpty() )
			{
				break;
			}
			RestoreCamera();
			break;
		}
		}
	} );

	// このオブジェクトの分で替えた描画先を、元に戻す（Pop が足りなくても。クリップ・2D の変換も、替える前に戻る）
	while ( !TargetStack.IsEmpty() )
	{
		RestoreTarget();
	}

	// このオブジェクトの分で替えたカメラを、元に戻す（Pop が足りなくても）
	while ( !CameraStack.IsEmpty() )
	{
		RestoreCamera();
	}

	// マスクは、使ったなら、マスクなしに戻す（始める前に使う設定になっていたなら、その設定へ）
	if ( bMaskChanged )
	{
		SetUseMaskScreenFlag( OldUseMask );
		SetMaskScreenGraph( -1 );
		SetMaskReverseEffectFlag( FALSE );
		SetMaskScreenGraphUseChannel( DX_MASKGRAPH_CH_A );
	}

	// 3D の描画状態を、元に戻す
	RenderState3D.Restore();

	// ライティングを、元に戻す（足したライトも外す）
	Lighting.Restore();

	// このオブジェクトで変えた状態を、次のオブジェクトに漏らさない
	if ( bBlendChanged )
	{
		SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
	}
	if ( bBrightChanged )
	{
		SetDrawBright( 255, 255, 255 );
	}
	if ( bWorldMatrixChanged )
	{
		const MATRIX Identity = MGetIdent();
		SetTransformToWorld( &Identity );
	}
	if ( bAddColorChanged )
	{
		SetDrawAddColor( OldAddColor[0], OldAddColor[1], OldAddColor[2] );
	}
	if ( bAlphaTestChanged )
	{
		SetDrawAlphaTest( OldAlphaTestMode, OldAlphaTestParam );
	}
	if ( bFilterChanged )
	{
		SetDrawMode( OldFilter );
	}
	if ( bClipChanged )
	{
		SetDrawArea( OldArea.left, OldArea.top, OldArea.right, OldArea.bottom );
	}
	if ( bTransform2DChanged )
	{
		ResetTransformTo2D();
	}
}

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
#include "Library/Math/MathUtils.h"
#include "Library/Platform/DxLib/DxLibAssetBackend.h"
#include "Library/Platform/DxLib/DxLibInclude.h"
#include "Library/Platform/DxLib/DxLibRibbonMesh.h"

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
		default: return DX_BLENDMODE_NOBLEND;
		}
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
	bool bBlendChanged = false;
	bool bWorldMatrixChanged = false;
	bool bBrightChanged = false;

	ForEachDrawCommand( Span, [&]( const FDrawCommandView& Command )
	{
		const int Fill = Command.Has( FDrawFlag::Fill ) ? TRUE : FALSE;

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
			const MATRIX Matrix = ToMatrix( Data.M );
			SetTransformToWorld( &Matrix );
			bWorldMatrixChanged = true;
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
		}
	} );

	// このオブジェクトで変えた状態を、次のオブジェクトに漏らさない
	if ( bBlendChanged ) SetDrawBlendMode( DX_BLENDMODE_NOBLEND, 0 );
	if ( bBrightChanged ) SetDrawBright( 255, 255, 255 );
	if ( bWorldMatrixChanged )
	{
		const MATRIX Identity = MGetIdent();
		SetTransformToWorld( &Identity );
	}
}

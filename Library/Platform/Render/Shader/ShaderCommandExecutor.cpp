// SPDX-License-Identifier: MIT
#include "ShaderCommandExecutor.h"
#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Platform/Render/Geometry/GeometryCommandExecutor.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	bool IsStage( int32 Stage )
	{
		return Stage == static_cast<int32>( EShaderStage::Vertex ) || Stage == static_cast<int32>( EShaderStage::Pixel );
	}
}

CShaderCommandExecutor::CShaderCommandExecutor( CGeometryCommandExecutor& InGeometry, IShaderDrawSource* InSource ) : Geometry( InGeometry ), Source( InSource )
{
}

bool CShaderCommandExecutor::IsShaderCommand( EDrawCommand Type )
{
	return Type == EDrawCommand::UseShader || Type == EDrawCommand::ShaderConstants || Type == EDrawCommand::ShaderTexture || Type == EDrawCommand::ShaderOutputTarget
		|| Type == EDrawCommand::PrimitiveShader3D || Type == EDrawCommand::ShaderRect || Type == EDrawCommand::ShaderQuad3D;
}

bool CShaderCommandExecutor::HasOutputTargets() const
{
	for ( int32 Index = 1; Index < NDrawShader::NumOutputTargets; ++Index )
	{
		if ( OutputTargets[Index] >= 0 )
		{
			return true;
		}
	}
	return false;
}

bool CShaderCommandExecutor::Draw( FShaderDrawRequest& Request, bool bAlphaBlend, bool bTwoSided )
{
	IRenderGeometry3D* Target = Geometry.GetGeometry();
	if ( !Source || !Target || ( Request.BuiltinPixel == EBuiltInPixelShader::None && PixelShader < 0 ) || ( Request.Builtin == EBuiltInVertexShader::None && VertexShader < 0 ) )
	{
		return false; // シェーダーが決まっていないと、何も描かない（DxLib と同じ）。
	}
	Request.VertexShader = VertexShader;
	Request.PixelShader = PixelShader;
	Request.State = Geometry.GetState();
	Request.State.bAlphaBlend = bAlphaBlend;
	if ( bTwoSided )
	{
		Request.State.Culling = EGeometryCulling::None;
	}
	TArray<FShaderConstantData> Views;
	Views.Reserve( Constants.Num() );
	for ( const FStoredConstants& Stored : Constants )
	{
		FShaderConstantData View;
		View.Stage = Stored.Stage;
		View.Slot = Stored.Slot;
		View.Bytes = TArrayView<const uint8>( Stored.Bytes.GetData(), Stored.Bytes.Num() );
		Views.Add( View );
	}
	Request.Constants = TArrayView<const FShaderConstantData>( Views.GetData(), Views.Num() );
	for ( int32 Stage = 0; Stage < NDrawShader::NumTextureStages; ++Stage )
	{
		Request.Textures[Stage] = Textures[Stage];
	}
	if ( Prepare && !Prepare( Request ) )
	{
		return false;
	}
	Target = Geometry.GetGeometry(); // 準備で、描画先のパスが替わることがある。
	return Target && Source->DrawShader( Request, *Target );
}

bool CShaderCommandExecutor::DrawBuiltIn( FShaderDrawRequest& Request, bool bTwoSided )
{
	IRenderGeometry3D* Target = Geometry.GetGeometry();
	if ( !Source || !Target )
	{
		return false;
	}
	if ( bTwoSided )
	{
		Request.State.Culling = EGeometryCulling::None;
	}
	if ( Prepare && !Prepare( Request ) )
	{
		return false;
	}
	Target = Geometry.GetGeometry(); // 準備で、描画先のパスが替わることがある。
	return Target && Source->DrawShader( Request, *Target );
}

CShaderCommandExecutor::EResult CShaderCommandExecutor::DrawRect( const FDrawShaderRectData& Data, EBuiltInPixelShader Pixel, bool bAlphaBlend )
{
	const float32 Numbers[] = { Data.X, Data.Y, Data.Width, Data.Height, Data.U0, Data.V0, Data.U1, Data.V1 };
	for ( const float32 Number : Numbers )
	{
		if ( !NMath::IsFinite( Number ) )
		{
			return EResult::Rejected;
		}
	}
	const IRenderGeometry3D* Target = Geometry.GetGeometry();
	if ( Data.Width <= 0.0f || Data.Height <= 0.0f || !Target || Target->GetWidth() < 1 || Target->GetHeight() < 1 )
	{
		return EResult::Rejected;
	}
	const float32 ScreenWidth = static_cast<float32>( Target->GetWidth() ), ScreenHeight = static_cast<float32>( Target->GetHeight() );
	// 画素の座標を、クリップ座標（x は -1〜1、y は上が +1）に直す。左上が (0, 0)。奥行きは 0（いちばん手前）。
	const float32 Left = Data.X / ScreenWidth * 2.0f - 1.0f, Right = ( Data.X + Data.Width ) / ScreenWidth * 2.0f - 1.0f;
	const float32 Top = 1.0f - Data.Y / ScreenHeight * 2.0f, Bottom = 1.0f - ( Data.Y + Data.Height ) / ScreenHeight * 2.0f;
	const float32 PositionsX[4] = { Left, Right, Left, Right }, PositionsY[4] = { Top, Top, Bottom, Bottom };
	const float32 Us[4] = { Data.U0, Data.U1, Data.U0, Data.U1 }, Vs[4] = { Data.V0, Data.V0, Data.V1, Data.V1 };
	FDrawShaderVertex3D Vertices[4];
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		FMemory::Memzero( &Vertices[Index], sizeof( FDrawShaderVertex3D ) );
		Vertices[Index].Position[0] = PositionsX[Index];
		Vertices[Index].Position[1] = PositionsY[Index];
		Vertices[Index].Diffuse[0] = Data.Color[0];
		Vertices[Index].Diffuse[1] = Data.Color[1];
		Vertices[Index].Diffuse[2] = Data.Color[2];
		Vertices[Index].Diffuse[3] = Data.Color[3];
		Vertices[Index].U = Us[Index];
		Vertices[Index].V = Vs[Index];
	}
	const uint32 Indices[6] = { 0, 1, 2, 2, 1, 3 }; // 三角形の帯を、2 枚の三角形にする。
	FShaderDrawRequest Request;
	Request.Builtin = EBuiltInVertexShader::Rect;
	Request.BuiltinPixel = Pixel;
	Request.Topology = EGeometryTopology::Triangles;
	Request.bUseDepth = false;
	Request.Vertices = reinterpret_cast<const uint8*>( Vertices );
	Request.NumVertices = 4;
	Request.Indices = Indices;
	Request.NumIndices = 6;
	return Draw( Request, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
}

bool CShaderCommandExecutor::DrawShadowPreview( const FDrawPreviewShadowMapData& Data, bool bAlphaBlend )
{
	FDrawShaderRectData Rect; // 画面の矩形へ、影の絵の全体を貼る。
	Rect.X = static_cast<float32>( Data.X0 );
	Rect.Y = static_cast<float32>( Data.Y0 );
	Rect.Width = static_cast<float32>( Data.X1 - Data.X0 );
	Rect.Height = static_cast<float32>( Data.Y1 - Data.Y0 );
	Rect.U0 = 0.0f;
	Rect.V0 = 0.0f;
	Rect.U1 = 1.0f;
	Rect.V1 = 1.0f;
	const int32 OldTexture = Textures[1]; // 影の絵は t1（組み込みのシェーダーの置き場）。利用者の画像の置き場は、元に戻す。
	Textures[1] = Data.Handle;
	const EResult Result = DrawRect( Rect, EBuiltInPixelShader::ShadowPreview, bAlphaBlend );
	Textures[1] = OldTexture;
	return Result == EResult::Executed;
}

CShaderCommandExecutor::EResult CShaderCommandExecutor::Execute( const FDrawCommandView& View, bool bAlphaBlend )
{
	switch ( View.Type )
	{
	case EDrawCommand::UseShader:
	{
		if ( !View.CanRead<FDrawUseShaderData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawUseShaderData>(); // 検査後の固定部。
		if ( !IsStage( Data.Stage ) )
		{
			return EResult::Rejected;
		}
		// 解放済み・種類の違うシェーダーは、使わない（前のシェーダーで描いてしまわないように、外す）。
		const bool bValid = Data.Handle >= 0 && Source && Source->IsShaderValid( Data.Handle, static_cast<EShaderStage>( Data.Stage ) );
		( Data.Stage == static_cast<int32>( EShaderStage::Vertex ) ? VertexShader : PixelShader ) = bValid ? Data.Handle : -1;
		return EResult::Executed;
	}
	case EDrawCommand::ShaderConstants:
	{
		if ( !View.CanRead<FDrawShaderConstantsData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawShaderConstantsData>(); // 検査後の固定部。
		constexpr int32 HeaderWords = static_cast<int32>( sizeof( FDrawShaderConstantsData ) / 4 );
		// 壊れた命令（使えないスロット・大きさ。命令の大きさが、中身にぴったり合わない）は、実行しない。
		if ( !IsStage( Data.Stage ) || Data.Slot < NDrawShader::FirstUserSlot || Data.Slot > NDrawShader::LastUserSlot || Data.NumBytes < 16 || Data.NumBytes > NDrawShader::MaxConstantBytes
			|| Data.NumBytes % 16 != 0 || View.NumDataWords != HeaderWords + Data.NumBytes / 4 )
		{
			return EResult::Rejected;
		}
		FStoredConstants* Found = nullptr;
		for ( FStoredConstants& Existing : Constants )
		{
			if ( Existing.Stage == Data.Stage && Existing.Slot == Data.Slot )
			{
				Found = &Existing;
				break;
			}
		}
		if ( !Found )
		{
			const int32 NewIndex = Constants.Add( FStoredConstants() );
			Found = &Constants[NewIndex];
			Found->Stage = Data.Stage;
			Found->Slot = Data.Slot;
		}
		Found->Bytes.SetNum( Data.NumBytes );
		FMemory::Memcpy( Found->Bytes.GetData(), View.Data + HeaderWords, static_cast<usize>( Data.NumBytes ) );
		return EResult::Executed;
	}
	case EDrawCommand::ShaderTexture:
	{
		if ( !View.CanRead<FDrawShaderTextureData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawShaderTextureData>(); // 検査後の固定部。
		if ( Data.Stage < 0 || Data.Stage >= NDrawShader::NumTextureStages )
		{
			return EResult::Rejected;
		}
		Textures[Data.Stage] = Data.Handle; // 解放済みの番号は、描くときに、空の画像になる。
		return EResult::Executed;
	}
	case EDrawCommand::ShaderOutputTarget:
	{
		if ( !View.CanRead<FDrawShaderOutputTargetData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawShaderOutputTargetData>(); // 検査後の固定部。
		// 0 は、いまの描画先（PushTarget）の出力なので、ここでは替えない。
		if ( Data.Index < 1 || Data.Index >= NDrawShader::NumOutputTargets )
		{
			return EResult::Rejected;
		}
		OutputTargets[Data.Index] = Data.Handle < 0 ? -1 : Data.Handle; // 描くときに、描画先として読めるか確かめる。
		return EResult::Executed;
	}
	case EDrawCommand::PrimitiveShader3D:
	{
		if ( !View.CanRead<FDrawPrimitiveShader3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawPrimitiveShader3DData>(); // 検査後の固定部。
		if ( Data.Type < 0 || Data.Type > static_cast<int32>( EDrawPrimitive::TriangleFan ) )
		{
			return EResult::Rejected;
		}
		const EDrawPrimitive Type = static_cast<EDrawPrimitive>( Data.Type );
		const bool bIndexed = Data.NumIndices > 0;
		const int32 Count = bIndexed ? Data.NumIndices : Data.NumVertices; // 使う頂点の数（添字なら、添字の数）。
		// 命令の大きさ（ヘッダーの 1 ワードも含む）が、頂点と添字の数にぴったり合うか。
		if ( Data.NumVertices <= 0 || Data.NumVertices > 65536 || Data.NumIndices < 0 || !NDrawPrimitive::IsValidCount( Type, Count ) || Count > 196608
			|| static_cast<usize>( View.NumDataWords ) + 1 != FDrawPrimitiveShader3DData::GetCommandWords( Data.NumVertices, Data.NumIndices ) )
		{
			return EResult::Rejected;
		}
		constexpr usize DataWords = sizeof( FDrawPrimitiveShader3DData ) / 4;
		constexpr usize VertexWords = sizeof( FDrawShaderVertex3D ) / 4;
		const uint8* VertexBytes = reinterpret_cast<const uint8*>( View.Data + DataWords ); // 頂点の並び（4 バイトの境界）。
		// 数値の検査（有限でない頂点は、GPU へ渡さない）。位置・補助の位置・U・V の 9 個を、浮動小数点の指数部がすべて 1（NaN・無限大）でないかで確かめる（1 つずつ関数を呼ばない）。
		static_assert( offsetof( FDrawShaderVertex3D, SubPosition ) == 3 * sizeof( uint32 ) && offsetof( FDrawShaderVertex3D, U ) == 18 * sizeof( uint32 ) && offsetof( FDrawShaderVertex3D, V ) == 19 * sizeof( uint32 ), "the finite check reads the vertex as 32-bit words" );
		constexpr uint32 ExponentMask = 0x7F800000u; // 浮動小数点の指数部。すべて 1 なら NaN か無限大。
		for ( int32 Index = 0; Index < Data.NumVertices; ++Index )
		{
			const uint32* Words = View.Data + DataWords + static_cast<usize>( Index ) * VertexWords; // この頂点の先頭（4 バイトの境界）。
			if ( ( Words[0] & ExponentMask ) == ExponentMask || ( Words[1] & ExponentMask ) == ExponentMask || ( Words[2] & ExponentMask ) == ExponentMask || ( Words[3] & ExponentMask ) == ExponentMask || ( Words[4] & ExponentMask ) == ExponentMask
				|| ( Words[5] & ExponentMask ) == ExponentMask || ( Words[6] & ExponentMask ) == ExponentMask || ( Words[18] & ExponentMask ) == ExponentMask || ( Words[19] & ExponentMask ) == ExponentMask )
			{
				return EResult::Rejected;
			}
		}
		// つなぎ方を、リスト（点・線分・三角形）へ展開する。添字なしのリストは、頂点を順に使う。
		TArray<uint32> Order; // 使う頂点の順。
		Order.AddUninitialized( Count );
		uint32* OrderData = Order.GetData();
		if ( bIndexed )
		{
			const uint16* IndexData = reinterpret_cast<const uint16*>( View.Data + DataWords + static_cast<usize>( Data.NumVertices ) * VertexWords ); // 添字の並び（4 バイトの境界）。
			const uint32 NumVertices = static_cast<uint32>( Data.NumVertices );
			for ( int32 Index = 0; Index < Count; ++Index )
			{
				const uint32 Value = IndexData[Index];
				if ( Value >= NumVertices )
				{
					return EResult::Rejected;
				}
				OrderData[Index] = Value;
			}
		}
		else
		{
			for ( int32 Index = 0; Index < Count; ++Index )
			{
				OrderData[Index] = static_cast<uint32>( Index );
			}
		}
		TArray<uint32> Expanded; // リストに展開した順。
		EGeometryTopology Topology = EGeometryTopology::Triangles;
		bool bNeedsIndices = bIndexed; // 添字なしのリストは、添字の列を作らずに、頂点を順に使う。
		if ( Type == EDrawPrimitive::Points || Type == EDrawPrimitive::Lines || Type == EDrawPrimitive::Triangles )
		{
			Topology = Type == EDrawPrimitive::Points ? EGeometryTopology::Points : Type == EDrawPrimitive::Lines ? EGeometryTopology::Lines : EGeometryTopology::Triangles;
			Expanded = MoveTemp( Order ); // そのまま使う（複写しない）。
		}
		else if ( Type == EDrawPrimitive::LineStrip )
		{
			Topology = EGeometryTopology::Lines;
			for ( int32 Index = 1; Index < Count; ++Index )
			{
				Expanded.Add( Order[Index - 1] );
				Expanded.Add( Order[Index] );
			}
			bNeedsIndices = true;
		}
		else
		{
			for ( int32 Index = 2; Index < Count; ++Index )
			{
				Expanded.Add( Order[Type == EDrawPrimitive::TriangleFan ? 0 : Index - ( Index % 2 ? 1 : 2 )] );
				Expanded.Add( Order[Type == EDrawPrimitive::TriangleFan ? Index - 1 : Index - ( Index % 2 ? 2 : 1 )] );
				Expanded.Add( Order[Index] );
			}
			bNeedsIndices = true;
		}
		if ( Expanded.IsEmpty() )
		{
			return EResult::Executed;
		}
		FShaderDrawRequest Request;
		Request.Topology = Topology;
		Request.bUseDepth = true;
		Request.Vertices = VertexBytes;
		Request.NumVertices = Data.NumVertices;
		if ( bNeedsIndices )
		{
			Request.Indices = Expanded.GetData();
			Request.NumIndices = Expanded.Num();
		}
		else
		{
			Request.NumVertices = Expanded.Num();
		}
		return Draw( Request, bAlphaBlend ) ? EResult::Executed : EResult::Rejected;
	}
	case EDrawCommand::ShaderRect:
	{
		if ( !View.CanRead<FDrawShaderRectData>() )
		{
			return EResult::Rejected;
		}
		return DrawRect( View.Read<FDrawShaderRectData>(), EBuiltInPixelShader::None, bAlphaBlend ); // 検査後の固定部。
	}
	case EDrawCommand::ShaderQuad3D:
	{
		if ( !View.CanRead<FDrawShaderQuad3DData>() )
		{
			return EResult::Rejected;
		}
		const auto Data = View.Read<FDrawShaderQuad3DData>(); // 検査後の固定部。
		for ( const float32 Extra : Data.Extra )
		{
			if ( !NMath::IsFinite( Extra ) )
			{
				return EResult::Rejected;
			}
		}
		TArray<FDrawShaderVertex3D> Vertices; // 三角形のリスト（クリップ座標つき）。
		if ( !Geometry.BuildShaderQuad( Data, Vertices ) )
		{
			return EResult::Rejected;
		}
		if ( Vertices.IsEmpty() )
		{
			return EResult::Executed;
		}
		FShaderDrawRequest Request;
		Request.Builtin = EBuiltInVertexShader::Quad3D;
		Request.Topology = EGeometryTopology::Triangles;
		Request.bUseDepth = true;
		Request.Vertices = reinterpret_cast<const uint8*>( Vertices.GetData() );
		Request.NumVertices = Vertices.Num();
		return Draw( Request, bAlphaBlend, true ) ? EResult::Executed : EResult::Rejected; // 四角形は、両面を描く。
	}
	default:
		return EResult::NotHandled;
	}
}

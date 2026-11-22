#include "DrawContext.h"

#include <cstdarg>

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Render/Draw/DrawModelFramePoseData.h"

void FDrawContext::Line3D( const FVector3& A, const FVector3& B, const FColor32& Color )
{
	FDrawLine3DData Data;
	Set3( Data.P0, A );
	Set3( Data.P1, B );
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Line3D, 0, Data );
}

void FDrawContext::Triangle3D( const FVector3& A, const FVector3& B, const FVector3& C, const FColor32& Color, bool bFill )
{
	FDrawTriangle3DData Data;
	Set3( Data.P0, A );
	Set3( Data.P1, B );
	Set3( Data.P2, C );
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Triangle3D, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext::Sphere3D( const FVector3& Center, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular, bool bFill )
{
	FDrawSphere3DData Data;
	Set3( Data.Center, Center );
	Data.Radius = Radius;
	Data.Div = DivNum;
	Data.Diffuse = Rgb( Diffuse );
	Data.Specular = Rgb( Specular );
	Push( EDrawCommand::Sphere3D, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext::Capsule3D( const FVector3& A, const FVector3& B, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular, bool bFill )
{
	FDrawCapsule3DData Data;
	Set3( Data.P0, A );
	Set3( Data.P1, B );
	Data.Radius = Radius;
	Data.Div = DivNum;
	Data.Diffuse = Rgb( Diffuse );
	Data.Specular = Rgb( Specular );
	Push( EDrawCommand::Capsule3D, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext::Cube3D( const FVector3& Min, const FVector3& Max, const FColor32& Diffuse, const FColor32& Specular, bool bFill )
{
	FDrawCube3DData Data;
	Set3( Data.Min, Min );
	Set3( Data.Max, Max );
	Data.Diffuse = Rgb( Diffuse );
	Data.Specular = Rgb( Specular );
	Push( EDrawCommand::Cube3D, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext::Billboard3D( const FVector3& Position, float32 Size, int32 GraphHandle, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	FDrawBillboard3DData Data;
	Set3( Data.Pos, Position );
	Data.Size = Size;
	Data.Angle = AngleRadians;
	Data.Handle = GraphHandle;
	Push( EDrawCommand::Billboard3D, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext::Pixel3D( const FVector3& Position, const FColor32& Color )
{
	if ( !IsFiniteVector( Position ) )
	{
		return;
	}
	FDrawPixel3DData Data;
	Set3( Data.Pos, Position );
	Data.Color = Rgb( Color );
	Push( EDrawCommand::Pixel3D, 0, Data );
}

void FDrawContext::Cone3D( const FVector3& Top, const FVector3& Bottom, float32 Radius, int32 DivNum, const FColor32& Diffuse, const FColor32& Specular, bool bFill )
{
	if ( !IsFiniteVector( Top ) || !IsFiniteVector( Bottom ) || !NMath::IsFinite( Radius ) )
	{
		return;
	}
	FDrawCone3DData Data;
	Set3( Data.Top, Top );
	Set3( Data.Bottom, Bottom );
	Data.Radius = Radius;
	Data.Div = DivNum;
	Data.Diffuse = Rgb( Diffuse );
	Data.Specular = Rgb( Specular );
	Push( EDrawCommand::Cone3D, bFill ? FDrawFlag::Fill : 0, Data );
}

void FDrawContext::BillboardPivot3D( const FVector3& Position, float32 Size, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !IsFiniteVector( Position ) || !AllFinite( Size, PivotX, PivotY, AngleRadians ) )
	{
		return;
	}
	FDrawBillboardPivot3DData Data;
	Set3( Data.Pos, Position );
	Data.Size = Size;
	Data.PivotX = PivotX;
	Data.PivotY = PivotY;
	Data.Angle = AngleRadians;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::BillboardPivot3D, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext::BillboardQuad3D( const FVector3& Position, float32 X0, float32 Y0, float32 X1, float32 Y1, float32 X2, float32 Y2, float32 X3, float32 Y3, FImageHandle Handle )
{
	if ( !Handle.IsValid() || !IsFiniteVector( Position ) || !AllFinite( X0, Y0, X1, Y1, X2, Y2, X3, Y3 ) )
	{
		return;
	}
	FDrawBillboardQuad3DData Data;
	Set3( Data.Pos, Position );
	Data.Corner[0] = X0;
	Data.Corner[1] = Y0;
	Data.Corner[2] = X1;
	Data.Corner[3] = Y1;
	Data.Corner[4] = X2;
	Data.Corner[5] = Y2;
	Data.Corner[6] = X3;
	Data.Corner[7] = Y3;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::BillboardQuad3D, 0, Data );
}

void FDrawContext::Sprite3D( const FVector3& Position, FImageHandle Handle, float32 Scale, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !IsFiniteVector( Position ) || !AllFinite( Scale, AngleRadians ) )
	{
		return;
	}
	FDrawSprite3DData Data;
	Set3( Data.Pos, Position );
	Data.Scale = Scale;
	Data.Angle = AngleRadians;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::Sprite3D, Flip( bFlipX, bFlipY ), Data );
}

void FDrawContext::SpriteStretch3D( const FVector3& Position, FImageHandle Handle, float32 ScaleX, float32 ScaleY )
{
	if ( !Handle.IsValid() || !IsFiniteVector( Position ) || !AllFinite( ScaleX, ScaleY ) )
	{
		return;
	}
	FDrawSpriteStretch3DData Data;
	Set3( Data.Pos, Position );
	Data.ScaleX = ScaleX;
	Data.ScaleY = ScaleY;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::SpriteStretch3D, 0, Data );
}

void FDrawContext::SpritePivot3D( const FVector3& Position, FImageHandle Handle, float32 PivotX, float32 PivotY, float32 ScaleX, float32 ScaleY, float32 AngleRadians, bool bFlipX, bool bFlipY )
{
	if ( !Handle.IsValid() || !IsFiniteVector( Position ) || !AllFinite( PivotX, PivotY, ScaleX, ScaleY, AngleRadians ) )
	{
		return;
	}
	FDrawSpritePivot3DData Data;
	Set3( Data.Pos, Position );
	Data.PivotX = PivotX;
	Data.PivotY = PivotY;
	Data.ScaleX = ScaleX;
	Data.ScaleY = ScaleY;
	Data.Angle = AngleRadians;
	Data.Handle = Handle.Value;
	Push( EDrawCommand::SpritePivot3D, Flip( bFlipX, bFlipY ), Data );
}

bool FDrawContext::Primitive3D( const FVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type )
{
	return PushPrimitive3D( Vertices, NumVertices, nullptr, 0, Type, -1 );
}

bool FDrawContext::Primitive3D( const FVertex3D* Vertices, int32 NumVertices, EDrawPrimitive Type, FImageHandle Texture )
{
	return Texture.IsValid() && PushPrimitive3D( Vertices, NumVertices, nullptr, 0, Type, Texture.Value );
}

bool FDrawContext::PrimitiveIndexed3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type )
{
	return Indices != nullptr && PushPrimitive3D( Vertices, NumVertices, Indices, NumIndices, Type, -1 );
}

bool FDrawContext::PrimitiveIndexed3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type, FImageHandle Texture )
{
	return Indices != nullptr && Texture.IsValid() && PushPrimitive3D( Vertices, NumVertices, Indices, NumIndices, Type, Texture.Value );
}

bool FDrawContext::PrimitiveBuffer3D( FVertexBufferHandle Vertices, EDrawPrimitive Type, const FBufferRange& Range )
{
	return PushPrimitiveBuffer3D( Vertices, FIndexBufferHandle(), false, Type, -1, Range );
}

bool FDrawContext::PrimitiveBuffer3D( FVertexBufferHandle Vertices, EDrawPrimitive Type, FImageHandle Texture, const FBufferRange& Range )
{
	return Texture.IsValid() && PushPrimitiveBuffer3D( Vertices, FIndexBufferHandle(), false, Type, Texture.Value, Range );
}

bool FDrawContext::PrimitiveIndexedBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, EDrawPrimitive Type, const FBufferRange& Range )
{
	return PushPrimitiveBuffer3D( Vertices, Indices, true, Type, -1, Range );
}

bool FDrawContext::PrimitiveIndexedBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, EDrawPrimitive Type, FImageHandle Texture, const FBufferRange& Range )
{
	return Texture.IsValid() && PushPrimitiveBuffer3D( Vertices, Indices, true, Type, Texture.Value, Range );
}

void FDrawContext::Quad3D( const FDrawQuad3DData& Data )
{
	Push( EDrawCommand::Quad3D, 0, Data );
}

void FDrawContext::Mesh3D( int32 VertexBuffer, int32 IndexBuffer, int32 NumVertices, int32 StartIndex, int32 NumIndices, int32 Texture, const FMatrix4x4& World, bool bTwoSided )
{
	FDrawMeshData Data;
	Data.VertexBuffer = VertexBuffer;
	Data.IndexBuffer = IndexBuffer;
	Data.NumVertices = NumVertices;
	Data.StartIndex = StartIndex;
	Data.NumIndices = NumIndices;
	Data.Texture = Texture;
	PackAffine( Data.M, World );
	Push( EDrawCommand::Mesh3D, bTwoSided ? FDrawFlag::TwoSided : 0, Data );
}

void FDrawContext::ModelAnimLayer( int32 ModelHandle, int32 AttachIndex, float32 AnimTime, float32 BlendRate )
{
	FDrawModelAnimData Data;
	Data.Handle = ModelHandle;
	Data.AttachIndex = AttachIndex;
	Data.Time = AnimTime;
	Data.Rate = BlendRate;
	Push( EDrawCommand::ModelAnim, 0, Data );
}

void FDrawContext::ModelAnimFrameRate( int32 ModelHandle, int32 AttachIndex, int32 Frame, float32 BlendRate, bool bIncludeChildren )
{
	if ( ModelHandle < 0 || AttachIndex < 0 || Frame < 0 || !NMath::IsFinite( BlendRate ) )
	{
		return;
	}
	FDrawModelAnimFrameData Data;
	Data.Handle = ModelHandle;
	Data.AttachIndex = AttachIndex;
	Data.Frame = Frame;
	Data.Rate = NMath::Clamp( BlendRate, 0.0f, 1.0f );
	Data.bIncludeChildren = bIncludeChildren ? 1 : 0;
	Push( EDrawCommand::ModelAnimFrame, 0, Data );
}

void FDrawContext::WorldMatrix( const FMatrix4x4& Matrix )
{
	FDrawMatrixData Data;
	PackAffine( Data.M, Matrix );
	Push( EDrawCommand::WorldMatrix, 0, Data );
}

void FDrawContext::PopWorldMatrix()
{
	const FDrawPopWorldMatrixData Data;
	Push( EDrawCommand::PopWorldMatrix, 0, Data );
}

void FDrawContext::Model( int32 ModelHandle, const FMatrix4x4& World, int32 AnimAttachIndex, float32 AnimTime )
{
	FDrawModelData Data;
	Data.Handle = ModelHandle;
	PackAffine( Data.M, World );
	Data.AnimIndex = AnimAttachIndex;
	Data.AnimTime = AnimTime;
	Push( EDrawCommand::Model3D, 0, Data );
}

void FDrawContext::MaterialQuad3D( const FDrawMaterialQuadData& Data )
{
	if ( !NDrawMaterialQuad::IsValid( Data ) )
	{
		return;
	}
	Push( EDrawCommand::MaterialQuad3D, 0, Data );
}

void FDrawContext::DistortedMaterialQuad3D( const FDrawDistortedMaterialQuadData& Data )
{
	if ( !NDrawDistortedMaterialQuad::IsValid( Data ) )
	{
		return;
	}
	Push( EDrawCommand::DistortedMaterialQuad3D, 0, Data );
}

void FDrawContext::MaterialRibbon3D( const FDrawMaterialRibbonData& Data, const FDrawMaterialRibbonData::FPoint* Points )
{
	// 入力値が記録先の再確保で変わらないよう、固定設定を先に値として保持する。
	FDrawMaterialRibbonData Header{};
	FMemory::Memcpy( &Header, &Data, sizeof( Header ) );
	// 既存命令と同じ設定・点列条件を、記録前に一括で検査する。
	if ( !NDrawMaterialRibbon::IsValidHeader( Header ) || Points == nullptr )
	{
		return;
	}
	TArray<FDrawMaterialRibbonData::FPoint> Copied;
	Copied.Append( Points, Header.Ribbon.NumPoints );
	if ( !NDrawMaterialRibbon::IsValid( Header, Copied.GetData() ) )
	{
		return;
	}
	// 固定設定と全点を合わせた命令長。MaxPoints検査後なので積は安全な範囲。
	constexpr size_t DataWords = sizeof( FDrawMaterialRibbonData ) / 4;
	// 全点を4byte word単位で記録する長さ。
	const size_t PointWords = static_cast<size_t>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) / 4;
	// command headerを含む最終word数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return;
	}
	// PointsやDataが既存bufferを指す場合も、広げる前に所有コピーする。
	TArray<uint32>& Words = Buffer->Words;
	// 追記する新命令の開始word。
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::MaterialRibbon3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Header, sizeof( Header ) );
	FMemory::Memcpy( Out + 1 + DataWords, Copied.GetData(), static_cast<size_t>( Header.Ribbon.NumPoints ) * sizeof( FDrawMaterialRibbonData::FPoint ) );
}

void FDrawContext::DistortedMaterialRibbon3D( const FDrawDistortedMaterialRibbonData& Data, const FDrawDistortedMaterialRibbonData::FPoint* Points )
{
	// buffer再確保で入力headerが変わらないよう、固定設定を先に値で保持する。
	FDrawDistortedMaterialRibbonData Header{};
	FMemory::Memcpy( &Header, &Data, sizeof( Header ) );
	if ( Points == nullptr || !NDrawDistortedMaterialRibbon::IsValidHeader( Header ) )
	{
		return;
	}
	// 入力配列を所有し、検証後に一括記録するための点列。
	TArray<FDrawDistortedMaterialRibbonData::FPoint> Copied;
	Copied.Append( Points, Header.Material.Ribbon.NumPoints );
	if ( !NDrawDistortedMaterialRibbon::IsValid( Header, Copied.GetData() ) )
	{
		return;
	}
	// 固定headerと全点を含む描画命令のword数。
	constexpr size_t DataWords = sizeof( FDrawDistortedMaterialRibbonData ) / 4;
	// 1024点以下の可変payload word数。
	const size_t PointWords = static_cast<size_t>( Header.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) / 4;
	// command header込みの合計word数。
	const size_t CommandWords = 1 + DataWords + PointWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return;
	}
	// 追記する描画命令が入る共有word buffer。
	TArray<uint32>& Words = Buffer->Words;
	// 新命令を記録する開始位置。
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::DistortedMaterialRibbon3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Header, sizeof( Header ) );
	FMemory::Memcpy( Out + 1 + DataWords, Copied.GetData(), static_cast<size_t>( Header.Material.Ribbon.NumPoints ) * sizeof( FDrawDistortedMaterialRibbonData::FPoint ) );
}

bool FDrawContext::ModelMaterial( const FDrawModelMaterialData& Data )
{
	// buffer再確保や呼出側aliasから独立させる固定命令payload。
	FDrawModelMaterialData Header{};
	FMemory::Memcpy( &Header, &Data, sizeof( Header ) );
	if ( !NDrawModelMaterial::IsValid( Header ) )
	{
		return false;
	}
	// 追加後のword数が上限を超えないかを先に調べる。
	constexpr size_t CommandWords = 1 + sizeof( FDrawModelMaterialData ) / 4;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return false;
	}
	// 固定命令を一度で広げて追記する共有word列。
	TArray<uint32>& Words = Buffer->Words;
	// 新命令を記録する開始位置。
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::ModelMaterial3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Header, sizeof( Header ) );
	return true;
}

bool FDrawContext::ModelWithFrameOverrides( int32 ModelHandle, const FMatrix4x4& World, const FDrawModelFrameOverrideData* Overrides, int32 NumOverrides, int32 AnimAttachIndex, float32 AnimTime )
{
	if ( ModelHandle < 0 || AnimAttachIndex < -1 || !NMath::IsFinite( AnimTime ) || AnimTime < 0.0f || NumOverrides < 0 || NumOverrides > NDrawModelFramePose::MaxOverrides || ( NumOverrides > 0 && Overrides == nullptr ) )
	{
		return false;
	}
	// world行列の有限性を調べる行番号。
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		// 現在行の有限性を調べる列番号。
		for ( int32 Column = 0; Column < 4; ++Column )
		{
			if ( !NMath::IsFinite( World.m[Row][Column] ) )
			{
				return false;
			}
		}
	}
	if ( World.m[0][3] != 0.0f || World.m[1][3] != 0.0f || World.m[2][3] != 0.0f || World.m[3][3] != 1.0f )
	{
		return false;
	}
	// buffer再確保より前に、呼出側配列を命令所有の値へ複写する。
	TArray<FDrawModelFrameOverrideData> Snapshot; // buffer変更前に所有するpose snapshot。
	if ( NumOverrides > 0 )
	{
		Snapshot.Append( Overrides, NumOverrides );
	}
	FDrawModelFramePoseData Data; // 可変長commandの固定header。
	Data.Handle = ModelHandle;
	PackAffine( Data.M, World );
	Data.AnimIndex = AnimAttachIndex;
	Data.AnimTime = AnimTime;
	Data.NumOverrides = NumOverrides;
	if ( !NDrawModelFramePose::IsValidHeader( Data ) || !NDrawModelFramePose::IsValidOverrides( Snapshot.GetData(), Snapshot.Num() ) )
	{
		return false;
	}
	constexpr int32 HeaderWords = sizeof( FDrawModelFramePoseData ) / 4; // 固定headerのword数。
	constexpr int32 OverrideWords = sizeof( FDrawModelFrameOverrideData ) / 4; // 1pose recordのword数。
	const int32 CommandWords = 1 + HeaderWords + NumOverrides * OverrideWords; // command header込みの長さ。
	TArray<uint32>& Words = Buffer->Words; // 追記する共有command列。
	if ( CommandWords > NDrawCommand::MaxWords || Words.Num() > TNumericLimits<int32>::Max() - CommandWords )
	{
		return false;
	}
	const int32 Begin = Words.AddZeroed( CommandWords ); // 新commandの先頭word位置。
	uint32* const Out = Words.GetData() + Begin; // 新commandを書き込む位置。
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::ModelFramePose3D, 0, CommandWords );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	if ( NumOverrides > 0 )
	{
		FMemory::Memcpy( Out + 1 + HeaderWords, Snapshot.GetData(), static_cast<size_t>( NumOverrides ) * sizeof( FDrawModelFrameOverrideData ) );
	}
	return true;
}

void FDrawContext::MaterialMesh3D( const FDrawMaterialMeshData& Data, const FMeshVertex* Vertices, const uint32* Indices, bool bUseRegularCulling )
{
	// buffer再確保で入力headerが変わらないよう、固定設定を先に値として保持する。
	FDrawMaterialMeshData Header{};
	FMemory::Memcpy( &Header, &Data, sizeof( Header ) );
	if ( Vertices == nullptr || Indices == nullptr || !NDrawMaterialMesh::IsValidHeader( Header ) )
	{
		return;
	}
	// 追記前に頂点payloadを所有し、入力aliasを切り離す配列。
	TArray<FMeshVertex> CopiedVertices;
	CopiedVertices.Append( Vertices, Header.NumVertices );
	// 追記前にindex payloadを所有し、入力aliasを切り離す配列。
	TArray<uint32> CopiedIndices;
	CopiedIndices.Append( Indices, Header.NumIndices );
	if ( !NDrawMaterialMesh::IsValid( Header, CopiedVertices.GetData(), CopiedIndices.GetData() ) )
	{
		return;
	}
	// 固定headerのword数。
	constexpr size_t DataWords = sizeof( FDrawMaterialMeshData ) / 4;
	// 頂点payloadのword数。
	const size_t VertexWords = static_cast<size_t>( Header.NumVertices ) * sizeof( FMeshVertex ) / 4;
	// index payloadのword数。
	const size_t IndexWords = static_cast<size_t>( Header.NumIndices ) * sizeof( uint32 ) / 4;
	// command headerを含めた全word数。
	const size_t CommandWords = 1 + DataWords + VertexWords + IndexWords;
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return;
	}
	// 所有コピー済みデータを一度で広げて追記するword列。
	TArray<uint32>& Words = Buffer->Words;
	// 新しい命令を書き始める位置。
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	// 材質meshの通常culling利用flag。
	const uint8 Flags = bUseRegularCulling ? FDrawFlag::UseRegularCulling : 0;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::MaterialMesh3D, Flags, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Header, sizeof( Header ) );
	FMemory::Memcpy( Out + 1 + DataWords, CopiedVertices.GetData(), static_cast<size_t>( Header.NumVertices ) * sizeof( FMeshVertex ) );
	FMemory::Memcpy( Out + 1 + DataWords + VertexWords, CopiedIndices.GetData(), static_cast<size_t>( Header.NumIndices ) * sizeof( uint32 ) );
}

bool FDrawContext::PushPrimitive3D( const FVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, EDrawPrimitive Type, int32 Texture )
{
	if ( Vertices == nullptr )
	{
		return false;
	}
	const bool bIndexed = Indices != nullptr;
	if ( !bIndexed && NumIndices != 0 )
	{
		return false;
	}
	// つなぎ方に合う数か（インデックスがあればインデックスの数、なければ頂点の数）。範囲の外のつなぎ方と、0 以下の数は、使えない数なので、ここで弾かれる
	// （インデックスつきで、頂点の数が 0 以下なら、あとのインデックスの確認が弾く）
	if ( !NDrawPrimitive::IsValidCount( Type, bIndexed ? NumIndices : NumVertices ) )
	{
		return false;
	}
	// 1 つの命令に収まる大きさか
	const size_t CommandWords = FDrawPrimitive3DData::GetCommandWords( NumVertices, NumIndices );
	if ( CommandWords > static_cast<size_t>( NDrawCommand::MaxWords ) || Buffer->Words.Num() > TNumericLimits<int32>::Max() - static_cast<int32>( CommandWords ) )
	{
		return false;
	}
	// 一部の頂点だけ捨てると、形が変わってしまうので、全部を先に確かめる
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		if ( !Vertices[Index].IsValid() )
		{
			return false;
		}
	}
	for ( int32 Index = 0; Index < NumIndices; ++Index )
	{
		if ( Indices[Index] >= NumVertices )
		{
			return false;
		}
	}

	constexpr size_t DataWords = sizeof( FDrawPrimitive3DData ) / 4;
	constexpr size_t VertexWords = sizeof( FDrawVertex3D ) / 4;
	FDrawPrimitive3DData Data;
	Data.Type = static_cast<int32>( Type );
	Data.Texture = Texture;
	Data.NumVertices = NumVertices;
	Data.NumIndices = NumIndices;
	TArray<uint32>& Words = Buffer->Words;
	const int32 Begin = Words.AddZeroed( static_cast<int32>( CommandWords ) );
	uint32* const Out = Words.GetData() + Begin;
	Out[0] = NDrawCommand::MakeHeader( EDrawCommand::Primitive3D, 0, static_cast<int32>( CommandWords ) );
	FMemory::Memcpy( Out + 1, &Data, sizeof( Data ) );
	for ( int32 Index = 0; Index < NumVertices; ++Index )
	{
		const FDrawVertex3D Vertex = Vertices[Index].ToDrawData();
		FMemory::Memcpy( Out + 1 + DataWords + static_cast<size_t>( Index ) * VertexWords, &Vertex, sizeof( Vertex ) );
	}
	if ( bIndexed )
	{
		// 2 個で 1 ワード。奇数個の最後の半分は、AddZeroed の 0 のまま
		FMemory::Memcpy( Out + 1 + DataWords + static_cast<size_t>( NumVertices ) * VertexWords, Indices, static_cast<size_t>( NumIndices ) * sizeof( uint16 ) );
	}
	return true;
}

bool FDrawContext::PushPrimitiveBuffer3D( FVertexBufferHandle Vertices, FIndexBufferHandle Indices, bool bIndexed, EDrawPrimitive Type, int32 Texture, const FBufferRange& Range )
{
	if ( !Vertices.IsValid() || ( bIndexed && !Indices.IsValid() ) )
	{
		return false;
	}
	// インデックスなしでは、BaseVertex は意味がない（0 のまま）
	if ( !bIndexed && Range.BaseVertex != 0 )
	{
		return false;
	}
	// 使う範囲（インデックスありならインデックスの範囲、なしなら頂点の範囲）が、バッファに収まり、つなぎ方に合う数か。足し算は、溢れないよう 64 ビットで
	const int32 NumBuffer = bIndexed ? Indices.NumIndices : Vertices.NumVertices;
	if ( Range.Start < 0 )
	{
		return false;
	}
	const int64 Num = Range.Num < 0 ? static_cast<int64>( NumBuffer ) - Range.Start : Range.Num;
	if ( Range.Start + Num > NumBuffer || !NDrawPrimitive::IsValidCount( Type, static_cast<int32>( Num ) ) )
	{
		return false;
	}
	FDrawPrimitiveBuffer3DData Data;
	Data.Type = static_cast<int32>( Type );
	Data.Texture = Texture;
	Data.VertexBuffer = Vertices.Value;
	Data.NumBufferVertices = Vertices.NumVertices;
	Data.IndexBuffer = bIndexed ? Indices.Value : -1;
	Data.NumBufferIndices = bIndexed ? Indices.NumIndices : 0;
	Data.Start = Range.Start;
	Data.Num = static_cast<int32>( Num );
	Data.BaseVertex = Range.BaseVertex;
	Push( EDrawCommand::PrimitiveBuffer3D, 0, Data );
	return true;
}

bool FDrawContext::PushWorldMatrix( const FMatrix4x4& Local )
{
	FDrawMatrixData Data;
	PackAffine( Data.M, Local );
	for ( const float32 Value : Data.M )
	{
		if ( !NMath::IsFinite( Value ) )
		{
			return false;
		}
	}
	Push( EDrawCommand::PushWorldMatrix, 0, Data );
	return true;
}

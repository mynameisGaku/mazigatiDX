#include "DxLibAssetBackend.h"


#include "Library/Core/Interfaces/Platform/IPlatform.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/CString.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Subsystem/SubsystemCollection.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

#include <psapi.h>   // windows.h（DxLib.h が読み込む）より後に

DEFINE_LOG_CATEGORY_STATIC( LogAssetBackend, Log )

REGISTER_SUBSYSTEM( CDxLibAssetBackend )

namespace
{
	// 非同期読み込みの設定を、この範囲だけ変える
	struct FAsyncScope
	{
		explicit FAsyncScope( bool bAsync ) { SetUseASyncLoadFlag( bAsync ? TRUE : FALSE ); }
		~FAsyncScope() { SetUseASyncLoadFlag( FALSE ); }
	};
}

void CDxLibAssetBackend::Initialize( FSubsystemCollection& Collection )
{
	// DxLib（プラットフォーム）が初期化されたあとに使う
	if ( Collection.InitializeDependency<IPlatform>() == nullptr )
	{
		LOG( LogAssetBackend, Error, "No platform is available. Assets cannot be loaded." );
	}
}

//-----------------------------------------------------------------------------
// 画像
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadTexture( const FString& Path, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	return LoadGraph( *Path );
}

int32 CDxLibAssetBackend::CreateTextureFromMemory( const void* FileImage, usize Size )
{
	return CreateGraphFromMem( FileImage, static_cast<int>( Size ) );
}

int32 CDxLibAssetBackend::CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height )
{
	if ( Source < 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 ) return NAsset::InvalidHandle;
	// 元グラフ上の指定矩形を収める寸法。
	int SourceWidth = 0;
	int SourceHeight = 0;
	if ( GetGraphSize( Source, &SourceWidth, &SourceHeight ) != 0 ) return NAsset::InvalidHandle;
	if ( static_cast<int64>( X ) + Width > SourceWidth || static_cast<int64>( Y ) + Height > SourceHeight ) return NAsset::InvalidHandle;
	return DerivationGraph( X, Y, Width, Height, Source );
}

bool CDxLibAssetBackend::GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] )
{
	if ( Texture < 0 ) return false;
	// 派生前のbase texture全体のpixel寸法。
	int TextureWidth = 0, TextureHeight = 0;
	// 派生グラフがbase texture上で占めるpixel矩形。
	int X = 0, Y = 0, Width = 0, Height = 0;
	if ( GetGraphTextureSize( Texture, &TextureWidth, &TextureHeight ) != 0 || TextureWidth <= 0 || TextureHeight <= 0 ) return false;
	if ( GetGraphUseBaseGraphArea( Texture, &X, &Y, &Width, &Height ) != 0 || X < 0 || Y < 0 || Width <= 0 || Height <= 0 || static_cast<int64>( X ) + Width > TextureWidth || static_cast<int64>( Y ) + Height > TextureHeight ) return false;
	// 一pixel領域でも一点を正確に標本化できるよう、両端を中央へ寄せる。
	// 半pixelを正規化した横・縦のUV幅。
	const float32 HalfU = 0.5f / static_cast<float32>( TextureWidth );
	const float32 HalfV = 0.5f / static_cast<float32>( TextureHeight );
	OutUV[0] = ( static_cast<float32>( X ) + HalfU * TextureWidth ) / static_cast<float32>( TextureWidth );
	OutUV[1] = ( static_cast<float32>( Y ) + HalfV * TextureHeight ) / static_cast<float32>( TextureHeight );
	OutUV[2] = ( static_cast<float32>( X + Width ) - HalfU * TextureWidth ) / static_cast<float32>( TextureWidth );
	OutUV[3] = ( static_cast<float32>( Y + Height ) - HalfV * TextureHeight ) / static_cast<float32>( TextureHeight );
	return NMath::IsFinite( OutUV[0] ) && NMath::IsFinite( OutUV[1] ) && NMath::IsFinite( OutUV[2] ) && NMath::IsFinite( OutUV[3] );
}

bool CDxLibAssetBackend::GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight )
{
	int Width = 0;
	int Height = 0;
	if ( GetGraphSize( Texture, &Width, &Height ) != 0 ) return false;
	OutWidth = Width;
	OutHeight = Height;
	return true;
}

void CDxLibAssetBackend::DeleteTexture( int32 Texture )
{
	DeleteGraph( Texture );
}

//-----------------------------------------------------------------------------
// モデル
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadModel( const FString& Path, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	return MV1LoadModel( *Path );
}

int32 CDxLibAssetBackend::DuplicateModel( int32 Source )
{
	return MV1DuplicateModel( Source );
}

void CDxLibAssetBackend::DeleteModel( int32 Model )
{
	MV1DeleteModel( Model );
}

int32 CDxLibAssetBackend::GetModelAnimCount( int32 Model )
{
	return MV1GetAnimNum( Model );
}

FString CDxLibAssetBackend::GetModelAnimName( int32 Model, int32 AnimIndex )
{
	const char* Name = MV1GetAnimName( Model, AnimIndex );
	return Name != nullptr ? FString( Name ) : FString();
}

float32 CDxLibAssetBackend::GetModelAnimLength( int32 Model, int32 AnimIndex )
{
	return MV1GetAnimTotalTime( Model, AnimIndex );
}

int32 CDxLibAssetBackend::AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel )
{
	return MV1AttachAnim( Model, AnimIndex, SourceModel, TRUE );
}

void CDxLibAssetBackend::DetachModelAnim( int32 Model, int32 AttachIndex )
{
	MV1DetachAnim( Model, AttachIndex );
}

int32 CDxLibAssetBackend::GetModelMeshCount( int32 Model )
{
	return MV1GetMeshNum( Model );
}

bool CDxLibAssetBackend::GetModelBounds( int32 Model, float32 ( &OutMin )[3], float32 ( &OutMax )[3] )
{
	const int32 NumMeshes = MV1GetMeshNum( Model );
	if ( NumMeshes <= 0 ) return false;

	// 描くときと同じ位置（フレームの変換を適用した頂点）の範囲。取れれば、これを使う
	if ( MV1SetupReferenceMesh( Model, -1, TRUE, TRUE ) >= 0 )
	{
		const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Model, -1, TRUE, TRUE );
		const bool bHasPolygons = Reference.PolygonNum > 0 && Reference.VertexNum > 0;
		if ( bHasPolygons )
		{
			OutMin[0] = Reference.MinPosition.x;
			OutMin[1] = Reference.MinPosition.y;
			OutMin[2] = Reference.MinPosition.z;
			OutMax[0] = Reference.MaxPosition.x;
			OutMax[1] = Reference.MaxPosition.y;
			OutMax[2] = Reference.MaxPosition.z;
		}
		MV1TerminateReferenceMesh( Model, -1, TRUE, TRUE );
		if ( bHasPolygons ) return true;
	}

	bool bAny = false;
	for ( int32 Mesh = 0; Mesh < NumMeshes; ++Mesh )
	{
		const VECTOR Low = MV1GetMeshMinPosition( Model, Mesh );
		const VECTOR High = MV1GetMeshMaxPosition( Model, Mesh );
		const float32 LowValues[3] = { Low.x, Low.y, Low.z };
		const float32 HighValues[3] = { High.x, High.y, High.z };
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			OutMin[Axis] = bAny ? ( LowValues[Axis] < OutMin[Axis] ? LowValues[Axis] : OutMin[Axis] ) : LowValues[Axis];
			OutMax[Axis] = bAny ? ( HighValues[Axis] > OutMax[Axis] ? HighValues[Axis] : OutMax[Axis] ) : HighValues[Axis];
		}
		bAny = true;
	}
	return bAny;
}

bool CDxLibAssetBackend::GetModelCollisionMesh( int32 Model, TArray<float32>& OutPositions, TArray<uint32>& OutIndices )
{
	if ( MV1GetMeshNum( Model ) <= 0 ) return false;
	if ( MV1SetupReferenceMesh( Model, -1, TRUE, TRUE ) < 0 ) return false;
	const MV1_REF_POLYGONLIST Reference = MV1GetReferenceMesh( Model, -1, TRUE, TRUE );
	const bool bOk = Reference.PolygonNum > 0 && Reference.VertexNum > 0 && Reference.Polygons != nullptr && Reference.Vertexs != nullptr;
	if ( bOk )
	{
		OutPositions.Reset();
		OutIndices.Reset();
		OutPositions.Reserve( Reference.VertexNum * 3 );
		OutIndices.Reserve( Reference.PolygonNum * 3 );
		for ( int32 i = 0; i < Reference.VertexNum; ++i )
		{
			OutPositions.Add( Reference.Vertexs[i].Position.x );
			OutPositions.Add( Reference.Vertexs[i].Position.y );
			OutPositions.Add( Reference.Vertexs[i].Position.z );
		}
		for ( int32 i = 0; i < Reference.PolygonNum; ++i )
		{
			for ( int32 Corner = 0; Corner < 3; ++Corner ) OutIndices.Add( static_cast<uint32>( Reference.Polygons[i].VIndex[Corner] ) );
		}
	}
	MV1TerminateReferenceMesh( Model, -1, TRUE, TRUE );
	return bOk;
}

bool CDxLibAssetBackend::CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer )
{
	OutVertexBuffer = NAsset::InvalidHandle;
	OutIndexBuffer = NAsset::InvalidHandle;
	if ( Vertices == nullptr || Indices == nullptr || NumVertices < 3 || NumIndices < 3 ) return false;

	TArray<VERTEX3D> Converted;
	Converted.Reserve( NumVertices );
	const COLOR_U8 Specular = GetColorU8( 0, 0, 0, 0 );
	for ( int32 i = 0; i < NumVertices; ++i )
	{
		VERTEX3D Vertex{};
		Vertex.pos = VGet( Vertices[i].Position[0], Vertices[i].Position[1], Vertices[i].Position[2] );
		Vertex.norm = VGet( Vertices[i].Normal[0], Vertices[i].Normal[1], Vertices[i].Normal[2] );
		Vertex.dif = GetColorU8( Vertices[i].Color[0], Vertices[i].Color[1], Vertices[i].Color[2], Vertices[i].Color[3] );
		Vertex.spc = Specular;
		Vertex.u = Vertices[i].U;
		Vertex.v = Vertices[i].V;
		Converted.Add( Vertex );
	}
	const int VertexBuffer = CreateVertexBuffer( NumVertices, DX_VERTEX_TYPE_NORMAL_3D );
	if ( VertexBuffer < 0 ) return false;
	const int IndexBuffer = CreateIndexBuffer( NumIndices, DX_INDEX_TYPE_32BIT );
	if ( IndexBuffer < 0 )
	{
		DeleteVertexBuffer( VertexBuffer );
		return false;
	}
	if ( SetVertexBufferData( 0, Converted.GetData(), NumVertices, VertexBuffer ) < 0 || SetIndexBufferData( 0, Indices, NumIndices, IndexBuffer ) < 0 )
	{
		::DeleteVertexBuffer( VertexBuffer );
		::DeleteIndexBuffer( IndexBuffer );
		return false;
	}
	OutVertexBuffer = VertexBuffer;
	OutIndexBuffer = IndexBuffer;
	return true;
}

void CDxLibAssetBackend::DeleteVertexBuffer( int32 VertexBuffer )
{
	if ( VertexBuffer >= 0 ) ::DeleteVertexBuffer( VertexBuffer );
}

void CDxLibAssetBackend::DeleteIndexBuffer( int32 IndexBuffer )
{
	if ( IndexBuffer >= 0 ) ::DeleteIndexBuffer( IndexBuffer );
}

int32 CDxLibAssetBackend::GetModelFrameCount( int32 Model )
{
	return MV1GetFrameNum( Model );
}

FString CDxLibAssetBackend::GetModelFrameName( int32 Model, int32 FrameIndex )
{
	const char* Name = MV1GetFrameName( Model, FrameIndex );
	return Name != nullptr ? FString( Name ) : FString();
}

bool CDxLibAssetBackend::SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning )
{
	int SaveType = MV1_SAVETYPE_NORMAL;
	if ( Options.Type == EModelSaveType::MeshOnly ) SaveType = MV1_SAVETYPE_MESH;
	else if ( Options.Type == EModelSaveType::AnimOnly ) SaveType = MV1_SAVETYPE_ANIM;

	// 圧縮: 法線 8bit・座標 16bit・ウェイト 8bit なし・アニメーション 16bit（DxLib の既定）。しない: すべて 32bit
	const int Normal8 = Options.bCompress ? 1 : 0, Position16 = Options.bCompress ? 1 : 0, Anim16 = Options.bCompress ? 1 : 0;
	const int Result = MV1SaveModelToMV1File( Model, *Path, SaveType, -1, TRUE, Normal8, Position16, 0, Anim16 );
	if ( Result == -2 ) OutWarning = "the model has animations that are not used by any bone";
	return Result == 0 || Result == -2;
}

//-----------------------------------------------------------------------------
// フォント
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::MakeFont( const FFontDesc& Desc )
{
	int FontType = DX_FONTTYPE_NORMAL;
	if ( Desc.EdgeSize > 0 ) FontType = Desc.bAntialias ? DX_FONTTYPE_ANTIALIASING_EDGE : DX_FONTTYPE_EDGE;
	else FontType = Desc.bAntialias ? DX_FONTTYPE_ANTIALIASING : DX_FONTTYPE_NORMAL;

	return CreateFontToHandle( Desc.Face.IsEmpty() ? nullptr : *Desc.Face, Desc.Size, Desc.Thickness, FontType, -1, Desc.EdgeSize > 0 ? Desc.EdgeSize : -1,
							   Desc.bItalic ? TRUE : FALSE );
}

void CDxLibAssetBackend::DeleteFont( int32 Font )
{
	DeleteFontToHandle( Font );
}

int32 CDxLibAssetBackend::MeasureText( int32 Font, const char* Text )
{
	if ( Text == nullptr ) return 0;
	const int Length = static_cast<int>( NCString::Length( Text ) );
	return Font >= 0 ? GetDrawStringWidthToHandle( Text, Length, Font ) : GetDrawStringWidth( Text, Length );
}

bool CDxLibAssetBackend::AddFontFile( const FString& Path )
{
	// パスは UTF-8 なので、A 系（システムのコードページ）ではなく W 系で渡す
	return AddFontResourceExW( Path.ToWide().GetData(), FR_PRIVATE, nullptr ) > 0;
}

void CDxLibAssetBackend::RemoveFontFile( const FString& Path )
{
	RemoveFontResourceExW( Path.ToWide().GetData(), FR_PRIVATE, nullptr );
}

//-----------------------------------------------------------------------------
// サウンド
//-----------------------------------------------------------------------------
int32 CDxLibAssetBackend::LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync )
{
	FAsyncScope Scope( bAsync );
	SetCreateSoundDataType( Mode == ESoundLoadMode::Stream ? DX_SOUNDDATATYPE_FILE : DX_SOUNDDATATYPE_MEMNOPRESS );
	const int Handle = LoadSoundMem( *Path );
	SetCreateSoundDataType( DX_SOUNDDATATYPE_MEMNOPRESS );   // 既定に戻す（ほかの読み込みに影響させない）
	return Handle;
}

int32 CDxLibAssetBackend::DuplicateSound( int32 Source )
{
	return DuplicateSoundMem( Source );
}

void CDxLibAssetBackend::DeleteSound( int32 Sound )
{
	DeleteSoundMem( Sound );
}

float32 CDxLibAssetBackend::GetSoundLength( int32 Sound )
{
	const LONGLONG Milliseconds = GetSoundTotalTime( Sound );
	return Milliseconds > 0 ? static_cast<float32>( Milliseconds ) / 1000.0f : -1.0f;
}

//-----------------------------------------------------------------------------
// メモリ
//-----------------------------------------------------------------------------
FBackendMemoryInfo CDxLibAssetBackend::GetMemoryInfo()
{
	FBackendMemoryInfo Info;
	ULONGLONG Total = 0;
	ULONGLONG Used = 0;
	if ( GetVideoMemorySizeEx( &Total, &Used ) == 0 )
	{
		Info.VideoMemoryTotal = static_cast<int64>( Total );
		Info.VideoMemoryUsed = static_cast<int64>( Used );
	}

	PROCESS_MEMORY_COUNTERS Counters = {};
	Counters.cb = sizeof( Counters );
	if ( GetProcessMemoryInfo( GetCurrentProcess(), &Counters, sizeof( Counters ) ) ) Info.ProcessWorkingSet = static_cast<int64>( Counters.WorkingSetSize );

	Info.NumTextureHandles = GetHandleNum( DX_HANDLETYPE_GRAPH );
	Info.NumModelHandles = GetHandleNum( DX_HANDLETYPE_MODEL );
	Info.NumSoundHandles = GetHandleNum( DX_HANDLETYPE_SOUND );
	Info.NumFontHandles = GetHandleNum( DX_HANDLETYPE_FONT );
	return Info;
}

int64 CDxLibAssetBackend::EstimateModelBytes( int32 Model )
{
	// 頂点（1 つ 64 バイトとして）、フレームの行列、使っているテクスチャ（幅 x 高さ x 4）
	int64 Bytes = static_cast<int64>( MV1GetFrameNum( Model ) ) * 64;
	const int NumLists = MV1GetTriangleListNum( Model );
	for ( int i = 0; i < NumLists; ++i ) Bytes += static_cast<int64>( MV1GetTriangleListVertexNum( Model, i ) ) * 64;

	const int NumTextures = MV1GetTextureNum( Model );
	for ( int i = 0; i < NumTextures; ++i )
	{
		const int Graph = MV1GetTextureGraphHandle( Model, i );
		int Width = 0;
		int Height = 0;
		if ( Graph >= 0 && GetGraphSize( Graph, &Width, &Height ) == 0 ) Bytes += static_cast<int64>( Width ) * Height * 4;
	}
	return Bytes;
}

int64 CDxLibAssetBackend::EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode )
{
	// 展開後は 16bit ステレオ（1 サンプル 4 バイト）として見積もる。再生しながら読む音は、読み込み用の領域だけ
	if ( Mode == ESoundLoadMode::Stream ) return 2 * 64 * 1024;
	return static_cast<int64>( GetSoundTotalSample( Sound ) ) * 4;
}

//-----------------------------------------------------------------------------
// 非同期
//-----------------------------------------------------------------------------
bool CDxLibAssetBackend::IsLoading( int32 Handle )
{
	return CheckHandleASyncLoad( Handle ) == TRUE;
}

int32 CDxLibAssetBackend::GetNumLoading()
{
	return GetASyncLoadNum();
}

#include "Model.h"

#include "Library/Core/Thread/GameThread.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Utility.h"
#include "Library/Framework/Asset/StaticModelSurface.h"
#include "Library/Math/MathUtils.h"

DEFINE_LOG_CATEGORY_STATIC( LogModel, Log )

namespace
{
	// 骨の親基準行列の16要素が有限で、透視変換を含まない厳密な形か調べる。
	bool IsCanonicalAffinePose( const FMatrix4x4& Matrix )
	{
		for ( int32 Row = 0; Row < 4; ++Row ) // 有限値を調べる行。
		{
			for ( int32 Column = 0; Column < 4; ++Column ) // 有限値を調べる列。
			{
				if ( !NMath::IsFinite( Matrix.m[Row][Column] ) )
				{
					return false;
				}
			}
		}
		return Matrix.m[0][3] == 0.0f && Matrix.m[1][3] == 0.0f && Matrix.m[2][3] == 0.0f && Matrix.m[3][3] == 1.0f;
	}
}

//=============================================================================
// CModel
//=============================================================================
CModel::CModel( const FString& InPath, int32 InHandle, bool bInAsync )
	: CAsset( EAssetType::Model, InPath )
	, Handle( InHandle )
	, bAsync( bInAsync )
{
}

CModel::~CModel()
{
	NAssetService::ReleaseModel( Handle );
}

bool CModel::IsReady() const
{
	if ( Handle < 0 ) return false;
	if ( !bAsync ) return true;
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && !Backend->IsLoading( Handle );
}

void CModel::RefreshClips() const
{
	if ( bClipsRead ) return;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() ) return;

	bClipsRead = true;
	const int32 Num = Backend->GetModelAnimCount( Handle );
	for ( int32 Index = 0; Index < Num; ++Index )
	{
		FModelClipInfo Clip;
		Clip.Name = Backend->GetModelAnimName( Handle, Index );
		Clip.AnimIndex = Index;
		Clip.Length = Backend->GetModelAnimLength( Handle, Index );
		Clips.Add( Clip );
	}
}

void CModel::RefreshShape() const
{
	if ( bShapeRead ) return;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() ) return;

	bShapeRead = true;
	MeshCount = Backend->GetModelMeshCount( Handle );
	const int32 NumBones = Backend->GetModelFrameCount( Handle );
	for ( int32 Index = 0; Index < NumBones; ++Index ) Bones.Add( Backend->GetModelFrameName( Handle, Index ) );
	float32 Low[3] = {}, High[3] = {};
	if ( MeshCount > 0 && Backend->GetModelBounds( Handle, Low, High ) )
	{
		bHasBounds = true;
		BoundsMin = FVector3( Low[0], Low[1], Low[2] );
		BoundsMax = FVector3( High[0], High[1], High[2] );
	}
}

bool CModel::GetCollisionMesh( TArray<FVector3>& OutVertices, TArray<uint32>& OutIndices ) const
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() || GetMeshCount() <= 0 ) return false;
	TArray<float32> Positions;
	TArray<uint32> Indices;
	if ( !Backend->GetModelCollisionMesh( Handle, Positions, Indices ) || Positions.Num() < 9 || Indices.Num() < 3 ) return false;
	OutVertices.Reset();
	OutVertices.Reserve( Positions.Num() / 3 );
	for ( int32 i = 0; i + 2 < Positions.Num(); i += 3 ) OutVertices.Add( FVector3( Positions[i], Positions[i + 1], Positions[i + 2] ) );
	OutIndices = Indices;
	return true;
}

TSharedPtr<const FStaticModelSurface> CModel::CreateStaticSurfaceSnapshot() const
{
	if ( !NAssetService::IsMainThread() || Handle < 0 ) return nullptr;
	// 読み込み済みモデルへ問い合わせるbackend。
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() ) return nullptr;

	// backendが成功時だけ書き込む面データ。
	FStaticModelSurfaceData Data;
	if ( !Backend->GetStaticModelSurface( Handle, Data ) ) return nullptr;
	return MakeShared<FStaticModelSurface>( AsShared(), MoveTemp( Data ) );
}

bool CModel::GetBounds( FVector3& OutMin, FVector3& OutMax ) const
{
	RefreshShape();
	if ( !bHasBounds ) return false;
	OutMin = BoundsMin;
	OutMax = BoundsMax;
	return true;
}

int32 CModel::GetMeshCount() const
{
	RefreshShape();
	return MeshCount;
}

bool CModel::IsAnimationOnly() const
{
	return IsReady() && GetMeshCount() == 0 && !GetClips().IsEmpty();
}

const TArray<FString>& CModel::GetBoneNames() const
{
	RefreshShape();
	return Bones;
}

FBoneMatch CModel::CompareBones( const CModel& AnimSource ) const
{
	return NModelCompat::MatchBones( GetBoneNames(), AnimSource.GetBoneNames() );
}

const TArray<FModelClipInfo>& CModel::GetClips() const
{
	RefreshClips();
	return Clips;
}

const FModelClipInfo* CModel::FindClip( const FString& Name ) const
{
	RefreshClips();
	return Clips.FindByPredicate( [&Name]( const FModelClipInfo& Clip ) { return Clip.Name == Name; } );
}

int64 CModel::GetMemoryBytes() const
{
	if ( MemoryBytes >= 0 ) return MemoryBytes;
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !IsReady() ) return 0;
	MemoryBytes = Backend->EstimateModelBytes( Handle );
	return MemoryBytes;
}

FString CModel::Describe() const
{
	if ( !IsReady() ) return "loading...";
	if ( GetMeshCount() == 0 ) return FString::Printf( "animation only, %d animation(s), %d bone(s)", GetClips().Num(), GetBoneNames().Num() );
	return FString::Printf( "%d mesh(es), %d animation(s)", GetMeshCount(), GetClips().Num() );
}

void CModel::RefreshStructure() const
{
	GAME_THREAD_ONLY();
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( bStructureRead || Backend == nullptr || !IsReady() )
	{
		return;
	}
	// 答えられなかったとき（読み込み中・未対応）は覚えず、次に聞かれたときに読み直す
	bStructureRead = Backend->GetModelStructure( Handle, Structure );
}

const FModelStructure& CModel::GetStructure() const
{
	RefreshStructure();
	return Structure;
}

int32 CModel::FindFrame( const FString& Name ) const
{
	const TArray<FModelFrameInfo>& Frames = GetStructure().Frames;
	for ( int32 Index = 0; Index < Frames.Num(); ++Index )
	{
		if ( Frames[Index].Name == Name )
		{
			return Index;
		}
	}
	return -1;
}

int32 CModel::FindMaterial( const FString& Name ) const
{
	const TArray<FModelMaterialInfo>& Materials = GetStructure().Materials;
	for ( int32 Index = 0; Index < Materials.Num(); ++Index )
	{
		if ( Materials[Index].Name == Name )
		{
			return Index;
		}
	}
	return -1;
}

int32 CModel::FindShape( const FString& Name ) const
{
	const TArray<FString>& Shapes = GetStructure().Shapes;
	for ( int32 Index = 0; Index < Shapes.Num(); ++Index )
	{
		if ( Shapes[Index] == Name )
		{
			return Index;
		}
	}
	return -1;
}

TSharedPtr<CModelInstance> CModel::CreateInstance()
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Handle < 0 ) return nullptr;
	if ( IsReady() && GetMeshCount() == 0 )
	{
		LOG( LogModel, Error, "Cannot create an instance of '%s': it has no mesh (it only holds animations). Use it as an animation source for another model.", *GetPath() );
		return nullptr;
	}

	const int32 InstanceHandle = Backend->DuplicateModel( Handle );
	if ( InstanceHandle < 0 )
	{
		LOG( LogModel, Error, "Failed to create an instance of the model '%s'.", *GetPath() );
		return nullptr;
	}
	return MakeShared<CModelInstance>( AsShared(), InstanceHandle );
}

//=============================================================================
// CModelInstance
//=============================================================================
CModelInstance::CModelInstance( const TSharedPtr<CModel>& InSource, int32 InHandle )
	: Source( InSource )
	, Handle( InHandle )
{
}

CModelInstance::~CModelInstance()
{
	// アニメーションを外す必要はない（モデルを削除すると一緒に消える）
	NAssetService::ReleaseModel( Handle );
}

int32 CModelInstance::AttachAnim( const TSharedPtr<CModel>& AnimSource, int32 AnimIndex, bool bReuseExisting )
{
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || !AnimSource.IsValid() || Handle < 0 ) return -1;

	if ( bReuseExisting )
	{
		for ( const FAttached& Entry : Attached )
		{
			if ( Entry.AnimSource == AnimSource && Entry.AnimIndex == AnimIndex )
			{
				return Entry.AttachIndex;
			}
		}
	}

	// AnimSource がこのインスタンスの元のモデルなら、自分自身のアニメーションを付ける
	const int32 SourceHandle = AnimSource == Source ? -1 : AnimSource->GetHandle();
	const int32 AttachIndex = Backend->AttachModelAnim( Handle, AnimIndex, SourceHandle );
	if ( AttachIndex < 0 )
	{
		LOG( LogModel, Warning, "Failed to attach animation %d of '%s' to the model '%s'.", AnimIndex, *AnimSource->GetPath(), *Source->GetPath() );
		return -1;
	}

	FAttached Entry;
	Entry.AnimSource = AnimSource;
	Entry.AnimIndex = AnimIndex;
	Entry.AttachIndex = AttachIndex;
	Attached.Add( Entry );
	return AttachIndex;
}

void CModelInstance::DetachAnim( int32 AttachIndex )
{
	const int32 Index = Attached.IndexOfByPredicate( [AttachIndex]( const FAttached& Entry ) { return Entry.AttachIndex == AttachIndex; } );
	if ( Index == INDEX_NONE ) return;

	if ( IAssetBackend* Backend = NAssetService::GetBackend() ) Backend->DetachModelAnim( Handle, AttachIndex );
	Attached.RemoveAt( Index );
}

bool CModelInstance::GetFrameMatrix( int32 Frame, EModelFrameSpace Space, FMatrix4x4& OutMatrix ) const
{
	GAME_THREAD_ONLY( false );
	IAssetBackend* Backend = NAssetService::GetBackend();
	return Backend != nullptr && Handle >= 0 && Backend->GetModelFrameMatrix( Handle, Frame, Space, OutMatrix );
}

bool CModelInstance::GetFrameLocalMatrixOverride( int32 Frame, bool& bOutOverridden, FMatrix4x4& OutLocalToParent ) const
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() || !Source.IsValid() || !Source->IsReady() || Handle < 0 || !Source->GetBoneNames().IsValidIndex( Frame ) )
	{
		return false;
	}
	IAssetBackend* Backend = NAssetService::GetBackend(); // 個体別pose上書き状態を読むbackend。
	return Backend != nullptr && Backend->GetModelFrameLocalMatrixOverride( Handle, Frame, bOutOverridden, OutLocalToParent );
}

bool CModelInstance::SetFrameLocalMatrixOverride( int32 Frame, const FMatrix4x4& LocalToParent )
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() || !IsCanonicalAffinePose( LocalToParent ) || !Source.IsValid() || !Source->IsReady() || Handle < 0 || !Source->GetBoneNames().IsValidIndex( Frame ) )
	{
		return false;
	}
	IAssetBackend* Backend = NAssetService::GetBackend(); // 個体別pose上書きを受けるbackend。
	return Backend != nullptr && Backend->SetModelFrameLocalMatrixOverride( Handle, Frame, LocalToParent );
}

bool CModelInstance::ClearFrameLocalMatrixOverride( int32 Frame )
{
	GAME_THREAD_ONLY( false );
	if ( !NAssetService::IsMainThread() || !Source.IsValid() || !Source->IsReady() || Handle < 0 || !Source->GetBoneNames().IsValidIndex( Frame ) )
	{
		return false;
	}
	IAssetBackend* Backend = NAssetService::GetBackend(); // 個体別pose上書きを解除するbackend。
	return Backend != nullptr && Backend->ClearModelFrameLocalMatrixOverride( Handle, Frame );
}

FModelAppearance CModelInstance::GetAppearance() const
{
	return FModelAppearance( Handle, Source.Get() );
}

FModelCollision CModelInstance::GetCollision() const
{
	return FModelCollision( Handle );
}

bool CModelInstance::GetFramePositions( TArray<FVector3>& OutPositions ) const
{
	GAME_THREAD_ONLY( false );
	OutPositions.Reset();
	IAssetBackend* Backend = NAssetService::GetBackend();
	if ( Backend == nullptr || Handle < 0 )
	{
		return false;
	}
	const int32 NumFrames = Backend->GetModelFrameCount( Handle );
	for ( int32 Index = 0; Index < NumFrames; ++Index )
	{
		float32 Position[3] = {};
		if ( !Backend->GetModelFramePosition( Handle, Index, Position ) )
		{
			OutPositions.Reset();
			return false;
		}
		OutPositions.Add( FVector3( Position[0], Position[1], Position[2] ) );
	}
	return OutPositions.Num() > 0;
}

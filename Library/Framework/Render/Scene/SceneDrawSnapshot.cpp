// SPDX-License-Identifier: MIT
#include "SceneDrawSnapshot.h"
#include "Library/Framework/Render/Scene/Private/SceneDrawSnapshotData.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"

bool FSceneDrawSnapshot::IsValid() const { return Data.IsValid(); }
int32 FSceneDrawSnapshot::GetNumItems() const { return Data ? Data->Items.Num() : 0; }
int32 FSceneDrawSnapshot::GetNumWords() const { return Data ? Data->NumWords : 0; }
void FSceneDrawSnapshot::Reset() { Data.Reset(); }

bool FSceneDrawSnapshot::Replay( IDrawExecutor& Executor ) const
{
	const TSharedPtr<const FSceneDrawSnapshotData> Playing = Data; // 再生先の処理が元の値を手放しても最後の項目まで保持する。
	if ( !Playing ) { return false; }
	for ( const FSceneDrawSnapshotData::FItem& Item : Playing->Items )
	{
		Executor.Execute( { Item.Words.GetData(), Item.Words.Num(), Item.ShaderMesh.IsValid() ? &Item.ShaderMesh : nullptr } );
	}
	return true;
}

// SPDX-License-Identifier: MIT
#include "PostProcessStack.h"

#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Render/Types/RenderTargetTypes.h"

namespace
{
	// 作業用の描画先の名前が、重ならないように
	int32 NextTargetId = 1;
}

int32 CPostProcessStack::Add( const TSharedPtr<CMaterial>& Material, float32 Intensity )
{
	if ( !Material || Material->GetDomain() != EMaterialDomain::PostProcess )
	{
		return -1;
	}
	FEntry Entry;
	Entry.Material = Material;
	Entry.Intensity = Intensity;
	Entries.Add( Entry );
	return Entries.Num() - 1;
}

bool CPostProcessStack::Remove( int32 Index )
{
	if ( !Entries.IsValidIndex( Index ) )
	{
		return false;
	}
	Entries.RemoveAt( Index );
	return true;
}

bool CPostProcessStack::Move( int32 From, int32 To )
{
	if ( !Entries.IsValidIndex( From ) || !Entries.IsValidIndex( To ) )
	{
		return false;
	}
	const FEntry Moved = Entries[From];
	Entries.RemoveAt( From );
	Entries.Insert( Moved, To );
	return true;
}

int32 CPostProcessStack::NumEnabled() const
{
	int32 Count = 0;
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		if ( Entries[Index].bEnabled && Entries[Index].Material && Entries[Index].Material->IsValid() )
		{
			++Count;
		}
	}
	return Count;
}

bool CPostProcessStack::EnsureTargets( CAssetSubsystem& Assets, int32 Width, int32 Height )
{
	if ( Targets[0] && Targets[1] && TargetWidth == Width && TargetHeight == Height )
	{
		return true;
	}
	FRenderTargetDesc Desc;
	Desc.Width = Width;
	Desc.Height = Height;
	Desc.bAlpha = false;
	Desc.bDepth = false;
	for ( int32 Index = 0; Index < 2; ++Index )
	{
		Targets[Index].Reset();
		Targets[Index] = Assets.CreateRenderTarget( FString::Printf( "PostProcessStack%d_%d", NextTargetId++, Index ), Desc );
		if ( !Targets[Index] )
		{
			Targets[0].Reset();
			Targets[1].Reset();
			return false;
		}
	}
	TargetWidth = Width;
	TargetHeight = Height;
	return true;
}

bool CPostProcessStack::Record( FDrawContext& Draw, CAssetSubsystem& Assets, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth, int32 SceneWidth, int32 SceneHeight, float32 X, float32 Y,
								float32 Width, float32 Height, const FMaterialFrameState& State )
{
	if ( !Scene || SceneWidth <= 0 || SceneHeight <= 0 )
	{
		return false;
	}
	TArray<int32> Passes;
	for ( int32 Index = 0; Index < Entries.Num(); ++Index )
	{
		if ( Entries[Index].bEnabled && Entries[Index].Material && Entries[Index].Material->IsValid() )
		{
			Passes.Add( Index );
		}
	}
	if ( Passes.IsEmpty() )
	{
		return false;
	}
	if ( Passes.Num() > 1 && !EnsureTargets( Assets, SceneWidth, SceneHeight ) )
	{
		return false;
	}

	TSharedPtr<IImage> Source = Scene;
	for ( int32 Pass = 0; Pass < Passes.Num(); ++Pass )
	{
		FEntry& Entry = Entries[Passes[Pass]];
		// 強さ: Intensity の数のパラメーターを持つマテリアルにだけ渡す
		Entry.Material->SetScalar( "Intensity", Entry.Intensity );
		const bool bLast = Pass == Passes.Num() - 1;
		if ( bLast )
		{
			return Entry.Material->DrawPostProcess( Draw, X, Y, Width, Height, Source, Depth, State );
		}
		// 途中の結果は、作業用の描画先へ（Scene と同じ大きさ）
		const TSharedPtr<CRenderTarget>& Target = Targets[Pass % 2];
		if ( !Draw.PushTarget( Target ) )
		{
			return false;
		}
		const bool bOk = Entry.Material->DrawPostProcess( Draw, 0.0f, 0.0f, static_cast<float32>( SceneWidth ), static_cast<float32>( SceneHeight ), Source, Depth, State );
		Draw.PopTarget();
		if ( !bOk )
		{
			return false;
		}
		Source = Target;
	}
	return false;
}

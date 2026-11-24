// SPDX-License-Identifier: MIT
#include "GeometryCommandExecutor.h"

CGeometryCommandExecutor::CGeometryCommandExecutor( IRenderGeometry3D* InGeometry, ITextureDrawSource* InTextures, IGeometryBufferSource* InBuffers, IStaticModelSource* InModels )
	: Run( InGeometry, InTextures, InBuffers, InModels )
	, Forward( Run )
	, Submitter( Run, Forward )
	, StateCommands( Run )
	, ShapeCommands( Run, Submitter )
	, ModelCommands( Run, Submitter, Forward )
{
}

void CGeometryCommandExecutor::SetShadowReceiver( int32 Slot, int32 ShadowMap )
{
	if ( Slot < 0 || Slot >= 3 )
	{
		return;
	}
	Run.ShadowSlotMaps[Slot] = ShadowMap < 0 ? -1 : ShadowMap;
	Run.ReceiverSlot = -1;
	for ( int32 Index = 0; Index < 3 && Run.ReceiverSlot < 0; ++Index )
	{
		Run.ReceiverSlot = Run.ShadowSlotMaps[Index] >= 0 ? Index : -1;
	}
}

bool CGeometryCommandExecutor::BuildShaderQuad( const FDrawShaderQuad3DData& Data, TArray<FDrawShaderVertex3D>& Out )
{
	return ShapeCommands.BuildShaderQuad( Data, Out );
}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::DrawModel( const FDrawModelData& Data, int32 Alpha, bool bAlphaBlend )
{
	return ModelCommands.DrawModel( Data, Alpha, bAlphaBlend );
}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::DrawModelView( const FStaticModelDrawView& Model, int32 Alpha, bool bAlphaBlend )
{
	return ModelCommands.DrawModelView( Model, Alpha, bAlphaBlend );
}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::DrawModelViewAt( const FStaticModelDrawView& Model, const float32 ( &Matrix )[12], int32 Alpha, bool bAlphaBlend )
{
	return ModelCommands.DrawModelViewAt( Model, Matrix, Alpha, bAlphaBlend );
}

CGeometryCommandExecutor::EResult CGeometryCommandExecutor::Execute( const FDrawCommandView& View, FDrawRgb Bright, int32 Alpha, bool bAlphaBlend )
{
	EResult Result = StateCommands.Execute( View ); // カメラ・ワールド・深度・ライト・フォグなどの状態。
	if ( Result == EResult::NotHandled )
	{
		Result = ShapeCommands.Execute( View, Bright, Alpha, bAlphaBlend ); // 3Dの図形。
	}
	if ( Result == EResult::NotHandled )
	{
		Result = ModelCommands.Execute( View, Alpha, bAlphaBlend ); // 静的モデル・メッシュ。
	}
	return Result;
}

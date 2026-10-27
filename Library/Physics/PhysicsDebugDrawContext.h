// SPDX-License-Identifier: NOASSERTION
#pragma once
#include "Library/Core/Interfaces/Physics/IPhysicsDebugDrawer.h"
#include "Library/Framework/Render/DrawContext.h"

//=============================================================================
// FPhysicsDebugDrawContext
//   物理のデバッグ描画の線を、FDrawContext（描画コマンド）の 3D の線として記録する。
//   物理の側（IPhysicsDebug）は FDrawContext を知らず、この橋渡しだけが両方を知る。
//
//   Scene.AddDrawSystem( ..., [&]( FDrawContext& Draw, ... )
//   {
//       FPhysicsDebugDrawContext Drawer( Draw );
//       Physics->DrawDebug( Drawer, FPhysicsDebugDrawSettings() );
//   } );
//=============================================================================
class FPhysicsDebugDrawContext final : public IPhysicsDebugDrawer
{
public:
	explicit FPhysicsDebugDrawContext( FDrawContext& InDraw ) : Draw( InDraw ) {}

	void DrawLine( const FVector3& A, const FVector3& B, const FColor32& Color ) override
	{
		Draw.Line3D( A, B, Color );
	}

private:
	FDrawContext& Draw;
};

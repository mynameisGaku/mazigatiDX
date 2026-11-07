// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Physics/IPhysicsDebugDrawer.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Render/Draw/DrawContext.h"

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
//
//   Mapper を渡すと、線は Line3D ではなく、Mapper が描く（2D のゲームで、物理の座標を、自分の画面の座標に変えて描きたいとき）。
//=============================================================================
class FPhysicsDebugDrawContext final : public IPhysicsDebugDrawer
{
public:
	using FLineMapper = TCallable<void( FDrawContext& Draw, const FVector3& A, const FVector3& B, const FColor32& Color )>;

	explicit FPhysicsDebugDrawContext( FDrawContext& InDraw, const FLineMapper* InMapper = nullptr ) : Draw( InDraw ), Mapper( InMapper != nullptr && *InMapper ? InMapper : nullptr ) {}

	void DrawLine( const FVector3& A, const FVector3& B, const FColor32& Color ) override
	{
		if ( Mapper != nullptr )
		{
			( *Mapper )( Draw, A, B, Color );
		}
		else
		{
			Draw.Line3D( A, B, Color );
		}
	}

private:
	FDrawContext& Draw;
	const FLineMapper* Mapper = nullptr;
};

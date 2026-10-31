// SPDX-License-Identifier: MIT
#include "DxLibShadowMap.h"

#include "Library/Framework/Render/ShadowMapView.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 命令のデータを、値の型に直して、使える値か確かめる（記録のときにも確かめるが、壊れた命令も来るので、実行のときにも確かめる）
	bool IsUsable( const FDrawBeginShadowMapData& Data )
	{
		FShadowMapView View;
		View.LightDirection = FVector3( Data.Direction[0], Data.Direction[1], Data.Direction[2] );
		View.bFitToCamera = Data.HasArea == 0;
		View.AreaMin = FVector3( Data.AreaMin[0], Data.AreaMin[1], Data.AreaMin[2] );
		View.AreaMax = FVector3( Data.AreaMax[0], Data.AreaMax[1], Data.AreaMax[2] );
		View.AdjustDepth = Data.AdjustDepth;
		return View.IsValid();
	}
}

bool FDxLibShadowMap::IsStateCommand( EDrawCommand Type )
{
	return Type == EDrawCommand::BeginShadowMap || Type == EDrawCommand::EndShadowMap || Type == EDrawCommand::UseShadowMap;
}

bool FDxLibShadowMap::IsBlockedInPass( EDrawCommand Type )
{
	switch ( Type )
	{
	case EDrawCommand::PushTarget:
	case EDrawCommand::PopTarget:
	case EDrawCommand::PushCamera:
	case EDrawCommand::PopCamera:
	case EDrawCommand::PushClip:
	case EDrawCommand::PopClip:
	case EDrawCommand::PushMask:
	case EDrawCommand::PopMask:
	case EDrawCommand::Clear:
	case EDrawCommand::ClearDepth:
	case EDrawCommand::CopyScreen:
	case EDrawCommand::ImageFilter:
	case EDrawCommand::ImageBlend:
	case EDrawCommand::UseShadowMap:
	case EDrawCommand::PreviewShadowMap:
		return true;
	default:
		return false;
	}
}

bool FDxLibShadowMap::Apply( const FDrawCommandView& Command )
{
	switch ( Command.Type )
	{
	case EDrawCommand::BeginShadowMap:
	{
		if ( !Command.CanRead<FDrawBeginShadowMapData>() )
		{
			// 壊れた命令。対の EndShadowMap まで、描く命令を飛ばす（描画先が替わっていないので、そのまま描くと画面に描かれてしまう）
			if ( State == EState::Idle )
			{
				State = EState::Skipping;
			}
			else
			{
				++NumNestedBegins;
			}
			return true;
		}
		if ( State != EState::Idle )
		{
			// 入れ子にはできない。内側は無視する（対の EndShadowMap も無視する）
			++NumNestedBegins;
			return true;
		}
		const FDrawBeginShadowMapData Data = Command.Read<FDrawBeginShadowMapData>();
		State = EState::Skipping;
		if ( Data.Handle < 0 || !IsUsable( Data ) )
		{
			return true;
		}
		// 向き・範囲・補正は、ShadowMap_DrawSetup のときに DxLib が覚える（実測: 描いたあとに替えても、影は変わらない）
		const VECTOR Direction = VGet( Data.Direction[0], Data.Direction[1], Data.Direction[2] );
		if ( SetShadowMapLightDirection( Data.Handle, Direction ) != 0 || SetShadowMapAdjustDepth( Data.Handle, Data.AdjustDepth ) != 0 )
		{
			return true;
		}
		if ( Data.HasArea != 0 )
		{
			if ( SetShadowMapDrawArea( Data.Handle, VGet( Data.AreaMin[0], Data.AreaMin[1], Data.AreaMin[2] ), VGet( Data.AreaMax[0], Data.AreaMax[1], Data.AreaMax[2] ) ) != 0 )
			{
				return true;
			}
		}
		else if ( ResetShadowMapDrawArea( Data.Handle ) != 0 )
		{
			return true;
		}
		if ( ShadowMap_DrawSetup( Data.Handle ) == 0 )
		{
			State = EState::Drawing;
		}
		return true;
	}
	case EDrawCommand::EndShadowMap:
	{
		if ( NumNestedBegins > 0 )
		{
			--NumNestedBegins;
			return true;
		}
		if ( State == EState::Drawing )
		{
			ShadowMap_DrawEnd();
		}
		State = EState::Idle;
		return true;
	}
	case EDrawCommand::UseShadowMap:
	{
		if ( !Command.CanRead<FDrawUseShadowMapData>() )
		{
			return true;
		}
		const FDrawUseShadowMapData Data = Command.Read<FDrawUseShadowMapData>();
		if ( Data.Slot < 0 || Data.Slot >= NDrawShadowMap::NumSlots )
		{
			return true;
		}
		if ( Data.Handle < 0 )
		{
			SetUseShadowMap( Data.Slot, -1 );
			bSlotUsed[Data.Slot] = false;
			return true;
		}
		// DxLib が断る（解放済みなど）なら、そのスロットは前のまま
		if ( SetUseShadowMap( Data.Slot, Data.Handle ) == 0 )
		{
			bSlotUsed[Data.Slot] = true;
		}
		return true;
	}
	case EDrawCommand::PreviewShadowMap:
	{
		if ( Command.CanRead<FDrawPreviewShadowMapData>() )
		{
			const FDrawPreviewShadowMapData Data = Command.Read<FDrawPreviewShadowMapData>();
			if ( Data.Handle >= 0 && Data.X1 > Data.X0 && Data.Y1 > Data.Y0 )
			{
				TestDrawShadowMap( Data.Handle, Data.X0, Data.Y0, Data.X1, Data.Y1 );
			}
		}
		return true;
	}
	default:
		return false;
	}
}

void FDxLibShadowMap::Restore()
{
	// 終わらせ忘れた影の絵を終わらせる（先に。描画先・カメラは、影の絵が終わってから戻す）
	if ( State == EState::Drawing )
	{
		ShadowMap_DrawEnd();
	}
	State = EState::Idle;
	NumNestedBegins = 0;
	for ( int Slot = 0; Slot < NDrawShadowMap::NumSlots; ++Slot )
	{
		if ( bSlotUsed[Slot] )
		{
			SetUseShadowMap( Slot, -1 );
			bSlotUsed[Slot] = false;
		}
	}
}

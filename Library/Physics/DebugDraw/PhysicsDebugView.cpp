// SPDX-License-Identifier: MIT
#include "PhysicsDebugView.h"

#include "Library/Framework/DebugView/DebugViews.h"
#include "Library/Physics/DebugDraw/PhysicsDebugDrawContext.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	bool GEnabled = false;
	FPhysicsDebugDrawSettings GSettings;
	int32 GLastLineCount = 0;
	FPhysicsDebugDrawContext::FLineMapper GLineMapper;
	bool GRegistered = false;
}

void NPhysicsDebugView::Register()
{
	if ( !GRegistered )
	{
		GRegistered = true;
		// 起動引数 --physics-debug は、最初の 1 回だけ（あとで、画面やコンソールでオフにしたものを、シーンの切り替えで、また、オンにしない）
		if ( NCommandLine::HasFlag( "physics-debug" ) )
		{
			GEnabled = true;
		}
	}
	using EType = FDebugViewOption::EType;
	FDebugView View;
	View.Name = ViewName;
	View.Description = "Rigid body shapes, joints and characters, drawn as lines over the game";
	View.pEnabled = &GEnabled;
	View.bHasHotkey = true;
	View.Hotkey = EKey::F4;
	const auto Add = [&View]( const char* Name, EType Type, void* Value, float32 Min, float32 Max, const char* Description )
	{
		FDebugViewOption Option;
		Option.Name = Name;
		Option.Type = Type;
		Option.Value = Value;
		Option.Min = Min;
		Option.Max = Max;
		Option.Description = Description;
		View.Options.Add( Option );
	};
	Add( "static", EType::Bool, &GSettings.bDrawStatic, 0.0f, 1.0f, "Draw bodies that do not move (terrain, walls)" );
	Add( "kinematic", EType::Bool, &GSettings.bDrawKinematic, 0.0f, 1.0f, "Draw kinematic bodies (moved by the program)" );
	Add( "dynamic", EType::Bool, &GSettings.bDrawDynamic, 0.0f, 1.0f, "Draw dynamic bodies (moved by the simulation)" );
	Add( "sensors", EType::Bool, &GSettings.bDrawSensors, 0.0f, 1.0f, "Draw sensors (triggers)" );
	Add( "bounds", EType::Bool, &GSettings.bDrawBounds, 0.0f, 1.0f, "Draw the bounding box of each body" );
	Add( "constraints", EType::Bool, &GSettings.bDrawConstraints, 0.0f, 1.0f, "Draw joints" );
	Add( "characters", EType::Bool, &GSettings.bDrawCharacters, 0.0f, 1.0f, "Draw the ground normal of characters" );
	Add( "region", EType::Bool, &GSettings.bUseRegion, 0.0f, 1.0f, "Only draw near the region center" );
	Add( "radius", EType::Float, &GSettings.RegionRadius, 0.1f, 100000.0f, "Radius of the region (meters)" );
	Add( "centerx", EType::Float, &GSettings.RegionCenter.x, -100000.0f, 100000.0f, "Region center, X" );
	Add( "centery", EType::Float, &GSettings.RegionCenter.y, -100000.0f, 100000.0f, "Region center, Y" );
	Add( "centerz", EType::Float, &GSettings.RegionCenter.z, -100000.0f, 100000.0f, "Region center, Z" );
	Add( "segments", EType::Int, &GSettings.CircleSegments, 8.0f, 128.0f, "Lines per circle / sphere" );
	Add( "maxtriangles", EType::Int, &GSettings.MaxMeshTriangles, 0.0f, 1000000.0f, "Triangle limit for one mesh / convex body (0 = do not draw them)" );
	FDebugViewRegistry::Global().Register( View );
}

bool NPhysicsDebugView::IsEnabled()
{
	return GEnabled;
}

void NPhysicsDebugView::SetEnabled( bool bEnabled )
{
	GEnabled = bEnabled;
}

FPhysicsDebugDrawSettings& NPhysicsDebugView::GetSettings()
{
	return GSettings;
}

int32 NPhysicsDebugView::Draw( const IPhysicsDebug& Debug, FDrawContext& Draw )
{
	if ( !GEnabled )
	{
		GLastLineCount = 0;
		return 0;
	}
	FPhysicsDebugDrawContext Drawer( Draw, &GLineMapper );
	GLastLineCount = Debug.DrawDebug( Drawer, GSettings );
	return GLastLineCount;
}

void NPhysicsDebugView::SetLineMapper( const FLineMapper& Mapper )
{
	GLineMapper = Mapper;
}

int32 NPhysicsDebugView::GetLastLineCount()
{
	return GLastLineCount;
}

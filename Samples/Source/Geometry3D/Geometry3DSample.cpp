// SPDX-License-Identifier: MIT
#include "Geometry3DSample.h"
#include "Source/Geometry3D/Geometry3DGallery.h"
#include "Source/Catalog/SampleCatalog.h"
#include "Library/Framework/Render/Scene/SceneDrawList.h"
#include "Library/Framework/Render/Draw/ImmediateDraw.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Library/Platform/System/CommandLine.h"

namespace
{
	// 描画APIを指定せず、同じ場面を各バックエンドへ渡す。
	const TSampleRegistrar<CGeometry3DSample> Registrar( FSampleInfo { "Geometry3D", "Geometry3D: camera and depth", "Unlit boxes, spheres, capsules, billboards, textured meshes and vertex/index buffers with camera and depth testing.", 16 } );
}

void CGeometry3DSample::OnEnter()
{
	const TOptional<FString> Option = NCommandLine::GetValue( "scene-snapshot" ); // 所有と状態分離の固定シーン。
	if ( Option.IsSet() )
	{
		CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>(); // 画像の作成はゲームスレッドで済ませる。
		if ( !Assets ) { return; }
		TSharedPtr<CDynamicTexture> Texture = Assets->CreateDynamicTexture( "Geometry3D.Snapshot", FPixelImage( 2, 2, FColor32::White ) ); // 全項目で共有する画像。
		if ( !Texture ) { return; }
		const FString Mode = Option.GetValue(); // reference・sorted・reverse・mutated。
		bSnapshotMode = true;
		bReferenceMode = Mode == "reference";
		FCamera Camera = FCamera::MakePerspective( FVector3( 0, 0, -9 ), FVector3::Zero ); // 公開時に固定するカメラ。
		TArray<FDrawBuffer> Sources; // ワーカーごとに独立して記録する入力。
		Sources.SetNum( 3 );
		TArray<FSceneDrawSnapshot> Batches; // 完了順ではなく添字順に合流する結果。
		Batches.SetNum( 3 );
		bool Accepted[3] = {}; // 各仕事が別々の要素へ受付結果を返す。
		auto Record = [&]( int32 Step )
		{
			const int32 Index = Mode == "reverse" ? 2 - Step : Step; // 逆順の記録でも同じ画像になるかを調べる。
			FDrawContext Draw( Sources[Index] ); // 他のワーカーの出力を触らない。
			Draw.UseLighting( false );
			Draw.Depth( Index == 0 ? FDepthState::Normal() : FDepthState::ReadOnly() );
			Draw.Blend( Index == 0 ? EDrawBlend::Opaque : EDrawBlend::Alpha, Index == 2 ? 200 : 255 );
			FDrawQuad3DData Quad; // 不透明な奥、青い奥、赤い手前の順に合成する板。
			Quad.Pos[2] = static_cast<float32>( 2 - Index );
			Quad.Width = 4;
			Quad.Height = 4;
			Quad.Handle = Texture->GetHandle();
			Quad.Color = Index == 0 ? FDrawRgba{ 40, 230, 60, 255 } : Index == 1 ? FDrawRgba{ 50, 70, 240, 128 } : FDrawRgba{ 240, 50, 60, 128 };
			Draw.Quad3D( Quad );
			CSceneDrawList Batch( Camera ); // 同じビューで各ジョブの値を固定する。
			Accepted[Step] = Batch.KeepAlive( Texture ) && Batch.Add( { Sources[Index].Words.GetData(), Sources[Index].Words.Num() }, Index == 0 ? ESceneDrawClass::Opaque : ESceneDrawClass::Translucent, FVector3( 0, 0, Quad.Pos[2] ) );
			Batches[Step] = Batch.Freeze();
		}; // ジョブからは描画APIや画像作成を呼ばない。
		if ( IJobSystem* Jobs = GetAppSubsystem<IJobSystem>() ) { Jobs->ParallelFor( 3, 1, "Geometry3D.RecordSnapshot", Record ); }
		else { for ( int32 Step = 0; Step < 3; ++Step ) { Record( Step ); } }
		if ( !Accepted[0] || !Accepted[1] || !Accepted[2] ) { bSnapshotMode = false; SpawnObject<CGeometry3DGallery>(); return; }
		CSceneDrawList Scene( Camera ); // 所有者が定めた順でのみ合流する。
		for ( const FSceneDrawSnapshot& Batch : Batches ) { if ( !Scene.Append( Batch ) ) { bSnapshotMode = false; SpawnObject<CGeometry3DGallery>(); return; } }
		Snapshot = Scene.Freeze();
		ReferencePackets.SetNum( 3 );
		for ( int32 Index = 0; Index < 3; ++Index )
		{
			FDrawContext Draw( ReferencePackets[Index] ); // 手動基準は並べ替え・合流を使わない。
			Draw.PushCamera( Camera );
			ReferencePackets[Index].Words.Append( Sources[Index].Words );
			Draw.PopCamera();
		}
		if ( Mode == "mutated" )
		{
			Camera.Position = FVector3( 1000, 1000, 1000 );
			for ( FDrawBuffer& Source : Sources ) { Source.Clear(); }
		}
		Texture.Reset(); // 以降、画像は確定した一覧の共有所有で保持する。
		return;
	}
	SpawnObject<CGeometry3DGallery>();
}

void CGeometry3DSample::OnSampleDraw( FImmediateDraw& Screen )
{
	if ( !bSnapshotMode ) { return; }
	Screen.Flush();
	IDrawExecutor* Executor = GetDrawExecutor(); // 再生は既存の描画スレッド窓口へ渡す。
	if ( !Executor ) { return; }
	if ( bReferenceMode )
	{
		for ( const FDrawBuffer& Packet : ReferencePackets ) { Executor->Execute( { Packet.Words.GetData(), Packet.Words.Num() } ); }
	}
	else { Snapshot.Replay( *Executor ); }
}

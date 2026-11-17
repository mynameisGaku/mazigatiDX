// SPDX-License-Identifier: MIT
#include "MaterialSnapshotScene.h"
#include "Library/Framework/Material/Material.h"
#include "Library/Framework/Render/Scene/SceneDrawList.h"
#include "Library/Framework/Asset/AssetSubsystem.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Core/Interfaces/Job/IJobSystem.h"
#include "Source/Common/ShaderMeshes.h"

namespace
{
	// 同じグラフ入力からUnlit、DefaultLit、Substrateの既存コンパイラへ渡す定義。
	FMaterialDefinition Definition( int32 Index )
	{
		FMaterialDefinition Result; // 外部の材質資産を変更しない検査用の定義。
		Result.Name = FString::Printf( "Snapshot%d", Index );
		Result.Domain = EMaterialDomain::Surface;
		Result.Shading = Index == 0 ? EMaterialShading::Unlit : Index == 1 ? EMaterialShading::DefaultLit : EMaterialShading::Substrate;
		FMaterialGraph& Graph = Result.Graph; // 全陰影方式で共有する入力。
		const int32 Tint = Graph.AddNode( EMaterialNodeKind::VectorParameter, "Tint", Index == 0 ? FVector4( 0.1f, 0.8f, 0.2f, 1 ) : Index == 1 ? FVector4( 0.1f, 0.2f, 0.9f, 1 ) : FVector4( 0.9f, 0.15f, 0.1f, 1 ) ); // 元の材質を変更したときの比較対象。
		const int32 Image = Graph.AddNode( EMaterialNodeKind::TextureSample, "Albedo" ); // 差し替え前の画像を保持する入力。
		const int32 Product = Graph.AddNode( EMaterialNodeKind::Multiply ); // 色と画像を両方とも使う。
		Graph.Connect( Product, 0, Tint ); Graph.Connect( Product, 1, Image );
		const int32 Color = Graph.AddNode( EMaterialNodeKind::Mask, "xyz" ); // 三成分の材質色。
		Graph.Connect( Color, 0, Product );
		const int32 Opacity = Graph.AddNode( EMaterialNodeKind::ScalarParameter, "Opacity", FVector4( Index == 0 ? 1.0f : 0.55f, 0, 0, 0 ) ); // 奥は不透明、手前は半透明。
		if ( Index == 2 )
		{
			const int32 Slab = Graph.AddNode( EMaterialNodeKind::SubstrateSlab ); // 既存の層の評価をそのまま使う。
			Graph.Connect( Slab, NMaterialSlabField::BaseColor, Color );
			Graph.Connect( Slab, NMaterialSlabField::Opacity, Opacity );
			Graph.SetOutput( "Surface", Slab );
		}
		else
		{
			Graph.SetOutput( Index == 0 ? "Emissive" : "BaseColor", Color );
			Graph.SetOutput( "Opacity", Opacity );
		}
		return Result;
	}
}

bool CMaterialSnapshotScene::Initialize( const FString& Mode )
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>(); // 資源の作成はジョブへ渡す前に済ませる。
	if ( !Assets ) { return false; }
	const TSharedPtr<CDynamicTexture> BaseImage = Assets->CreateDynamicTexture( "Snapshot.Base", FPixelImage( 2, 2, FColor32( 190, 220, 170 ) ) ); // 固定する画像。
	const TSharedPtr<CDynamicTexture> Changed = Assets->CreateDynamicTexture( "Snapshot.Changed", FPixelImage( 2, 2, FColor32( 240, 30, 240 ) ) ); // 変更が画像へ出ることも確認する画像。
	if ( !BaseImage || !Changed ) { return false; }
	const FCamera Camera = FCamera::MakePerspective( FVector3( 0, 0, -9 ), FVector3::Zero ); // 記録する固定ビュー。
	FMaterialSceneState Scene; // 三種類の陰影へ同じ光を渡す。
	Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( { 1280, 720 } );
	Scene.CameraPosition = Camera.Position;
	Scene.LightDirection = FVector3( 0.2f, -0.4f, 1.0f );
	Scene.LightColor = FVector3( 1.0f, 0.9f, 0.8f );
	TSharedPtr<CMaterial> Materials[3]; // 固定後に変更・解放する元の材質。
	FMaterialSurfaceSnapshot Captured[3]; // ワーカーはこの不変値だけを読む。
	const NShaderMeshes::FMesh& Mesh = NShaderMeshes::GetSphere(); // 静的な形の初期化もゲームスレッドで済ませる。
	Reference.SetNum( 3 );
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		Materials[Index] = CMaterial::Create( Definition( Index ) );
		if ( !Materials[Index] || !Materials[Index]->SetTexture( "Albedo", BaseImage ) ) { return false; }
		Captured[Index] = Materials[Index]->CreateSurfaceSnapshot( Scene );
		if ( !Captured[Index].IsValid() ) { return false; }
		FDrawBuffer Probe; // 有効な固定材質でも不正な個数は入力を読まず、以前の出力を保つ。
		Probe.Words.Add( 0x12345678 );
		if ( Captured[Index].BuildMeshCommands( Probe, Mesh.Vertices.GetData(), TNumericLimits<int32>::Max(), Mesh.Indices.GetData(), Mesh.Indices.Num(), FMatrix4x4() ) || Probe.Words.Num() != 1 || Probe.Words[0] != 0x12345678 ) { return false; }
		if ( Captured[Index].BuildMeshCommands( Probe, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), TNumericLimits<int32>::Max(), FMatrix4x4() ) || Probe.Words.Num() != 1 || Probe.Words[0] != 0x12345678 ) { return false; }
		FDrawContext Draw( Reference[Index] ); // 手動の基準は元の材質の従来経路で作る。
		Draw.Depth( Index == 0 ? FDepthState::Normal() : FDepthState::ReadOnly() );
		Draw.Blend( Index == 0 ? EDrawBlend::Opaque : EDrawBlend::Alpha );
		if ( !Materials[Index]->BeginSurface( Draw, Scene ) || !Materials[Index]->DrawSurfaceMesh( Draw, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), FMatrix4x4::Scale( 1.8f ) * FMatrix4x4::Translate( FVector3( 0, 0, static_cast<float32>( 2 - Index ) ) ), FColor32::White ) ) { return false; }
		Materials[Index]->EndSurface( Draw );
	}
	FSceneDrawSnapshot Batches[3]; // 各記録元の公開結果。
	bool Accepted[3] = {}; // 各記録元が別々に書き込む成否。
	auto Record = [&]( int32 Step )
	{
		const int32 Index = Mode == "reverse" ? 2 - Step : Step; // 完了順でなく、明示した合流順を使う。
		const FVector3 Center( 0, 0, static_cast<float32>( 2 - Index ) ); // 固定ビュー方向の奥行き。
		CSceneDrawList Batch( Camera ); // 同じ可変の一覧を共有しない。
		Accepted[Step] = Captured[Index].AddMesh( Batch, Index == 0 ? ESceneDrawClass::Opaque : ESceneDrawClass::Translucent, Center, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), FMatrix4x4::Scale( 1.8f ) * FMatrix4x4::Translate( Center ) );
		Batches[Step] = Batch.Freeze();
	}; // ここからはCMaterialやアセット窓口へアクセスしない。
	if ( IJobSystem* Jobs = GetAppSubsystem<IJobSystem>() ) { Jobs->ParallelFor( 3, 1, "Materials.RecordSnapshot", Record ); }
	else { for ( int32 Step = 0; Step < 3; ++Step ) { Record( Step ); } }
	if ( !Accepted[0] || !Accepted[1] || !Accepted[2] ) { return false; }
	CSceneDrawList List( Camera ); // 完了後に所有者が一定順で合流する。
	for ( const FSceneDrawSnapshot& Batch : Batches ) { if ( !List.Append( Batch ) ) { return false; } }
	Snapshot = List.Freeze();
	bManual = Mode == "reference" || Mode == "live-mutated";
	if ( Mode == "mutated" || Mode == "live-mutated" )
	{
		Scene.LightColor = FVector3::Zero;
		for ( int32 Index = 0; Index < 3; ++Index )
		{
			Materials[Index]->SetVector( "Tint", FVector4( 0.1f, 0.9f, 0.9f, 1 ) );
			Materials[Index]->SetTexture( "Albedo", Changed );
			if ( !Materials[Index]->Reload( Definition( Index ) ) ) { return false; }
			if ( Mode == "live-mutated" )
			{
				Reference[Index].Clear();
				FDrawContext Draw( Reference[Index] ); // 固定しない経路では変更が画像へ出ることを確認する。
				Draw.Depth( Index == 0 ? FDepthState::Normal() : FDepthState::ReadOnly() );
				Draw.Blend( Index == 0 ? EDrawBlend::Opaque : EDrawBlend::Alpha );
				if ( !Materials[Index]->BeginSurface( Draw, Scene ) || !Materials[Index]->DrawSurfaceMesh( Draw, Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num(), FMatrix4x4::Scale( 1.8f ) * FMatrix4x4::Translate( FVector3( 0, 0, static_cast<float32>( 2 - Index ) ) ), FColor32::White ) ) { return false; }
				Materials[Index]->EndSurface( Draw );
				LiveMaterials.Add( Materials[Index] );
			}
		}
	}
	return Snapshot.IsValid();
}

void CMaterialSnapshotScene::Draw( IDrawExecutor& Executor ) const
{
	if ( bManual ) { for ( const FDrawBuffer& Item : Reference ) { Executor.Execute( { Item.Words.GetData(), Item.Words.Num() } ); } }
	else { Snapshot.Replay( Executor ); }
}

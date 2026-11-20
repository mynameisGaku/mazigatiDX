// SPDX-License-Identifier: MIT
#include "Library/Framework/Render/Mesh/ShaderMeshSnapshot.h"
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
		Result.SurfaceBlend = Index == 0 ? EMaterialSurfaceBlend::Opaque : EMaterialSurfaceBlend::Translucent;
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
	if ( Mode == "masked" ) { return InitializeMasked(); }
	if ( Mode == "masked-shadow" ) { return InitializeMaskedShadow(); }
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
	const FShaderMeshSnapshot Shape = FShaderMeshSnapshot::Create( Mesh.Vertices.GetData(), Mesh.Vertices.Num(), Mesh.Indices.GetData(), Mesh.Indices.Num() ); // 三つの材質と並列記録が同じ形状を使う。
	if ( !Shape.IsValid() ) { return false; }
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
		Accepted[Step] = Captured[Index].AddMesh( Batch, Center, Shape, FMatrix4x4::Scale( 1.8f ) * FMatrix4x4::Translate( Center ) );
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
			FMaterialDefinition ChangedDefinition = Definition( Index ); // 元の定義の分類も変えて固定値を検査する。
			ChangedDefinition.SurfaceBlend = EMaterialSurfaceBlend::Manual;
			if ( !Materials[Index]->Reload( ChangedDefinition ) ) { return false; }
			ESceneDrawClass FrozenClass; // 再読み込み後も固定時の分類を返す。
			if ( !Captured[Index].TryGetDrawClass( FrozenClass ) || FrozenClass != ( Index == 0 ? ESceneDrawClass::Opaque : ESceneDrawClass::Translucent ) ) { return false; }
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

bool CMaterialSnapshotScene::InitializeMasked()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>(); // 一色の試験画像を作成する窓口。
	if ( !Assets ) { return false; }
	const TSharedPtr<CDynamicTexture> White = Assets->CreateDynamicTexture( "Snapshot.MaskWhite", FPixelImage( 1, 1, FColor32::White ) ); // グラフへ白を渡す。
	if ( !White ) { return false; }
	const FCamera Camera = FCamera::MakePerspective( FVector3( 0, 0, -9 ), FVector3::Zero ); // 左右の画素を対称に検査する。
	FMaterialSceneState Scene; // Unlitの平面が使う固定ビュー。
	Scene.ViewProjection = Camera.GetViewMatrix() * Camera.GetProjectionMatrix( { 1280, 720 } );
	const FShaderVertex3D Vertices[4] = { FShaderVertex3D::Make( FVector3( -1, -1, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( -1, 1, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( 1, 1, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( 1, -1, 0 ), FVector3::Up, FColor32::White ) }; // 左右と背面で同じ四角形を使う。
	const uint16 Indices[6] = { 0, 1, 2, 0, 2, 3 }; // 四角形の二枚の三角形。
	const FShaderMeshSnapshot Mesh = FShaderMeshSnapshot::Create( Vertices, 4, Indices, 6 ); // 再生まで一覧が保持する。
	CSceneDrawList Batch( Camera ); // 半透明を先に受付し、分類による描画順も使う。
	for ( int32 Index = 0; Index < 3; ++Index )
	{
		FMaterialDefinition Def = Definition( 0 ); // 共通のUnlitグラフ。
		Def.SurfaceBlend = Index == 0 ? EMaterialSurfaceBlend::Translucent : EMaterialSurfaceBlend::Masked;
		const TSharedPtr<CMaterial> Material = CMaterial::Create( Def ); // 左右は同じグラフへ別の不透明度を渡す。
		if ( !Material || !Material->SetTexture( "Albedo", White ) ) { return false; }
		Material->SetVector( "Tint", Index == 0 ? FVector4( 1, 0, 0, 1 ) : FVector4( 0, 1, 0, 1 ) );
		if ( !Material->SetScalar( "Opacity", Index == 1 ? 0.5f : 0.75f ) ) { return false; }
		const FMaterialSurfaceSnapshot Frozen = Material->CreateSurfaceSnapshot( Scene ); // 完成したシェーダーと分類を共有する。
		const FVector3 Center( Index == 0 ? 0.0f : Index == 1 ? -1.1f : 1.1f, 0, Index == 0 ? 0.5f : 0.0f ); // 奥の赤を手前の緑の穴から見る。
		const FMatrix4x4 World = FMatrix4x4::Scale( Index == 0 ? 2.5f : 1.0f ) * FMatrix4x4::Translate( Center ); // 奥の平面が左右を覆う。
		if ( Index != 0 && Frozen.AddMesh( Batch, ESceneDrawClass::Opaque, Center, Mesh, World ) ) { return false; }
		if ( !Frozen.AddMesh( Batch, Center, Mesh, World ) ) { return false; }
	}
	Snapshot = Batch.Freeze();
	return Snapshot.IsValid() && Snapshot.GetNumItems() == 3;
}

bool CMaterialSnapshotScene::InitializeMaskedShadow()
{
	CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>(); // 影とマスク画像を所有する窓口。
	if ( !Assets ) { return false; }
	MaskShadow = CMaterialShadowMap::Create( *Assets, "Snapshot.MaskShadow", 256 );
	if ( !MaskShadow ) { return false; }
	MaskShadow->SetLight( FVector3( 0, 0, 1 ), FVector3::Zero, 2.5f );
	FMaterialDefinition Def = Definition( 0 ); // 色と同じグラフに画像のアルファを掛ける。
	Def.SurfaceBlend = EMaterialSurfaceBlend::Masked;
	const int32 Image = Def.Graph.AddNode( EMaterialNodeKind::TextureSample, "Cutout" ); // 材質インスタンスで差し替える入力。
	const int32 Alpha = Def.Graph.AddNode( EMaterialNodeKind::Mask, "w" ); // 画像のアルファ成分。
	Def.Graph.Connect( Alpha, 0, Image );
	const int32 Opacity = Def.Graph.AddNode( EMaterialNodeKind::ScalarParameter, "ShadowOpacity", FVector4( 0.5f, 0, 0, 0 ) ); // しきい値と同じ値を含める。
	const int32 Product = Def.Graph.AddNode( EMaterialNodeKind::Multiply ); // 色と影で同じ不透明度を評価する。
	Def.Graph.Connect( Product, 0, Alpha );
	Def.Graph.Connect( Product, 1, Opacity );
	Def.Graph.SetOutput( "Opacity", Product );
	const TSharedPtr<CMaterial> Parent = CMaterial::Create( Def ); // 共通の影シェーダーを持つ親材質。
	const TSharedPtr<CDynamicTexture> White = Assets->CreateDynamicTexture( "Snapshot.ShadowWhite", FPixelImage( 1, 1, FColor32::White ) ); // 有効な不透明度を持つ画像。
	const TSharedPtr<CDynamicTexture> Empty = Assets->CreateDynamicTexture( "Snapshot.ShadowEmpty", FPixelImage( 1, 1, FColor32( 255, 255, 255, 0 ) ) ); // RGBは同じで透明な画像。
	if ( !Parent || !White || !Empty || !Parent->SetTexture( "Cutout", White ) || !Parent->PrepareMaskedShadowShader() ) { return false; }
	const TSharedPtr<CMaterial> Solid = CMaterial::CreateInstance( Parent ); // 親の白画像と上書きの不透明度を使う。
	const TSharedPtr<CMaterial> Hole = CMaterial::CreateInstance( Parent ); // 透明な画像を個体で上書きする。
	if ( !Solid || !Hole || !Solid->SetScalar( "ShadowOpacity", 0.75f ) || !Hole->SetScalar( "ShadowOpacity", 0.75f ) || !Hole->SetTexture( "Cutout", Empty ) ) { return false; }
	const FShaderVertex3D Verts[4] = { FShaderVertex3D::Make( FVector3( -0.6f, -0.6f, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( -0.6f, 0.6f, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( 0.6f, 0.6f, 0 ), FVector3::Up, FColor32::White ), FShaderVertex3D::Make( FVector3( 0.6f, -0.6f, 0 ), FVector3::Up, FColor32::White ) }; // 等値・残る画素・透明の三領域。
	const uint16 Indices[6] = { 0, 1, 2, 0, 2, 3 }; // 四角形の添字。
	Reference.SetNum( 1 );
	FDrawContext Draw( Reference[0] ); // 影生成と結果の表示を同じ実行範囲に記録する。
	FMaterialSceneState Scene; // 色パスと共通に渡せる不透明度の入力。
	if ( !MaskShadow->Begin( Draw ) ) { return false; }
	const bool bEqual = MaskShadow->DrawMaskedMesh( Draw, *Parent, Scene, Verts, 4, Indices, 6, FMatrix4x4::Translate( FVector3( -1.4f, 0, 0 ) ) ); // 0.5と同値で穴。
	const bool bSolid = MaskShadow->DrawMaskedMesh( Draw, *Solid, Scene, Verts, 4, Indices, 6, FMatrix4x4::Identity ); // 0.75かつ白なので深度を書く。
	const bool bHole = MaskShadow->DrawMaskedMesh( Draw, *Hole, Scene, Verts, 4, Indices, 6, FMatrix4x4::Translate( FVector3( 1.4f, 0, 0 ) ) ); // 個体の画像で穴。
	const bool bRear = MaskShadow->DrawMesh( Draw, Verts, 4, Indices, 6, FMatrix4x4::Scale( 4.0f ) * FMatrix4x4::Translate( FVector3( 0, 0, 2.0f ) ) ); // 前の穴から見える通常の奥の影。
	MaskShadow->End( Draw );
	Draw.Image( 512.0f, 232.0f, MaskShadow->GetTarget()->GetImageHandle() );
	LiveMaterials.Add( Parent );
	LiveMaterials.Add( Solid );
	LiveMaterials.Add( Hole );
	bManual = true;
	return bEqual && bSolid && bHole && bRear;
}

void CMaterialSnapshotScene::Draw( IDrawExecutor& Executor ) const
{
	if ( bManual ) { for ( const FDrawBuffer& Item : Reference ) { Executor.Execute( { Item.Words.GetData(), Item.Words.Num() } ); } }
	else { Snapshot.Replay( Executor ); }
}

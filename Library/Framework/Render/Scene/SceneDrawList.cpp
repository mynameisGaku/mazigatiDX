// SPDX-License-Identifier: MIT
#include "SceneDrawList.h"
#include "Library/Framework/Render/Scene/Private/SceneDrawSnapshotData.h"
#include "Library/Framework/Render/Scene/SceneDrawOrder.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Scene/Private/SceneShaderCommandValidation.h"

namespace
{
	constexpr int32 MaxItems = 65536; // 一覧へ受け付ける描画項目数。
	constexpr int32 MaxWords = 4 * 1024 * 1024; // カメラを含む合計word数。
	constexpr int32 MaxOwners = 4096; // 一覧で保持する共有所有の数。

	// 項目内で閉じる描画状態の固定長を返す。それ以外は拒否する。
	int32 StateWords( EDrawCommand Type )
	{
		switch ( Type )
		{
		case EDrawCommand::WorldMatrix: return sizeof( FDrawMatrixData ) / 4;
		case EDrawCommand::PushWorldMatrix: return sizeof( FDrawMatrixData ) / 4;
		case EDrawCommand::PopWorldMatrix: return sizeof( FDrawPopWorldMatrixData ) / 4;
		case EDrawCommand::Blend: return sizeof( FDrawBlendData ) / 4;
		case EDrawCommand::CustomBlend: return sizeof( FDrawCustomBlendData ) / 4;
		case EDrawCommand::Bright: return sizeof( FDrawBrightData ) / 4;
		case EDrawCommand::AddColor: return sizeof( FDrawAddColorData ) / 4;
		case EDrawCommand::AlphaTest: return sizeof( FDrawAlphaTestData ) / 4;
		case EDrawCommand::Filter: return sizeof( FDrawFilterData ) / 4;
		case EDrawCommand::Depth: return sizeof( FDrawDepthData ) / 4;
		case EDrawCommand::Culling: return sizeof( FDrawCullingData ) / 4;
		case EDrawCommand::TextureAddress: return sizeof( FDrawTextureAddressData ) / 4;
		case EDrawCommand::TextureTransform: return sizeof( FDrawTextureTransformData ) / 4;
		case EDrawCommand::Fog:
		case EDrawCommand::HeightFog: return sizeof( FDrawFogData ) / 4;
		case EDrawCommand::Anisotropy: return sizeof( FDrawAnisotropyData ) / 4;
		case EDrawCommand::UseLighting: return sizeof( FDrawUseLightingData ) / 4;
		case EDrawCommand::DefaultLight: return sizeof( FDrawDefaultLightData ) / 4;
		case EDrawCommand::AmbientLight: return sizeof( FDrawAmbientLightData ) / 4;
		case EDrawCommand::SurfaceMaterial: return sizeof( FDrawSurfaceMaterialData ) / 4;
		case EDrawCommand::LightingOptions: return sizeof( FDrawLightingOptionsData ) / 4;
		case EDrawCommand::PushLight: return sizeof( FDrawLightData ) / 4;
		case EDrawCommand::PopLight: return sizeof( FDrawPopLightData ) / 4;
		default: return -1;
		}
	}

	// 全命令の長さと、範囲外へ状態を漏らさない構成を確認してから所有コピーする。
	bool ValidCommands( const FDrawSpan& Commands, bool& bUsesMesh, bool& bUsesPose )
	{
		if ( !Commands.Words || Commands.NumWords <= 0 || Commands.NumWords > MaxWords ) { return false; }
		int32 WorldDepth = 0; // 項目内で閉じるワールド行列の範囲。
		int32 LightDepth = 0; // 項目内で閉じるライトの範囲。
		bool bHasDraw = false; // 状態だけの項目は記録しない。
		bool ShaderBound[2] = {}; // 頂点と画素のシェーダーは項目内で明示する。
		for ( int32 Offset = 0; Offset < Commands.NumWords; )
		{
			const uint32 Header = Commands.Words[Offset]; // 残り長を調べるヘッダー。
			const int32 Count = static_cast<int32>( Header >> 16 ); // ヘッダー込みのword数。
			if ( Count < 1 || Count > Commands.NumWords - Offset ) { return false; }
			const FDrawCommandView Command{ static_cast<EDrawCommand>( Header & 0xff ), static_cast<uint8>( Header >> 8 ), Commands.Words + Offset + 1, Count - 1 }; // 範囲検査済みの参照。
			if ( NSceneDrawOrder::IsIndependent( Command ) ) { bHasDraw = true; }
			else if ( NSceneShaderCommand::IsState( Command ) )
			{
				if ( Command.Type == EDrawCommand::UseShader )
				{
					const FDrawUseShaderData Shader = Command.Read<FDrawUseShaderData>(); // 段と番号を検査済みの指定。
					ShaderBound[Shader.Stage] = Shader.Handle >= 0;
				}
			}
			else if ( Command.Type == EDrawCommand::SharedShaderMesh3D )
			{
				FDrawCommandView Mesh = Command; // 形状は作成時に検査済み。ここでは参照と描画状態を確認する。
				if ( !ShaderBound[0] || !ShaderBound[1] || !FShaderMeshSnapshot::Resolve( Commands, Mesh ) ) { return false; }
				bUsesMesh = true;
				bHasDraw = true;
			}
			else if ( Command.Type == EDrawCommand::ModelPoseSnapshot3D )
			{
				if ( !FModelPoseSnapshot::Resolve( Commands, Command ) ) { return false; }
				bUsesPose = true;
				bHasDraw = true;
			}
			else if ( NSceneShaderCommand::IsDraw( Command ) ) { if ( !ShaderBound[0] || !ShaderBound[1] ) { return false; } bHasDraw = true; }
			else if ( StateWords( Command.Type ) != Command.NumDataWords || StateWords( Command.Type ) < 0 ) { return false; }
			if ( Command.Type == EDrawCommand::PushWorldMatrix ) { ++WorldDepth; }
			if ( Command.Type == EDrawCommand::PopWorldMatrix && --WorldDepth < 0 ) { return false; }
			if ( Command.Type == EDrawCommand::PushLight ) { ++LightDepth; }
			if ( Command.Type == EDrawCommand::PopLight && --LightDepth < 0 ) { return false; }
			Offset += Count;
		}
		return bHasDraw && WorldDepth == 0 && LightDepth == 0;
	}
}

CSceneDrawList::CSceneDrawList( const FCamera& Camera ) { Reset( Camera ); }
bool CSceneDrawList::IsValid() const { return Data.IsValid(); }

bool CSceneDrawList::Reset( const FCamera& Camera )
{
	if ( !Camera.IsValid() ) { return false; }
	FDrawBuffer View; // 再生する値と合流時の一致判定を同じ形式へ固定する。
	FDrawContext Draw( View ); // カメラの既存の記録形式を再利用する。
	if ( !Draw.PushCamera( Camera ) ) { return false; }
	TSharedPtr<FSceneDrawSnapshotData> Next = MakeShared<FSceneDrawSnapshotData>(); // 完成するまで現在の記録を変更しない。
	Next->Camera = Camera;
	Next->CameraWords = MoveTemp( View.Words );
	Data = MoveTemp( Next );
	return true;
}

bool CSceneDrawList::Add( const FDrawSpan& Commands, ESceneDrawClass Class, const FVector3& WorldCenter )
{
	bool bUsesMesh = false; // 項目が実際に参照する場合だけ形状を保持する。
	bool bUsesPose = false; // 項目が実際に参照する場合だけモデル姿勢を保持する。
	if ( !Data || Class > ESceneDrawClass::Translucent || Data->Items.Num() >= MaxItems || !ValidCommands( Commands, bUsesMesh, bUsesPose ) ) { return false; }
	const float32 Depth = FVector3::Dot( WorldCenter - Data->Camera.Position, Data->Camera.GetForward() ); // ビューに固定した奥行き。
	if ( !NMath::IsFinite( Depth ) ) { return false; }
	constexpr int32 PopWords = 1 + sizeof( FDrawPopCameraData ) / 4; // 各項目でカメラを必ず閉じる長さ。
	const int32 Overhead = Data->CameraWords.Num() + PopWords; // 追加する固定ビューの命令長。
	if ( Commands.NumWords > MaxWords - Overhead - Data->NumWords ) { return false; }
	FSceneDrawSnapshotData::FItem Item; // ここから追加完了まで入力を所有コピーする。
	Item.Words = Data->CameraWords;
	Item.Words.Append( Commands.Words, Commands.NumWords );
	if ( bUsesMesh ) { Item.ShaderMesh = *Commands.ShaderMesh; }
	if ( bUsesPose ) { Item.ModelPose = *Commands.ModelPose; }
	Item.Words.Add( NDrawCommand::MakeHeader( EDrawCommand::PopCamera, 0, PopWords ) );
	Item.Words.Add( 0 );
	Item.Class = Class;
	Item.Depth = Depth;
	Item.Ordinal = Data->Items.Num();
	Data->NumWords += Item.Words.Num();
	Data->Items.Add( MoveTemp( Item ) );
	return true;
}

bool CSceneDrawList::AddModel( const FModelPoseSnapshot& Pose, ESceneDrawClass Class, const FVector3& WorldCenter, const FMatrix4x4& World, bool bLit )
{
	if ( !Pose.IsValid() || ( Class != ESceneDrawClass::Opaque && Class != ESceneDrawClass::Translucent ) ) { return false; }
	FDrawBuffer Item; // 配置と状態だけを記録し、頂点は複製しない。
	FDrawContext Draw( Item ); // 配置は既存の検査付き命令で記録する。
	if ( !Draw.PushWorldMatrix( World ) ) { return false; }
	Draw.UseLighting( bLit );
	Draw.Depth( Class == ESceneDrawClass::Translucent ? FDepthState::ReadOnly() : FDepthState::Normal() );
	Draw.Blend( Class == ESceneDrawClass::Translucent ? EDrawBlend::Alpha : EDrawBlend::Opaque );
	Item.Words.Add( NDrawCommand::MakeHeader( EDrawCommand::ModelPoseSnapshot3D, 0, 1 ) );
	Draw.PopWorldMatrix();
	return Add( { Item.Words.GetData(), Item.Words.Num(), nullptr, &Pose }, Class, WorldCenter );
}

bool CSceneDrawList::KeepAliveAlias( const TSharedPtr<const uint8>& Owner )
{
	if ( !Data || !Owner || Data->Owners.Num() >= MaxOwners ) { return false; }
	Data->Owners.Add( Owner );
	return true;
}

bool CSceneDrawList::AddOwned( const FDrawSpan& Commands, ESceneDrawClass Class, const FVector3& WorldCenter, const TSharedPtr<const uint8>& Owner )
{
	if ( !Data || !Owner || Data->Owners.Num() >= MaxOwners || !Add( Commands, Class, WorldCenter ) ) { return false; }
	Data->Owners.Add( Owner );
	return true;
}

bool CSceneDrawList::Append( const FSceneDrawSnapshot& Batch )
{
	if ( !Data || !Batch.Data || Data->CameraWords != Batch.Data->CameraWords || Batch.Data->Items.Num() > MaxItems - Data->Items.Num() || Batch.Data->NumWords > MaxWords - Data->NumWords || Batch.Data->Owners.Num() > MaxOwners - Data->Owners.Num() ) { return false; }
	for ( const FSceneDrawSnapshotData::FItem& Source : Batch.Data->Items )
	{
		FSceneDrawSnapshotData::FItem Item = Source; // 確定済みの一覧を変更せず、合流先で所有する。
		Item.Ordinal = Data->Items.Num();
		Data->Items.Add( MoveTemp( Item ) );
	}
	Data->NumWords += Batch.Data->NumWords;
	Data->Owners.Append( Batch.Data->Owners );
	return true;
}

FSceneDrawSnapshot CSceneDrawList::Freeze()
{
	FSceneDrawSnapshot Snapshot; // 未作成の一覧では無効な値のまま返す。
	if ( !Data ) { return Snapshot; }
	Data->Items.Sort( []( const FSceneDrawSnapshotData::FItem& A, const FSceneDrawSnapshotData::FItem& B )
	{
		if ( A.Class != B.Class ) { return A.Class < B.Class; }
		if ( A.Class == ESceneDrawClass::Translucent && A.Depth != B.Depth ) { return A.Depth > B.Depth; }
		return A.Ordinal < B.Ordinal;
	} );
	Snapshot.Data = MoveTemp( Data );
	return Snapshot;
}

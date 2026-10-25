// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectForceFieldEditorGeometrySelfTest.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "AssetSelfTestSupport.h"
#include "Library/DebugUI/EffectEditor.h"
#include "Library/DebugUI/EffectEditorInternal.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Effect/EffectForceFieldDefinition.h"
#include "Library/Framework/Effect/ParticleEffectBackend.h"
#include "Library/Framework/Effect/ParticleEffectEditing.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/Editor/EditorPlacement.h"
#include "Library/Framework/World/WorldEdit.h"
#include "Library/Framework/Asset/WorldAsset.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/FileSystem.h"
#include "VendorModules/ImGui/imgui_internal.h"

namespace
{
	// real testの間だけAssets rootをsandboxへ向け、終了時に必ず前のrootへ戻す。
	class FAssetRootScope
	{
	public:
		// 現在rootを保存し、標準Assets directoryへ切り替える。
		explicit FAssetRootScope( CAssetManager& InAssets ) : Assets( InAssets ), PreviousRoot( InAssets.GetRootDirectory() )
		{
			// 実行環境から見つけたsandbox Assets directory。
			const TOptional<FString> FoundRoot = NFileSystem::FindDirectory( "Assets" );
			if ( !FoundRoot.IsSet() ) return;
			SandboxRoot = NFileSystem::GetFullPath( FoundRoot.GetValue() );
			Assets.SetRootDirectory( SandboxRoot );
			bReady = Assets.GetRootDirectory() == SandboxRoot;
		}

		// scope終了時に切替前のrootを必ず復元する。
		~FAssetRootScope() { Restore(); }
		// 保存された以前のrootを復元し、結果を返す。
		bool Restore()
		{
			if ( !bRestored )
			{
				Assets.SetRootDirectory( PreviousRoot );
				bRestored = true;
			}
			return Assets.GetRootDirectory() == PreviousRoot;
		}
		// sandbox rootを正しく選べたか返す。
		bool IsReady() const { return bReady; }
		// 呼出元が使っていたrootを返す。
		const FString& GetPreviousRoot() const { return PreviousRoot; }

	private:
		// rootを一時変更するasset manager。
		CAssetManager& Assets;
		// real test開始前に復元するroot path。
		FString PreviousRoot;
		// runnerが所有するAssets sandbox root path。
		FString SandboxRoot;
		// Assets folderを見つけてrootを切替えた印。
		bool bReady = false;
		// destructorと明示復元が重ならない印。
		bool bRestored = false;
	};

	// カメラの全値が同じかを比較し、失敗時の部分更新も見つける。
	bool SameCamera( const FEditorCamera& A, const FEditorCamera& B )
	{
		return A.Target == B.Target && A.Yaw == B.Yaw && A.Pitch == B.Pitch && A.Distance == B.Distance;
	}

	// 独立した三次元長さ計算で既知点を画面内へ投影できるか調べる。
	bool IsProjectedInside( const FEditorCamera& Camera, const FEditorViewport& Viewport, const FVector3& Point, float32 Margin = 1.0f )
	{
		// 射影された画面座標。
		FVector2 Pixel;
		// 既知点までの視線方向距離。
		float32 Depth = 0.0f;
		return Camera.WorldToScreen( Viewport, Point, Pixel, &Depth ) && Depth > Viewport.Near && Depth < Viewport.Far && Pixel.x >= Margin && Pixel.y >= Margin && Pixel.x <= Viewport.Width - Margin && Pixel.y <= Viewport.Height - Margin;
	}

	// 力場ありの既知emitterを作り、カメラboundsが扱う既存形状も混ぜる。
	FParticleEffectDefinition MakeForceFieldGeometryDefinition()
	{
		// 既知の力場を含む試験文書。
		FParticleEffectDefinition Definition;
		Definition.Name = "Force field geometry";
		// travelとLightning範囲を持つ一つ目のemitter。
		FParticleEmitterDefinition Emitter;
		Emitter.Lifetime = { 1.0f, 2.0f };
		Emitter.Speed = { 0.5f, 1.5f };
		Emitter.Gravity = FVector3( 0.0f, -0.25f, 0.0f );
		Emitter.Shape = EParticleShape::Box;
		Emitter.BoxSize = FVector3( 0.4f, 0.6f, 0.8f );
		Emitter.StartSize = { 0.5f, 1.0f };
		Emitter.EndSize = { 0.25f, 0.75f };
		Emitter.Appearance.Size.Keys.Empty();
		Emitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		Emitter.Appearance.Size.Keys.Add( { 1.0f, 2.0f } );
		Emitter.Path.Kind = FEffectPathDefinition::EKind::Lightning;
		Emitter.Path.End = FVector3( 1.0f, 2.0f, 3.0f );
		Emitter.Path.Jitter = 0.2f;
		Emitter.Path.Branches = 2;
		Emitter.Path.BranchLength = 0.4f;
		// 正規化端点を検査するUniform field。
		FEffectForceFieldDefinition Uniform;
		Uniform.Kind = FEffectForceFieldDefinition::EKind::Uniform;
		Uniform.Acceleration = FVector3( 4.0f, 0.0f, 0.0f );
		Emitter.ForceFields.Add( Uniform );
		// 符号方向とWorld中心を検査する負Radial。
		FEffectForceFieldDefinition Radial;
		Radial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		Radial.Position = FVector3( 2.0f, -1.0f, 3.0f );
		Radial.Radius = 0.001f;
		Radial.Strength = -2.0f;
		Emitter.ForceFields.Add( Radial );
		// 正strengthで外向きに働く既知field。
		FEffectForceFieldDefinition PositiveRadial;
		PositiveRadial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		PositiveRadial.Position = FVector3( -3.0f, 1.0f, 2.0f );
		PositiveRadial.Radius = 0.25f;
		PositiveRadial.Strength = 1.5f;
		Emitter.ForceFields.Add( PositiveRadial );
		Definition.Emitters.Add( Emitter );
		// 別中心のfieldを持つ二つ目のemitter。
		FParticleEmitterDefinition SecondEmitter;
		SecondEmitter.Lifetime = { 0.5f, 0.75f };
		SecondEmitter.Speed = { 0.25f, 0.5f };
		// 二つ目のemitterにだけ働く一様field。
		FEffectForceFieldDefinition SecondUniform;
		SecondUniform.Acceleration = FVector3( 0.0f, 1.0f, 0.0f );
		SecondEmitter.ForceFields.Add( SecondUniform );
		Definition.Emitters.Add( SecondEmitter );
		return Definition;
	}

	// 既知の単独bounds球をfocusし、手計算した六端点をprojectionで検査する。
	bool HasFocusedAxisExtents( const FParticleEffectDefinition& Definition, const FEditorViewport& Viewport, float64 Radius )
	{
		// 対象definitionだけをfocusする編集状態。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		if ( !NEffectEditorUI::FocusCameraOnForceFields( State, Viewport ) ) return false;
		// 球の六方向を表す独立sample点。
		const FVector3 Points[] = { FVector3( static_cast<float32>( Radius ), 0.0f, 0.0f ), FVector3( -static_cast<float32>( Radius ), 0.0f, 0.0f ), FVector3( 0.0f, static_cast<float32>( Radius ), 0.0f ), FVector3( 0.0f, -static_cast<float32>( Radius ), 0.0f ), FVector3( 0.0f, 0.0f, static_cast<float32>( Radius ) ), FVector3( 0.0f, 0.0f, -static_cast<float32>( Radius ) ) };
		for ( const FVector3& Point : Points ) if ( !IsProjectedInside( State.Camera, Viewport, Point ) ) return false;
		return true;
	}

	// 描画bufferから実際のsphere・line payloadと色を所有して読み取る。
	struct FGuideReadback
	{
		// 実bufferから読み取ったwire球payload。
		TArray<FDrawSphere3DData> Spheres;
		// 実bufferから読み取った矢印線payload。
		TArray<FDrawLine3DData> Lines;
	};

	// line payloadの両端をFVector3として読む。
	FVector3 LinePoint( const float32 Point[3] )
	{
		return FVector3( Point[0], Point[1], Point[2] );
	}

	// guide bufferを型付きで読み、未知命令を読み飛ばす。
	FGuideReadback ReadGuides( const FDrawBuffer& Buffer )
	{
		// 検査で所有するguide command列。
		FGuideReadback Result;
		if ( Buffer.Words.empty() ) return Result;
		const auto ReadCommand = [&]( const FDrawCommandView& Command )
		{
			if ( Command.Type == EDrawCommand::Sphere3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawSphere3DData ) / 4 ) ) Result.Spheres.Add( Command.Read<FDrawSphere3DData>() );
			if ( Command.Type == EDrawCommand::Line3D && Command.NumDataWords == static_cast<int32>( sizeof( FDrawLine3DData ) / 4 ) ) Result.Lines.Add( Command.Read<FDrawLine3DData>() );
		};
		ForEachDrawCommand( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) }, ReadCommand );
		return Result;
	}

	// endpointと色を独立に照合する。
	bool HasLine( const FGuideReadback& Readback, const FVector3& A, const FVector3& B, uint8 R, uint8 G, uint8 Blue )
	{
		for ( const FDrawLine3DData& Line : Readback.Lines )
		{
			// commandに記録された矢印始点。
			const FVector3 P0 = LinePoint( Line.P0 );
			// commandに記録された矢印終点。
			const FVector3 P1 = LinePoint( Line.P1 );
			if ( P0 == A && P1 == B && Line.Color.R == R && Line.Color.G == G && Line.Color.B == Blue ) return true;
		}
		return false;
	}

	// 浮動小数点座標を独立した許容差で比べる。
	bool NearCoordinate( float64 A, float64 B, float64 Epsilon )
	{
		return std::fabs( A - B ) <= Epsilon;
	}

	// command列全体が指定RGBを維持するか確かめる。
	bool AllLinesHaveColor( const FGuideReadback& Readback, uint8 R, uint8 G, uint8 Blue )
	{
		// 描画bufferに記録された全line。
		for ( const FDrawLine3DData& Line : Readback.Lines ) if ( Line.Color.R != R || Line.Color.G != G || Line.Color.B != Blue ) return false;
		return !Readback.Lines.IsEmpty();
	}

	// radial円線の実endpointから三平面・半径・色・既知軸点を確かめる。
	bool HasRadialCircleGuides( const FGuideReadback& Readback, const FVector3& Center, float32 Radius, uint8 R, uint8 G, uint8 Blue )
	{
		// float三角関数の丸めを吸収する座標許容差。
		const float64 Epsilon = std::max( 1.0e-6, static_cast<float64>( Radius ) * 1.0e-4 );
		// XY円に属する線分数。
		int32 XYCount = 0;
		// XZ円に属する線分数。
		int32 XZCount = 0;
		// YZ円に属する線分数。
		int32 YZCount = 0;
		// 三円の実線分数。
		int32 CircleCount = 0;
		// 既知の正負X軸上の円endpointが見つかったか。
		bool bPositiveX = false;
		bool bNegativeX = false;
		// 既知の正負Y軸上の円endpointが見つかったか。
		bool bPositiveY = false;
		bool bNegativeY = false;
		// 既知の正負Z軸上の円endpointが見つかったか。
		bool bPositiveZ = false;
		bool bNegativeZ = false;
		// 円線または矢印を含む実command列。
		for ( const FDrawLine3DData& Line : Readback.Lines )
		{
			if ( Line.Color.R != R || Line.Color.G != G || Line.Color.B != Blue ) return false;
			// commandに記録された円線の開始点。
			const FVector3 P0 = LinePoint( Line.P0 );
			// commandに記録された円線の終点。
			const FVector3 P1 = LinePoint( Line.P1 );
			// 両端点が指定半径の円周上にあるか。
			const bool bBothOnRadius = NearCoordinate( std::sqrt( ( P0.x - Center.x ) * ( P0.x - Center.x ) + ( P0.y - Center.y ) * ( P0.y - Center.y ) + ( P0.z - Center.z ) * ( P0.z - Center.z ) ), Radius, Epsilon ) && NearCoordinate( std::sqrt( ( P1.x - Center.x ) * ( P1.x - Center.x ) + ( P1.y - Center.y ) * ( P1.y - Center.y ) + ( P1.z - Center.z ) * ( P1.z - Center.z ) ), Radius, Epsilon );
			if ( !bBothOnRadius ) continue;
			++CircleCount;
			// 線分両端がXY平面にあるか。
			const bool bInXY = NearCoordinate( P0.z, Center.z, Epsilon ) && NearCoordinate( P1.z, Center.z, Epsilon );
			// 線分両端がXZ平面にあるか。
			const bool bInXZ = NearCoordinate( P0.y, Center.y, Epsilon ) && NearCoordinate( P1.y, Center.y, Epsilon );
			// 線分両端がYZ平面にあるか。
			const bool bInYZ = NearCoordinate( P0.x, Center.x, Epsilon ) && NearCoordinate( P1.x, Center.x, Epsilon );
			if ( bInXY ) ++XYCount;
			if ( bInXZ ) ++XZCount;
			if ( bInYZ ) ++YZCount;
			if ( !bInXY && !bInXZ && !bInYZ ) return false;
			// 円線端点が軸上の既知四分点に当たるか。
			for ( const FVector3& Point : { P0, P1 } )
			{
				bPositiveX |= NearCoordinate( Point.x, Center.x + Radius, Epsilon ) && NearCoordinate( Point.y, Center.y, Epsilon ) && NearCoordinate( Point.z, Center.z, Epsilon );
				bNegativeX |= NearCoordinate( Point.x, Center.x - Radius, Epsilon ) && NearCoordinate( Point.y, Center.y, Epsilon ) && NearCoordinate( Point.z, Center.z, Epsilon );
				bPositiveY |= NearCoordinate( Point.x, Center.x, Epsilon ) && NearCoordinate( Point.y, Center.y + Radius, Epsilon ) && NearCoordinate( Point.z, Center.z, Epsilon );
				bNegativeY |= NearCoordinate( Point.x, Center.x, Epsilon ) && NearCoordinate( Point.y, Center.y - Radius, Epsilon ) && NearCoordinate( Point.z, Center.z, Epsilon );
				bPositiveZ |= NearCoordinate( Point.x, Center.x, Epsilon ) && NearCoordinate( Point.y, Center.y, Epsilon ) && NearCoordinate( Point.z, Center.z + Radius, Epsilon );
				bNegativeZ |= NearCoordinate( Point.x, Center.x, Epsilon ) && NearCoordinate( Point.y, Center.y, Epsilon ) && NearCoordinate( Point.z, Center.z - Radius, Epsilon );
			}
		}
		return CircleCount == 96 && XYCount == 32 && XZCount == 32 && YZCount == 32 && bPositiveX && bNegativeX && bPositiveY && bNegativeY && bPositiveZ && bNegativeZ;
	}

	// 生産presetの編集copyを保存し、fresh parseと通常playbackで値を確認する。
	void TestOneForceFieldPreset( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor, const FString& PresetPath, const FString& SavePath )
	{
		C.Group = "effect-force-field-preset-real";
		// 力場copyに対応する専用World一時path。
		const FString WorldPath = SavePath + ".world";
		// このtestが作る二つのpathを安全に所有できるか。
		const bool bTemporaryPathsUnused = !Assets.ResolvePath( SavePath ).IsSet() && !Assets.ResolvePath( WorldPath ).IsSet();
		C.Check( bTemporaryPathsUnused, "sandboxの力場SaveAs先とWorld保存先が未使用である" );
		if ( !bTemporaryPathsUnused ) return;
		// sandbox内で解決した実presetの絶対path。
		const TOptional<FString> PresetFullPath = Assets.ResolvePath( PresetPath );
		NJson::CJsonObject OriginalJson;
		// 原本JSONを保存前比較用に読めたか。
		const bool bOriginalReadable = PresetFullPath.IsSet() && OriginalJson.LoadFromFile( PresetFullPath.GetValue() );
		C.Check( bOriginalReadable, "実力場presetの原本JSONを読む" );
		if ( !bOriginalReadable ) return;
		// 原本を編集後と比較する不変JSON値。
		const NlohmannJson OriginalSnapshot = OriginalJson.GetRoot();
		// focus関数を直接呼ぶ編集状態。
		FEffectEditorState State;
		// 原本hashを得るための独立parse結果。
		FParticleEffectDefinition OriginalDefinition;
		// 原本JSONを有効なdefinitionにできたか。
		const bool bOriginalParsed = FParticleEffectDefinition::FromJson( OriginalSnapshot, OriginalDefinition );
		// copy開始前の原本definition hash。
		const uint64 OriginalHash = bOriginalParsed ? OriginalDefinition.ComputeHash() : 0ull;
		// 製品のpreset copy API結果。
		const bool bCreated = NDebugUI::NewEffectFromPreset( State, PresetPath );
		C.Check( bOriginalParsed && bCreated && State.FilePath.IsEmpty() && State.Document.IsDirty() && !State.bPreviewMotion && State.Document.Get().ComputeHash() == OriginalHash, "実presetから原本を変えず非移動の未保存copyを作る" );
		if ( !bCreated )
		{
			NDebugUI::ReleaseEffectEditor( State );
			return;
		}
		// emitter 0に実際のfieldがあるか。
		bool bHasOriginalFields = State.Document.Get().Emitters.Num() > 0 && !State.Document.Get().Emitters[0].ForceFields.IsEmpty();
		C.Check( bHasOriginalFields, "実presetに編集対象の力場がある" );
		if ( !bHasOriginalFields )
		{
			NDebugUI::ReleaseEffectEditor( State );
			return;
		}
		// 少なくとも一つのemitter編集が記録されたか。
		bool bEdited = false;
		// 実presetの全emitterを編集APIへ通す。
		for ( int32 EmitterIndex = 0; EmitterIndex < State.Document.Get().Emitters.Num(); ++EmitterIndex )
		{
			// APIへ渡す現在emitterの編集copy。
			FParticleEmitterDefinition EditedEmitter = State.Document.Get().Emitters[EmitterIndex];
			// 実presetに元からあるfieldだけを編集する件数。
			const int32 OriginalFieldCount = EditedEmitter.ForceFields.Num();
			if ( EditedEmitter.ForceFields.Num() < FEffectForceFieldDefinition::MaxFields )
			{
				// 無効状態の保持値を検査する追加radial field。
				FEffectForceFieldDefinition DisabledField;
				DisabledField.bEnabled = false;
				DisabledField.Kind = FEffectForceFieldDefinition::EKind::Radial;
				DisabledField.Acceleration = FVector3( 13.0f, 14.0f, 15.0f );
				DisabledField.Position = FVector3( 16.0f, 17.0f, 18.0f );
				DisabledField.Radius = 19.0f;
				DisabledField.Strength = -20.0f;
				EditedEmitter.ForceFields.Add( DisabledField );
			}
			// 保存値と未使用値を含む各fieldを変更する。
			for ( int32 FieldIndex = 0; FieldIndex < OriginalFieldCount; ++FieldIndex )
			{
				// 編集対象のfield保存値。
				FEffectForceFieldDefinition& Field = EditedEmitter.ForceFields[FieldIndex];
				Field.bEnabled = FieldIndex == 0;
				Field.Kind = FieldIndex == 0 ? FEffectForceFieldDefinition::EKind::Uniform : FEffectForceFieldDefinition::EKind::Radial;
				Field.Acceleration = FVector3( 2.0f + FieldIndex, 0.5f, -0.25f );
				Field.Position = FVector3( 3.0f + FieldIndex, 4.0f, -2.0f );
				Field.Radius = 5.0f + FieldIndex;
				Field.Strength = -6.0f - FieldIndex;
			}
			if ( EmitterIndex == 0 )
			{
				EditedEmitter.Rate = 0.0f;
				EditedEmitter.Bursts.Empty();
				EditedEmitter.Bursts.Add( { 0.0f, 2 } );
				EditedEmitter.Speed = { 0.0f, 0.0f };
				EditedEmitter.Shape = EParticleShape::Box;
				EditedEmitter.BoxSize = FVector3( 0.25f, 0.25f, 0.25f );
				EditedEmitter.Lifetime = { 5.0f, 5.0f };
				EditedEmitter.Gravity = FVector3::Zero;
				EditedEmitter.Drag = 0.0f;
				EditedEmitter.ShapeRadius = 0.0f;
			}
			bEdited |= NParticleEdit::SetEmitter( State.Document, EmitterIndex, EditedEmitter, "force-field-preset" );
		}
		C.Check( bEdited && State.Document.Get().Emitters[0].ForceFields.Num() == 2 && State.Document.Get().Emitters[0].ForceFields[0].Acceleration == FVector3( 2.0f, 0.5f, -0.25f ) && State.Document.Get().Emitters[0].ForceFields[0].Position == FVector3( 3.0f, 4.0f, -2.0f ) && State.Document.Get().Emitters[0].ForceFields[0].Radius == 5.0f && State.Document.Get().Emitters[0].ForceFields[0].Strength == -6.0f && !State.Document.Get().Emitters[0].ForceFields[1].bEnabled && State.Document.Get().Emitters[0].ForceFields[1].Acceleration == FVector3( 13.0f, 14.0f, 15.0f ) && State.Document.Get().Emitters[0].ForceFields[1].Position == FVector3( 16.0f, 17.0f, 18.0f ) && State.Document.Get().Emitters[0].ForceFields[1].Radius == 19.0f && State.Document.Get().Emitters[0].ForceFields[1].Strength == -20.0f, "実編集APIで有効・無効fieldの全保存値と種類切替後の未使用値を変える" );
		// 保存後にfresh parseと比べる編集済みhash。
		const uint64 EditedHash = State.Document.Get().ComputeHash();
		// 製品のSaveAs API結果。
		const bool bSaved = bEdited && NDebugUI::SaveEffectFile( State, SavePath );
		C.Check( bSaved && State.FilePath == SavePath && !State.Document.IsDirty() && State.SaveSerial == 1u, "実preset copyをsandboxへSaveAsする" );
		if ( !bSaved )
		{
			NDebugUI::ReleaseEffectEditor( State );
			return;
		}
		NDebugUI::ReleaseEffectEditor( State );
		// SaveAs fileのsandbox内絶対path。
		const TOptional<FString> SavedFullPath = Assets.ResolvePath( SavePath );
		NJson::CJsonObject SavedJson;
		// 保存JSONから作る独立のfresh definition。
		FParticleEffectDefinition FreshDefinition;
		// fresh JSON parseとdefinition変換の成否。
		const bool bFresh = SavedFullPath.IsSet() && SavedJson.LoadFromFile( SavedFullPath.GetValue() ) && FParticleEffectDefinition::FromJson( SavedJson.GetRoot(), FreshDefinition );
		C.Check( bFresh && FreshDefinition.ComputeHash() == EditedHash && FreshDefinition.Emitters.IsValidIndex( 0 ) && FreshDefinition.Emitters[0].ForceFields.Num() == 2 && FreshDefinition.Emitters[0].ForceFields[0].Strength == -6.0f && !FreshDefinition.Emitters[0].ForceFields[1].bEnabled && FreshDefinition.Emitters[0].ForceFields[1].Acceleration == FVector3( 13.0f, 14.0f, 15.0f ) && FreshDefinition.Emitters[0].ForceFields[1].Position == FVector3( 16.0f, 17.0f, 18.0f ) && FreshDefinition.Emitters[0].ForceFields[1].Radius == 19.0f && FreshDefinition.Emitters[0].ForceFields[1].Strength == -20.0f, "SaveAs後にfresh definitionの全field値・無効中設定とhashを照合する" );
		// 保存後の原本が同じJSONか再確認するreader。
		NJson::CJsonObject AfterOriginalJson;
		C.Check( AfterOriginalJson.LoadFromFile( PresetFullPath.GetValue() ) && AfterOriginalJson.GetRoot() == OriginalSnapshot, "編集・SaveAs後も実preset原本JSONを保つ" );
		if ( !bFresh )
		{
			if ( SavedFullPath.IsSet() ) NFileSystem::RemoveFile( SavedFullPath.GetValue() );
			return;
		}
		// 実EditorPlacement APIで保存済みcopyをWorld部品として配置する。
		// 実placement APIで編集するWorld文書。
		TWorldDocument WorldDocument;
		// 配置を始める空のWorld定義。
		FWorldDefinition EmptyWorld;
		EmptyWorld.Name = "Force field preset placement";
		WorldDocument.Reset( EmptyWorld );
		// EditorPlacementが割り当てるeffect object ID。
		const int32 ObjectId = NWorldEditor::AddAssetObject( WorldDocument, SavePath, WorldNoId, FVector3( 4.0f, 2.0f, 1.0f ), nullptr );
		// EditorPlacementが作成したobjectを読む参照。
		const FWorldObject* Placed = WorldDocument.Get().FindObject( ObjectId );
		// 配置objectがSaveAs pathを参照するか。
		const bool bPlaced = ObjectId != WorldNoId && Placed != nullptr && Placed->Effect.IsSet() && Placed->Effect->Path == SavePath;
		// 配置を含むWorldの保存結果。
		const bool bWorldSaved = bPlaced && Assets.SaveWorld( WorldPath, WorldDocument.Get() );
		// 保存後にasset managerからfresh loadしたWorld。
		TSharedPtr<CWorldAsset> FreshWorld = bWorldSaved ? Assets.LoadWorld( WorldPath ) : TSharedPtr<CWorldAsset>();
		// fresh loadしたWorld内の配置object。
		const FWorldObject* FreshPlaced = FreshWorld.IsValid() ? FreshWorld->GetDefinition().FindObject( ObjectId ) : nullptr;
		C.Check( bPlaced && bWorldSaved && FreshPlaced != nullptr && FreshPlaced->Effect.IsSet() && FreshPlaced->Effect->Path == SavePath, "EditorPlacement・World save/fresh load後もeffect pathを保つ" );
		// 保存Worldから得たpathを内蔵registryへ渡し、AssetManager load経路を使う。
		const FString PlaybackPath = FreshPlaced != nullptr && FreshPlaced->Effect.IsSet() ? FreshPlaced->Effect->Path : FString();
		// 保存済みWorld placementの姿勢。
		const FTransform PlaybackTransform = FreshPlaced != nullptr ? FreshPlaced->Transform.ToTransform() : FTransform();
		// 内蔵particle backendだけを含む通常registry。
		FEffectBackendRegistry Backends = FEffectBackendRegistry::CreateWithBuiltins();
		// pathからAssets.LoadEffect経由で作られた通常playback。
		TSharedPtr<IEffectPlayback> Playback = Backends.Play( PlaybackPath, &Assets, FEffectPlayParams(), PlaybackTransform );
		// registryに内蔵particle backendだけを登録しているため型を安全に絞れる。
		FParticlePlayback* ParticlePlayback = static_cast<FParticlePlayback*>( Playback.Get() );
		if ( Playback.IsValid() ) Playback->Update( 0.01f );
		if ( Playback.IsValid() ) Playback->Update( 0.5f );
		// native playbackが所有する読み取り専用simulation。
		const FParticleSimulation* Simulation = ParticlePlayback != nullptr ? &ParticlePlayback->GetSimulation() : nullptr;
		// 半秒の一様加速後に期待する速度。
		const bool bForceApplied = Simulation != nullptr && Simulation->Emitters.IsValidIndex( 0 ) && Simulation->Emitters[0].Particles.IsValidIndex( 0 ) && std::fabs( Simulation->Emitters[0].Particles[0].Velocity.x - 1.0f ) < 1.0e-4f;
		C.Check( bForceApplied, "通常IEffectPlaybackの固定刻みで保存済み一様場を適用する" );
		if ( Playback.IsValid() )
		{
			// Record直前のsimulation時計。
			const float32 TimeBeforeRecord = Simulation != nullptr ? Simulation->Time : -1.0f;
			// native playbackが書き込む命令buffer。
			FDrawBuffer Buffer;
			// playback commandを記録する描画窓口。
			FDrawContext Draw( Buffer );
			Playback->Record( Draw );
			// Record直後のsimulation時計。
			const float32 TimeAfterRecord = Simulation != nullptr ? Simulation->Time : -2.0f;
			C.Check( !Buffer.Words.empty() && TimeAfterRecord == TimeBeforeRecord, "Playback Recordはparticle commandを作り時計を進めない" );
			if ( !Buffer.Words.empty() ) Executor.Execute( FDrawSpan{ Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );
		}
		Playback.Reset();
		FreshWorld.Reset();
		if ( SavedFullPath.IsSet() ) C.Check( NFileSystem::RemoveFile( SavedFullPath.GetValue() ), "sandboxの保存済み力場copyを削除する" );
		// 後片付け対象として解決したWorld file。
		const TOptional<FString> WorldFullPath = Assets.ResolvePath( WorldPath );
		if ( WorldFullPath.IsSet() ) C.Check( NFileSystem::RemoveFile( WorldFullPath.GetValue() ), "sandboxの配置Worldを削除する" );
	}

	// endpointを許容差付きで照合する。正規化・回転で生じるfloatの丸めを吸収する。
	bool HasLineNear( const FGuideReadback& Readback, const FVector3& A, const FVector3& B, uint8 R, uint8 G, uint8 Blue, float64 Epsilon = 1.0e-4 )
	{
		for ( const FDrawLine3DData& Line : Readback.Lines )
		{
			// commandに記録された始点と終点。
			const FVector3 P0 = LinePoint( Line.P0 );
			const FVector3 P1 = LinePoint( Line.P1 );
			// 両端が期待座標に近いか。
			const bool bNear = NearCoordinate( P0.x, A.x, Epsilon ) && NearCoordinate( P0.y, A.y, Epsilon ) && NearCoordinate( P0.z, A.z, Epsilon ) && NearCoordinate( P1.x, B.x, Epsilon ) && NearCoordinate( P1.y, B.y, Epsilon ) && NearCoordinate( P1.z, B.z, Epsilon );
			if ( bNear && Line.Color.R == R && Line.Color.G == G && Line.Color.B == Blue ) return true;
		}
		return false;
	}

	// 二つのベクトルを成分ごとに許容差付きで比べる。
	bool NearVector( const FVector3& A, const FVector3& B, float64 Epsilon = 1.0e-4 )
	{
		return NearCoordinate( A.x, B.x, Epsilon ) && NearCoordinate( A.y, B.y, Epsilon ) && NearCoordinate( A.z, B.z, Epsilon );
	}

	// 渦・乱流・追従する場のguideを、実commandの座標・色・本数から独立式で照合する。
	void TestForceFieldKindGuides( NAssetTest::FChecker& C )
	{
		C.Group = "effect-force-field-editor-kind-guides";
		// 検査対象のfieldを一つのemitterへ並べた文書。
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		// 非単位の斜め軸を持つ正のVortex。
		FEffectForceFieldDefinition PositiveVortex;
		PositiveVortex.Kind = FEffectForceFieldDefinition::EKind::Vortex;
		PositiveVortex.Position = FVector3( 7.0f, 8.0f, 9.0f );
		PositiveVortex.Axis = FVector3( 0.0f, 3.0f, 4.0f );
		PositiveVortex.Radius = 2.0f;
		PositiveVortex.Strength = 3.0f;
		Emitter.ForceFields.Add( PositiveVortex );
		// 周回方向が逆になる負のVortex。
		FEffectForceFieldDefinition NegativeVortex = PositiveVortex;
		NegativeVortex.Strength = -3.0f;
		Emitter.ForceFields.Add( NegativeVortex );
		// 3軸矢印を持つTurbulence。
		FEffectForceFieldDefinition Gust;
		Gust.Kind = FEffectForceFieldDefinition::EKind::Turbulence;
		Gust.Position = FVector3( -2.0f, 1.0f, 0.5f );
		Gust.Radius = 1.5f;
		Gust.Strength = 2.0f;
		Emitter.ForceFields.Add( Gust );
		// 強さ0で矢印を描かないTurbulence。
		FEffectForceFieldDefinition QuietGust = Gust;
		QuietGust.Strength = 0.0f;
		Emitter.ForceFields.Add( QuietGust );
		// エミッター姿勢へ追従するRadial。
		FEffectForceFieldDefinition FollowRadial;
		FollowRadial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		FollowRadial.bFollowEmitter = true;
		FollowRadial.Position = FVector3( 1.0f, 0.0f, 0.0f );
		FollowRadial.Radius = 2.0f;
		FollowRadial.Strength = -2.0f;
		Emitter.ForceFields.Add( FollowRadial );
		// エミッター姿勢へ追従するUniform。
		FEffectForceFieldDefinition FollowUniform;
		FollowUniform.bFollowEmitter = true;
		FollowUniform.Acceleration = FVector3( 0.0f, 0.0f, 5.0f );
		Emitter.ForceFields.Add( FollowUniform );
		// エミッター姿勢へ追従するVortex。
		FEffectForceFieldDefinition FollowVortex;
		FollowVortex.Kind = FEffectForceFieldDefinition::EKind::Vortex;
		FollowVortex.bFollowEmitter = true;
		FollowVortex.Position = FVector3( 0.0f, 1.0f, 0.0f );
		FollowVortex.Axis = FVector3( 0.0f, 1.0f, 0.0f );
		FollowVortex.Radius = 2.0f;
		FollowVortex.Strength = 1.0f;
		Emitter.ForceFields.Add( FollowVortex );
		// 強さ0でも回転軸矢印だけは描くVortex。
		FEffectForceFieldDefinition QuietVortex = PositiveVortex;
		QuietVortex.Strength = 0.0f;
		Emitter.ForceFields.Add( QuietVortex );
		Definition.Emitters.Add( Emitter );
		// guide記録用の編集状態。
		FEffectEditorState State;
		State.Document.Reset( Definition );
		// 90度Z回転・移動・非一様拡大を持つエミッター姿勢。拡大はguideへ使わない。
		const FQuaternion Turn = FQuaternion::FromAxisAngle( FVector3::Forward, 1.57079632679f );
		const FTransform EmitterTransform( FVector3( 10.0f, 20.0f, 30.0f ), Turn, FVector3( 2.0f, 3.0f, 4.0f ) );
		// 指定fieldと姿勢でguideを記録して読み直す関数。
		const auto Record = [&State]( int32 Index, const FTransform& Transform ) -> FGuideReadback
		{
			State.SelectedForceField = Index;
			// 記録した命令列。
			FDrawBuffer Buffer;
			// 記録窓口。
			FDrawContext Draw( Buffer );
			NEffectEditorUI::RecordForceFieldGuidesForTransform( State, Draw, Transform );
			return ReadGuides( Buffer );
		};

		// 正Vortex: 世界固定なので姿勢を変えても同じ。軸は(0,.6,.8)、周回矢印の始点は中心から-X側1.04m。
		const FGuideReadback Positive = Record( 0, EmitterTransform );
		const FGuideReadback PositiveIdentity = Record( 0, FTransform::Identity );
		const FVector3 VortexCenter( 7.0f, 8.0f, 9.0f );
		const FVector3 AxisEnd( 7.0f, 8.72f, 9.96f );
		const FVector3 TangentStart( 5.96f, 8.0f, 9.0f );
		const FVector3 PositiveTangentEnd( 5.96f, 7.68f, 9.24f );
		C.Check( Positive.Spheres.IsEmpty() && Positive.Lines.Num() == 102 && HasRadialCircleGuides( Positive, VortexCenter, 2.0f, 40, 220, 230 ) && HasLineNear( Positive, VortexCenter, AxisEnd, 40, 220, 230 ) && HasLineNear( Positive, TangentStart, PositiveTangentEnd, 40, 220, 230 ), "世界固定のVortexはWorld中心・半径2の三円と、正規化軸の矢印・周回矢印を実commandへ記録する" );
		C.Check( PositiveIdentity.Lines.Num() == Positive.Lines.Num() && HasLineNear( PositiveIdentity, VortexCenter, AxisEnd, 40, 220, 230 ) && HasLineNear( PositiveIdentity, TangentStart, PositiveTangentEnd, 40, 220, 230 ), "追従しないVortexのguideはエミッター姿勢に依らない" );
		// 周回矢印が実際の加速度の向きと一致するか。
		const FVector3 PositiveAcceleration = PositiveVortex.EvaluateAcceleration( TangentStart ).Normalized();
		C.Check( NearVector( PositiveAcceleration, ( PositiveTangentEnd - TangentStart ).Normalized() ), "Vortexの周回矢印は、矢印始点での実際の加速度の向きと一致する" );
		// 負Vortex: 周回矢印だけが反転する。
		const FGuideReadback Negative = Record( 1, EmitterTransform );
		const FVector3 NegativeTangentEnd( 5.96f, 8.32f, 8.76f );
		const FVector3 NegativeAcceleration = NegativeVortex.EvaluateAcceleration( TangentStart ).Normalized();
		C.Check( Negative.Lines.Num() == 102 && HasLineNear( Negative, VortexCenter, AxisEnd, 40, 220, 230 ) && HasLineNear( Negative, TangentStart, NegativeTangentEnd, 40, 220, 230 ) && !HasLineNear( Negative, TangentStart, PositiveTangentEnd, 40, 220, 230 ) && NearVector( NegativeAcceleration, ( NegativeTangentEnd - TangentStart ).Normalized() ), "負Strengthは周回矢印だけを逆向きにし、回転軸矢印を保つ" );
		// 強さ0のVortex: 軸矢印だけが残る。
		const FGuideReadback QuietVortexReadback = Record( 7, EmitterTransform );
		C.Check( QuietVortexReadback.Lines.Num() == 99 && HasLineNear( QuietVortexReadback, VortexCenter, AxisEnd, 40, 220, 230 ) && !HasLineNear( QuietVortexReadback, TangentStart, PositiveTangentEnd, 40, 220, 230 ), "強さ0のVortexは三円と回転軸矢印だけを記録する" );
		// Turbulence: 中心から3軸へ0.3mの矢印。
		const FGuideReadback GustReadback = Record( 2, EmitterTransform );
		const FVector3 GustCenter( -2.0f, 1.0f, 0.5f );
		C.Check( GustReadback.Spheres.IsEmpty() && GustReadback.Lines.Num() == 105 && HasRadialCircleGuides( GustReadback, GustCenter, 1.5f, 40, 220, 230 ) && HasLineNear( GustReadback, GustCenter, FVector3( -1.7f, 1.0f, 0.5f ), 40, 220, 230 ) && HasLineNear( GustReadback, GustCenter, FVector3( -2.0f, 1.3f, 0.5f ), 40, 220, 230 ) && HasLineNear( GustReadback, GustCenter, FVector3( -2.0f, 1.0f, 0.8f ), 40, 220, 230 ), "TurbulenceはWorld中心・半径1.5の三円と、中心から3軸へ出る矢印を記録する" );
		const FGuideReadback QuietGustReadback = Record( 3, EmitterTransform );
		C.Check( QuietGustReadback.Lines.Num() == 96 && HasRadialCircleGuides( QuietGustReadback, GustCenter, 1.5f, 40, 220, 230 ), "強さ0のTurbulenceは三円だけを記録する" );
		// 追従Radial: 中心がエミッター位置へ移り、局所X軸が世界Yへ回る。
		const FGuideReadback FollowRadialReadback = Record( 4, EmitterTransform );
		const FVector3 FollowCenter( 10.0f, 21.0f, 30.0f );
		C.Check( FollowRadialReadback.Lines.Num() == 99 && HasRadialCircleGuides( FollowRadialReadback, FollowCenter, 2.0f, 40, 220, 230 ) && HasLineNear( FollowRadialReadback, FVector3( 10.0f, 22.04f, 30.0f ), FVector3( 10.0f, 21.64f, 30.0f ), 40, 220, 230 ), "追従Radialの三円と中心向き矢印はエミッターの移動・回転へ追従し、拡大率を半径へ掛けない" );
		// 追従Uniform: 原点がエミッター位置へ移る。
		const FGuideReadback FollowUniformReadback = Record( 5, EmitterTransform );
		C.Check( FollowUniformReadback.Lines.Num() == 3 && HasLineNear( FollowUniformReadback, FVector3( 10.0f, 20.0f, 30.0f ), FVector3( 10.0f, 20.0f, 31.25f ), 40, 220, 230 ), "追従Uniformの矢印はエミッター位置から回転後の加速度方向へ伸びる" );
		// 追従Vortex: 局所Y軸が世界-Xへ回り、周回矢印は世界-Y側へ向く。
		const FGuideReadback FollowVortexReadback = Record( 6, EmitterTransform );
		const FVector3 FollowVortexCenter( 9.0f, 20.0f, 30.0f );
		const FVector3 FollowTangentStart( 9.0f, 20.0f, 28.96f );
		const FVector3 FollowTangentEnd( 9.0f, 19.6f, 28.96f );
		// 追従fieldの実際の加速度の向き。
		const FVector3 FollowAcceleration = FollowVortex.EvaluateAcceleration( FollowTangentStart, EmitterTransform, 0.0f ).Normalized();
		C.Check( FollowVortexReadback.Lines.Num() == 102 && HasRadialCircleGuides( FollowVortexReadback, FollowVortexCenter, 2.0f, 40, 220, 230 ) && HasLineNear( FollowVortexReadback, FollowVortexCenter, FVector3( 7.8f, 20.0f, 30.0f ), 40, 220, 230 ) && HasLineNear( FollowVortexReadback, FollowTangentStart, FollowTangentEnd, 40, 220, 230 ) && NearVector( FollowAcceleration, ( FollowTangentEnd - FollowTangentStart ).Normalized() ), "追従Vortexの中心・回転軸・周回矢印が姿勢に追従し、実際の加速度の向きと一致する" );
		// 非有限・ゼロの回転では、追従するfieldだけが何も描かない。
		const FTransform ZeroRotation( FVector3( 10.0f, 20.0f, 30.0f ), FQuaternion( 0.0f, 0.0f, 0.0f, 0.0f ) );
		const FGuideReadback ZeroFollow = Record( 4, ZeroRotation );
		const FGuideReadback ZeroFixed = Record( 0, ZeroRotation );
		C.Check( ZeroFollow.Lines.IsEmpty() && ZeroFollow.Spheres.IsEmpty() && ZeroFixed.Lines.Num() == 102, "ゼロ回転の姿勢では追従fieldだけが描かれず、世界固定のfieldは影響を受けない" );
		// 試し再生の移動が追従guideの姿勢になるか。
		State.bPreviewMotion = false;
		const FTransform StillTransform = NEffectEditorUI::GetPreviewEmitterTransform( State );
		State.bPreviewMotion = true;
		State.PreviewMotionTime = 0.5f;
		const FTransform MovingTransform = NEffectEditorUI::GetPreviewEmitterTransform( State );
		State.SelectedForceField = 5;
		// 試し再生中の姿勢で記録したguide。
		FDrawBuffer MotionBuffer;
		FDrawContext MotionDraw( MotionBuffer );
		NEffectEditorUI::RecordForceFieldGuides( State, MotionDraw );
		const FGuideReadback MotionReadback = ReadGuides( MotionBuffer );
		const FVector3 MotionOrigin( static_cast<float32>( std::cos( 0.5 ) * 1.2 ), 1.0f, static_cast<float32>( std::sin( 0.5 ) * 1.2 ) );
		C.Check( StillTransform.Position == FVector3::Zero && NearVector( MovingTransform.Position, MotionOrigin ) && HasLineNear( MotionReadback, MotionOrigin, MotionOrigin + FVector3( 0.0f, 0.0f, 1.25f ), 40, 220, 230 ), "追従するfieldのguideは、Move emitter previewの現在の円運動の姿勢に付いて動く" );
	}

	// 1つの力場だけを持ち、他のbounds寄与を0にした検査文書を作る。
	FParticleEffectDefinition MakeSingleFieldDefinition( const FEffectForceFieldDefinition& Field )
	{
		// 力場以外が範囲へ寄与しない文書。重力や発生球半径もゼロにして力場球範囲だけを取り出す。
		FParticleEffectDefinition Definition;
		FParticleEmitterDefinition Emitter;
		Emitter.Lifetime = { 1.0f, 1.0f };
		Emitter.Speed = { 0.0f, 0.0f };
		Emitter.StartSize = { 0.0f, 0.0f };
		Emitter.EndSize = { 0.0f, 0.0f };
		Emitter.Gravity = FVector3::Zero;
		Emitter.ShapeRadius = 0.0f;
		Emitter.ForceFields.Add( Field );
		Definition.Emitters.Add( Emitter );
		return Definition;
	}

	// 渦・乱流の球範囲と加速度上限が、自動視点へ種類別に反映されるか確かめる。
	void TestForceFieldKindFocus( NAssetTest::FChecker& C )
	{
		C.Group = "effect-force-field-editor-kind-focus";
		// 既知の画角とclip距離を持つ検査viewport。
		const FEditorViewport Viewport{ 1280, 720, 0.8f, 0.05f, 2000.0f };
		// 強さ0・半径3のVortex。軸矢印(1.8m)は半径内にあり、範囲は球半径3になる。
		FEffectForceFieldDefinition Vortex;
		Vortex.Kind = FEffectForceFieldDefinition::EKind::Vortex;
		Vortex.Radius = 3.0f;
		Vortex.Strength = 0.0f;
		C.Check( HasFocusedAxisExtents( MakeSingleFieldDefinition( Vortex ), Viewport, 3.0 ), "Vortexの球範囲を自動視点へ入れる" );
		// 半径より軸矢印が長くなる極小Vortex。
		FEffectForceFieldDefinition TinyVortex = Vortex;
		TinyVortex.Radius = 0.001f;
		C.Check( HasFocusedAxisExtents( MakeSingleFieldDefinition( TinyVortex ), Viewport, 0.3 ), "極小Vortexは回転軸矢印の長さ0.3mまで自動視点へ入れる" );
		// 3軸が最大2|Strength|まで変わるTurbulence。移動範囲は2*sqrt(3)*|Strength|*T^2。
		FEffectForceFieldDefinition Gust;
		Gust.Kind = FEffectForceFieldDefinition::EKind::Turbulence;
		Gust.Radius = 2.0f;
		Gust.Strength = 1.0f;
		C.Check( HasFocusedAxisExtents( MakeSingleFieldDefinition( Gust ), Viewport, 2.0 * std::sqrt( 3.0 ) ), "Turbulenceの移動範囲は各軸最大2|Strength|として自動視点へ入れる" );
		// Vortexの使わないAccelerationが一様場の矢印範囲を足さないことを、同じ視点になるかで確かめる。
		FEffectForceFieldDefinition PlainVortex;
		PlainVortex.Kind = FEffectForceFieldDefinition::EKind::Vortex;
		PlainVortex.Radius = 1.0f;
		PlainVortex.Strength = 0.0f;
		FEffectForceFieldDefinition AcceleratedVortex = PlainVortex;
		AcceleratedVortex.Acceleration = FVector3( 0.0f, 5.0f, 0.0f );
		FEffectEditorState Plain;
		Plain.Document.Reset( MakeSingleFieldDefinition( PlainVortex ) );
		FEffectEditorState Accelerated;
		Accelerated.Document.Reset( MakeSingleFieldDefinition( AcceleratedVortex ) );
		const bool bPlainFocused = NEffectEditorUI::FocusCameraOnForceFields( Plain, Viewport );
		const bool bAcceleratedFocused = NEffectEditorUI::FocusCameraOnForceFields( Accelerated, Viewport );
		C.Check( bPlainFocused && bAcceleratedFocused && SameCamera( Plain.Camera, Accelerated.Camera ), "Vortexの使わないAccelerationは自動視点の範囲へ影響しない" );
		// 原点から離れたVortexの球が、視点に収まるか。
		FEffectForceFieldDefinition FarVortex = Vortex;
		FarVortex.Position = FVector3( 20.0f, 0.0f, 0.0f );
		FarVortex.Strength = 4.0f;
		FEffectEditorState Far;
		Far.Document.Reset( MakeSingleFieldDefinition( FarVortex ) );
		bool bFarVisible = NEffectEditorUI::FocusCameraOnForceFields( Far, Viewport );
		for ( const FVector3& Point : { FVector3( 23.0f, 0.0f, 0.0f ), FVector3( 17.0f, 0.0f, 0.0f ), FVector3( 20.0f, 3.0f, 0.0f ), FVector3( 20.0f, -3.0f, 0.0f ), FVector3( 20.0f, 0.0f, 3.0f ), FVector3( 20.0f, 0.0f, -3.0f ) } ) bFarVisible = bFarVisible && IsProjectedInside( Far.Camera, Viewport, Point );
		C.Check( bFarVisible, "原点から離れたVortexの球範囲も自動視点に入る" );
	}

	// presetをAsset sandboxから読み、definitionへ変換する。
	bool LoadForceFieldPresetDefinition( CAssetManager& Assets, const FString& Path, FParticleEffectDefinition& Out, TArray<FString>& Warnings )
	{
		// sandbox内で解決したpresetの絶対path。
		const TOptional<FString> FullPath = Assets.ResolvePath( Path );
		NJson::CJsonObject Json;
		return FullPath.IsSet() && Json.LoadFromFile( FullPath.GetValue() ) && FParticleEffectDefinition::FromJson( Json.GetRoot(), Out, &Warnings );
	}

	// 指定位置のエミッターで1秒進めた粒子状態を作る。
	FParticleSimulation RunForceFieldPresetSimulation( const FParticleEffectDefinition& Definition, const FVector3& Position )
	{
		// 進める粒子状態。
		FParticleSimulation Simulation;
		Simulation.Transform = FTransform( Position );
		NParticle::Start( Simulation, MakeShared<FParticleEffectDefinition>( Definition ) );
		for ( int32 Step = 0; Step < 16; ++Step ) NParticle::Advance( Simulation, 0.0625f );
		return Simulation;
	}

	// 全粒子について、指定中心まわりのY軸角運動量の和を求める。
	float64 SumAngularMomentumY( const FParticleSimulation& Simulation, const FVector3& Center )
	{
		float64 Sum = 0.0;
		for ( const FParticleEmitterState& State : Simulation.Emitters ) for ( const FParticle& P : State.Particles ) Sum += static_cast<float64>( P.Position.z - Center.z ) * P.Velocity.x - static_cast<float64>( P.Position.x - Center.x ) * P.Velocity.z;
		return Sum;
	}

	// 同じ順序の粒子を、それぞれの基準位置からの相対位置で比べた最大差を返す。数が違えば大きな値を返す。
	float64 MaxRelativeParticleDistance( const FParticleSimulation& A, const FVector3& CenterA, const FParticleSimulation& B, const FVector3& CenterB )
	{
		if ( A.GetNumParticles() != B.GetNumParticles() || A.Emitters.Num() != B.Emitters.Num() ) return 1.0e9;
		float64 Worst = 0.0;
		for ( int32 EmitterIndex = 0; EmitterIndex < A.Emitters.Num(); ++EmitterIndex ) for ( int32 ParticleIndex = 0; ParticleIndex < A.Emitters[EmitterIndex].Particles.Num(); ++ParticleIndex )
		{
			// 相対位置の差。
			const FVector3 Delta = ( A.Emitters[EmitterIndex].Particles[ParticleIndex].Position - CenterA ) - ( B.Emitters[EmitterIndex].Particles[ParticleIndex].Position - CenterB );
			Worst = std::max( Worst, static_cast<float64>( Delta.Length() ) );
		}
		return Worst;
	}

	// 渦・乱流の組み込み例が、保存値・追従・実際の粒子運動の両面で期待どおりか確かめる。
	void TestKindPreset( NAssetTest::FChecker& C, CAssetManager& Assets, const FString& Path, FEffectForceFieldDefinition::EKind ExpectedKind )
	{
		C.Group = "effect-force-field-kind-preset-real";
		FParticleEffectDefinition Original;
		TArray<FString> Warnings;
		const bool bLoaded = LoadForceFieldPresetDefinition( Assets, Path, Original, Warnings );
		// 全emitterが期待した追従する場を一つだけ持つか。
		bool bFields = bLoaded && Original.Emitters.Num() == 2;
		for ( const FParticleEmitterDefinition& Emitter : Original.Emitters ) bFields = bFields && Emitter.ForceFields.Num() == 1 && Emitter.ForceFields[0].Kind == ExpectedKind && Emitter.ForceFields[0].bEnabled && Emitter.ForceFields[0].bFollowEmitter && Emitter.ForceFields[0].IsValidSettings() && Emitter.ForceFields[0].Radius > 0.0f && Emitter.ForceFields[0].Strength != 0.0f;
		C.Check( bLoaded && Warnings.IsEmpty() && !FParticleEffectDefinition::HasErrors( Original.Validate() ) && bFields, "組み込み例は警告なしに読め、全emitterが期待した種類の有効な追従fieldを一つ持つ" );
		// 保存と再読込で同じ定義・hashに戻るか。
		FParticleEffectDefinition RoundTrip;
		C.Check( FParticleEffectDefinition::FromJson( Original.ToJson(), RoundTrip ) && RoundTrip.ComputeHash() == Original.ComputeHash() && RoundTrip.ToJson() == Original.ToJson(), "組み込み例はJSON往復で同じ定義とhashに戻る" );
		if ( !bFields ) return;
		// 力場だけを無効にした比較定義と、追従をやめた比較定義。
		FParticleEffectDefinition Disabled = Original;
		FParticleEffectDefinition Fixed = Original;
		for ( int32 EmitterIndex = 0; EmitterIndex < Original.Emitters.Num(); ++EmitterIndex )
		{
			Disabled.Emitters[EmitterIndex].ForceFields[0].bEnabled = false;
			Fixed.Emitters[EmitterIndex].ForceFields[0].bFollowEmitter = false;
		}
		// エミッターを原点と、場から十分離れた位置へ置く。
		const FVector3 Origin = FVector3::Zero;
		const FVector3 Away( 6.0f, 0.0f, -3.0f );
		const FParticleSimulation OnSim = RunForceFieldPresetSimulation( Original, Origin );
		const FParticleSimulation OffSim = RunForceFieldPresetSimulation( Disabled, Origin );
		const FParticleSimulation AwaySim = RunForceFieldPresetSimulation( Original, Away );
		const FParticleSimulation FixedAwaySim = RunForceFieldPresetSimulation( Fixed, Away );
		// 平行移動しても追従する場なら、相対的な運動が変わらない。
		const float64 FollowDifference = MaxRelativeParticleDistance( OnSim, Origin, AwaySim, Away );
		// 追従しない場は原点に残り、離れたエミッターの粒子へ届かない。
		const float64 FixedDifference = MaxRelativeParticleDistance( OffSim, Origin, FixedAwaySim, Away );
		// 場がある時とない時の運動の差。
		const float64 FieldDifference = MaxRelativeParticleDistance( OnSim, Origin, OffSim, Origin );
		C.Check( OnSim.GetNumParticles() > 40 && FollowDifference < 2.0e-3 && FixedDifference < 2.0e-3 && FieldDifference > 0.2, "組み込み例の場は、エミッターを平行移動しても相対運動を保ち、追従をやめると離れた粒子へ届かず、場なしと明らかに異なる運動を作る (%d particles, follow %.5f, fixed %.5f, field %.3f)", OnSim.GetNumParticles(), FollowDifference, FixedDifference, FieldDifference );
		if ( ExpectedKind == FEffectForceFieldDefinition::EKind::Vortex )
		{
			// 正のStrengthでは、軸の周りに正の角運動量が生まれる。
			const float64 OnSwirl = SumAngularMomentumY( OnSim, Origin );
			const float64 OffSwirl = SumAngularMomentumY( OffSim, Origin );
			C.Check( OnSwirl > OffSwirl + 5.0 && SumAngularMomentumY( AwaySim, Away ) > OffSwirl + 5.0, "渦の組み込み例は、場なしより明らかに大きい軸周りの角運動量を粒子へ与える (on %.3f, off %.3f)", OnSwirl, OffSwirl );
		}
	}

	// 実Effect Editorのメニューバーをキーボード操作で検証する独立ImGui文脈。
	class FEffectEditorMenuContext
	{
	public:
		// 呼出元のImGui文脈を退避し、キーボード操作用の独立文脈を初期化する。
		FEffectEditorMenuContext() : Previous( ImGui::GetCurrentContext() ), Context( ImGui::CreateContext() )
		{
			ImGui::SetCurrentContext( Context );
			// メニューと両パネルが収まる画面寸法とキーボード設定。
			ImGuiIO& Io = ImGui::GetIO();
			Io.DisplaySize = ImVec2( 1600.0f, 1100.0f );
			Io.DeltaTime = 1.0f / 60.0f;
			Io.IniFilename = nullptr;
			Io.LogFilename = nullptr;
			Io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
			Io.ConfigInputTrickleEventQueue = false;
			Io.Fonts->AddFontDefaultBitmap();
			bFontsReady = Io.Fonts->Build();
		}

		// 独立文脈を破棄し、呼出元のImGui文脈を復元する。
		~FEffectEditorMenuContext()
		{
			ImGui::DestroyContext( Context );
			ImGui::SetCurrentContext( Previous );
		}

		// ImGui文脈の所有権を複製しない。
		FEffectEditorMenuContext( const FEffectEditorMenuContext& ) = delete;
		// ImGui文脈の所有権を代入しない。
		FEffectEditorMenuContext& operator=( const FEffectEditorMenuContext& ) = delete;

		// フォント構築が完了して描画可能か返す。
		bool IsReady() const { return bFontsReady; }
		// 全フレームが正しい開始・終了対応を保ったか返す。
		bool AreFramesBalanced() const { return bFramesBalanced; }

		// 実Effect Editorを1フレーム描画してImGuiフレームを完結させる。
		void Frame( FEffectEditorState& State, bool& bOpen )
		{
			ImGui::NewFrame();
			NDebugUI::DrawEffectEditor( State, &bOpen );
			ImGui::Render();
			bFramesBalanced = bFramesBalanced && !Context->WithinFrameScope && Context->CurrentWindowStack.Size == 0;
		}

		// メニュー初期化や折り返し移動がNavIdへ反映されるまでフレームを進める。
		void SettleNav( FEffectEditorState& State, bool& bOpen )
		{
			// 保留中のナビゲーション要求を消化する反復。
			for ( int32 Pass = 0; Pass < 4 && ( Context->NavMoveForwardToNextFrame || Context->NavMoveSubmitted || Context->NavInitRequest || Context->NavInitResult.ID != 0 ); ++Pass ) Frame( State, bOpen );
		}

		// Altキーを押して離し、Effect Editorのメニュー層へフォーカスを移す。
		void TapAlt( FEffectEditorState& State, bool& bOpen )
		{
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Alt, true );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_LeftAlt, true );
			Frame( State, bOpen );
			ImGui::GetIO().AddKeyEvent( ImGuiKey_LeftAlt, false );
			ImGui::GetIO().AddKeyEvent( ImGuiMod_Alt, false );
			Frame( State, bOpen );
			SettleNav( State, bOpen );
		}

		// 指定キーを1回押して離し、ナビゲーション結果を確定させる。
		void Tap( FEffectEditorState& State, bool& bOpen, ImGuiKey Key )
		{
			ImGui::GetIO().AddKeyEvent( Key, true );
			Frame( State, bOpen );
			ImGui::GetIO().AddKeyEvent( Key, false );
			Frame( State, bOpen );
			SettleNav( State, bOpen );
		}

		// メニューバーのPresetを開き、末尾方向へ指定回数移動して項目を確定する。
		void SelectPresetFromMenu( FEffectEditorState& State, bool& bOpen, int32 UpCount )
		{
			TapAlt( State, bOpen );
			Tap( State, bOpen, ImGuiKey_RightArrow );
			Tap( State, bOpen, ImGuiKey_Enter );
			// メニュー先頭から上方向折り返しで末尾側項目へ移動する反復。
			for ( int32 Step = 0; Step < UpCount; ++Step ) Tap( State, bOpen, ImGuiKey_UpArrow );
			Tap( State, bOpen, ImGuiKey_Enter );
		}

	private:
		// 退避した呼出元のImGui文脈。
		ImGuiContext* Previous = nullptr;
		// このテスト専用の独立ImGui文脈。
		ImGuiContext* Context = nullptr;
		// フォントアトラスの構築成否。
		bool bFontsReady = false;
		// 全フレームのスコープ整合が保たれているか。
		bool bFramesBalanced = true;
	};

	// スコープ終了時にEffect EditorのGPU描画先と再生資源を必ず解放するガード。
	class FEffectEditorStateScope
	{
	public:
		// 解放対象の編集状態を受け取る。
		explicit FEffectEditorStateScope( FEffectEditorState& InState ) : State( InState ) {}
		// スコープ終了時に描画先と再生資源を解放する。
		~FEffectEditorStateScope() { NDebugUI::ReleaseEffectEditor( State ); }
		// 状態ガードを複製しない。
		FEffectEditorStateScope( const FEffectEditorStateScope& ) = delete;
		// 状態ガードを代入しない。
		FEffectEditorStateScope& operator=( const FEffectEditorStateScope& ) = delete;

	private:
		// 解放するEffect Editorの状態。
		FEffectEditorState& State;
	};

	// 実Effect EditorのPresetメニューからGustとWhirlwindをキーボード選択できるか確かめる。
	void TestForceFieldPresetMenu( NAssetTest::FChecker& C )
	{
		C.Group = "effect-force-field-preset-menu";
		// テスト開始前の呼出元ImGui文脈。
		ImGuiContext* PreviousContext = ImGui::GetCurrentContext();
		{
			// キーボード操作を流す独立ImGui文脈。
			FEffectEditorMenuContext Menu;
			C.Check( Menu.IsReady(), "Presetメニュー検査用の独立ImGui文脈を初期化する" );
			if ( !Menu.IsReady() ) return;
			// 実DrawEffectEditorへ渡す編集状態。
			FEffectEditorState State;
			// 途中終了でもGPU資源を漏らさない解放ガード。
			FEffectEditorStateScope StateScope( State );
			// Effect Editorウィンドウの開閉フラグ。
			bool bOpen = true;
			Menu.Frame( State, bOpen );
			State.Document.MarkSaved();
			Menu.SelectPresetFromMenu( State, bOpen, 1 );
			// Gust選択直後に期待する状態一式が揃っているか。
			const bool bGustSelected = bOpen && State.Document.IsDirty() && State.Document.Get().Name == "Gust" && State.FilePath.IsEmpty() && State.SaveAsPath == "Effects/force_field_gust_copy.effect.json" && State.Document.Get().Emitters.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields[0].Kind == FEffectForceFieldDefinition::EKind::Turbulence;
			C.Check( bGustSelected, "PresetメニューからGustを選ぶとTurbulence力場を持つ未保存コピーが作られる" );
			State.Document.MarkSaved();
			Menu.SelectPresetFromMenu( State, bOpen, 2 );
			// Whirlwind選択直後に期待する状態一式が揃っているか。
			const bool bWhirlwindSelected = bOpen && State.Document.IsDirty() && State.Document.Get().Name == "Whirlwind" && State.FilePath.IsEmpty() && State.SaveAsPath == "Effects/force_field_whirlwind_copy.effect.json" && State.Document.Get().Emitters.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields.IsValidIndex( 0 ) && State.Document.Get().Emitters[0].ForceFields[0].Kind == FEffectForceFieldDefinition::EKind::Vortex;
			C.Check( bWhirlwindSelected, "PresetメニューからWhirlwindを選ぶとVortex力場を持つ未保存コピーが作られる" );
			NDebugUI::ReleaseEffectEditor( State );
			C.Check( !State.Playback.IsValid() && State.Surface.GetHandle() < 0 && Menu.AreFramesBalanced(), "Presetメニュー検証後にEffect Editorの再生資源とGPU描画先を解放する" );
		}
		C.Check( ImGui::GetCurrentContext() == PreviousContext, "Presetメニュー検証後に呼出元のImGui文脈を復元する" );
	}
}

namespace NWorldTest
{
	// 既知の空間点と描画payloadを使い、製品helperの再計算に依存せずに確かめる。
	void RunEffectForceFieldEditorGeometryTests( NAssetTest::FChecker& C )
	{
		C.Group = "effect-force-field-editor-camera";
		// 既知の画角とclip距離を持つ検査viewport。
		const FEditorViewport Viewport{ 1280, 720, 0.8f, 0.05f, 2000.0f };
		// focus関数を直接呼ぶ編集状態。
		FEffectEditorState State;
		State.Document.Reset( MakeForceFieldGeometryDefinition() );
		// focus前のdefinition hash。
		const uint64 DefinitionHash = State.Document.Get().ComputeHash();
		// focus前のundo件数。
		const int32 UndoCount = State.Document.GetNumUndo();
		// focus前に保つcamera一式。
		const FEditorCamera BeforeFocus = State.Camera;
		// 既知geometryに対するfocus成否。
		const bool bFocused = NEffectEditorUI::FocusCameraOnForceFields( State, Viewport );
		C.Check( bFocused && State.Document.Get().ComputeHash() == DefinitionHash && State.Document.GetNumUndo() == UndoCount, "multiEmitter focusは文書hashと履歴を変えない" );
		// 最長particle lifetime。
		const float64 Lifetime = 2.0;
		// speed rangeの最大値。
		const float64 SpeedMax = 1.5;
		// 有効field strengthの手計算上限和。
		const float64 ForceLimit = 4.0 + 2.0 + 1.5;
		// gravity vectorの手計算長さ。
		const float64 GravityLength = 0.25;
		// 半陰的更新に対する保守移動範囲。
		const float64 Travel = SpeedMax * Lifetime + ( GravityLength + ForceLimit ) * Lifetime * Lifetime;
		// box発生範囲の手計算半対角。
		const float64 SpawnRadius = 0.5 * std::sqrt( 0.4 * 0.4 + 0.6 * 0.6 + 0.8 * 0.8 );
		// 最大sizeとappearance倍率から出す半対角。
		const float64 ParticleRadius = 1.0 * 2.0 * 0.7071067811865476;
		// Lightning終点の手計算距離。
		const float64 EndLength = std::sqrt( 1.0 * 1.0 + 2.0 * 2.0 + 3.0 * 3.0 );
		// 主幹二軸揺れと枝端の独立spread係数。
		const float64 Spread = 1.0 + std::sqrt( 2.0 ) * 0.2 + 0.4 + std::sqrt( 2.0 ) * 0.35 * 0.2;
		// size余白込みLightning最大距離。
		const float64 LightningRadius = EndLength * Spread + ParticleRadius;
		// spawn・travel・粒子・pathを合算した球半径。
		const float64 EmitterRadius = SpawnRadius + Travel + std::max( ParticleRadius, 0.0 ) + LightningRadius;
		// 六方向の独立投影結果を集約する値。
		bool bEmitterExtremesVisible = bFocused;
		for ( const FVector3& Point : { FVector3( static_cast<float32>( EmitterRadius ), 0.0f, 0.0f ), FVector3( -static_cast<float32>( EmitterRadius ), 0.0f, 0.0f ), FVector3( 0.0f, static_cast<float32>( EmitterRadius ), 0.0f ), FVector3( 0.0f, -static_cast<float32>( EmitterRadius ), 0.0f ), FVector3( 0.0f, 0.0f, static_cast<float32>( EmitterRadius ) ), FVector3( 0.0f, 0.0f, -static_cast<float32>( EmitterRadius ) ) } ) bEmitterExtremesVisible = bEmitterExtremesVisible && IsProjectedInside( State.Camera, Viewport, Point );
		// 小半径radialの球と矢印を含む手計算範囲。
		const float64 TinyGuide = std::max( 0.001 * 0.52 + std::clamp( 0.001 * 0.2, 0.25, 1.25 ), 0.001 );
		// radial guide外周の投影結果を集約する値。
		bool bTinyFieldVisible = bFocused;
		for ( const FVector3& Point : { FVector3( static_cast<float32>( 2.0 + TinyGuide ), -1.0f, 3.0f ), FVector3( static_cast<float32>( 2.0 - TinyGuide ), -1.0f, 3.0f ), FVector3( 2.0f, static_cast<float32>( -1.0 + TinyGuide ), 3.0f ), FVector3( 2.0f, static_cast<float32>( -1.0 - TinyGuide ), 3.0f ), FVector3( 2.0f, -1.0f, static_cast<float32>( 3.0 + TinyGuide ) ), FVector3( 2.0f, -1.0f, static_cast<float32>( 3.0 - TinyGuide ) ) } ) bTinyFieldVisible = bTinyFieldVisible && IsProjectedInside( State.Camera, Viewport, Point );
		// 正Radialの球と最短矢印を手計算した合成半径。
		const float64 PositiveGuide = std::max( 0.25, 0.25 * 0.52 + std::clamp( 0.25 * 0.2, 0.25, 1.25 ) );
		// 正Radial guide外周の六方向projectionを集約する値。
		bool bPositiveFieldVisible = bFocused;
		for ( const FVector3& Point : { FVector3( static_cast<float32>( -3.0 + PositiveGuide ), 1.0f, 2.0f ), FVector3( static_cast<float32>( -3.0 - PositiveGuide ), 1.0f, 2.0f ), FVector3( -3.0f, static_cast<float32>( 1.0 + PositiveGuide ), 2.0f ), FVector3( -3.0f, static_cast<float32>( 1.0 - PositiveGuide ), 2.0f ), FVector3( -3.0f, 1.0f, static_cast<float32>( 2.0 + PositiveGuide ) ), FVector3( -3.0f, 1.0f, static_cast<float32>( 2.0 - PositiveGuide ) ) } ) bPositiveFieldVisible = bPositiveFieldVisible && IsProjectedInside( State.Camera, Viewport, Point );
		C.Check( bEmitterExtremesVisible && bTinyFieldVisible && bPositiveFieldVisible, "独立式のtravel・spawn・size・Lightning二軸揺れと正負radial guideを画面内へ投影する" );
		C.Check( State.Camera.Distance != BeforeFocus.Distance || State.Camera.Target != BeforeFocus.Target, "focus成功時は既定cameraを既知geometryへ移す" );

		C.Group = "effect-force-field-editor-camera-bounds";
		// T=2、speed=3、gravity=1、uniform=2から手計算するtravel長。
		const float64 TravelExtent = 3.0 * 2.0 + ( 1.0 + 2.0 ) * 2.0 * 2.0;
		// travel計算だけを持つ検査definition。
		FParticleEffectDefinition TravelDefinition;
		// 他のbounds寄与を0にした移動emitter。
		FParticleEmitterDefinition TravelEmitter;
		TravelEmitter.Lifetime = { 2.0f, 2.0f };
		TravelEmitter.Speed = { 3.0f, 3.0f };
		TravelEmitter.Gravity = FVector3( 0.0f, 1.0f, 0.0f );
		TravelEmitter.StartSize = { 0.0f, 0.0f };
		TravelEmitter.EndSize = { 0.0f, 0.0f };
		// travel加速度だけを与える一様field。
		FEffectForceFieldDefinition TravelField;
		TravelField.Acceleration = FVector3( 2.0f, 0.0f, 0.0f );
		TravelEmitter.ForceFields.Add( TravelField );
		TravelDefinition.Emitters.Add( TravelEmitter );
		C.Check( HasFocusedAxisExtents( TravelDefinition, Viewport, TravelExtent ), "speedMax*T+(gravityLength+uniformLength)*T^2の単独travel boundsを六方向で投影する" );

		// box発生範囲だけを持つ検査definition。
		FParticleEffectDefinition SpawnDefinition;
		// 移動と粒子sizeを0にしたbox emitter。
		FParticleEmitterDefinition SpawnEmitter;
		SpawnEmitter.Lifetime = { 1.0f, 1.0f };
		SpawnEmitter.Speed = { 0.0f, 0.0f };
		SpawnEmitter.Shape = EParticleShape::Box;
		SpawnEmitter.BoxSize = FVector3( 4.0f, 2.0f, 2.0f );
		SpawnEmitter.StartSize = { 0.0f, 0.0f };
		SpawnEmitter.EndSize = { 0.0f, 0.0f };
		// travelを増やさないゼロ一様field。
		FEffectForceFieldDefinition SpawnField;
		SpawnField.Acceleration = FVector3::Zero;
		SpawnEmitter.ForceFields.Add( SpawnField );
		SpawnDefinition.Emitters.Add( SpawnEmitter );
		// BoxSizeの半対角を独立に計算した値。
		const float64 SpawnExtent = 0.5 * std::sqrt( 4.0 * 4.0 + 2.0 * 2.0 + 2.0 * 2.0 );
		C.Check( HasFocusedAxisExtents( SpawnDefinition, Viewport, SpawnExtent ), "box半対角の単独spawn boundsを六方向で投影する" );

		// size倍率だけを持つ検査definition。
		FParticleEffectDefinition SizeDefinition;
		// speed0で曲線倍率を最大3にするemitter。
		FParticleEmitterDefinition SizeEmitter;
		SizeEmitter.Lifetime = { 1.0f, 1.0f };
		SizeEmitter.Speed = { 0.0f, 0.0f };
		SizeEmitter.StartSize = { 10.0f, 10.0f };
		SizeEmitter.EndSize = { 10.0f, 10.0f };
		SizeEmitter.Appearance.Size.Keys.Empty();
		SizeEmitter.Appearance.Size.Keys.Add( { 0.0f, 1.0f } );
		SizeEmitter.Appearance.Size.Keys.Add( { 1.0f, 3.0f } );
		// travelを増やさないゼロ一様field。
		FEffectForceFieldDefinition SizeField;
		SizeField.Acceleration = FVector3::Zero;
		SizeEmitter.ForceFields.Add( SizeField );
		SizeDefinition.Emitters.Add( SizeEmitter );
		// size10・倍率3の四角形半対角。
		const float64 SizeExtent = 10.0 * 3.0 * 0.7071067811865476;
		C.Check( HasFocusedAxisExtents( SizeDefinition, Viewport, SizeExtent ), "最大sizeとcurve倍率の単独粒子boundsを六方向で投影する" );

		// Lightning完成形だけを持つ検査definition。
		FParticleEffectDefinition LightningDefinition;
		// 移動を止め、pathだけをboundsへ加えるemitter。
		FParticleEmitterDefinition LightningEmitter;
		LightningEmitter.Lifetime = { 1.0f, 1.0f };
		LightningEmitter.Speed = { 0.0f, 0.0f };
		LightningEmitter.StartSize = { 0.2f, 0.2f };
		LightningEmitter.EndSize = { 0.2f, 0.2f };
		LightningEmitter.Path.Kind = FEffectPathDefinition::EKind::Lightning;
		LightningEmitter.Path.End = FVector3( 2.0f, 3.0f, 6.0f );
		LightningEmitter.Path.Jitter = 0.4f;
		LightningEmitter.Path.Branches = 2;
		LightningEmitter.Path.BranchLength = 0.6f;
		// travelを増やさないゼロ一様field。
		FEffectForceFieldDefinition LightningField;
		LightningField.Acceleration = FVector3::Zero;
		LightningEmitter.ForceFields.Add( LightningField );
		LightningDefinition.Emitters.Add( LightningEmitter );
		// Lightning終点ベクトルの独立長さ。
		const float64 LightningEnd = std::sqrt( 2.0 * 2.0 + 3.0 * 3.0 + 6.0 * 6.0 );
		// jitter二軸と枝長・枝揺れの手計算倍率。
		const float64 LightningSpread = 1.0 + std::sqrt( 2.0 ) * 0.4 + 0.6 + std::sqrt( 2.0 ) * 0.35 * 0.4;
		// jitter・枝・粒子sizeを含む期待半径。
		const float64 LightningExtent = LightningEnd * LightningSpread + 0.2 * 0.7071067811865476;
		C.Check( HasFocusedAxisExtents( LightningDefinition, Viewport, LightningExtent ), "Lightning主幹二軸揺れと枝先sqrt2項の単独boundsを六方向で投影する" );

		// 既知definitionの一回限りfocus状態.
		FEffectEditorState Pending;
		Pending.Document.Reset( MakeForceFieldGeometryDefinition() );
		NEffectEditorUI::SetPendingEffectCameraFocus( Pending, Pending.Document.Get() );
		// SetPendingがeffect focus印を立てたか。
		const bool bPendingSet = Pending.bEffectGeometryCameraFocusPending;
		// 初回pending focusが成功したか。
		const bool bPendingApplied = NEffectEditorUI::ApplyPendingEffectCameraFocus( Pending, Viewport );
		Pending.Camera.Pan( Viewport, 17.0f, -9.0f );
		// manual pan後に保持すべきcamera。
		const FEditorCamera ManualCamera = Pending.Camera;
		// 消費済みpendingが再適用されないか。
		const bool bSecondApply = NEffectEditorUI::ApplyPendingEffectCameraFocus( Pending, Viewport );
		C.Check( bPendingSet && bPendingApplied && !Pending.bEffectGeometryCameraFocusPending && !bSecondApply && SameCamera( Pending.Camera, ManualCamera ), "pending focusを一度消費した後は手動panを保つ" );

		// near面の境界再現だけを含むdefinition。
		FParticleEffectDefinition TinyDefinition;
		// 移動も発生範囲もない最小emitter。
		FParticleEmitterDefinition TinyEmitter;
		TinyEmitter.Lifetime = { 1.0f, 1.0f };
		TinyEmitter.Speed = { 0.0f, 0.0f };
		TinyEmitter.Gravity = FVector3::Zero;
		TinyEmitter.ShapeRadius = 0.0f;
		TinyEmitter.StartSize = { 0.001f, 0.001f };
		TinyEmitter.EndSize = { 0.001f, 0.001f };
		// radius .001でstrengthが0のradial field。
		FEffectForceFieldDefinition TinyField;
		TinyField.Kind = FEffectForceFieldDefinition::EKind::Radial;
		TinyField.Radius = 0.001f;
		TinyField.Strength = 0.0f;
		TinyEmitter.ForceFields.Add( TinyField );
		TinyDefinition.Emitters.Add( TinyEmitter );
		// 最小radial boundsを初回focusする状態。
		FEffectEditorState TinyPending;
		TinyPending.Document.Reset( TinyDefinition );
		NEffectEditorUI::SetPendingEffectCameraFocus( TinyPending, TinyPending.Document.Get() );
		// near面境界での初回focus成否。
		const bool bTinyPendingFocused = NEffectEditorUI::ApplyPendingEffectCameraFocus( TinyPending, Viewport );
		// 中心と六端点がnear面より前か集約する値。
		bool bTinyBoundsInFrontOfNear = bTinyPendingFocused;
		for ( const FVector3& Point : { FVector3::Zero, FVector3( 0.001f, 0.0f, 0.0f ), FVector3( -0.001f, 0.0f, 0.0f ), FVector3( 0.0f, 0.001f, 0.0f ), FVector3( 0.0f, -0.001f, 0.0f ), FVector3( 0.0f, 0.0f, 0.001f ), FVector3( 0.0f, 0.0f, -0.001f ) } ) bTinyBoundsInFrontOfNear = bTinyBoundsInFrontOfNear && IsProjectedInside( TinyPending.Camera, Viewport, Point );
		C.Check( bTinyPendingFocused && !TinyPending.bEffectGeometryCameraFocusPending && bTinyBoundsInFrontOfNear, "radius .001・strength 0の初回pending focusは成功し全bounds点をnear面の先へ置く" );

		// 粒子側の範囲が全て0でradial guideだけを持つemitter。
		TinyDefinition.Emitters[0].StartSize = { 0.0f, 0.0f };
		TinyDefinition.Emitters[0].EndSize = { 0.0f, 0.0f };
		TinyDefinition.Emitters[0].ForceFields[0].Position = FVector3( 2.0f, 0.0f, 0.0f );
		TinyDefinition.Emitters[0].ForceFields[0].Radius = 1.0f;
		// 半径0のemitter点をradial guide球とunionできるか見るfocus状態。
		FEffectEditorState GuideOnlyFocus;
		GuideOnlyFocus.Document.Reset( TinyDefinition );
		C.Check( NEffectEditorUI::FocusCameraOnForceFields( GuideOnlyFocus, Viewport ), "粒子範囲0でも中心(2,0,0)・半径1のstrength0 radial guideだけでfocusする" );

		// field無しの失敗時camera確認状態。
		FEffectEditorState Empty;
		// 空文書で保つ元camera。
		const FEditorCamera EmptyCamera = Empty.Camera;
		C.Check( !NEffectEditorUI::FocusCameraOnForceFields( Empty, Viewport ) && SameCamera( Empty.Camera, EmptyCamera ), "fieldなしではfalseを返しcameraを全て保つ" );
		// 不正入力による失敗時camera確認状態。
		FEffectEditorState Invalid;
		// 非有限radiusを含む失敗用definition。
		FParticleEffectDefinition InvalidDefinition = MakeForceFieldGeometryDefinition();
		InvalidDefinition.Emitters[0].ForceFields[1].Radius = std::numeric_limits<float32>::quiet_NaN();
		Invalid.Document.Reset( InvalidDefinition );
		// 不正入力検査前に保つ元camera。
		const FEditorCamera InvalidCamera = Invalid.Camera;
		C.Check( !NEffectEditorUI::FocusCameraOnForceFields( Invalid, Viewport ) && SameCamera( Invalid.Camera, InvalidCamera ), "NaN fieldではcameraを全て保つ" );
		InvalidDefinition = MakeForceFieldGeometryDefinition();
		InvalidDefinition.Emitters[0].Speed.Max = 1.0e20f;
		Invalid.Document.Reset( InvalidDefinition );
		C.Check( !NEffectEditorUI::FocusCameraOnForceFields( Invalid, Viewport ) && SameCamera( Invalid.Camera, InvalidCamera ), "camera上限を越える有限travel boundsではcameraを保つ" );

		C.Group = "effect-force-field-editor-guides";
		// guide記録に使う非保存editor状態。
		FEffectEditorState GuideState;
		// Uniform・Radialが選べるguide文書。
		FParticleEffectDefinition GuideDefinition;
		// 三つのguide fieldを持つemitter。
		FParticleEmitterDefinition GuideEmitter;
		// 正規化端点を検査するUniform field。
		FEffectForceFieldDefinition Uniform;
		Uniform.Acceleration = FVector3( 0.0f, -3.0f, 4.0f );
		GuideEmitter.ForceFields.Add( Uniform );
		// 符号方向とWorld中心を検査する負Radial。
		FEffectForceFieldDefinition Radial;
		Radial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		Radial.Position = FVector3( 7.0f, 8.0f, 9.0f );
		Radial.Radius = 2.0f;
		Radial.Strength = -3.0f;
		GuideEmitter.ForceFields.Add( Radial );
		// 既定最短矢印を検査する正tiny Radial。
		FEffectForceFieldDefinition TinyPositiveRadial;
		TinyPositiveRadial.Kind = FEffectForceFieldDefinition::EKind::Radial;
		TinyPositiveRadial.Position = FVector3( -2.0f, 1.0f, 0.5f );
		TinyPositiveRadial.Radius = 0.001f;
		TinyPositiveRadial.Strength = 1.0f;
		GuideEmitter.ForceFields.Add( TinyPositiveRadial );
		GuideDefinition.Emitters.Add( GuideEmitter );
		GuideState.Document.Reset( GuideDefinition );
		// guide記録直前のpreview clock。
		const float32 GuideClock = GuideState.PreviewMotionTime;
		// guide記録前の文書保存値。
		const uint64 GuideHash = GuideState.Document.Get().ComputeHash();
		// guide記録前の編集履歴件数。
		const int32 GuideUndo = GuideState.Document.GetNumUndo();
		// Uniform guideが生成する実command列。
		FDrawBuffer UniformBuffer;
		// Uniform payloadの記録窓口。
		FDrawContext UniformDraw( UniformBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, UniformDraw );
		// ForEachDrawCommandから得たUniform payload。
		const FGuideReadback UniformReadback = ReadGuides( UniformBuffer );
		// 製品設定と同じ固定長を独立に置く。
		const float32 UniformLength = 1.25f;
		// 加速度を正規化して手計算した矢印端点。
		const FVector3 UniformEnd = FVector3( 0.0f, -3.0f, 4.0f ).Normalized() * UniformLength;
		C.Check( UniformReadback.Lines.Num() == 3 && UniformReadback.Spheres.IsEmpty() && AllLinesHaveColor( UniformReadback, 40, 220, 230 ) && HasLine( UniformReadback, FVector3::Zero, UniformEnd, 40, 220, 230 ), "Uniform arrowはWorld原点から加速度を正規化した1.25m端点へ伸びる" );
		GuideState.SelectedForceField = 1;
		// 負Radial guideの実command列。
		FDrawBuffer RadialBuffer;
		// 負Radial payloadの記録窓口。
		FDrawContext RadialDraw( RadialBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, RadialDraw );
		// bufferから読み直した負Radial payload。
		const FGuideReadback RadialReadback = ReadGuides( RadialBuffer );
		// 半径の52%位置に置く矢印始点。
		const FVector3 RadialStart( 7.0f + 2.0f * 0.52f, 8.0f, 9.0f );
		// 負strengthが中心向きへ示す端点。
		const FVector3 RadialEnd = RadialStart + FVector3::Left * 0.4f;
		C.Check( RadialReadback.Spheres.IsEmpty() && RadialReadback.Lines.Num() == 99 && HasRadialCircleGuides( RadialReadback, FVector3( 7.0f, 8.0f, 9.0f ), 2.0f, 40, 220, 230 ) && HasLine( RadialReadback, RadialStart, RadialEnd, 40, 220, 230 ), "負RadialはWorld中心・半径2の三円と中心向き矢印を実commandへ記録する" );
		GuideState.SelectedForceField = 2;
		// tiny positive Radial guideの実command列。
		FDrawBuffer TinyBuffer;
		// tiny radial payloadの記録窓口。
		FDrawContext TinyDraw( TinyBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, TinyDraw );
		// bufferから読み直したtiny radial payload。
		const FGuideReadback TinyReadback = ReadGuides( TinyBuffer );
		// tiny radius境界に置く矢印始点。
		const FVector3 TinyStart( -2.0f + 0.001f * 0.52f, 1.0f, 0.5f );
		// 正strengthが外向きへ示す最短端点。
		const FVector3 TinyEnd = TinyStart + FVector3::Right * 0.25f;
		C.Check( TinyReadback.Spheres.IsEmpty() && TinyReadback.Lines.Num() == 99 && HasRadialCircleGuides( TinyReadback, FVector3( -2.0f, 1.0f, 0.5f ), 0.001f, 40, 220, 230 ) && HasLine( TinyReadback, TinyStart, TinyEnd, 40, 220, 230 ), "正tiny radialは三平面の半径.001円と0.25m外向き矢印を記録する" );
		GuideState.SelectedForceField = 1;
		GuideState.Document.BeginEdit().Emitters[0].ForceFields[1].bEnabled = false;
		GuideState.Document.EndEditSession();
		// 無効fieldの色payload用command列。
		FDrawBuffer DisabledBuffer;
		// 無効field payloadの記録窓口。
		FDrawContext DisabledDraw( DisabledBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, DisabledDraw );
		// bufferから読み直した無効field payload。
		const FGuideReadback DisabledReadback = ReadGuides( DisabledBuffer );
		C.Check( DisabledReadback.Spheres.IsEmpty() && DisabledReadback.Lines.Num() == 99 && HasRadialCircleGuides( DisabledReadback, FVector3( 7.0f, 8.0f, 9.0f ), 2.0f, 125, 140, 150 ) && HasLine( DisabledReadback, RadialStart, RadialEnd, 125, 140, 150 ), "disabled radial guideの三円と矢印を灰色commandにする" );
		GuideState.bShowForceFieldGuides = false;
		// guide非表示の空出力確認buffer。
		FDrawBuffer HiddenBuffer;
		// 非表示出力用の記録窓口。
		FDrawContext HiddenDraw( HiddenBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, HiddenDraw );
		GuideState.bShowForceFieldGuides = true;
		GuideState.SelectedForceField = 99;
		// 範囲外field indexの空出力確認buffer。
		FDrawBuffer InvalidSelectionBuffer;
		// 範囲外field選択用の記録窓口。
		FDrawContext InvalidSelectionDraw( InvalidSelectionBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, InvalidSelectionDraw );
		GuideState.SelectedForceField = 0;
		GuideState.SelectedEmitter = 99;
		// 範囲外emitter indexの空出力確認buffer。
		FDrawBuffer InvalidEmitterBuffer;
		// 範囲外emitter選択用の記録窓口。
		FDrawContext InvalidEmitterDraw( InvalidEmitterBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, InvalidEmitterDraw );
		GuideState.SelectedEmitter = 0;
		GuideState.Document.BeginEdit().Emitters[0].ForceFields[0].Strength = std::numeric_limits<float32>::infinity();
		GuideState.Document.EndEditSession();
		// 保存値不正fieldの空出力確認buffer。
		FDrawBuffer InvalidFieldBuffer;
		// 保存値不正field用の記録窓口。
		FDrawContext InvalidFieldDraw( InvalidFieldBuffer );
		NEffectEditorUI::RecordForceFieldGuides( GuideState, InvalidFieldDraw );
		C.Check( HiddenBuffer.Words.empty() && InvalidSelectionBuffer.Words.empty() && InvalidEmitterBuffer.Words.empty() && InvalidFieldBuffer.Words.empty(), "hidden・無効emitter/field選択・不正fieldではguide commandを作らない" );
		C.Check( GuideState.Document.Get().ComputeHash() != GuideHash && GuideState.Document.GetNumUndo() == GuideUndo + 2, "guide記録は文書hashとUndo履歴に触れず、変化は明示編集だけが作る" );
		C.Check( GuideState.PreviewMotionTime == GuideClock, "guide記録はpreview clockを進めない" );
		TestForceFieldKindGuides( C );
		TestForceFieldKindFocus( C );
	}

	// 実プリセット二種を編集し、保存後World配置とnative再生へ渡す。
	void RunForceFieldPresetTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor )
	{
		C.Group = "effect-force-field-preset-root";
		// preset real testだけで使うAssets root guard。
		FAssetRootScope RootScope( Assets );
		C.Check( RootScope.IsReady(), "real preset testがrunnerのAssets sandbox rootへ切替える" );
		if ( !RootScope.IsReady() ) return;
		TestOneForceFieldPreset( C, Assets, Executor, "Effects/force_field_wind.effect.json", "Effects/selftest_force_field_wind.effect.json" );
		TestOneForceFieldPreset( C, Assets, Executor, "Effects/force_field_gravity_well.effect.json", "Effects/selftest_force_field_gravity_well.effect.json" );
		TestOneForceFieldPreset( C, Assets, Executor, "Effects/force_field_whirlwind.effect.json", "Effects/selftest_force_field_whirlwind.effect.json" );
		TestOneForceFieldPreset( C, Assets, Executor, "Effects/force_field_gust.effect.json", "Effects/selftest_force_field_gust.effect.json" );
		TestKindPreset( C, Assets, "Effects/force_field_whirlwind.effect.json", FEffectForceFieldDefinition::EKind::Vortex );
		TestKindPreset( C, Assets, "Effects/force_field_gust.effect.json", FEffectForceFieldDefinition::EKind::Turbulence );
		TestForceFieldPresetMenu( C );
		// 全preset処理後に前のrootへ戻ったか。
		const bool bRootRestored = RootScope.Restore();
		C.Check( bRootRestored && Assets.GetRootDirectory() == RootScope.GetPreviousRoot(), "preset保存・World cleanup後にAssets rootを元へ戻す" );
	}
}

namespace NWorldTest
{
	// 保存済みforce field presetがEditor・World・標準playbackを通ることを確かめる。
	void RunEffectForceFieldEditorPresetRealTests( NAssetTest::FChecker& C, CAssetManager& Assets, IDrawExecutor& Executor )
	{
		RunForceFieldPresetTests( C, Assets, Executor );
	}
}

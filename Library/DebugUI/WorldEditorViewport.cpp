#include <algorithm>
#include <cmath>

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/DebugUI/WorldEditorInternal.h"
#include "Library/Framework/Engine/Engine.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Framework/World/Editor/EditorPicking.h"
#include "Library/Framework/World/WorldCollision.h"
#include "Library/Framework/World/WorldHierarchy.h"
#include "Library/Framework/World/WorldRender.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// ワールドエディタの 3D の視界（ビューポート）
//   - 3D の描画（DxLib）: 格子・モデル・ビルボード・目印・当たり判定の枠を、専用の画面に描き、ImGui の画像として出す。
//     モデルとビルボードは、実行時と同じ関数（FDrawContext、NWorld::MakeBillboard）で描くので、ゲームと見え方が同じになる。
//   - 重ね描き（ImGui）: 選択の枠・ギズモ・名前・矩形選択・軸の目印。奥行きに関係なく、いつも見える。
//   - 入力: カメラの操作・クリックと矩形での選択・ギズモのドラッグ・アセットのドロップ。選択やギズモの結果は CWorldEditorSession が決める。
//=============================================================================
namespace
{
	using FState = FWorldEditorState;
	using EMouseMode = FState::EMouseMode;

	constexpr float32 DragThreshold = 4.0f;   // これ以上動いたら、クリックでなく矩形選択

	FColor32 ToColor32( ImU32 Color )
	{
		return FColor32( static_cast<uint8>( ( Color >> IM_COL32_R_SHIFT ) & 0xFF ), static_cast<uint8>( ( Color >> IM_COL32_G_SHIFT ) & 0xFF ),
						 static_cast<uint8>( ( Color >> IM_COL32_B_SHIFT ) & 0xFF ), static_cast<uint8>( ( Color >> IM_COL32_A_SHIFT ) & 0xFF ) );
	}

	ImU32 WithAlpha( ImU32 Color, int32 Alpha )
	{
		return ( Color & ~IM_COL32_A_MASK ) | ( static_cast<ImU32>( NMath::Clamp( Alpha, 0, 255 ) ) << IM_COL32_A_SHIFT );
	}

	ImU32 AxisColor( int32 Axis ) { return Axis == 0 ? NWorldEditorUI::ColorAxisX : Axis == 1 ? NWorldEditorUI::ColorAxisY : NWorldEditorUI::ColorAxisZ; }

	void EnsureScreen( FState& S, int32 Width, int32 Height )
	{
		if ( S.ViewScreen >= 0 && S.ViewWidth == Width && S.ViewHeight == Height ) return;
		if ( S.ViewScreen >= 0 ) DeleteGraph( S.ViewScreen );
		S.ViewScreen = MakeScreen( Width, Height, FALSE );
		S.ViewWidth = Width;
		S.ViewHeight = Height;
	}

	//=========================================================================
	// 3D の描画
	//=========================================================================
	void RecordGrid( FDrawContext& Draw, const FEditorCamera& Camera )
	{
		// 注視点までの距離に合わせた 1, 10, 100 ... の間隔。10 本ごとに濃い線
		const float32 Cell = NMath::Pow( 10.0f, NMath::Floor( NMath::Log10( NMath::Max( Camera.Distance * 0.25f, 0.01f ) ) ) );
		constexpr int32 Half = 24;
		const int32 CenterX = static_cast<int32>( NMath::Round( Camera.Target.x / Cell ) );
		const int32 CenterZ = static_cast<int32>( NMath::Round( Camera.Target.z / Cell ) );
		const float32 MinX = static_cast<float32>( CenterX - Half ) * Cell, MaxX = static_cast<float32>( CenterX + Half ) * Cell;
		const float32 MinZ = static_cast<float32>( CenterZ - Half ) * Cell, MaxZ = static_cast<float32>( CenterZ + Half ) * Cell;
		const FColor32 Minor( 58, 61, 70, 255 ), Major( 82, 86, 98, 255 );
		for ( int32 i = -Half; i <= Half; ++i )
		{
			const int32 IndexX = CenterX + i, IndexZ = CenterZ + i;
			const float32 X = static_cast<float32>( IndexX ) * Cell, Z = static_cast<float32>( IndexZ ) * Cell;
			if ( IndexX == 0 ) Draw.Line3D( FVector3( X, 0, MinZ ), FVector3( X, 0, MaxZ ), FColor32( 70, 100, 190, 255 ) );   // Z 軸
			else Draw.Line3D( FVector3( X, 0, MinZ ), FVector3( X, 0, MaxZ ), IndexX % 10 == 0 ? Major : Minor );
			if ( IndexZ == 0 ) Draw.Line3D( FVector3( MinX, 0, Z ), FVector3( MaxX, 0, Z ), FColor32( 190, 75, 75, 255 ) );    // X 軸
			else Draw.Line3D( FVector3( MinX, 0, Z ), FVector3( MaxX, 0, Z ), IndexZ % 10 == 0 ? Major : Minor );
		}
	}

	// 当たり判定の色（凡例と同じ）
	FColor32 GetColliderColor( EEditorColliderDisplay Display )
	{
		switch ( Display )
		{
		case EEditorColliderDisplay::Solid: return FColor32( 80, 220, 110, 255 );
		case EEditorColliderDisplay::SolidForced: return FColor32( 200, 230, 80, 255 );
		case EEditorColliderDisplay::Trigger: return FColor32( 70, 200, 230, 255 );
		case EEditorColliderDisplay::Off: return FColor32( 130, 90, 90, 255 );
		case EEditorColliderDisplay::OffForced: return FColor32( 190, 80, 140, 255 );
		}
		return FColor32( 255, 255, 255, 255 );
	}

	// 1 フレームに描く、メッシュの当たり判定の三角形の数（全オブジェクトの合計）。多いと、線の数で重くなる
	constexpr int32 MeshWireTriangleBudget = 30000;

	void RecordWire( FDrawContext& Draw, const FEditorShape& Shape, const FColor32& Color )
	{
		TArray<FVector3> Lines;
		NWorldEditor::AppendShapeWireLines( Shape, 20, Lines );
		for ( int32 i = 0; i + 1 < Lines.Num(); i += 2 ) Draw.Line3D( Lines[i], Lines[i + 1], Color );
	}

	// 入れ子のワールドの、描く形の置き場の鍵（オブジェクトの Id と区別できるよう、上位のビットを立てる。同じ内容は、同じ鍵で共有される）
	int32 MakeNestedMeshKey( const FString& PathKey, int32 ObjectId )
	{
		uint32 Hash = 2166136261u;
		for ( int32 i = 0; i < PathKey.Len(); ++i ) Hash = ( Hash ^ static_cast<uint32>( PathKey[i] ) ) * 16777619u;
		Hash = ( Hash ^ static_cast<uint32>( ObjectId ) ) * 16777619u;
		return static_cast<int32>( 0x40000000u | ( Hash & 0x3FFFFFFFu ) );
	}

	// 入れ子のワールド（Prefab 部品）の中身を描く。HostIds は最上位から現在の親までの配置 Id。
	bool RecordPrefabContents( FDrawContext& Draw, FState& S, const FString& Path, const FTransform& Host, const TArray<int32>& HostIds, TArray<FString>& Stack, TSet<int32>& SeenMeshes )
	{
		FAssetEditorInfo* Info = S.AssetInfo.Get();
		if ( Info == nullptr || Path.IsEmpty() || Stack.Num() >= WorldMaxPrefabDepth ) return false;
		const FString Key = NWorld::NormalizeAssetPath( Path );
		for ( const FString& Visited : Stack )
		{
			if ( Visited == Key ) return false;
		}
		const FWorldDefinition* Contents = Info->GetWorld( Path );
		if ( Contents == nullptr ) return false;
		Stack.Add( Key );
		const TArray<FTransform> Transforms = NWorld::ComputeAllWorldTransforms( *Contents );
		for ( int32 i = 0; i < Contents->Objects.Num() && i < Transforms.Num(); ++i )
		{
			const FWorldObject& Object = Contents->Objects[i];
			if ( !Object.bEnabled ) continue;
			const FTransform Placed = FTransform::Combine( Host, Transforms[i] );
			if ( Object.Mesh.IsSet() )
			{
				const int32 MeshKey = MakeNestedMeshKey( Key, Object.Id );
				SeenMeshes.Add( MeshKey );
				NWorldEditorUI::RecordMeshObject( S, Object, Placed, Draw, MeshKey );
			}
			if ( Object.Model.IsSet() )
			{
				// Prefab の親配置鎖へ leaf Id を加え、別配置の個体と分ける。
				FWorldEditorModelPreviewKey PreviewKey;
				PreviewKey.ObjectIds = HostIds;
				PreviewKey.ObjectIds.Add( Object.Id );
				NWorldEditorUI::RecordModelObject( S, PreviewKey, Placed, Draw );
			}
			if ( Object.Billboard.IsSet() )
			{
				if ( const TSharedPtr<CTexture> Texture = Info->GetTexture( Object.Billboard.GetValue().Texture ); Texture.IsValid() && Texture->GetHandle() >= 0 ) NWorld::MakeBillboard( Object.Billboard.GetValue(), Texture ).Record( Draw, Placed );
			}
			if ( Object.Prefab.IsSet() )
			{
				// 子 Prefab の host Id を末尾へ加え、配置階層をそのまま保持する。
				TArray<int32> ChildHostIds = HostIds;
				ChildHostIds.Add( Object.Id );
				RecordPrefabContents( Draw, S, Object.Prefab.GetValue().Path, Placed, ChildHostIds, Stack, SeenMeshes );
			}
		}
		Stack.RemoveAt( Stack.Num() - 1 );
		return true;
	}

	void RecordScene( FDrawContext& Draw, FState& S )
	{
		const FWorldDefinition& World = S.Session.Document.Get();
		FAssetEditorInfo* Info = S.AssetInfo.Get();
		const FEditorShapeContext Context = NWorldEditorUI::MakeShapeContext( S );

		int32 MeshTrianglesLeft = MeshWireTriangleBudget;
		S.bMeshWireTruncated = false;
		S.FrameTransforms = NWorld::ComputeAllWorldTransforms( World );
		S.FrameShapes.Reset();
		S.FrameShapes.Reserve( World.Objects.Num() );

		if ( S.bShowGrid ) RecordGrid( Draw, S.Session.Camera );
		TSet<int32> SeenMeshes;

		for ( int32 Index = 0; Index < World.Objects.Num(); ++Index )
		{
			const FWorldObject& Object = World.Objects[Index];
			const FTransform& Placed = S.FrameTransforms[Index];
			S.FrameShapes.Add( NWorldEditor::GatherShapes( Object, Placed, Context ) );
			const TArray<FEditorShape>& Shapes = S.FrameShapes[Index];
			const bool bSelected = S.Session.IsSelected( Object.Id );
			bool bShownModel = false;
			bool bShownBoard = false;
			if ( Object.Mesh.IsSet() )
			{
				SeenMeshes.Add( Object.Id );
				if ( NWorldEditorUI::RecordMeshObject( S, Object, Placed, Draw, Object.Id ) ) bShownModel = true;
			}
			if ( Object.Prefab.IsSet() && Object.bEnabled )
			{
				TArray<FString> Stack;
				// 配置元 Prefab を鎖の先頭にして、同一Prefabの複数配置を区別する。
				TArray<int32> HostIds;
				HostIds.Add( Object.Id );
				if ( RecordPrefabContents( Draw, S, Object.Prefab.GetValue().Path, Placed, HostIds, Stack, SeenMeshes ) ) bShownModel = true;
			}

			if ( Object.bEnabled && Info != nullptr )
			{
				if ( Object.Model.IsSet() )
				{
					// ルート配置は自身の Id だけで個体を識別する。
					FWorldEditorModelPreviewKey PreviewKey;
					PreviewKey.ObjectIds.Add( Object.Id );
					if ( NWorldEditorUI::RecordModelObject( S, PreviewKey, Placed, Draw ) ) bShownModel = true;
				}
				if ( Object.Billboard.IsSet() )
				{
					if ( const TSharedPtr<CTexture> Texture = Info->GetTexture( Object.Billboard.GetValue().Texture ); Texture.IsValid() && Texture->GetHandle() >= 0 )
					{
						NWorld::MakeBillboard( Object.Billboard.GetValue(), Texture ).Record( Draw, Placed );
						bShownBoard = true;
					}
				}
			}

			for ( const FEditorShape& Shape : Shapes )
			{
				switch ( Shape.Role )
				{
				case EEditorShapeRole::Model:
					// 読み込めない・無効なものは、枠で場所を示す
					if ( !bShownModel ) RecordWire( Draw, Shape, Object.bEnabled ? FColor32( 230, 70, 220, 255 ) : FColor32( 90, 90, 100, 255 ) );
					break;
				case EEditorShapeRole::Billboard:
					if ( !bShownBoard ) RecordWire( Draw, Shape, Object.bEnabled ? FColor32( 230, 70, 220, 255 ) : FColor32( 90, 90, 100, 255 ) );
					break;
				case EEditorShapeRole::Collider:
				{
					const bool bDraw = S.ColliderView == FState::EColliderView::All || ( S.ColliderView == FState::EColliderView::Selected && bSelected );
					if ( !bDraw ) break;
					const FColor32 Color = GetColliderColor( NWorldEditor::ClassifyColliderDisplay( World.Settings, Object ) );
					// 編集メッシュを使う当たり判定は、面の縁を描く
					if ( NWorld::UsesEditMeshCollider( Object ) )
					{
						TArray<FVector3> Lines;
						const int32 NumFaces = NMath::Min( Object.Mesh.GetValue().GetNumFaces(), MeshTrianglesLeft );
						if ( !NWorldEditor::AppendEditMeshWireLines( Object.Mesh.GetValue(), Placed, NumFaces, Lines ) ) S.bMeshWireTruncated = true;
						MeshTrianglesLeft -= NumFaces;
						for ( int32 i = 0; i + 1 < Lines.Num(); i += 2 ) Draw.Line3D( Lines[i], Lines[i + 1], Color );
						break;
					}
					// メッシュの当たり判定は、三角形そのものを描く（出せなければ、モデルの大きさの箱）
					if ( Object.Collider.IsSet() && Object.Collider.GetValue().Shape == EWorldColliderShape::Mesh && Info != nullptr )
					{
						if ( const FEditorCollisionMesh* Mesh = Info->GetCollisionMesh( NWorld::GetColliderMeshPath( Object ) ) )
						{
							TArray<FVector3> Lines;
							const int32 NumTriangles = NMath::Min( Mesh->GetNumTriangles(), MeshTrianglesLeft );
							if ( !NWorldEditor::AppendMeshWireLines( *Mesh, Placed, NumTriangles, Lines ) ) S.bMeshWireTruncated = true;
							MeshTrianglesLeft -= NumTriangles;
							for ( int32 i = 0; i + 1 < Lines.Num(); i += 2 ) Draw.Line3D( Lines[i], Lines[i + 1], Color );
							break;
						}
					}
					RecordWire( Draw, Shape, Color );
					break;
				}
				case EEditorShapeRole::Sound:
				case EEditorShapeRole::Effect:
				case EEditorShapeRole::Marker:
				{
					// 向きがわかる、3 本の軸の十字
					const float32 R = Shape.Radius;
					const FQuaternion& Rotation = Placed.Rotation;
					Draw.Line3D( Placed.Position - Rotation.Rotate( FVector3::Right ) * R, Placed.Position + Rotation.Rotate( FVector3::Right ) * R, ToColor32( NWorldEditorUI::ColorAxisX ) );
					Draw.Line3D( Placed.Position - Rotation.Rotate( FVector3::Up ) * R, Placed.Position + Rotation.Rotate( FVector3::Up ) * R, ToColor32( NWorldEditorUI::ColorAxisY ) );
					Draw.Line3D( Placed.Position - Rotation.Rotate( FVector3::Forward ) * R, Placed.Position + Rotation.Rotate( FVector3::Forward ) * R, ToColor32( NWorldEditorUI::ColorAxisZ ) );
					break;
				}
				}
			}

			// 選んだ音の、聞こえる範囲
			if ( bSelected && Object.Sound.IsSet() )
			{
				FEditorShape Range;
				Range.Kind = EEditorShapeKind::Sphere;
				Range.Center = Placed.Position;
				Range.Radius = Object.Sound.GetValue().MinDistance;
				RecordWire( Draw, Range, FColor32( 255, 220, 90, 255 ) );
				Range.Radius = Object.Sound.GetValue().MaxDistance;
				RecordWire( Draw, Range, FColor32( 200, 140, 60, 255 ) );
			}
		}
		NWorldEditorUI::PruneMeshViews( S, SeenMeshes );
	}

	void RenderScene( FState& S )
	{
		IDrawExecutor* Executor = GetAppSubsystem<IDrawExecutor>();
		if ( Executor == nullptr || S.ViewScreen < 0 ) return;
		const FEditorCamera& Camera = S.Session.Camera;

		S.View.Near = NMath::Max( Camera.Distance * 0.01f, 0.01f );
		S.View.Far = NMath::Max( Camera.Distance * 300.0f, 500.0f );

		const float32 OldNear = GetCameraNear();
		const float32 OldFar = GetCameraFar();
		int R = 0, G = 0, B = 0;
		GetBackgroundColor( &R, &G, &B );

		// 光は、カメラの向きから当てる（どの面も見える）。ほかの描画に影響しないよう、あとで元に戻す
		const VECTOR OldLightDirection = GetLightDirection();
		const COLOR_F OldLightAmbient = GetLightAmbColor();
		SetLightDirection( Camera.GetForward() );
		SetLightAmbColor( GetColorF( 0.4f, 0.4f, 0.45f, 1.0f ) );

		SetDrawScreen( S.ViewScreen );
		SetBackgroundColor( 38, 40, 48 );
		ClearDrawScreen();
		SetBackgroundColor( R, G, B );
		SetUseZBuffer3D( TRUE );
		SetWriteZBuffer3D( TRUE );
		SetCameraNearFar( S.View.Near, S.View.Far );
		SetupCamera_Perspective( S.View.FovY );
		SetCameraPositionAndTargetAndUpVec( Camera.GetEye(), Camera.Target, FVector3::Up );
		// 材質meshの表示時間を現在frameへ反映してからscene commandを記録する。
		NWorldEditorUI::UpdateMeshMaterialPreviews( S, ImGui::GetIO().DeltaTime );
		NWorldEditorUI::UpdateModelMaterialPreviews( S, ImGui::GetIO().DeltaTime, Executor );

		FDrawBuffer Buffer;
		FDrawContext Draw( Buffer );
		NWorldEditorUI::RecordWorldScene( S, Draw );
		Executor->Execute( FDrawSpan { Buffer.Words.data(), static_cast<int32>( Buffer.Words.size() ) } );

		// エフェクトは、深度を書かない 2 回目で（半透明・加算の粒子が、後ろの粒子を隠さないように）。深度の比較は残るので、モデルの後ろでは隠れる
		NWorldEditorUI::UpdateEffectPreviews( S, ImGui::GetIO().DeltaTime );
		FDrawBuffer EffectBuffer;
		FDrawContext EffectDraw( EffectBuffer );
		NWorldEditorUI::RecordEffectPreviews( S, EffectDraw );
		if ( !EffectBuffer.Words.empty() )
		{
			SetWriteZBuffer3D( FALSE );
			Executor->Execute( FDrawSpan { EffectBuffer.Words.data(), static_cast<int32>( EffectBuffer.Words.size() ) } );
		}

		SetUseZBuffer3D( FALSE );
		SetWriteZBuffer3D( FALSE );
		SetDrawScreen( DX_SCREEN_BACK );
		SetCameraNearFar( OldNear, OldFar );
		SetLightDirection( OldLightDirection );
		SetLightAmbColor( OldLightAmbient );
	}

	//=========================================================================
	// 重ね描き
	//=========================================================================
	struct FOverlay
	{
		FState& S;
		ImDrawList* DrawList;
		ImVec2 Origin;

		bool Project( const FVector3& P, ImVec2& Out ) const
		{
			FVector2 Pixel;
			if ( !S.Session.Camera.WorldToScreen( S.View, P, Pixel ) ) return false;
			Out = ImVec2( Origin.x + Pixel.x, Origin.y + Pixel.y );
			return true;
		}

		void Line( const FVector3& A, const FVector3& B, ImU32 Color, float32 Thickness ) const
		{
			FVector2 PA, PB;
			if ( !S.Session.Camera.ProjectSegment( S.View, A, B, PA, PB ) ) return;
			DrawList->AddLine( ImVec2( Origin.x + PA.x, Origin.y + PA.y ), ImVec2( Origin.x + PB.x, Origin.y + PB.y ), Color, Thickness );
		}

		void Wire( const FEditorShape& Shape, ImU32 Color, float32 Thickness ) const
		{
			TArray<FVector3> Lines;
			NWorldEditor::AppendShapeWireLines( Shape, 24, Lines );
			for ( int32 i = 0; i + 1 < Lines.Num(); i += 2 ) Line( Lines[i], Lines[i + 1], Color, Thickness );
		}

		void Label( const FVector3& P, const FString& Text, ImU32 Color ) const
		{
			ImVec2 Position;
			if ( !Project( P, Position ) ) return;
			DrawList->AddText( ImVec2( Position.x + 8.0f, Position.y - 18.0f ), IM_COL32( 0, 0, 0, 200 ), UI_TEXT( Text ) );
			DrawList->AddText( ImVec2( Position.x + 7.0f, Position.y - 19.0f ), Color, UI_TEXT( Text ) );
		}
	};

	bool IsActiveOrHover( const FState& S, EEditorGizmoHandle Handle )
	{
		const CWorldEditorSession& Session = S.Session;
		return Session.IsGizmoDragging() ? Session.GetGizmoDrag().GetHandle() == Handle : S.HoverHandle == Handle;
	}

	void DrawArrowHead( const FOverlay& O, const ImVec2& From, const ImVec2& To, ImU32 Color )
	{
		const ImVec2 Delta( To.x - From.x, To.y - From.y );
		const float32 Length = NMath::Sqrt( Delta.x * Delta.x + Delta.y * Delta.y );
		if ( Length < 4.0f ) return;
		const ImVec2 Direction( Delta.x / Length, Delta.y / Length );
		const ImVec2 Perpendicular( -Direction.y, Direction.x );
		const ImVec2 Tip( To.x + Direction.x * 12.0f, To.y + Direction.y * 12.0f );
		O.DrawList->AddTriangleFilled( Tip, ImVec2( To.x + Perpendicular.x * 5.5f, To.y + Perpendicular.y * 5.5f ), ImVec2( To.x - Perpendicular.x * 5.5f, To.y - Perpendicular.y * 5.5f ), Color );
	}

	void DrawGizmo( const FOverlay& O )
	{
		FState& S = O.S;
		CWorldEditorSession& Session = S.Session;
		FEditorGizmoFrame Frame;
		if ( !S.bShowGizmo || !Session.GetGizmoFrame( S.View, Frame ) ) return;
		ImDrawList* DL = O.DrawList;

		ImVec2 Center;
		if ( !O.Project( Frame.Origin, Center ) ) return;

		if ( Session.GizmoMode == EEditorGizmoMode::Rotate )
		{
			const float32 Radius = NWorldEditor::GetGizmoRingRadius( Frame );
			const FVector3 ToCamera = Session.Camera.GetEye() - Frame.Origin;
			for ( int32 Axis = 0; Axis < 3; ++Axis )
			{
				const bool bHighlight = IsActiveOrHover( S, GetGizmoAxisHandle( Axis ) );
				const ImU32 Color = bHighlight ? NWorldEditorUI::ColorHighlight : AxisColor( Axis );
				const FVector3 U = Frame.Axes[( Axis + 1 ) % 3], V = Frame.Axes[( Axis + 2 ) % 3];
				constexpr int32 Segments = 64;
				FVector3 Previous = Frame.Origin + U * Radius;
				for ( int32 i = 1; i <= Segments; ++i )
				{
					const float32 Angle = NMath::TWO_PI * static_cast<float32>( i ) / static_cast<float32>( Segments );
					const FVector3 Point = Frame.Origin + ( U * NMath::Cos( Angle ) + V * NMath::Sin( Angle ) ) * Radius;
					const bool bFront = FVector3::Dot( Point - Frame.Origin, ToCamera ) >= 0.0f;
					O.Line( Previous, Point, bFront ? Color : WithAlpha( Color, 70 ), bFront ? ( bHighlight ? 4.0f : 3.0f ) : 1.0f );
					Previous = Point;
				}
			}
			DL->AddCircleFilled( Center, 3.0f, IM_COL32( 230, 230, 230, 255 ) );
			return;
		}

		const bool bScale = Session.GizmoMode == EEditorGizmoMode::Scale;
		if ( !bScale )
		{
			// 面の取っ手
			for ( int32 Normal = 0; Normal < 3; ++Normal )
			{
				FVector3 Corners[4];
				NWorldEditor::GetGizmoPlaneCorners( Frame, Normal, Corners );
				ImVec2 Points[4];
				bool bAll = true;
				for ( int32 i = 0; i < 4; ++i ) bAll = bAll && O.Project( Corners[i], Points[i] );
				if ( !bAll ) continue;
				const bool bHighlight = IsActiveOrHover( S, GetGizmoPlaneHandle( Normal ) );
				const ImU32 Color = bHighlight ? NWorldEditorUI::ColorHighlight : AxisColor( Normal );
				DL->AddConvexPolyFilled( Points, 4, WithAlpha( Color, bHighlight ? 120 : 70 ) );
				DL->AddPolyline( Points, 4, WithAlpha( Color, 220 ), ImDrawFlags_Closed, 1.5f );
			}
		}
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			FVector3 Start, End;
			NWorldEditor::GetGizmoAxisSegment( Frame, Axis, Start, End );
			ImVec2 PA, PB;
			if ( !O.Project( Start, PA ) || !O.Project( End, PB ) ) continue;
			const bool bHighlight = IsActiveOrHover( S, GetGizmoAxisHandle( Axis ) );
			const ImU32 Color = bHighlight ? NWorldEditorUI::ColorHighlight : AxisColor( Axis );
			DL->AddLine( PA, PB, Color, bHighlight ? 4.0f : 3.0f );
			if ( bScale ) DL->AddRectFilled( ImVec2( PB.x - 5.0f, PB.y - 5.0f ), ImVec2( PB.x + 5.0f, PB.y + 5.0f ), Color );
			else DrawArrowHead( O, PA, PB, Color );
		}
		const bool bFree = IsActiveOrHover( S, EEditorGizmoHandle::Free );
		const ImU32 CenterColor = bFree ? NWorldEditorUI::ColorHighlight : IM_COL32( 235, 235, 235, 255 );
		if ( bScale ) DL->AddRectFilled( ImVec2( Center.x - 6.0f, Center.y - 6.0f ), ImVec2( Center.x + 6.0f, Center.y + 6.0f ), CenterColor );
		else DL->AddCircleFilled( Center, 5.5f, CenterColor );
	}

	void DrawDragReadout( const FOverlay& O )
	{
		CWorldEditorSession& Session = O.S.Session;
		if ( !Session.IsGizmoDragging() ) return;
		const FEditorGizmoDelta& Delta = Session.GetGizmoDelta();
		FString Text;
		switch ( Session.GizmoMode )
		{
		case EEditorGizmoMode::Translate: Text = FString::Printf( "move  %+.2f  %+.2f  %+.2f", Delta.Translation.x, Delta.Translation.y, Delta.Translation.z ); break;
		case EEditorGizmoMode::Rotate: Text = FString::Printf( "rotate  %+.1f deg", Delta.AngleRadians * NMath::RAD2DEG ); break;
		case EEditorGizmoMode::Scale: Text = Delta.Scale.x == Delta.Scale.y && Delta.Scale.y == Delta.Scale.z ? FString::Printf( "scale  x%.2f", Delta.Scale.x ) : FString::Printf( "scale  x%.2f  x%.2f  x%.2f", Delta.Scale.x, Delta.Scale.y, Delta.Scale.z ); break;
		}
		const ImVec2 Mouse = ImGui::GetIO().MousePos;
		const ImVec2 Position( Mouse.x + 16.0f, Mouse.y + 18.0f );
		const ImVec2 Size = ImGui::CalcTextSize( UI_TEXT( Text ) );
		O.DrawList->AddRectFilled( ImVec2( Position.x - 4.0f, Position.y - 2.0f ), ImVec2( Position.x + Size.x + 4.0f, Position.y + Size.y + 2.0f ), IM_COL32( 0, 0, 0, 190 ), 3.0f );
		O.DrawList->AddText( Position, NWorldEditorUI::ColorHighlight, UI_TEXT( Text ) );
	}

	void DrawAxesWidget( const FOverlay& O, const ImVec2& Size )
	{
		const FEditorCamera& Camera = O.S.Session.Camera;
		const ImVec2 Base( O.Origin.x + 46.0f, O.Origin.y + Size.y - 46.0f );
		const char* Names[3] = { "X", "Y", "Z" };
		const FVector3 Axes[3] = { FVector3::Right, FVector3::Up, FVector3::Forward };
		for ( int32 Axis = 0; Axis < 3; ++Axis )
		{
			const ImVec2 Tip( Base.x + FVector3::Dot( Axes[Axis], Camera.GetRight() ) * 30.0f, Base.y - FVector3::Dot( Axes[Axis], Camera.GetUp() ) * 30.0f );
			O.DrawList->AddLine( Base, Tip, AxisColor( Axis ), 2.0f );
			O.DrawList->AddText( ImVec2( Tip.x + 3.0f, Tip.y - 7.0f ), AxisColor( Axis ), Names[Axis] );
		}
	}

	void DrawOverlay( FState& S, ImDrawList* DrawList, const ImVec2& Origin, const ImVec2& Size )
	{
		const FOverlay O { S, DrawList, Origin };
		const FWorldDefinition& World = S.Session.Document.Get();
		DrawList->PushClipRect( Origin, ImVec2( Origin.x + Size.x, Origin.y + Size.y ), true );

		// 選択の枠（主な選択は明るく）・名前
		for ( int32 Index = 0; Index < World.Objects.Num() && Index < S.FrameShapes.Num(); ++Index )
		{
			const FWorldObject& Object = World.Objects[Index];
			const bool bSelected = S.Session.IsSelected( Object.Id );
			const bool bHover = Object.Id == S.HoverObjectId && !bSelected && S.MouseMode == EMouseMode::None;
			const TArray<FEditorShape>& Shapes = S.FrameShapes[Index];
			if ( bSelected )
			{
				const bool bPrimary = Object.Id == S.Session.GetPrimary();
				for ( const FEditorShape& Shape : Shapes )
				{
					if ( Shape.Role == EEditorShapeRole::Marker || Shape.Role == EEditorShapeRole::Sound || Shape.Role == EEditorShapeRole::Effect ) O.Wire( Shape, bPrimary ? NWorldEditorUI::ColorPrimary : NWorldEditorUI::ColorSelected, 2.0f );
					else O.Wire( Shape, bPrimary ? NWorldEditorUI::ColorPrimary : NWorldEditorUI::ColorSelected, 2.0f );
				}
			}
			else if ( bHover )
			{
				for ( const FEditorShape& Shape : Shapes ) O.Wire( Shape, IM_COL32( 255, 255, 255, 170 ), 1.5f );
			}

			const bool bMarker = !Shapes.IsEmpty() && ( Shapes[0].Role == EEditorShapeRole::Marker || Shapes[0].Role == EEditorShapeRole::Sound || Shapes[0].Role == EEditorShapeRole::Effect );
			if ( !Shapes.IsEmpty() && ( bSelected || bHover || ( S.bShowNames && bMarker ) ) )
			{
				const ImU32 Color = bSelected ? NWorldEditorUI::ColorPrimary : bHover ? IM_COL32( 255, 255, 255, 255 ) : IM_COL32( 205, 210, 225, 235 );
				O.Label( NWorldEditor::GetShapeCenter( Shapes[0] ), Object.Name.IsEmpty() ? FString::Printf( "#%d", Object.Id ) : Object.Name, Color );
			}
		}

		DrawGizmo( O );
		if ( S.Session.IsMeshEditing() ) NWorldEditorUI::DrawMeshOverlay( S, DrawList, Origin );

		// アセットを置く場所
		if ( S.bHasDropPreview )
		{
			ImVec2 Position;
			if ( O.Project( S.DropPreviewPosition, Position ) )
			{
				DrawList->AddCircle( Position, 11.0f, NWorldEditorUI::ColorHighlight, 24, 2.0f );
				DrawList->AddLine( ImVec2( Position.x - 15.0f, Position.y ), ImVec2( Position.x + 15.0f, Position.y ), NWorldEditorUI::ColorHighlight, 1.5f );
				DrawList->AddLine( ImVec2( Position.x, Position.y - 15.0f ), ImVec2( Position.x, Position.y + 15.0f ), NWorldEditorUI::ColorHighlight, 1.5f );
				DrawList->AddText( ImVec2( Position.x + 16.0f, Position.y + 8.0f ), NWorldEditorUI::ColorHighlight, UI_TEXT( S.DropPreviewName ) );
			}
		}

		// 矩形選択
		if ( S.MouseMode == EMouseMode::Marquee )
		{
			const ImVec2 Mouse = ImGui::GetIO().MousePos;
			const ImVec2 A( Origin.x + S.PressPosition.x, Origin.y + S.PressPosition.y );
			DrawList->AddRectFilled( A, Mouse, IM_COL32( 255, 200, 80, 40 ) );
			DrawList->AddRect( A, Mouse, IM_COL32( 255, 200, 80, 220 ) );
		}

		DrawDragReadout( O );
		DrawAxesWidget( O, Size );
		DrawList->PopClipRect();

		// 左上の情報
		const FString Info = FString::Printf( "Distance %.1f   Grid %.3g", S.Session.Camera.Distance, NMath::Pow( 10.0f, NMath::Floor( NMath::Log10( NMath::Max( S.Session.Camera.Distance * 0.25f, 0.01f ) ) ) ) );
		DrawList->AddText( ImVec2( Origin.x + 8.0f, Origin.y + 6.0f ), IM_COL32( 200, 205, 220, 200 ), UI_TEXT( Info ) );

		// 当たり判定の凡例（色の意味）
		if ( S.ColliderView != FState::EColliderView::Off )
		{
			float32 Y = Origin.y + 24.0f;
			for ( const EEditorColliderDisplay Display : { EEditorColliderDisplay::Solid, EEditorColliderDisplay::SolidForced, EEditorColliderDisplay::Trigger, EEditorColliderDisplay::Off, EEditorColliderDisplay::OffForced } )
			{
				const FColor32 Color = GetColliderColor( Display );
				DrawList->AddRectFilled( ImVec2( Origin.x + 8.0f, Y + 3.0f ), ImVec2( Origin.x + 18.0f, Y + 11.0f ), IM_COL32( Color.r, Color.g, Color.b, 255 ) );
				DrawList->AddText( ImVec2( Origin.x + 24.0f, Y ), IM_COL32( 200, 205, 220, 190 ), NWorldEditor::GetColliderDisplayLabel( Display ) );
				Y += 14.0f;
			}
			if ( S.bMeshWireTruncated ) DrawList->AddText( ImVec2( Origin.x + 8.0f, Y ), IM_COL32( 255, 190, 90, 230 ), "mesh outlines: too many triangles, only part is drawn" );
		}
	}

	//=========================================================================
	// 入力
	//=========================================================================
	void UpdateHover( FState& S, const FEditorRay& Ray, float32 MouseX, float32 MouseY )
	{
		CWorldEditorSession& Session = S.Session;
		S.HoverHandle = EEditorGizmoHandle::None;
		S.HoverObjectId = WorldNoId;
		S.HoverMeshHit = FEditMeshHit();
		FEditorGizmoFrame Frame;
		if ( S.bShowGizmo && Session.GetGizmoFrame( S.View, Frame ) ) S.HoverHandle = NWorldEditor::HitTestGizmo( Frame, Session.GizmoMode, Ray );
		if ( S.HoverHandle != EEditorGizmoHandle::None ) return;
		// 編集メッシュの編集中は、オブジェクトでなく、メッシュの要素を指す
		if ( Session.IsMeshEditing() )
		{
			if ( const FEditMesh* Mesh = Session.GetEditedMesh() ) S.HoverMeshHit = NWorldEditor::PickMeshElement( *Mesh, Session.GetEditedWorldTransform(), Session.Camera, S.View, MouseX, MouseY, Session.GetMeshElement() );
			return;
		}
		const TArray<FEditorHit> Hits = NWorldEditor::PickObjects( Session.Document.Get(), Ray, NWorldEditorUI::MakeShapeContext( S ) );
		if ( !Hits.IsEmpty() ) S.HoverObjectId = Hits[0].ObjectId;
	}

	CWorldEditorSession::ESelectMode GetSelectMode( const FWorldEditorInput& Input )
	{
		if ( Input.bCtrl ) return CWorldEditorSession::ESelectMode::Toggle;
		if ( Input.bShift ) return CWorldEditorSession::ESelectMode::Add;
		return CWorldEditorSession::ESelectMode::Replace;
	}

	// ImGui の入力から、写しを作る
	FWorldEditorInput CaptureInput( const FState& S, const ImVec2& Origin, bool bHovered )
	{
		if ( S.bUseInputOverride ) return S.InputOverride;
		const ImGuiIO& IO = ImGui::GetIO();
		FWorldEditorInput Input;
		Input.MouseX = IO.MousePos.x - Origin.x;
		Input.MouseY = IO.MousePos.y - Origin.y;
		Input.MouseDeltaX = IO.MouseDelta.x;
		Input.MouseDeltaY = IO.MouseDelta.y;
		Input.Wheel = IO.MouseWheel;
		Input.bLeftDown = ImGui::IsMouseDown( ImGuiMouseButton_Left );
		Input.bMiddleDown = ImGui::IsMouseDown( ImGuiMouseButton_Middle );
		Input.bRightDown = ImGui::IsMouseDown( ImGuiMouseButton_Right );
		Input.bLeftClicked = ImGui::IsMouseClicked( ImGuiMouseButton_Left );
		Input.bMiddleClicked = ImGui::IsMouseClicked( ImGuiMouseButton_Middle );
		Input.bRightClicked = ImGui::IsMouseClicked( ImGuiMouseButton_Right );
		Input.bCtrl = IO.KeyCtrl;
		Input.bShift = IO.KeyShift;
		Input.bAlt = IO.KeyAlt;
		Input.bEscapePressed = ImGui::IsKeyPressed( ImGuiKey_Escape, false );
		const auto Key = [&]( ImGuiKey K ) { return ImGui::IsKeyDown( K ) ? 1.0f : 0.0f; };
		Input.Fly = FVector3( Key( ImGuiKey_D ) - Key( ImGuiKey_A ), Key( ImGuiKey_E ) - Key( ImGuiKey_Q ), Key( ImGuiKey_W ) - Key( ImGuiKey_S ) );
		Input.DeltaTime = IO.DeltaTime;
		Input.bHovered = bHovered;
		return Input;
	}

	void HandleDrop( FState& S, const ImVec2& Origin )
	{
		S.bHasDropPreview = false;
		if ( !ImGui::BeginDragDropTarget() ) return;
		const ImGuiPayload* Payload = ImGui::AcceptDragDropPayload( NWorldEditorUI::AssetPayloadType, ImGuiDragDropFlags_AcceptBeforeDelivery | ImGuiDragDropFlags_AcceptNoDrawDefaultRect );
		if ( Payload != nullptr && Payload->Data != nullptr )
		{
			const FString Path = FString( static_cast<const char*>( Payload->Data ) );
			const ImVec2 Mouse = ImGui::GetIO().MousePos;
			const FEditorRay Ray = S.Session.Camera.ScreenToRay( S.View, Mouse.x - Origin.x, Mouse.y - Origin.y );
			FEditorPlacementOptions Options;
			if ( S.Session.bSnap ) Options.SnapStep = S.Session.MoveSnap;
			const FEditorPlacement Placement = NWorldEditor::ComputePlacement( Ray, &S.Session.Document.Get(), NWorldEditorUI::MakeShapeContext( S ), Options );
			S.bHasDropPreview = true;
			S.DropPreviewPosition = Placement.Position;
			S.DropPreviewName = NWorldEditor::GetAssetDisplayName( Path );
			if ( Payload->IsDelivery() )
			{
				if ( S.Session.CreateAsset( Path, Placement.Position, S.AssetInfo.Get() ) == WorldNoId ) NWorldEditorUI::SetStatus( S, "Cannot place '" + Path + "': not a model, image or sound file", true );
				S.bHasDropPreview = false;
			}
		}
		ImGui::EndDragDropTarget();
	}
}

void NWorldEditorUI::RecordWorldScene( FWorldEditorState& S, FDrawContext& Draw )
{
	RecordScene( Draw, S );
}

void NWorldEditorUI::DrawViewport( FWorldEditorState& S )
{
	const ImVec2 Origin = ImGui::GetCursorScreenPos();
	ImVec2 Size = ImGui::GetContentRegionAvail();
	Size.y -= ImGui::GetFrameHeightWithSpacing() + 4.0f;   // 下の状態表示のぶん
	if ( Size.x < 64.0f || Size.y < 64.0f )
	{
		S.ViewOrigin = Origin;
		S.ViewSize = ImVec2( 0.0f, 0.0f );
		return;
	}
	Size.x = NMath::Floor( Size.x );
	Size.y = NMath::Floor( Size.y );
	S.ViewOrigin = Origin;
	S.ViewSize = Size;
	S.View.Width = static_cast<int32>( Size.x );
	S.View.Height = static_cast<int32>( Size.y );

	EnsureScreen( S, S.View.Width, S.View.Height );
	RenderScene( S );

	ImDrawList* DrawList = ImGui::GetWindowDrawList();
	if ( S.ViewScreen >= 0 ) DrawList->AddImage( NDebugUI::GetImGuiTexture( S.ViewScreen ), Origin, ImVec2( Origin.x + static_cast<float32>( S.ViewWidth ), Origin.y + static_cast<float32>( S.ViewHeight ) ) );

	ImGui::InvisibleButton( "##WorldView", Size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle | ImGuiButtonFlags_MouseButtonRight );
	const bool bHovered = ImGui::IsItemHovered();
	HandleDrop( S, Origin );
	if ( bHovered && ImGui::IsWindowFocused( ImGuiFocusedFlags_RootAndChildWindows ) ) S.bAnyPanelFocused = true;
	NDebugUI::ProcessWorldEditorInput( S, CaptureInput( S, Origin, bHovered ) );
	DrawOverlay( S, DrawList, Origin, Size );
}

void NWorldEditorUI::ReleaseViewport( FWorldEditorState& S )
{
	if ( S.ViewScreen >= 0 )
	{
		DeleteGraph( S.ViewScreen );
		S.ViewScreen = -1;
	}
	S.ViewWidth = 0;
	S.ViewHeight = 0;
}

void NDebugUI::ProcessWorldEditorInput( FWorldEditorState& S, const FWorldEditorInput& Input )
{
	using EMouseMode = FState::EMouseMode;
	CWorldEditorSession& Session = S.Session;
	FEditorCamera& Camera = Session.Camera;
	const FEditorRay Ray = Camera.ScreenToRay( S.View, Input.MouseX, Input.MouseY );
	S.bViewHovered = Input.bHovered;

	if ( Input.bHovered && Input.Wheel != 0.0f && S.MouseMode == EMouseMode::None ) Camera.Zoom( Input.Wheel );

	switch ( S.MouseMode )
	{
	case EMouseMode::None:
	{
		if ( !Input.bHovered )
		{
			S.HoverHandle = EEditorGizmoHandle::None;
			S.HoverObjectId = WorldNoId;
			break;
		}
		UpdateHover( S, Ray, Input.MouseX, Input.MouseY );
		if ( Input.bRightClicked ) S.MouseMode = EMouseMode::Look;
		else if ( Input.bMiddleClicked ) S.MouseMode = EMouseMode::Pan;
		else if ( Input.bLeftClicked )
		{
			if ( Input.bAlt ) S.MouseMode = EMouseMode::Orbit;
			else if ( S.HoverHandle != EEditorGizmoHandle::None && Session.BeginGizmoDrag( S.View, S.HoverHandle, Ray ) ) S.MouseMode = EMouseMode::Gizmo;
			else
			{
				S.MouseMode = EMouseMode::Pending;
				S.PressPosition = ImVec2( Input.MouseX, Input.MouseY );
			}
		}
		break;
	}
	case EMouseMode::Orbit:
		if ( Input.bLeftDown ) Camera.Orbit( Input.MouseDeltaX, Input.MouseDeltaY );
		else S.MouseMode = EMouseMode::None;
		break;
	case EMouseMode::Pan:
		if ( Input.bMiddleDown ) Camera.Pan( S.View, Input.MouseDeltaX, Input.MouseDeltaY );
		else S.MouseMode = EMouseMode::None;
		break;
	case EMouseMode::Look:
		if ( Input.bRightDown )
		{
			Camera.LookAround( Input.MouseDeltaX, Input.MouseDeltaY );
			// 飛ぶ: 右・上・前。Shift で速く
			Camera.MoveLocal( Input.Fly * ( Camera.Distance * 0.8f * ( Input.bShift ? 3.0f : 1.0f ) * Input.DeltaTime ) );
		}
		else S.MouseMode = EMouseMode::None;
		break;
	case EMouseMode::Gizmo:
		if ( !Session.IsGizmoDragging() )
		{
			S.MouseMode = EMouseMode::None;   // 外から取り消された（Esc のショートカットなど）
		}
		else if ( Input.bEscapePressed || Input.bRightClicked )
		{
			Session.CancelGizmoDrag();
			S.MouseMode = EMouseMode::None;
		}
		else if ( Input.bLeftDown ) Session.UpdateGizmoDrag( Ray, Input.bCtrl );
		else
		{
			Session.EndGizmoDrag();
			S.MouseMode = EMouseMode::None;
		}
		break;
	case EMouseMode::Pending:
	{
		const float32 Moved = NMath::Max( NMath::Abs( Input.MouseX - S.PressPosition.x ), NMath::Abs( Input.MouseY - S.PressPosition.y ) );
		if ( !Input.bLeftDown )
		{
			// クリック: 同じものを続けて押すと、重なった次のもの。編集メッシュの編集中は、メッシュの要素を選ぶ
			const CWorldEditorSession::ESelectMode Mode = GetSelectMode( Input );
			if ( Session.IsMeshEditing() ) Session.MeshClickSelect( S.View, S.PressPosition.x, S.PressPosition.y, Mode );
			else
			{
				const FEditorRay PressRay = Camera.ScreenToRay( S.View, S.PressPosition.x, S.PressPosition.y );
				Session.ClickSelect( PressRay, NWorldEditorUI::MakeShapeContext( S ), Mode, Mode == CWorldEditorSession::ESelectMode::Replace );
			}
			S.MouseMode = EMouseMode::None;
		}
		else if ( Moved > DragThreshold ) S.MouseMode = EMouseMode::Marquee;
		break;
	}
	case EMouseMode::Marquee:
		if ( !Input.bLeftDown )
		{
			if ( Session.IsMeshEditing() ) Session.MeshRectSelect( S.View, S.PressPosition.x, S.PressPosition.y, Input.MouseX, Input.MouseY, GetSelectMode( Input ), !Input.bAlt );
			else Session.RectSelect( NWorldEditorUI::MakeShapeContext( S ), S.PressPosition.x, S.PressPosition.y, Input.MouseX, Input.MouseY, GetSelectMode( Input ) );
			S.MouseMode = EMouseMode::None;
		}
		break;
	}
}

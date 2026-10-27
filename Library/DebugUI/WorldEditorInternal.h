// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITORINTERNAL_H
#define FRAMEWORK_DEBUGUI_WORLDEDITORINTERNAL_H
#include "Library/DebugUI/WorldEditor.h"

class CAssetManager;

//=============================================================================
// ワールドエディタの画面の中で、ファイルをまたいで使うもの（WorldEditor*.cpp 専用）
//=============================================================================
namespace NWorldEditorUI
{
	// アセットを視界・アウトライナーへドラッグするときの、ImGui の Payload の種類
	constexpr const char* AssetPayloadType = "WORLD_ASSET";      // 中身: アセットのパス（'\0' で終わる文字列）
	constexpr const char* ObjectPayloadType = "WORLD_OBJECTS";   // 中身は使わない（選択中のオブジェクトを動かす）

	CAssetManager* GetAssets();

	// 次の窓の最初の位置と大きさを、アプリの画面に対する割合で決める（FirstUseEver。ユーザーが動かした位置は、ImGui が覚える）
	void SetNextWindowRect( float32 X, float32 Y, float32 Width, float32 Height );
	void SetStatus( FWorldEditorState& S, const FString& Text, bool bError = false );

	// アセットのルートの下のファイルを数え直す（3 秒に 1 回まで。bForce なら必ず）
	void RescanFiles( FWorldEditorState& S, bool bForce );

	// 検証の問題の一覧を、文書が変わっていれば作り直す
	void RefreshIssues( FWorldEditorState& S );

	// ピッキング・図形のための周りの情報（いまのカメラとビューポート）
	FEditorShapeContext MakeShapeContext( FWorldEditorState& S );

	// 視界の中央の先にある、置く場所（地面か、すでにあるものの表面）。メニューから作るときの場所
	FVector3 GetViewCenterPlacement( FWorldEditorState& S );

	// ワールド全体が収まるようにカメラを動かす（空なら、既定の位置）
	void FrameAll( FWorldEditorState& S );

	// 区切りと名前つきの色
	constexpr ImU32 ColorAxisX = IM_COL32( 235, 80, 80, 255 );
	constexpr ImU32 ColorAxisY = IM_COL32( 90, 215, 90, 255 );
	constexpr ImU32 ColorAxisZ = IM_COL32( 90, 130, 255, 255 );
	constexpr ImU32 ColorHighlight = IM_COL32( 255, 215, 60, 255 );
	constexpr ImU32 ColorSelected = IM_COL32( 255, 150, 40, 255 );
	constexpr ImU32 ColorPrimary = IM_COL32( 255, 205, 90, 255 );

	// 各窓
	void DrawViewport( FWorldEditorState& S );
	void ReleaseViewport( FWorldEditorState& S );
	void DrawOutliner( FWorldEditorState& S );
	void DrawInspector( FWorldEditorState& S );
	void DrawAssetBrowser( FWorldEditorState& S );
	void DrawIssuesPanel( FWorldEditorState& S );

	// メニューから作る・編集するときの、共通の操作
	void CreateEmptyAtView( FWorldEditorState& S );
	void CreateColliderAtView( FWorldEditorState& S, EWorldColliderShape Shape, bool bTrigger );
	void CreateAssetAtView( FWorldEditorState& S, const FString& AssetPath );

	// 入れ子のワールド（Prefab）
	// 別のワールドを開く（未保存の変更があれば、確認してから）
	void RequestOpenWorld( FWorldEditorState& S, const FString& Path );
	// 選んだオブジェクト（根と子孫）を、別の .world（Path）に保存して、その Prefab を置いたオブジェクト 1 つに置き換える。できたら true
	bool MakePrefabFromSelection( FWorldEditorState& S, const FString& Path );

	// エフェクトのプレビュー（WorldEditorEffects.cpp）
	// 部品を持つ有効なオブジェクトの再生を、作る・進める・やり直す・捨てる（bPlayEffects が false なら全部捨てる）
	void UpdateEffectPreviews( FWorldEditorState& S, float32 DeltaTime );
	// 再生中のものを記録する（粒子は深度を書かない 2 回目の描画で出すので、シーンとは別の FDrawContext に）
	void RecordEffectPreviews( FWorldEditorState& S, FDrawContext& Draw );

	// 編集メッシュ（WorldEditorMesh.cpp）
	void CreateMeshAtView( FWorldEditorState& S, EEditMeshPrimitive Kind );
	// Mesh 部品を持つ、主な選択のオブジェクトの、編集を始める・終わる
	void ToggleMeshEdit( FWorldEditorState& S );
	// メッシュを、実行時と同じ FMeshRenderer で描く（内容が変わったときだけ、作り直す）。描けたら true
	bool RecordMeshObject( FWorldEditorState& S, const FWorldObject& Object, const FTransform& Placed, FDrawContext& Draw, int32 CacheKey );
	// 記録前にmesh材質preview時計を一度だけ進める。
	void UpdateMeshMaterialPreviews( FWorldEditorState& S, float32 DeltaTime );
	// このフレームに見えたオブジェクトの Id 以外の、描く形を捨てる
	void PruneMeshViews( FWorldEditorState& S, const TSet<int32>& Seen );
	// 全配置の専用model instanceを更新・準備し、時計を記録前に進める。
	void UpdateModelMaterialPreviews( FWorldEditorState& S, float32 DeltaTime, const IDrawExecutor* Executor );
	// 対応する専用previewを記録し、native fallbackも含めて描画を扱えたか返す。
	bool RecordModelObject( const FWorldEditorState& S, const FWorldEditorModelPreviewKey& Key, const FTransform& Placed, FDrawContext& Draw );
	// このフレームに現れなかった個体preview ownerを解放する。
	void PruneModelPreviews( FWorldEditorState& S, const TSet<FWorldEditorModelPreviewKey>& Seen );
	// viewportとself-testが共有する、実sceneのcommand記録入口。
	void RecordWorldScene( FWorldEditorState& S, FDrawContext& Draw );
	// 編集中の頂点・辺・面の重ね描き（ImGui）。Origin はビューポートの左上
	void DrawMeshOverlay( FWorldEditorState& S, ImDrawList* DrawList, const ImVec2& Origin );
	// 編集中のツールバー（要素の種類・選ぶ・押し出し・差す・消す・つなぐ・割る・形を足す）
	void DrawMeshToolbar( FWorldEditorState& S );
	// Inspector の Mesh 部品の中身（大きさ・材質）
	void DrawMeshComponentBody( FWorldEditorState& S, const FWorldObject& Object );
}

#endif

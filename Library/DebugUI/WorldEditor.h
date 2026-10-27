// SPDX-License-Identifier: MIT
#ifndef FRAMEWORK_DEBUGUI_WORLDEDITOR_H
#define FRAMEWORK_DEBUGUI_WORLDEDITOR_H
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Asset/EditorAssetInfo.h"
#include "Library/Framework/Effect/EffectBackend.h"
#include "Library/Framework/Render/MeshRender.h"
#include "Library/DebugUI/WorldEditorModelPreview.h"
#include "Library/DebugUI/WorldEditorModelPreviewKey.h"
#include "Library/Framework/World/Editor/EditorSession.h"
#include "Library/Framework/World/WorldValidate.h"
#include "VendorModules/ImGui/imgui.h"

//=============================================================================
// ワールド（.world）のレベルエディタ
//   モデル・画像・音・当たり判定の形・空のオブジェクト（タグでコードの生成物を結び付ける場所）を、3D の視界に置いて、.world に保存する。
//
//   窓（ドッキングできる）:
//     World Editor    メニュー・ツールバー・3D の視界（ビューポート）
//     World Outliner  オブジェクトの木。ドラッグで親子の付け替え、絞り込み（tag:Enemy / comp:Model / #12 / 名前）
//     World Inspector 選んだオブジェクトの名前・位置・タグ・プロパティ・部品・当たり判定の決定の理由・検証の問題
//     World Assets    アセットの一覧。視界へドラッグして置く（ダブルクリックで視界の中央に）
//     World Issues    検証の問題の一覧（クリックで選ぶ）と、ワールドの設定（当たり判定のタグ規則）
//
//   視界の操作:
//     右ボタンを押しながら: 見回す + W A S D（前後左右）/ Q E（下上）/ Shift で速く    中ボタンのドラッグ: 平行に動かす
//     Alt + 左ドラッグ: 注視点のまわりを回る    ホイール: 近づく / 離れる    F: 選んだものに寄る
//     左クリック: 選ぶ（Ctrl で切り替え、Shift で追加。同じ場所を続けてクリックすると、重なった次のもの）    左ドラッグ（空いた所から）: 矩形で選ぶ
//     ギズモの取っ手をドラッグ: 動かす（Ctrl を押している間、スナップを逆にする。Esc / 右ボタンで取り消し）
//     Tab: 編集メッシュの編集の始め / 終わり（Mesh 部品を持つオブジェクトを選んで）。編集中は、1 / 2 / 3: 頂点 / 辺 / 面、クリック・矩形で要素を選び、
//          ギズモで動かす。ツールバー: 押し出し・差す・消す・裏返し・つなぐ・割る・溶接・形を足す    L: つながった面を選ぶ
//     W / E / R: 移動 / 回転 / 拡大    X: ワールド / ローカル    Delete: 消す    Ctrl+D: 複製    Ctrl+C / V: コピー / 貼り付け
//     Ctrl+G: グループにする（Ctrl+Shift+G で外す）    Ctrl+Z / Ctrl+Y: 元に戻す / やり直す    Ctrl+S: 保存    Ctrl+A: すべて選ぶ    Esc: 選択を外す
//
//   編集の中身は World/Editor（EditorSession.h など、画面なしでテストされる純粋なコード）と NWorldEdit。ここは画面だけ。
//=============================================================================
//-----------------------------------------------------------------------------
// ビューポートへの入力の写し（1 フレーム分）
//   ImGui の入力から作る（CaptureInput）。視界の操作は、この写しだけを見て決める（ProcessWorldEditorInput）ので、
//   ImGui なしで、「どう操作したらどうなるか」を確かめられる。
//-----------------------------------------------------------------------------
struct FWorldEditorInput
{
	float32 MouseX = 0.0f;           // ビューポートの左上からのピクセル
	float32 MouseY = 0.0f;
	float32 MouseDeltaX = 0.0f;      // 前のフレームからの動き
	float32 MouseDeltaY = 0.0f;
	float32 Wheel = 0.0f;
	bool bLeftDown = false;          // ボタンを押している
	bool bMiddleDown = false;
	bool bRightDown = false;
	bool bLeftClicked = false;       // このフレームで押した
	bool bMiddleClicked = false;
	bool bRightClicked = false;
	bool bCtrl = false;
	bool bShift = false;
	bool bAlt = false;
	bool bEscapePressed = false;
	FVector3 Fly = FVector3::Zero;   // 飛ぶ向き（右・上・前。各 -1..1。W A S D Q E）
	float32 DeltaTime = 1.0f / 60.0f;
	bool bHovered = true;            // マウスがビューポートの上にある（ほかの窓に隠れていない）
};

struct FWorldEditorState
{
	//-------------------------------------------------------------------------
	// 編集の状態（文書・選択・カメラ・ギズモ）
	//-------------------------------------------------------------------------
	CWorldEditorSession Session;
	TUniquePtr<FAssetEditorInfo> AssetInfo;   // アセットの大きさ・有無・読み込み済みのもの。CAssetManager が使えるとき作る

	//-------------------------------------------------------------------------
	// 窓
	//-------------------------------------------------------------------------
	bool bShowOutliner = true;
	bool bShowInspector = true;
	bool bShowAssets = true;
	bool bShowIssues = true;

	//-------------------------------------------------------------------------
	// 表示
	//-------------------------------------------------------------------------
	enum class EColliderView : uint8 { Off, Selected, All };
	EColliderView ColliderView = EColliderView::Selected;
	bool bShowGrid = true;
	bool bShowNames = true;
	bool bShowGizmo = true;
	bool bMeshWireTruncated = false;            // 最後に描いたとき、メッシュの当たり判定の枠線を、三角形が多すぎて一部しか描かなかった

	//-------------------------------------------------------------------------
	// ビューポート（毎フレーム描き直す）
	//-------------------------------------------------------------------------
	int32 ViewScreen = -1;
	int32 ViewWidth = 0;
	int32 ViewHeight = 0;
	ImVec2 ViewOrigin = ImVec2( 0.0f, 0.0f );   // 最後に描いたビューポートの位置と大きさ（ImGui の画面座標。確認用の自動操作が使う）
	ImVec2 ViewSize = ImVec2( 0.0f, 0.0f );
	FEditorViewport View;
	EEditorGizmoHandle HoverHandle = EEditorGizmoHandle::None;
	int32 HoverObjectId = WorldNoId;
	enum class EMouseMode : uint8 { None, Orbit, Pan, Look, Gizmo, Pending, Marquee };
	EMouseMode MouseMode = EMouseMode::None;
	ImVec2 PressPosition = ImVec2( 0.0f, 0.0f );
	bool bPressAdd = false;
	bool bPressToggle = false;
	bool bViewHovered = false;
	bool bUseInputOverride = false;             // true なら、ImGui の入力の代わりに InputOverride を使う（確認用の自動操作）
	FWorldEditorInput InputOverride;
	bool bAnyPanelFocused = false;              // 編集用の窓のどれかにフォーカスがある（ショートカットを受ける）
	TArray<FTransform> FrameTransforms;         // 最後に描いたときの、オブジェクトごと（Objects と同じ並び）のワールドでの姿勢と図形
	TArray<TArray<FEditorShape>> FrameShapes;
	bool bHasDropPreview = false;               // アセットを視界の上へドラッグしている間の、置く場所
	FVector3 DropPreviewPosition = FVector3::Zero;
	FString DropPreviewName;

	//-------------------------------------------------------------------------
	// 編集メッシュ
	//-------------------------------------------------------------------------
	// オブジェクトの Id -> 描く形（メッシュの内容が変わったときだけ、作り直す）
	struct FMeshView
	{
		uint64 Hash = 0;
		bool bBuilt = false;
		FMeshRenderer Renderer;
	};
	TMap<int32, FMeshView> MeshViews;
	// 完全なPrefab host鎖とleaf Idで識別する、個体ごとのmodel描画owner。
	TMap<FWorldEditorModelPreviewKey, FWorldEditorModelPreview> ModelPreviews;
	FEditMeshHit HoverMeshHit;                  // 編集中、マウスの下の要素（最後に処理した入力から）
	float32 MeshExtrudeDistance = 1.0f;         // ツールバーの入力
	float32 MeshInsetAmount = 0.3f;
	float32 MeshWeldDistance = 0.01f;
	int32 MeshAppendKind = 0;                   // EEditMeshPrimitive

	//-------------------------------------------------------------------------
	// エフェクトのプレビュー（WorldEditorEffects.cpp）
	//-------------------------------------------------------------------------
	// オブジェクトの Id -> 再生中のもの（部品の値が変わる・やり直しを押すと、最初から）
	struct FEffectPreview
	{
		uint64 Hash = 0;
		TSharedPtr<IEffectPlayback> Playback;   // 読み込めなければ空
		float32 FinishedTime = 0.0f;            // 終わってからの秒（繰り返さないエフェクトを、少し待ってやり直す）
	};
	TMap<int32, FEffectPreview> EffectPreviews;
	bool bPlayEffects = true;
	uint32 EffectRestartSerial = 0;             // 増やすと、すべてやり直す

	//-------------------------------------------------------------------------
	// アウトライナー
	//-------------------------------------------------------------------------
	TSet<int32> Collapsed;
	FString OutlinerFilter;
	int32 LastClickedId = WorldNoId;

	//-------------------------------------------------------------------------
	// アセット一覧
	//-------------------------------------------------------------------------
	TArray<FString> ModelFiles;
	TArray<FString> TextureFiles;
	TArray<FString> SoundFiles;
	TArray<FString> AnimationFiles;
	TArray<FString> WorldFiles;
	TArray<FString> EffectFiles;
	double LastScanTime = -100.0;
	FString AssetFilter;
	int32 AssetKind = 0;                        // 0 すべて / 1 モデル / 2 画像 / 3 音 / 4 ワールド / 5 エフェクト

	//-------------------------------------------------------------------------
	// 検証
	//-------------------------------------------------------------------------
	TArray<FWorldIssue> Issues;
	uint32 IssuesRevision = ~0u;
	bool bIssuesShowWarnings = true;

	//-------------------------------------------------------------------------
	// インスペクターの入力
	//-------------------------------------------------------------------------
	FString NewTag;
	FString NewPropName;
	int32 NewPropType = 0;
	FString NewRuleTag;

	//-------------------------------------------------------------------------
	// ファイル
	//-------------------------------------------------------------------------
	FString StatusMessage;
	bool bStatusIsError = false;
	FString OpenSelection;
	FString MakePrefabPath;                     // 「選んだものを Prefab にする」の保存先
	bool bRequestOpenMakePrefabPopup = false;
	FString SaveAsPath;
	bool bRequestOpenUnsavedPopup = false;
	bool bRequestOpenOpenPopup = false;
	bool bRequestOpenSaveAsPopup = false;
	enum class EPending : uint8 { None, New, Open, Close };
	EPending Pending = EPending::None;
	FString PendingPath;
	bool bCloseRequested = false;
};

namespace NDebugUI
{
	// 視界への入力 1 フレーム分を処理する（カメラの操作・クリック・矩形・ギズモのドラッグ）。ImGui を使わない。
	// 視界の大きさ（State.View）が決まっている（1 度は描いた）ことが前提
	void ProcessWorldEditorInput( FWorldEditorState& State, const FWorldEditorInput& Input );

	// ウィンドウ（と、そのパネル）を出す。pOpen が null でなければ、× ボタンで false になる
	void DrawWorldEditor( FWorldEditorState& State, bool* pOpen = nullptr );

	bool OpenWorldFile( FWorldEditorState& State, const FString& AssetPath );
	void NewWorld( FWorldEditorState& State );
	// 保存する（Path が空なら、いま開いているファイル）。保存先が無ければ false
	bool SaveWorldFile( FWorldEditorState& State, const FString& AssetPath = FString() );
	// 3D の視界に使った画面を片付ける（アプリの終了前に）
	void ReleaseWorldEditor( FWorldEditorState& State );
}

#endif

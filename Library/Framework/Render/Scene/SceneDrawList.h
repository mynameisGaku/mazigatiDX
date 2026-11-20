// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/Render/Scene/SceneDrawSnapshot.h"
#include "Library/Core/Render/Scene/SceneDrawClass.h"
#include "Library/Core/Render/DrawSpan.h"
#include "Library/Math/Vector3.h"

struct FCamera;
struct FMatrix4x4;

// 一つの記録元がカメラと独立した3D項目を集め、確定後は不変の一覧へ所有を渡す。
// ワーカーごとに別の一覧を作り、呼出し側が決めた順でAppendする。同じ一覧へ同時に書き込まない。
class CSceneDrawList
{
public:
	// カメラを値で固定する。不正なカメラでは未作成のままになる。
	explicit CSceneDrawList( const FCamera& Camera );
	// 可変の記録所有を複製せず、移動でだけ渡す。
	CSceneDrawList( const CSceneDrawList& ) = delete;
	CSceneDrawList& operator=( const CSceneDrawList& ) = delete;
	CSceneDrawList( CSceneDrawList&& ) noexcept = default;
	CSceneDrawList& operator=( CSceneDrawList&& ) noexcept = default;
	// 受付中の一覧が有効かを返す。確定後はfalse。
	bool IsValid() const;
	// 新しいカメラで記録を始める。以前に確定した値は変更しない。
	bool Reset( const FCamera& Camera );
	// 状態と3D命令を所有コピーする。奥行きは固定カメラの前方向へWorldCenterを射影する。
	// 2D・カメラ・描画先・消去・姿勢変更・不正な長さ・不均衡な状態範囲・上限超過は拒否し、元を保つ。
	bool Add( const FDrawSpan& Commands, ESceneDrawClass Class, const FVector3& WorldCenter );
	// 固定したモデル姿勢と配置を一つの項目として記録する。切り抜き・不正な配置ではfalse。
	bool AddModel( const FModelPoseSnapshot& Pose, ESceneDrawClass Class, const FVector3& WorldCenter, const FMatrix4x4& World, bool bLit = true );
	// 命令と既存の共有所有を一緒に受け付ける。拒否時は項目も所有参照も追加しない。
	template<typename TObject>
	bool Add( const FDrawSpan& Commands, ESceneDrawClass Class, const FVector3& WorldCenter, const TSharedPtr<TObject>& Owner )
	{
		return Owner && AddOwned( Commands, Class, WorldCenter, TSharedPtr<const uint8>( Owner, reinterpret_cast<const uint8*>( Owner.Get() ) ) );
	}
	// 同じカメラの確定一覧を末尾へ複写する。不一致・未作成・上限超過では両方を変更しない。
	bool Append( const FSceneDrawSnapshot& Batch );
	// 分類と奥行きを安定して並べ、所有を確定値へ渡す。この一覧はResetまで受付を終える。
	FSceneDrawSnapshot Freeze();
	// 既存の共有所有を一覧の寿命まで保持する。所有者の破棄スレッドの契約は利用者が守る。
	// null、受付終了、参照数上限ではfalse。資源の更新を禁止する仕組みではない。
	template<typename TObject>
	bool KeepAlive( const TSharedPtr<TObject>& Owner )
	{
		return Owner.IsValid() && KeepAliveAlias( TSharedPtr<const uint8>( Owner, reinterpret_cast<const uint8*>( Owner.Get() ) ) );
	}

private:
	// 所有参照の上限を確認してから項目と参照を一緒に追加する。
	bool AddOwned( const FDrawSpan& Commands, ESceneDrawClass Class, const FVector3& WorldCenter, const TSharedPtr<const uint8>& Owner );
	// 型を参照せず、既存の所有者の制御部分だけを共有して保持する。
	bool KeepAliveAlias( const TSharedPtr<const uint8>& Owner );
	TSharedPtr<FSceneDrawSnapshotData> Data; // この記録元だけが変更できる未確定の一覧。
};

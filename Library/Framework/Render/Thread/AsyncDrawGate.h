// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Collection/Set.h"
#include "Library/Core/Render/GraphicsPipelineDraw.h"
#include "Library/Core/Render/TextureResourceHandle.h"

// 3D の描画を、結果を待たずに描画スレッドへ積んでよいかを決める（ゲームスレッドだけで使う）。
// 待たずに積むと、実行先が描画を拒否しても、呼び出し元はそれを知れない。そこで、拒否になりうる条件を、ゲームスレッドの側で先に潰す。
//  ・初めて使う組み合わせ（パイプライン・描画先・バッファ・画像・大きさ）は、同期で実行して、成功したら「確かめ済み」と覚える。待たずに積むのは、確かめ済みの組み合わせの繰り返しだけ。
//  ・実行先が持つ上限（1 フレームの描画数・保持するバッファ・画像・パイプラインの数と大きさ・転送領域の量）は、同じ数え方で先に数え、近づいたら同期へ戻す。
//  ・書き込み中の描画先の画像を読む描画は、競合の判定が要るので、いつも同期で実行する。
// 資源の作成・更新・解放があったら、確かめ済みを捨てる（InvalidateVerified）。
class CAsyncDrawGate
{
  public:
	// 待たずに積んでよいか、同期で実行するか。
	enum class EAdmission : uint8
	{
		Queue, // 待たずに積んでよい。数えに加えてある。
		Verify, // 同期で実行して、結果を NoteExecuted で知らせる。
	};

	// 実行先の上限に対して、手前で同期へ戻す余裕を持たせた、待たずに積める上限。
	static constexpr int32 MaxDraws = NGraphicsPipelineLimits::MaxDrawsPerFrame - 24; // 実行先は MaxDrawsPerFrame 回まで。手前で同期へ戻す余裕を持たせる。
	static constexpr int32 MaxPinnedBuffers = 240; // 実行先は 256 個まで。
	static constexpr int32 MaxPinnedTextures = 240; // 実行先は 256 枚まで。
	static constexpr int32 MaxPinnedPipelines = 240; // 実行先は 256 個まで。
	static constexpr uint64 MaxPinnedBytes = 224ull * 1024 * 1024; // 実行先は 256MiB まで。
	static constexpr uint64 MaxUploadBytes = 40ull * 1024 * 1024; // 実行先は 64MiB まで。残りは、同じフレームの 2D の頂点が使う。

	// 新しいフレームの始まり。数えを捨てて、描画先を通常の画面に戻す。確かめ済みは、描画先が同じなら、そのまま使う。
	void BeginFrame();
	// 描画先が変わった（描画先パス・撮影の始まりと終わり）。TargetKey は描画先の組み合わせを表す値（通常の画面は 0）。
	// WrittenTextures は、いま書き込み先になっている画像。これらを読む描画は、いつも同期で確かめる。
	void SetTarget( uint64 InTargetKey, TArrayView<const FTextureResourceHandle> InWrittenTextures );
	// 資源の作成・更新・解放があった。確かめ済みを捨てる（番号が別の資源に使い回されても、古い確認を使わない）。
	void InvalidateVerified();
	// 共有バッファの描画を、待たずに積んでよいか。Queue のときは、数えに加える。
	EAdmission Admit( const FGraphicsPipelineDraw &Draw );
	// 一時データの描画を、待たずに積んでよいか。Queue のときは、数えに加える。
	EAdmission AdmitTransient( const FGraphicsPipelineTransientDraw &Draw );
	// 同期で実行した結果を知らせる。受け付けられたら、確かめ済みとして覚えて、数えに加える。
	void NoteExecuted( const FGraphicsPipelineDraw &Draw, bool bAccepted );
	void NoteExecutedTransient( const FGraphicsPipelineTransientDraw &Draw, bool bAccepted );

	// 今フレームに数えた描画の数。
	int32 GetNumDraws() const { return NumDraws; }
	// 確かめ済みの組み合わせの数。
	int32 GetNumVerified() const { return Verified.Num(); }

  private:
	// 描画が使う資源。数えと、書き込み先との競合の判定に使う。
	struct FUse
	{
		uint64 Key = 0; // 組み合わせを表す値。
		bool bTouchesWrittenTexture = false; // 書き込み中の画像を読む。
		bool bValid = false; // 待たずに積める形（頂点数・大きさなどが範囲内）。
		uint64 PipelineValue = 0;
		TArray<uint64> Buffers; // 初めて使うかを調べる、共有バッファの番号。
		TArray<uint64> Textures; // 画像の番号。
		uint64 PinnedBytes = 0; // 共有バッファの大きさの合計（見積もり）。
		uint64 UploadBytes = 0; // 転送領域へ載せる量の見積もり。
	};
	// 共有バッファの描画から、使う資源を調べる。
	FUse Inspect( const FGraphicsPipelineDraw &Draw ) const;
	// 一時データの描画から、使う資源を調べる。
	FUse InspectTransient( const FGraphicsPipelineTransientDraw &Draw ) const;
	// 画像が書き込み先か。
	bool IsWritten( uint64 Texture ) const;
	// 数えの上限内に収まるか。
	bool FitsBudget( const FUse &Use ) const;
	// 数えに加える。
	void Count( const FUse &Use );
	// 確かめ済みとして覚える。
	void Remember( const FUse &Use );

	TSet<uint64> Verified; // 同期で成功した組み合わせ。
	TSet<uint64> PinnedBuffers; // 今フレームに数えたバッファ。
	TSet<uint64> PinnedTextures; // 今フレームに数えた画像。
	TSet<uint64> PinnedPipelines; // 今フレームに数えたパイプライン。
	TArray<uint64> WrittenTextures; // 書き込み先の画像の番号。
	uint64 TargetKey = 0; // 描画先の組み合わせ。確かめ済みの値に混ぜる。
	uint64 PinnedBytes = 0; // 今フレームに数えた共有バッファの大きさ。
	uint64 UploadBytes = 0; // 今フレームに数えた転送領域の量。
	int32 NumDraws = 0; // 今フレームに数えた描画の数。
};

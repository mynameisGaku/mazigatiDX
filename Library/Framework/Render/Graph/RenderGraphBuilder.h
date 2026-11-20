// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Map.h"
#include "Library/Core/Render/RenderCommandContext.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Render/Graph/RenderGraphPassDesc.h"

class IRenderSurface;

// フレーム内の画像と描画パスを集め、宣言を検査して登録順に実行する。所有スレッドから使う。
class CRenderGraphBuilder
{
public:
	// 実行中の一覧を入れ替えないよう、グラフ自体は複製・移動しない。
	CRenderGraphBuilder() = default;
	CRenderGraphBuilder( const CRenderGraphBuilder& ) = delete;
	CRenderGraphBuilder& operator=( const CRenderGraphBuilder& ) = delete;

	using PassCallback = TCallable<bool( const FRenderCommandContext& )>; // 描画先を借りる。失敗はfalseを返し、例外を投げない。

	// 既存画像を取り込む。実行完了までは呼び出し側が資源を保持する。無効番号・重複は拒否。
	bool ImportTexture( FTextureResourceHandle Texture, bool bHasContents );
	// 末尾にパスを登録する。設定とコールバックはグラフが所有する。
	bool AddPass( const FRenderGraphPassDesc& Desc, PassCallback Callback );
	// 一意の名前で、対象パスの直前または直後に挿入する。
	bool InsertPassBefore( const FString& Anchor, const FRenderGraphPassDesc& Desc, PassCallback Callback );
	bool InsertPassAfter( const FString& Anchor, const FRenderGraphPassDesc& Desc, PassCallback Callback );
	// 依存する画像と順序を保ち、名前で処理だけを差し替える。
	bool ReplacePass( const FString& Name, PassCallback Callback );
	// 同じ名前と順序の位置で設定と処理を差し替える。存在しない名前は拒否。
	bool ReplacePass( const FRenderGraphPassDesc& Desc, PassCallback Callback );
	// 未登録参照、初期化前の読み取り・保持、同一パス内の衝突を検査する。
	bool Validate( FString* OutError = nullptr ) const;
	// 全件を検査してから、フレーム内で一回ずつ実行する。途中失敗では後続を呼ばない。
	// コールバック内でパスを開閉せず、宣言した画像だけを使う。発行済み描画の取り消しは行わない。
	bool Execute( IRenderSurface& Surface, FString* OutError = nullptr );

private:
	struct FPass
	{
		FRenderGraphPassDesc Desc; // 登録時に複写した画像参照と描画先。
		PassCallback Callback; // 実行まで値を所有する処理。
	};
	// 名前に対応する位置かINDEX_NONEを返す。
	int32 FindPass( const FString& Name ) const;
	// 位置・名前・登録上限を確認してから挿入する。
	bool InsertAt( int32 Position, const FRenderGraphPassDesc& Desc, PassCallback Callback );
	TArray<FPass> Passes; // 外部から指定した安定した順序。
	TMap<uint64, bool> Textures; // 取り込んだ画像と、実行前から有効な内容の有無。
	bool bExecuting = false; // 実行中の再入と変更を拒否する。
};

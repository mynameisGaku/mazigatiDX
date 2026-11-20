// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IRenderBufferResources.h"

// 同じ頂点・添字の再登場を確認してGPUバッファを共有する。資源所有先と同じ実行スレッドで使い、並行操作しない。
class CShaderMeshBufferCache
{
public:
	// 資源の所有先を借りる。所有先はShutdownまで存続させる。上限0では記録しない。
	void Initialize( IRenderBufferResources* InBuffers, uint64 InMaxBytes = 32ull * 1024 * 1024, int32 InMaxEntries = 32 );
	// 所有する全バッファを既存のGPU完了管理へ返し、借用を終了する。
	void Shutdown();
	// 未初期化の空キャッシュを作る。
	CShaderMeshBufferCache() = default;
	CShaderMeshBufferCache( const CShaderMeshBufferCache& ) = delete;
	CShaderMeshBufferCache& operator=( const CShaderMeshBufferCache& ) = delete;
	// 借用先が存続する間に終了すること。
	~CShaderMeshBufferCache();
	// 初回は値だけを保持してfalse。再登場時に登録し、成功時だけ出力を変更する。添字なしも受け付ける。
	bool Acquire( TArrayView<const uint8> Vertices, TArrayView<const uint8> Indices, FBufferResourceHandle& OutVertices, FBufferResourceHandle& OutIndices );
	// 比較用のCPUコピーの合計。登録中のGPUバッファもこの値以下。解放待ちは所有先が管理する。
	uint64 GetNumBytes() const { return NumBytes; }
	// 未登録の候補を含む件数。
	int32 GetNumEntries() const { return Entries.Num(); }

private:
	// 内容の照合と、作成済みの資源を一緒に所有する。
	struct FEntry
	{
		uint32 Hash = 0; // 内容検索を絞る値。共有前には全バイトも比較する。
		TArray<uint8> Vertices; // 元配列の変更・解放に影響されない照合用コピー。
		TArray<uint8> Indices; // 空なら添字なし。
		FBufferResourceHandle VertexBuffer; // 作成後だけ有効な所有資源。
		FBufferResourceHandle IndexBuffer; // 添字がある場合だけ作成する。
	};
	// 指定した項目の資源とコピーを捨てる。使用中のGPU資源は所有先が完了まで保持する。
	void Remove( int32 Index );
	IRenderBufferResources* Buffers = nullptr; // 終了まで借りる既存の資源窓口。
	TArray<FEntry> Entries; // 先頭が最も長く使われていない項目。
	uint64 MaxBytes = 0; // CPUコピーの合計上限。GPU側にも同じ上限を適用する。
	uint64 NumBytes = 0; // 現在のCPUコピーの合計。
	int32 MaxEntries = 0; // 小さい形状が際限なく増えることを防ぐ上限。
};

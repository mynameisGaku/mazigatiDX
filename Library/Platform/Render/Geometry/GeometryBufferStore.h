// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Interfaces/Render/IGeometryBufferSource.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/Map.h"

// APIに依存しない形状の所有。GPUへ送る前のローカル頂点とindexを保持する。
class CGeometryBufferStore : public IGeometryBufferSource
{
public:
	// 指定数のゼロ頂点を作る。不正・容量超過時は-1。
	int32 MakeVertexBuffer( int32 NumVertices );
	// 全入力を検査して範囲を更新する。失敗時は元の内容を保つ。
	bool WriteVertexBuffer( int32 Handle, int32 Start, const FDrawVertex3D* Vertices, int32 Count );
	// 登録番号を無効にして頂点を解放する。
	void DeleteVertexBuffer( int32 Handle );
	// 指定数のゼロindexを作る。入力の16／32bit形式を保存する。
	int32 MakeIndexBuffer( int32 NumIndices, bool b32Bit );
	// 指定した形式のindexを32bitへ展開して所有する。失敗時は元の内容を保つ。
	bool WriteIndexBuffer( int32 Handle, int32 Start, const void* Indices, int32 Count );
	// 登録番号を無効にしてindexを解放する。
	void DeleteIndexBuffer( int32 Handle );
	// 検査済みの三角形を二つのバッファへ登録する。失敗時は出力を保ち、途中の登録を残さない。
	bool CreateMesh( const FDrawVertex3D* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer );
	// 有効な登録だけを貸す。書き換え・削除・追加前に借用を終える。
	bool GetVertexBufferView( int32 Handle, FGeometryVertexBufferView& Out ) override;
	// 有効な登録だけを貸す。書き換え・削除・追加前に借用を終える。
	bool GetIndexBufferView( int32 Handle, FGeometryIndexBufferView& Out ) override;
	// モデルとは別の鍵の領域を使い、その組の頂点かindexへ書き込んだ後だけ、更新版を替える（ほかのバッファへの書き込みでは、替えない）。
	bool GetMeshCacheIdentity( int32 VertexBuffer, int32 IndexBuffer, uint64& Key, uint64& Version ) override;
	// 同じ版を、一定の回数、描いたあとだけ true（描くたびに書き換えるバッファは、GPU へ置き直す費用が見合わないので、いつまでも false）。
	bool GetStableMeshCacheIdentity( int32 VertexBuffer, int32 IndexBuffer, uint64& Key, uint64& Version ) override;
	// 登録を全て消す。番号は再利用しない。
	void Reset();
private:
	// 組ごとの、同じ版を描いた回数。
	struct FMeshUse
	{
		uint64 Version = 0; // 数えている版。
		int32 Draws = 0; // この版で、描いた回数。
	};
	// 元の書き込み形式と、展開済みのindex。
	struct FIndexRecord
	{
		TArray<uint32> Values; // 描画で参照する32bitの列。
		bool b32Bit = false; // 書き込み元が32bitか。
	};
	// 頂点で使う数値が全て有限か。
	static bool IsValidVertex( const FDrawVertex3D& Vertex );
	TMap<int32, TArray<FDrawVertex3D>> Vertices; // 登録頂点。
	TMap<int32, FIndexRecord> Indices; // 登録index。
	uint64 Bytes = 0; // 保持中の総バイト数。
	TMap<uint64, uint64> MeshKeys; // 頂点と添字の組から再利用しない識別への表。
	TMap<uint64, FMeshUse> MeshUses; // 組の識別から、同じ版を描いた回数への表。
	static constexpr int32 MinDrawsBeforeSharing = 8; // この回数だけ、同じ版を描いたら、GPU の共有メッシュへ置く。
	uint64 Revision = 1; // この所有者の書き込みごとに増える版（書き込みの通し番号）。
	TMap<int32, uint64> VertexRevisions; // 頂点バッファごとの、最後に書いたときの Revision。ほかのバッファへの書き込みでは、変わらない。
	TMap<int32, uint64> IndexRevisions; // indexバッファごとの、最後に書いたときの Revision。
	int32 NextHandle = 0; // 解放後も再利用しない番号。
	static constexpr uint64 MaxBytes = 64 * 1024 * 1024; // 全バッファの保持上限。
};

// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/ArrayView.h"
#include "Library/Core/Interfaces/Render/IIndexBuffer.h"
#include "Library/Core/Memory/SharedPtr.h"

//=============================================================================
// EIndexType
//   添字 1 つの大きさ。16 ビットは、頂点 65,536 個まで指せる（BaseVertex で、先の頂点も指せる）。32 ビットは、メモリが倍になる。
//=============================================================================
enum class EIndexType : uint8
{
	Index16,
	Index32,
};

//=============================================================================
// CIndexBuffer
//   GPU に置いた、添字（どの頂点を、どの順で使うか）の並び（DxLib のインデックスバッファ）。CVertexBuffer と組み合わせて、同じ頂点を使い回して、形を描く（四角形なら、頂点 4 つと添字 6 つ）。
//   大きさ（添字の数）と、添字の大きさ（EIndexType）は、作ったあと変わらない。中身は、SetData で書き換えられる（添字の大きさと同じ型の並びで）。
//   そのほかは CVertexBuffer と同じ（ファイルから読むアセットではない・メインスレッドだけで作り、書き換える・描かれるまで手放さない）。
//
//   const uint16 Indices[6] = { 0, 1, 2, 2, 1, 3 };
//   TSharedPtr<CIndexBuffer> Buffer = CIndexBuffer::Create( TArrayView<const uint16>( Indices, 6 ) );     // 16 ビットの添字
//   Draw.PrimitiveIndexedBuffer3D( Vertices, Buffer, EDrawPrimitive::Triangles );
//=============================================================================
class CIndexBuffer final : public IIndexBuffer
{
public:
	// 作れる添字の数の上限
	static constexpr int32 MaxIndices = 1 << 26;

	// 添字 NumIndices 個の空のバッファを作る（中身は 0 で埋まる）。NumIndices が 1 未満か MaxIndices を超える・メインスレッドでない・作れないときは、nullptr
	static TSharedPtr<CIndexBuffer> Create( int32 NumIndices, EIndexType Type );

	// 添字の並びから、バッファを作って、中身を書く（uint16 なら 16 ビット、uint32 なら 32 ビットのバッファ）。添字が 0 個・MaxIndices を超える・メインスレッドでない・作れないときは、nullptr
	static TSharedPtr<CIndexBuffer> Create( TArrayView<const uint16> Indices );
	static TSharedPtr<CIndexBuffer> Create( TArrayView<const uint32> Indices );

	~CIndexBuffer() override;
	CIndexBuffer( const CIndexBuffer& ) = delete;
	CIndexBuffer& operator=( const CIndexBuffer& ) = delete;

	// StartIndex 番目から、Indices を書く。バッファの添字の大きさと、並びの型が合わない・範囲が収まらない（StartIndex が負・0 個・終わりがはみ出す）・メインスレッドでないときは、何も書かず false。
	// 添字が指す頂点が、頂点バッファにあるかは、確かめない（添字を頂点バッファの範囲に収めるのは、呼ぶ側の責任。範囲の外を指す添字があると、DxLib は失敗を返し、先に処理した三角形だけを描くことがある。描かれるものは決まらないが、落ちない）
	bool SetData( int32 StartIndex, TArrayView<const uint16> Indices );
	bool SetData( int32 StartIndex, TArrayView<const uint32> Indices );

	// 添字の数（作ったときの大きさ）
	int32 GetNumIndices() const { return NumIndices; }

	// 添字の大きさ
	EIndexType GetType() const { return Type; }

	FIndexBufferHandle GetIndexBufferHandle() const override;

private:
	CIndexBuffer( int32 InIndexBuffer, int32 InNumIndices, EIndexType InType )
		: IndexBuffer( InIndexBuffer )
		, NumIndices( InNumIndices )
		, Type( InType )
	{
	}

	// Indices（Count 個。型は、バッファの添字の大きさと同じ）を書く
	bool Write( int32 StartIndex, const void* Indices, int32 Count, EIndexType ExpectedType );

	int32 IndexBuffer = -1;
	int32 NumIndices = 0;
	EIndexType Type = EIndexType::Index16;
};

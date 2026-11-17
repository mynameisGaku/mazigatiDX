// SPDX-License-Identifier: MIT
#include "SceneDrawOrder.h"
#include "Library/Framework/Render/Draw/DrawCommand.h"
#include "Library/Framework/Render/Scene/DrawSceneKeyData.h"
#include "Library/Math/MathUtils.h"

namespace
{
	constexpr int32 MaxSceneWords = 4 * 1024 * 1024; // 並べ替えで複製するword数の上限。
	constexpr int32 MaxSceneKeys = 65536; // 一つのSpanで扱う分類付き描画数の上限。

	// 入力が単独で読める固定長3D描画かを検査する。状態や可変長の命令は境界にする。
	bool IsIndependentDraw( EDrawCommand Type, int32 Count, const uint32* Words )
	{
		switch ( Type )
		{
		case EDrawCommand::Triangle3D: return Count == 1 + sizeof( FDrawTriangle3DData ) / 4;
		case EDrawCommand::Sphere3D: return Count == 1 + sizeof( FDrawSphere3DData ) / 4;
		case EDrawCommand::Capsule3D: return Count == 1 + sizeof( FDrawCapsule3DData ) / 4;
		case EDrawCommand::Cube3D: return Count == 1 + sizeof( FDrawCube3DData ) / 4;
		case EDrawCommand::Cone3D: return Count == 1 + sizeof( FDrawCone3DData ) / 4;
		case EDrawCommand::Quad3D: return Count == 1 + sizeof( FDrawQuad3DData ) / 4;
		case EDrawCommand::Mesh3D: return Count == 1 + sizeof( FDrawMeshData ) / 4;
		case EDrawCommand::Model3D:
		{
			if ( Count != 1 + sizeof( FDrawModelData ) / 4 ) { return false; }
			FDrawModelData Model; // 姿勢を書き換える描画は後続の同じモデルへ影響するため並べない。
			FMemory::Memcpy( &Model, Words + 1, sizeof( Model ) );
			return Model.AnimIndex < 0;
		}
		default: return false;
		}
	}

	// 元のword列を参照する一描画分。参照先はPrepareの実行中に変更しない。
	struct FSceneItem
	{
		int32 Offset = 0; // キー命令の開始位置。
		int32 Count = 0; // キーと描画を合わせたword数。
		FDrawSceneKeyData Key; // この描画だけの分類と奥行き。
		int32 Ordinal = 0; // 同じ分類・深度の元の順序。
	};
}

FDrawSpan NSceneDrawOrder::Prepare( const FDrawSpan& Input, TArray<uint32>& Storage, bool& bValid )
{
	Storage.Reset();
	bValid = false;
	if ( Input.NumWords < 0 || ( Input.NumWords > 0 && !Input.Words ) ) { return Input; }
	bool bScene = false; // 検査中に開いているシーン。
	bool bHasScene = false; // 複製が必要なシーン命令があるか。
	int32 ShadowDepth = 0; // 影の生成中はシーンの並べ替えを開始しない。
	int32 NumKeys = 0; // 上限を検査する分類命令数。
	for ( int32 Offset = 0; Offset < Input.NumWords; )
	{
		const uint32 Header = Input.Words[Offset]; // 検査する命令のヘッダー。
		const int32 Count = static_cast<int32>( Header >> 16 ); // 残り長を越えない命令長。
		if ( Count < 1 || Count > Input.NumWords - Offset ) { return Input; }
		const EDrawCommand Type = static_cast<EDrawCommand>( Header & 0xff ); // 命令の種類。
		if ( Type == EDrawCommand::BeginShadowMap ) { ++ShadowDepth; }
		if ( Type == EDrawCommand::EndShadowMap && ShadowDepth > 0 ) { --ShadowDepth; }
		if ( Type == EDrawCommand::BeginScene3D || Type == EDrawCommand::EndScene3D )
		{
			if ( Count != 1 || ( Header & 0xff00 ) != 0 ) { return Input; }
			if ( Type == EDrawCommand::BeginScene3D )
			{
				if ( bScene || ShadowDepth != 0 ) { return Input; }
				bScene = true;
				bHasScene = true;
			}
			else
			{
				if ( !bScene ) { return Input; }
				bScene = false;
			}
		}
		if ( Type == EDrawCommand::SceneDrawKey3D )
		{
			if ( !bScene || ShadowDepth != 0 || Count != 3 || ( Header & 0xff00 ) != 0 || ++NumKeys > MaxSceneKeys ) { return Input; }
			FDrawSceneKeyData Key; // 生のword列から安全にコピーするキー。
			FMemory::Memcpy( &Key, Input.Words + Offset + 1, sizeof( Key ) );
			if ( Key.Class > ESceneDrawClass::Translucent || !NMath::IsFinite( Key.Depth ) ) { return Input; }
		}
		Offset += Count;
	}
	if ( bScene || ( bHasScene && Input.NumWords > MaxSceneWords ) ) { return Input; }
	bValid = true;
	if ( !bHasScene ) { return Input; }
	TArray<FSceneItem> Items; // 同じ状態のまま描ける連続した項目だけを保持する。
	auto Flush = [&]()
	{
		Items.Sort( []( const FSceneItem& A, const FSceneItem& B )
		{
			if ( A.Key.Class != B.Key.Class ) { return A.Key.Class < B.Key.Class; }
			if ( A.Key.Class == ESceneDrawClass::Translucent && A.Key.Depth != B.Key.Depth ) { return A.Key.Depth > B.Key.Depth; }
			return A.Ordinal < B.Ordinal;
		} );
		for ( const FSceneItem& Item : Items ) { Storage.Append( Input.Words + Item.Offset, Item.Count ); }
		Items.Reset();
	}; // 境界の直前で分類順・奥行き順を確定する。
	for ( int32 Offset = 0; Offset < Input.NumWords; )
	{
		const uint32 Header = Input.Words[Offset]; // 検査済み命令のヘッダー。
		const int32 Count = static_cast<int32>( Header >> 16 ); // この命令のword数。
		const EDrawCommand Type = static_cast<EDrawCommand>( Header & 0xff ); // 境界か分類付き描画かを選ぶ種類。
		if ( Type == EDrawCommand::BeginScene3D ) { bScene = true; }
		if ( Type == EDrawCommand::EndScene3D ) { bScene = false; }
		if ( bScene && Type == EDrawCommand::SceneDrawKey3D && Offset + Count < Input.NumWords )
		{
			const uint32 Next = Input.Words[Offset + Count]; // キーの直後の描画候補。
			const int32 DrawWords = static_cast<int32>( Next >> 16 ); // 検査済みの次命令長。
			if ( IsIndependentDraw( static_cast<EDrawCommand>( Next & 0xff ), DrawWords, Input.Words + Offset + Count ) )
			{
				FSceneItem Item; // キーと描画を一緒に移動して対応を維持する。
				Item.Offset = Offset;
				Item.Count = Count + DrawWords;
				Item.Ordinal = Offset;
				FMemory::Memcpy( &Item.Key, Input.Words + Offset + 1, sizeof( Item.Key ) );
				Items.Add( Item );
				Offset += Item.Count;
				continue;
			}
		}
		Flush();
		Storage.Append( Input.Words + Offset, Count );
		Offset += Count;
	}
	Flush();
	return { Storage.GetData(), Storage.Num() };
}

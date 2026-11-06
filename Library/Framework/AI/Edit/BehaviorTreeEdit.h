// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Framework/AI/BehaviorTree/BehaviorTreeDefinition.h"
#include "Library/Framework/Asset/Definition/DefinitionDocument.h"

//=============================================================================
// ビヘイビアツリー定義の編集（エディタの画面から独立した、編集の操作と履歴）
//   エディタ（ImGui）に依存しない。画面は、ここの関数を呼ぶだけにして、「どう編集されるか」（番号のずれの補正・部分木の移動と複製・元に戻す）はここでテストする。
//   すべての関数は、Document.BeginEdit() で履歴に積んでから書き換える（範囲外の番号・できない操作・変わらない操作は、何もしないで、履歴にも積まない）。
//   連続した変更は、CoalesceKey に同じ文字列を渡し続けると、1 回の編集にまとまる。
//
//   ノードの番号は、FBehaviorTreeDefinition::Nodes の番号（先頭がルート。親の前に子が来る順）。
//   子の位置 At は、親の子の中の、何番目か（0 から。兄弟の順 = 優先順位）。範囲外（INDEX_NONE を含む）は、いちばん最後。
//=============================================================================
using TBehaviorTreeDocument = TDefinitionDocument<FBehaviorTreeDefinition>;

namespace NBehaviorTreeEdit
{
	void SetBlackboard( TBehaviorTreeDocument& Document, const FString& Path );

	// ノードを足す。Parent が -1 なら、ルート（ツリーが空のときだけ）。葉（Task）の下には足せない。
	// 葉（Type が Task）は TaskName を登録した Task の名前にする。足したノードの番号を返す（足せなければ INDEX_NONE）
	int32 AddNode( TBehaviorTreeDocument& Document, int32 Parent, EBTNodeType Type, const FString& TaskName = FString(), int32 At = INDEX_NONE );

	// ノードを、子孫ごと消す（ルートを消すと、ツリーが空になる）
	void RemoveNode( TBehaviorTreeDocument& Document, int32 NodeIndex );

	// ノードを、子孫ごと、別の親の下へ動かす（同じ親の中の並べ替えもこれ。At は、動かしたあとの位置）。
	// ルートは動かせない。自分の子孫の下・葉の下へは動かせない。動かせたら true（OutNewIndex に、動かしたあとの、そのノードの番号を返す）
	bool MoveNode( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 NewParent, int32 At, int32* OutNewIndex = nullptr );

	// ノードを、子孫ごと複製して、元のすぐ右の兄弟にする。複製のルートの番号を返す（ルートは複製できない。INDEX_NONE）
	int32 DuplicateNode( TBehaviorTreeDocument& Document, int32 NodeIndex );

	// ノードの種類を変える（子は保つ。子を持つノードを葉にはできない）。変えられたら true
	bool ChangeNodeType( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTNodeType Type );

	// ノードを、新しい Selector / Sequence / Parallel で包む（そのノードが、新しいノードの唯一の子になる）。新しいノードの番号を返す
	int32 WrapNode( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTNodeType CompositeType );

	void SetNodeName( TBehaviorTreeDocument& Document, int32 NodeIndex, const FString& Name, const char* CoalesceKey = nullptr );
	// 葉の Task の呼び出し（葉以外には効かない）
	void SetNodeTask( TBehaviorTreeDocument& Document, int32 NodeIndex, const FAICall& Call, const char* CoalesceKey = nullptr );
	void SetParallelMode( TBehaviorTreeDocument& Document, int32 NodeIndex, EBTParallelMode Mode );

	// デコレーター
	int32 AddDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, const FBTDecoratorDef& Decorator );
	void RemoveDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index );
	void MoveDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 From, int32 To );
	void SetDecorator( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index, const FBTDecoratorDef& Decorator, const char* CoalesceKey = nullptr );

	// サービス
	int32 AddService( TBehaviorTreeDocument& Document, int32 NodeIndex, const FBTServiceDef& Service );
	void RemoveService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index );
	void MoveService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 From, int32 To );
	void SetService( TBehaviorTreeDocument& Document, int32 NodeIndex, int32 Index, const FBTServiceDef& Service, const char* CoalesceKey = nullptr );

	// 部分木のコピー・貼り付け（クリップボードの中身は JSON。.gxbt と同じ書式で、Root がそのノード）
	NlohmannJson CopySubtree( const FBehaviorTreeDefinition& Definition, int32 NodeIndex );
	// 貼り付ける。ツリーが空で Parent が -1 なら、ルートにする。貼り付けたルートの番号を返す（JSON が読めない・置けないなら INDEX_NONE）
	int32 PasteSubtree( TBehaviorTreeDocument& Document, int32 Parent, int32 At, const NlohmannJson& Subtree );

	// ブラックボードのキーを使っているデコレーターの数（BlackboardCondition。部品のパラメータの中は数えない）
	int32 CountBlackboardKeyUsage( const FBehaviorTreeDefinition& Definition, const FString& Key );
	// ブラックボードのキーの改名に合わせて、デコレーターの参照を直す。直した数を返す（0 なら、履歴に積まない）
	int32 RenameBlackboardKey( TBehaviorTreeDocument& Document, const FString& OldName, const FString& NewName );
}

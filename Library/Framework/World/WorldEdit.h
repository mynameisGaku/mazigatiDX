#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Framework/Asset/DefinitionDocument.h"
#include "Library/Framework/World/WorldDefinition.h"

//=============================================================================
// ワールドの編集
//   エディタ（ImGui）に依存しない、編集の操作と履歴。エディタの画面は、ここの関数を呼ぶだけにして、
//   「どう編集されるか」（Id の割り当て・親子の整合・複製での参照の付け替え・元に戻す）はここでテストできる。
//   履歴は TDefinitionDocument（定義の JSON のスナップショット）なので、知らない項目も一緒に戻る。
//
//   TWorldDocument Document;
//   Document.Reset( World );
//   const int32 Torch = NWorldEdit::AddObject( Document, "Torch", WorldNoId, Transform );
//   NWorldEdit::AddTag( Document, Torch, "Light" );
//   NWorldEdit::EditObject( Document, Torch )->Model = FWorldModel{ "Models/Torch.mv1" };   // 細かい項目は、直接書き換える
//   Document.Undo();
//
//   スライダーのドラッグのような連続した変更は、CoalesceKey を渡し続けると 1 回の編集にまとまる（TDefinitionDocument を参照）。
//=============================================================================
using TWorldDocument = TDefinitionDocument<FWorldDefinition>;

namespace NWorldEdit
{
	//-------------------------------------------------------------------------
	// オブジェクト
	//-------------------------------------------------------------------------
	// 新しいオブジェクトを足して、その Id を返す（ParentId の子の、最後に入る）。ParentId が存在しなければ WorldNoId（足さない）
	int32 AddObject( TWorldDocument& Document, const FString& Name, int32 ParentId = WorldNoId, const FWorldTransform& Transform = FWorldTransform() );

	// オブジェクトと、その子孫を消す。Id は二度と使われない。消せたら true
	bool RemoveObject( TWorldDocument& Document, int32 ObjectId );

	// ObjectId の、書き換えてよいオブジェクトを返す（編集の前に履歴へ積む）。見つからなければ nullptr（履歴は積まない）。
	// 親・Id・階層に関わる項目は、ここでは書き換えない（Reparent を使う）
	FWorldObject* EditObject( TWorldDocument& Document, int32 ObjectId, const char* CoalesceKey = nullptr );

	// 親を付け替える。NewParentId が WorldNoId なら最上位。BeforeSiblingId のオブジェクトの手前に入る（WorldNoId なら、最後）。
	// bKeepWorldTransform が true なら、ワールドでの位置・向き・大きさが変わらないように、ローカルの値を直す。
	// 自分や子孫の下へは動かせない（false）。存在しない Id も false
	bool Reparent( TWorldDocument& Document, int32 ObjectId, int32 NewParentId, int32 BeforeSiblingId = WorldNoId, bool bKeepWorldTransform = false );

	// オブジェクトと子孫を複製して、複製の根の Id を返す（元の部分木のすぐ後ろに入る）。複製の中の参照（プロパティの Object）は、複製どうしに付け替える。
	// 見つからなければ WorldNoId
	int32 DuplicateObject( TWorldDocument& Document, int32 ObjectId );

	//-------------------------------------------------------------------------
	// 姿勢
	//-------------------------------------------------------------------------
	// SetWorldTransform が書き換える部分（ギズモで動かしている間に、向きの値を、クォータニオンとの往復で変えてしまわないため）
	struct FWorldTransformMask
	{
		bool bPosition = true;
		bool bRotation = true;
		bool bScale = true;
	};

	// ワールドでの姿勢を指定して書く（親があれば、親の中のローカルの値に直す）。見つからなければ false
	bool SetWorldTransform( TWorldDocument& Document, int32 ObjectId, const FTransform& World, const FWorldTransformMask& Mask = FWorldTransformMask(), const char* CoalesceKey = nullptr );

	//-------------------------------------------------------------------------
	// 複数のオブジェクト
	//   1 回の「元に戻す」にまとまる。Ids の中で、祖先も Ids に入っているものは、祖先と一緒に扱われる（根だけが対象。NWorld::FilterToRoots）。
	//-------------------------------------------------------------------------
	// 消す。消した根の数を返す
	int32 RemoveObjects( TWorldDocument& Document, const TArray<int32>& Ids );

	// 複製する（それぞれ、元のすぐ後ろへ）。複製の根の Id を返す（元の並びの順）
	TArray<int32> DuplicateObjects( TWorldDocument& Document, const TArray<int32>& Ids );

	// 付け替える先の位置: Target の手前 / 後ろ（兄弟として）/ 中（最後の子として）
	enum class EWorldDropPosition : uint8 { Before, After, Inside };

	// Ids を Target に対して Position の位置へ動かせるか（Target が無い・自分や自分の子孫が Target・Ids に根が無い、のは false）
	bool CanMoveObjects( const FWorldDefinition& World, const TArray<int32>& Ids, int32 TargetId, EWorldDropPosition Position );

	// 動かす。ワールドでの位置・向き・大きさは変わらない（親が変わるので、ローカルの値を直す）。選んだ順でなく、配列の順で並ぶ
	bool MoveObjects( TWorldDocument& Document, const TArray<int32>& Ids, int32 TargetId, EWorldDropPosition Position );

	// 空の親を作って、Ids をその子にする（ワールドでの位置は変わらない）。親は、Ids の根の位置の中心に置く。
	// 根が同じ親を持つなら、その親の子（最初の根のあった位置）、そうでなければ最上位に作る。作った親の Id を返す（Ids に根が無ければ WorldNoId）
	int32 GroupObjects( TWorldDocument& Document, const TArray<int32>& Ids, const FString& Name );

	// 部品を持たない親を外す: 子を、親の親へ、親のあった位置に出して、親を消す（ワールドの位置は変わらない）。
	// 部品（モデルなど）を持つオブジェクトと、子のいないオブジェクト（グループではなく、外すと消えてしまう）は false
	bool UngroupObject( TWorldDocument& Document, int32 GroupId );

	//-------------------------------------------------------------------------
	// コピーと貼り付け（別のワールドへも貼れる）
	//-------------------------------------------------------------------------
	struct FWorldClipboard
	{
		// 親が先の順。根の Parent は WorldNoId、根の Transform はワールドでの値。子の Parent と Object プロパティは、コピー元の Id
		TArray<FWorldObject> Objects;

		bool IsEmpty() const { return Objects.IsEmpty(); }
	};

	// Ids の根と、その子孫をコピーする（コピー元は変えない）
	FWorldClipboard CopyObjects( const FWorldDefinition& World, const TArray<int32>& Ids );

	// 貼り付ける。ParentId の子（WorldNoId なら最上位）の最後に入る。ワールドでの位置が、WorldOffset だけずれる。
	// 新しい Id を振り、貼り付けた中の参照（親・Object プロパティ）は貼り付けどうしに付け替える。貼り付けた根の Id を返す。ParentId が存在しなければ空
	TArray<int32> PasteObjects( TWorldDocument& Document, const FWorldClipboard& Clipboard, int32 ParentId = WorldNoId, const FVector3& WorldOffset = FVector3::Zero );

	//-------------------------------------------------------------------------
	// 入れ子のワールド（プレハブ）
	//-------------------------------------------------------------------------
	// Ids の根と子孫を、別の .world にできる定義として取り出す（World は変えない）。Origin（ワールドでの位置）が、取り出した定義の原点になる
	// （根の位置が、Origin からの相対になる）。Id は 1 から振り直す（中の参照は付け替える）。Ids に根が無ければ、空の定義
	FWorldDefinition ExtractSubWorld( const FWorldDefinition& World, const TArray<int32>& Ids, const FVector3& Origin, const FString& Name );

	// Ids の根と子孫を消して、Origin に、Prefab 部品（PrefabPath）を持つ 1 つのオブジェクトを置く。置いたオブジェクトの Id を返す（根が無ければ WorldNoId）。
	// 根がみな同じ親を持つなら、その親の子、そうでなければ最上位に置く。1 回の「元に戻す」
	int32 ReplaceWithPrefab( TWorldDocument& Document, const TArray<int32>& Ids, const FVector3& Origin, const FString& PrefabPath, const FString& Name );

	// Prefab 部品を持つオブジェクトを、Prefab（その .world の定義）の中身で置き換える: 中身のオブジェクトを、このオブジェクトの子として新しい Id で足し
	// （位置・向き・大きさは、ホストからの相対のまま）、Prefab 部品を外す。ホストは残る。足した根の Id を返す。ホストが無い・Prefab 部品が無いなら空。1 回の「元に戻す」
	TArray<int32> UnpackPrefab( TWorldDocument& Document, int32 ObjectId, const FWorldDefinition& Prefab );

	//-------------------------------------------------------------------------
	// タグ・プロパティ
	//-------------------------------------------------------------------------
	// 空・すでにある場合は false（履歴は積まない）
	bool AddTag( TWorldDocument& Document, int32 ObjectId, const FString& Tag );
	bool RemoveTag( TWorldDocument& Document, int32 ObjectId, const FString& Tag );
	// 同じ名前があれば置き換える
	bool SetProp( TWorldDocument& Document, int32 ObjectId, const FWorldProp& Prop, const char* CoalesceKey = nullptr );
	bool RemoveProp( TWorldDocument& Document, int32 ObjectId, const FString& PropName );

	//-------------------------------------------------------------------------
	// 当たり判定のタグ規則
	//-------------------------------------------------------------------------
	// 規則を最後に足す。同じタグの規則がすでにあれば、その規則の指定を入れ替える（規則の番号を返す）。タグが空なら INDEX_NONE
	int32 SetCollisionRule( TWorldDocument& Document, const FString& Tag, bool bCollide );
	bool RemoveCollisionRule( TWorldDocument& Document, int32 RuleIndex );
	// 順番を入れ替える（最初に一致した規則だけが使われるので、順番に意味がある）
	bool MoveCollisionRule( TWorldDocument& Document, int32 From, int32 To );
}

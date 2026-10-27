#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Json/JsonObject.h"

//=============================================================================
// データアセット（独自の JSON）の編集中の入れ物と、元に戻す / やり直す履歴
//   TDefinition は、ToJson() const と static bool FromJson( const NlohmannJson&, TDefinition& ) を持つ定義の型
//   （FSpriteSheetDefinition / FModelAnimationDefinition / NWorld::FWorldDefinition など）。
//   エディタ（ImGui）には依存しない。
//=============================================================================
//-----------------------------------------------------------------------------
// TDefinitionDocument
//   編集中の定義と、元に戻す / やり直す履歴、未保存の印。
//   履歴は、編集の前の定義を JSON にしたもの（定義の ToJson / FromJson をそのまま使うので、項目が増えても履歴は壊れない）。
//
//   TSpriteSheetDocument Document;
//   Document.Reset( Definition );
//   Document.BeginEdit().Clips.Add( Clip );        // 編集の前に履歴へ積まれる
//   Document.Undo();
//
//   スライダーのドラッグのような連続した変更は、同じ Key を渡し続けると 1 回の編集にまとまる
//   （EndEditSession で区切る。ImGui なら、どの項目も操作されていないフレームで呼ぶ）。
//
//   いくつもの編集操作を 1 回の「元に戻す」にまとめたいとき（複数のオブジェクトの削除など）は、FBatch で囲む。
//   中で何回 BeginEdit しても、履歴に積まれるのは最初の 1 回だけ。
//   {
//       TSpriteSheetDocument::FBatch Batch( Document );
//       NSpriteSheetEdit::RemoveClip( Document, 3 );
//       NSpriteSheetEdit::RemoveClip( Document, 1 );
//   }
//-----------------------------------------------------------------------------
template<typename TDefinition>
class TDefinitionDocument
{
public:
	const TDefinition& Get() const { return Definition; }

	// 履歴を消して、定義を入れ替える。保存済み（未保存の印なし）として扱う
	void Reset( const TDefinition& InDefinition )
	{
		Definition = InDefinition;
		UndoStack.Reset();
		RedoStack.Reset();
		LastKey.clear();
		SavedSnapshot = Snapshot();
		bDirty = false;
		++Revision;
	}

	// 編集を始める。現在の定義を履歴に積み、書き換えてよい定義を返す。
	// Key が前回と同じで、EndEditSession されていなければ、積まずにそのまま返す（連続した変更を 1 つの編集にまとめる）
	TDefinition& BeginEdit( const char* Key = nullptr )
	{
		if ( BatchDepth > 0 )
		{
			// まとめ編集の中: 最初の 1 回だけ履歴に積む（Key でのまとめ方は使わない）
			if ( !bBatchStarted )
			{
				PushUndoSnapshot();
				bBatchStarted = true;
			}
			LastKey.clear();
			bDirty = true;
			++Revision;
			return Definition;
		}
		const bool bContinue = Key != nullptr && !LastKey.empty() && LastKey == Key;
		if ( !bContinue ) PushUndoSnapshot();
		LastKey = Key != nullptr ? Key : "";
		bDirty = true;
		++Revision;
		return Definition;
	}
	void EndEditSession() { LastKey.clear(); }

	// 複数の編集を 1 回にまとめる（入れ子にできる。いちばん外側を抜けたところで区切る）。FBatch を使うと、抜け忘れない
	void BeginBatch()
	{
		if ( BatchDepth == 0 )
		{
			bBatchStarted = false;
			LastKey.clear();
		}
		++BatchDepth;
	}
	void EndBatch()
	{
		if ( BatchDepth > 0 && --BatchDepth == 0 )
		{
			bBatchStarted = false;
			LastKey.clear();
		}
	}

	class FBatch
	{
	public:
		explicit FBatch( TDefinitionDocument& InDocument ) : Document( InDocument ) { Document.BeginBatch(); }
		~FBatch() { Document.EndBatch(); }
		FBatch( const FBatch& ) = delete;
		FBatch& operator=( const FBatch& ) = delete;

	private:
		TDefinitionDocument& Document;
	};

	bool CanUndo() const { return !UndoStack.IsEmpty(); }
	bool CanRedo() const { return !RedoStack.IsEmpty(); }

	bool Undo()
	{
		if ( UndoStack.IsEmpty() || BatchDepth > 0 ) return false;
		RedoStack.Add( Snapshot() );
		Restore( UndoStack.Last() );
		UndoStack.Pop();
		return true;
	}

	bool Redo()
	{
		if ( RedoStack.IsEmpty() || BatchDepth > 0 ) return false;
		UndoStack.Add( Snapshot() );
		Restore( RedoStack.Last() );
		RedoStack.Pop();
		return true;
	}

	// 最後の編集を元に戻して、やり直しにも残さない（編集の途中でやめたとき。ドラッグを Esc で取り消すなど）
	bool UndoAndForget()
	{
		if ( UndoStack.IsEmpty() || BatchDepth > 0 ) return false;
		Restore( UndoStack.Last() );
		UndoStack.Pop();
		return true;
	}

	// 未保存の変更があるか（元に戻して保存した状態に戻れば false）
	bool IsDirty() const { return bDirty; }
	void MarkSaved()
	{
		SavedSnapshot = Snapshot();
		bDirty = false;
	}
	// 編集・元に戻す・やり直すのたびに増える（表示のキャッシュを作り直す目印）
	uint32 GetRevision() const { return Revision; }
	int32 GetNumUndo() const { return UndoStack.Num(); }

	static constexpr size_t MaxHistory = 200;

private:
	NJson::FJsonString Snapshot() const { return Definition.ToJson().dump(); }

	void PushUndoSnapshot()
	{
		UndoStack.Add( Snapshot() );
		if ( UndoStack.Num() > MaxHistory ) UndoStack.RemoveAt( 0 );
		RedoStack.Reset();
	}

	void Restore( const NJson::FJsonString& Json )
	{
		TDefinition Restored;
		if ( TDefinition::FromJson( NlohmannJson::parse( Json, nullptr, false ), Restored ) ) Definition = Restored;
		LastKey.clear();
		bDirty = Snapshot() != SavedSnapshot;
		++Revision;
	}

	TDefinition Definition;
	TArray<NJson::FJsonString> UndoStack;
	TArray<NJson::FJsonString> RedoStack;
	NJson::FJsonString LastKey;
	NJson::FJsonString SavedSnapshot;
	int32 BatchDepth = 0;
	bool bBatchStarted = false;
	bool bDirty = false;
	uint32 Revision = 0;
};

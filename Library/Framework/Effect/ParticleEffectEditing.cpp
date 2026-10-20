// SPDX-License-Identifier: NOASSERTION
#include "ParticleEffectEditing.h"

#include "Library/Framework/Asset/DefinitionEditing.h"

namespace
{
	// 一意の名前を決めるため、文書内の名前だけを集める。
	TArray<FString> EmitterNames( const TParticleEffectDocument& Document )
	{
		// 既存の名前の一覧。
		TArray<FString> Names;
		// 登録順にあるエミッターから名前を取り出す。
		for ( const FParticleEmitterDefinition& Emitter : Document.Get().Emitters ) Names.Add( Emitter.Name );
		return Names;
	}

	// 丸めを伴う JSON を使わず、エミッターの全項目を比較するための識別値を求める。
	uint64 EmitterHash( const FParticleEmitterDefinition& Emitter )
	{
		// 比較対象だけを入れた一時定義。
		FParticleEffectDefinition Definition;
		Definition.Emitters.Add( Emitter );
		return Definition.ComputeHash();
	}

	// 再生内容に関わる全体設定から同値判定用の識別値を求める。表示名は呼び出し側で直接比べる。
	uint64 PropertiesHash( float32 Duration, bool bLoop, uint32 Seed )
	{
		// エミッターを含まない一時定義。
		FParticleEffectDefinition Definition;
		Definition.Duration = Duration;
		Definition.bLoop = bLoop;
		Definition.Seed = Seed;
		return Definition.ComputeHash();
	}
}

int32 NParticleEdit::AddEmitter( TParticleEffectDocument& Document, const FString& Name )
{
	if ( Document.Get().Emitters.Num() >= FParticleEffectDefinition::MaxEmitters ) return INDEX_NONE;
	// 既存の名前と重ならない、新しいエミッター。
	FParticleEmitterDefinition Emitter;
	Emitter.Name = NDefinitionEdit::MakeUniqueName( EmitterNames( Document ), Name, "Emitter" );
	return Document.BeginEdit().Emitters.Add( Emitter );
}

int32 NParticleEdit::DuplicateEmitter( TParticleEffectDocument& Document, int32 Index )
{
	if ( !Document.Get().Emitters.IsValidIndex( Index ) || Document.Get().Emitters.Num() >= FParticleEffectDefinition::MaxEmitters ) return INDEX_NONE;
	// 編集開始前に内容をコピーし、配列の移動で参照が失われないようにする。
	FParticleEmitterDefinition Emitter = Document.Get().Emitters[Index];
	Emitter.Name = NDefinitionEdit::MakeUniqueName( EmitterNames( Document ), Emitter.Name, "Emitter" );
	Document.BeginEdit().Emitters.Insert( Emitter, Index + 1 );
	return Index + 1;
}

bool NParticleEdit::RemoveEmitter( TParticleEffectDocument& Document, int32 Index )
{
	if ( !Document.Get().Emitters.IsValidIndex( Index ) ) return false;
	Document.BeginEdit().Emitters.RemoveAt( Index );
	return true;
}

bool NParticleEdit::MoveEmitter( TParticleEffectDocument& Document, int32 From, int32 To )
{
	if ( !Document.Get().Emitters.IsValidIndex( From ) || !Document.Get().Emitters.IsValidIndex( To ) || From == To ) return false;
	// 移すエミッターの内容を、削除前に保存する。
	const FParticleEmitterDefinition Moving = Document.Get().Emitters[From];
	// 1 回の取り消しで戻せる、並べ替え対象の配列。
	TArray<FParticleEmitterDefinition>& Emitters = Document.BeginEdit().Emitters;
	Emitters.RemoveAt( From );
	Emitters.Insert( Moving, To );
	return true;
}

bool NParticleEdit::SetEmitter( TParticleEffectDocument& Document, int32 Index, const FParticleEmitterDefinition& Emitter, const char* Key )
{
	if ( !Document.Get().Emitters.IsValidIndex( Index ) ) return false;
	// 一括生成の順番だけを正規化し、範囲や数値は入力どおり保つ。
	FParticleEmitterDefinition Updated = Emitter;
	Updated.SortBursts();
	if ( EmitterHash( Document.Get().Emitters[Index] ) == EmitterHash( Updated ) ) return false;
	Document.BeginEdit( Key ).Emitters[Index] = Updated;
	return true;
}

bool NParticleEdit::SetProperties( TParticleEffectDocument& Document, const FString& Name, float32 Duration, bool bLoop, uint32 Seed, const char* Key )
{
	// 比較に使う、編集前の定義。
	const FParticleEffectDefinition& Current = Document.Get();
	if ( Current.Name == Name && PropertiesHash( Current.Duration, Current.bLoop, Current.Seed ) == PropertiesHash( Duration, bLoop, Seed ) ) return false;
	// エミッターや形式の版には触れず、全体の設定だけを更新する。
	FParticleEffectDefinition& Updated = Document.BeginEdit( Key );
	Updated.Name = Name;
	Updated.Duration = Duration;
	Updated.bLoop = bLoop;
	Updated.Seed = Seed;
	return true;
}

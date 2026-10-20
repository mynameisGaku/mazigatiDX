// SPDX-License-Identifier: NOASSERTION
#include "Library/Framework/Effect/ParticleEffectEditing.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// 順番と隣の項目の保持を確かめる、名前と生成速度の異なる 3 個の定義を作る。
	FParticleEffectDefinition MakeEditingDefinition()
	{
		// 保存状態との比較に使う初期定義。
		FParticleEffectDefinition Definition;
		Definition.Name = "Editing";
		// 並べ替え後も識別できるエミッターの番号。
		for ( int32 Index = 0; Index < 3; ++Index )
		{
			// 番号ごとに名前と速度を変えたエミッター。
			FParticleEmitterDefinition Emitter;
			Emitter.Name = FString::Printf( "Emitter %d", Index + 1 );
			Emitter.Rate = static_cast<float32>( Index + 1 );
			Definition.Emitters.Add( Emitter );
		}
		return Definition;
	}

	// 追加・複製・削除が内容と履歴を保ち、名前の重複を避けることを確認する。
	void TestEmitterStructure( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-structure";
		// 空から作り始める文書。
		TParticleEffectDocument Document;
		Document.Reset( FParticleEffectDefinition() );
		C.Check( NParticleEdit::AddEmitter( Document, "  Smoke  " ) == 0 && Document.Get().Emitters[0].Name == "Smoke", "追加した名前の前後の空白を除き、追加位置を返す" );
		C.Check( NParticleEdit::AddEmitter( Document, "Smoke" ) == 1 && Document.Get().Emitters[1].Name == "Smoke 2", "同じ名前の追加には番号を付ける" );
		C.Check( NParticleEdit::AddEmitter( Document, "  " ) == 2 && Document.Get().Emitters[2].Name == "Emitter" && Document.GetNumUndo() == 3 && Document.IsDirty(), "空の名前には既定名を使い、追加ごとに 1 回の履歴を作る" );

		// 全項目を持つエミッターを含めた、複製前の定義。
		FParticleEffectDefinition Original = MakeEditingDefinition();
		Original.Emitters[0] = NEffectTest::FullEmitter();
		Document.Reset( Original );
		C.Check( NParticleEdit::DuplicateEmitter( Document, 0 ) == 1 && Document.Get().Emitters.Num() == 4 && Document.Get().Emitters[1].Name == "Sparks 2", "複製は元の直後に挿入し、重ならない名前を付ける" );
		// コピー元の全項目と、コピー後に変える名前だけで作った期待値。
		FParticleEffectDefinition Duplicated = Original;
		Duplicated.Emitters.Insert( Original.Emitters[0], 1 );
		Duplicated.Emitters[1].Name = "Sparks 2";
		C.Check( Document.Get().ComputeHash() == Duplicated.ComputeHash() && Document.GetNumUndo() == 1, "複製では全項目と前後のエミッターを保ち、履歴は 1 回だけ作る" );
		C.Check( Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash() && !Document.IsDirty(), "複製の取り消しで、元の定義と保存状態へ戻る" );
		C.Check( Document.Redo() && Document.Get().ComputeHash() == Duplicated.ComputeHash() && Document.IsDirty(), "複製のやり直しで、内容と未保存の状態が戻る" );
		C.Check( NParticleEdit::RemoveEmitter( Document, 1 ) && Document.Get().ComputeHash() == Original.ComputeHash() && Document.GetNumUndo() == 2, "削除した位置だけを詰め、ほかのエミッターを保つ" );
		C.Check( Document.Undo() && Document.Get().ComputeHash() == Duplicated.ComputeHash(), "削除を取り消すと、同じ位置に全項目が戻る" );
		C.Check( Document.Redo() && Document.Get().ComputeHash() == Original.ComputeHash() && !Document.IsDirty(), "削除のやり直しで、保存した内容に一致すれば未保存の印も消える" );

		Document.Reset( FParticleEffectDefinition() );
		C.Check( NParticleEdit::AddEmitter( Document ) == 0 && NParticleEdit::DuplicateEmitter( Document, 0 ) == 1 && NParticleEdit::DuplicateEmitter( Document, 0 ) == 1 && Document.Get().Emitters[1].Name == "Emitter 3" && Document.Get().Emitters[2].Name == "Emitter 2", "追加時の既定名と、繰り返し複製時の名前の重複回避を確認する" );
	}

	// 無効な指定と上限到達では、定義・履歴・保存状態が変わらないことを確認する。
	void TestInvalidAndLimits( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-limits";
		// 不正な添字を渡す前の定義。
		const FParticleEffectDefinition Original = MakeEditingDefinition();
		// 同じ文書へ失敗する操作を続ける。
		TParticleEffectDocument Document;
		Document.Reset( Original );
		// 操作に使う、全項目入りの置き換え候補。
		const FParticleEmitterDefinition Emitter = NEffectTest::FullEmitter();
		// 失敗だけでは変わらないはずの変更番号。
		const uint32 Revision = Document.GetRevision();
		C.Check( NParticleEdit::DuplicateEmitter( Document, -1 ) == INDEX_NONE && NParticleEdit::DuplicateEmitter( Document, 3 ) == INDEX_NONE && !NParticleEdit::RemoveEmitter( Document, -1 ) && !NParticleEdit::RemoveEmitter( Document, 3 ), "複製と削除は負の番号と末尾の外の番号を拒否する" );
		C.Check( !NParticleEdit::MoveEmitter( Document, -1, 0 ) && !NParticleEdit::MoveEmitter( Document, 0, -1 ) && !NParticleEdit::MoveEmitter( Document, 3, 0 ) && !NParticleEdit::MoveEmitter( Document, 0, 3 ) && !NParticleEdit::MoveEmitter( Document, 1, 1 ), "移動元・移動先が範囲外、または同じ番号なら変更しない" );
		C.Check( !NParticleEdit::SetEmitter( Document, -1, Emitter ) && !NParticleEdit::SetEmitter( Document, 3, Emitter ) && Document.Get().ComputeHash() == Original.ComputeHash() && Document.GetNumUndo() == 0 && !Document.IsDirty() && Document.GetRevision() == Revision, "範囲外への置き換えも含め、失敗時には定義・履歴・変更番号・保存状態を保つ" );

		Document.Reset( FParticleEffectDefinition() );
		// 上限までの追加が、連続した位置を返したか。
		bool bAddedAll = true;
		// 上限ぴったりまで追加する番号。
		for ( int32 Index = 0; Index < FParticleEffectDefinition::MaxEmitters; ++Index ) bAddedAll = bAddedAll && NParticleEdit::AddEmitter( Document ) == Index;
		C.Check( bAddedAll && Document.Get().Emitters.Num() == FParticleEffectDefinition::MaxEmitters, "エミッター数が上限になるまでは追加できる" );
		Document.MarkSaved();
		// 上限到達時の内容。
		const uint64 FullHash = Document.Get().ComputeHash();
		// 上限到達時の履歴数。
		const int32 FullHistory = Document.GetNumUndo();
		// 上限到達時の変更番号。
		const uint32 FullRevision = Document.GetRevision();
		C.Check( NParticleEdit::AddEmitter( Document, "Too many" ) == INDEX_NONE && NParticleEdit::DuplicateEmitter( Document, 0 ) == INDEX_NONE && Document.Get().ComputeHash() == FullHash && Document.GetNumUndo() == FullHistory && Document.GetRevision() == FullRevision && !Document.IsDirty(), "上限到達後の追加・複製は、内容と保存状態を変えずに拒否する" );
		C.Check( NParticleEdit::RemoveEmitter( Document, 0 ) && NParticleEdit::AddEmitter( Document, "Replacement" ) == FParticleEffectDefinition::MaxEmitters - 1 && Document.Get().Emitters.Last().Name == "Replacement", "削除で空きができれば、上限まで再び追加できる" );
	}

	// 並べ替えの行き先と、逆操作で内容が戻ることを確認する。
	void TestMove( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-move";
		// 3 個が異なる内容を持つ、並べ替え前の定義。
		const FParticleEffectDefinition Original = MakeEditingDefinition();
		// 並べ替える文書。
		TParticleEffectDocument Document;
		Document.Reset( Original );
		C.Check( NParticleEdit::MoveEmitter( Document, 0, 2 ) && Document.Get().Emitters[0].Name == "Emitter 2" && Document.Get().Emitters[1].Name == "Emitter 3" && Document.Get().Emitters[2].Name == "Emitter 1" && Document.Get().Emitters[0].Rate == 2.0f && Document.Get().Emitters[2].Rate == 1.0f && Document.GetNumUndo() == 1, "先頭から末尾への移動は、内容を保ったまま指定番号へ置く" );
		// やり直し後との比較に使う並べ替え後の内容。
		const uint64 MovedHash = Document.Get().ComputeHash();
		C.Check( Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash() && !Document.IsDirty(), "並べ替えの取り消しで順番と保存状態が戻る" );
		C.Check( Document.Redo() && Document.Get().ComputeHash() == MovedHash, "並べ替えのやり直しで順番が再現される" );
		C.Check( NParticleEdit::MoveEmitter( Document, 2, 0 ) && Document.Get().ComputeHash() == Original.ComputeHash() && Document.GetNumUndo() == 2, "末尾から先頭への移動も、指定番号に戻せる" );
	}

	// 全項目の置き換えと時刻順の正規化が、ほかの項目を変えないことを確認する。
	void TestSetEmitter( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-emitter";
		// 隣のエミッターの保持を確かめる初期定義。
		const FParticleEffectDefinition Original = MakeEditingDefinition();
		// 置き換える文書。
		TParticleEffectDocument Document;
		Document.Reset( Original );
		// 生成の時刻だけを逆順にした、全項目入りの入力。
		FParticleEmitterDefinition Input = NEffectTest::FullEmitter();
		Input.Name = "Emitter 1";
		Input.Rate = 12.345678f;
		Input.Bursts.Reset();
		Input.Bursts.Add( NEffectTest::Burst( 1.5f, 3 ) );
		Input.Bursts.Add( NEffectTest::Burst( 0.5f, 7 ) );
		// 時刻順だけを直した期待値。
		FParticleEffectDefinition Expected = Original;
		Expected.Emitters[1] = Input;
		Expected.Emitters[1].Bursts[0] = NEffectTest::Burst( 0.5f, 7 );
		Expected.Emitters[1].Bursts[1] = NEffectTest::Burst( 1.5f, 3 );
		C.Check( NParticleEdit::SetEmitter( Document, 1, Input ) && Document.Get().ComputeHash() == Expected.ComputeHash() && Document.Get().Emitters[1].Rate == 12.345678f && Document.Get().Emitters[1].Name == "Emitter 1", "全項目を置き換え、名前や小数を勝手に変えず、一括生成だけを時刻順にする" );
		C.Check( Input.Bursts[0].Time == 1.5f && Input.Bursts[1].Time == 0.5f && Document.Get().Emitters[0].Rate == 1.0f && Document.Get().Emitters[2].Rate == 3.0f, "入力の順番と、選択していないエミッターを変更しない" );
		// 同値入力では変わらないはずの変更番号。
		const uint32 Revision = Document.GetRevision();
		C.Check( !NParticleEdit::SetEmitter( Document, 1, Input ) && Document.GetNumUndo() == 1 && Document.GetRevision() == Revision, "時刻順に直すと同値になる入力では、履歴と変更番号を増やさない" );
		Input.Rate += 0.000001f;
		C.Check( NParticleEdit::SetEmitter( Document, 1, Input ) && Document.Get().Emitters[1].Rate == Input.Rate && Document.GetNumUndo() == 2, "JSON の丸めで消える小さな差も、編集として保持する" );

		Document.Reset( Original );
		C.Check( NParticleEdit::SetEmitter( Document, 0, Document.Get().Emitters[2] ) && Document.Get().Emitters[0].Rate == 3.0f && Document.Get().Emitters[2].Rate == 3.0f, "同じ文書内の参照を入力しても、コピー元を壊さず置き換えられる" );
	}

	// 履歴の往復で小数の精度と、同時刻の一括生成の順番を保つことを確認する。
	void TestPrecisionAndBurstOrder( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-precision";
		// 小数第 4 位より後にも意味のある値を持つ保存済みの定義。
		FParticleEffectDefinition Original = MakeEditingDefinition();
		Original.Duration = 0.1234567f;
		Original.Emitters[0].Rate = 12.345678f;
		Original.Emitters[0].Gravity = FVector3( 0.1234567f, -9.876543f, 0.2345678f );
		Original.Emitters[0].Lifetime = { 0.1234567f, 1.987654f };
		Original.Emitters[1].BoxSize = FVector3( 0.3456789f, 2.3456789f, 3.456789f );
		// 名前だけの変更で、関係のない小数が変わらないか確かめる文書。
		TParticleEffectDocument Document;
		Document.Reset( Original );
		C.Check( NParticleEdit::SetProperties( Document, "Precision", Original.Duration, Original.bLoop, Original.Seed ) && Document.Undo() && Document.Get().Name == Original.Name && Document.Get().ComputeHash() == Original.ComputeHash() && Document.Get().Duration == Original.Duration && Document.Get().Emitters[0].Rate == Original.Emitters[0].Rate && Document.Get().Emitters[0].Gravity.y == Original.Emitters[0].Gravity.y && Document.Get().Emitters[0].Lifetime.Max == Original.Emitters[0].Lifetime.Max && !Document.IsDirty(), "名前だけの編集を取り消しても、無関係な継続秒・生成速度・重力・範囲の精度を失わず保存状態へ戻る" );
		C.Check( Document.Redo() && Document.Get().Name == "Precision" && Document.Get().ComputeHash() == Original.ComputeHash() && Document.Get().Emitters[1].BoxSize.z == Original.Emitters[1].BoxSize.z && Document.IsDirty(), "名前だけの編集のやり直しでも、選択していないエミッターまで同じ小数を保つ" );

		C.Group = "effect-edit-stable-bursts";
		// 一般の並べ替えで同順位の順番が入れ替わりやすい、20 個の同時刻の一括生成。
		FParticleEmitterDefinition Emitter = Original.Emitters[0];
		Emitter.Bursts.Reset();
		// 順番を Count で識別できる、同時刻の生成項目。
		for ( int32 Index = 0; Index < 20; ++Index ) Emitter.Bursts.Add( NEffectTest::Burst( 0.125f, Index + 1 ) );
		Original.Emitters[0] = Emitter;
		Document.Reset( Original );
		Emitter.Bursts[8].Count = 99;
		C.Check( NParticleEdit::SetEmitter( Document, 0, Emitter ), "同時刻の一括生成を 20 個持つエミッターの、1 個の生成数だけを変える" );
		// 変更した 1 個以外は、元の番号のまま並んでいるか。
		bool bStable = Document.Get().Emitters[0].Bursts.Num() == 20;
		// 各項目の時刻と、その位置にあるべき生成数を確かめる番号。
		for ( int32 Index = 0; bStable && Index < 20; ++Index ) bStable = Document.Get().Emitters[0].Bursts[Index].Time == 0.125f && Document.Get().Emitters[0].Bursts[Index].Count == ( Index == 8 ? 99 : Index + 1 );
		C.Check( bStable && Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash(), "同時刻の項目の順番を変えず、取り消しても元の順番を復元する" );
	}

	// 連続編集を 1 回の履歴にまとめ、保存済みの値へ戻せることを確認する。
	void TestHistoryAndProperties( NAssetTest::FChecker& C )
	{
		C.Group = "effect-edit-history";
		// 履歴の始点となる保存済みの定義。
		const FParticleEffectDefinition Original = MakeEditingDefinition();
		// 連続編集を行う文書。
		TParticleEffectDocument Document;
		Document.Reset( Original );
		// 生成速度のドラッグ操作に相当する編集値。
		FParticleEmitterDefinition Emitter = Original.Emitters[0];
		Emitter.Rate = 4.0f;
		C.Check( NParticleEdit::SetEmitter( Document, 0, Emitter, "Rate" ), "生成速度の連続編集を開始する" );
		Emitter.Rate = 5.0f;
		C.Check( NParticleEdit::SetEmitter( Document, 0, Emitter, "Rate" ) && Document.GetNumUndo() == 1, "同じ識別名で続く変更は 1 回の履歴にまとめる" );
		Document.EndEditSession();
		Emitter.Rate = 6.0f;
		C.Check( NParticleEdit::SetEmitter( Document, 0, Emitter, "Rate" ) && Document.GetNumUndo() == 2, "連続編集を終えた後の変更は、別の履歴にする" );
		C.Check( Document.Undo() && Document.Get().Emitters[0].Rate == 5.0f && Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash() && !Document.IsDirty(), "2 回の取り消しで、連続編集前の定義と保存状態へ戻る" );
		C.Check( Document.Redo() && Document.Get().Emitters[0].Rate == 5.0f && Document.Redo() && Document.Get().Emitters[0].Rate == 6.0f && Document.IsDirty(), "やり直しは連続編集の最後の値を復元する" );

		Document.Reset( Original );
		// 同値の全体設定では変えない変更番号。
		const uint32 Revision = Document.GetRevision();
		C.Check( !NParticleEdit::SetProperties( Document, Original.Name, Original.Duration, Original.bLoop, Original.Seed ) && Document.GetNumUndo() == 0 && Document.GetRevision() == Revision && !Document.IsDirty(), "同値の全体設定では履歴・変更番号・保存状態を変えない" );
		C.Check( NParticleEdit::SetProperties( Document, "Renamed", Original.Duration, Original.bLoop, Original.Seed ) && Document.Get().Name == "Renamed" && Document.GetNumUndo() == 1 && Document.IsDirty(), "名前だけの変更も反映し、履歴と未保存の印を作る" );
		// 同名の再入力では増やさない変更番号。
		const uint32 NameRevision = Document.GetRevision();
		C.Check( !NParticleEdit::SetProperties( Document, "Renamed", Original.Duration, Original.bLoop, Original.Seed ) && Document.GetNumUndo() == 1 && Document.GetRevision() == NameRevision, "同じ名前の再入力では履歴と変更番号を増やさない" );
		C.Check( Document.Undo() && Document.Get().Name == Original.Name && !Document.IsDirty() && Document.Redo() && Document.Get().Name == "Renamed" && Document.IsDirty(), "名前だけの変更も取り消しとやり直しで保存状態ごと復元できる" );
		Document.Reset( Original );
		C.Check( NParticleEdit::SetProperties( Document, "  Edited  ", -0.1234567f, false, 0u ) && Document.Get().Name == "  Edited  " && Document.Get().Duration == -0.1234567f && !Document.Get().bLoop && Document.Get().Seed == 0u && Document.Get().Version == Original.Version && Document.Get().Emitters.Num() == 3 && Document.Get().Emitters[0].Rate == 1.0f && Document.Get().Emitters[2].Rate == 3.0f, "全体設定は名前・継続秒・繰り返し・シードだけを変え、入力値を丸めず版とエミッターを保つ" );
		C.Check( Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash() && !Document.IsDirty(), "全体設定も 1 回の取り消しで元へ戻せる" );

		Document.Reset( Original );
		C.Check( NParticleEdit::SetProperties( Document, "Changed", 0.5f, false, 7u, "Properties" ) && NParticleEdit::SetProperties( Document, "Changed", 1.5f, false, 7u, "Properties" ) && Document.GetNumUndo() == 1, "全体設定の連続変更も 1 回の履歴にまとまる" );
		C.Check( Document.Undo() && Document.Get().ComputeHash() == Original.ComputeHash() && Document.Redo() && Document.Get().Name == "Changed" && Document.Get().Duration == 1.5f && !Document.Get().bLoop && Document.Get().Seed == 7u, "全体設定の取り消しとやり直しは、連続編集の最後の値を復元する" );
		Document.MarkSaved();
		C.Check( !Document.IsDirty() && NParticleEdit::SetProperties( Document, "Changed again", 1.5f, false, 7u ) && Document.IsDirty() && Document.Undo() && !Document.IsDirty() && Document.Get().Name == "Changed" && Document.Redo() && Document.IsDirty(), "保存した状態へ取り消せば未保存の印が消え、やり直せば再び付く" );
	}
}

// シーンに依存しないエフェクト定義の編集・履歴・上限を確認する。
void NWorldTest::RunEffectEditingTests( NAssetTest::FChecker& C )
{
	TestEmitterStructure( C );
	TestInvalidAndLimits( C );
	TestMove( C );
	TestSetEmitter( C );
	TestPrecisionAndBurstOrder( C );
	TestHistoryAndProperties( C );
}

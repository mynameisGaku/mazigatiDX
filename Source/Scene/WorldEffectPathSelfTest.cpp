// SPDX-License-Identifier: NOASSERTION
#include "Library/Framework/Effect/EffectPathGeometry.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// 中心線の全項目が同じかを、余白のバイトに依存せず確認する。
	bool SameGeometry( const FEffectPathGeometry& A, const FEffectPathGeometry& B )
	{
		if ( A.Strips.Num() != B.Strips.Num() ) return false;
		// 一本ずつ比較する中心線の番号。
		for ( int32 Strip = 0; Strip < A.Strips.Num(); ++Strip )
		{
			if ( A.Strips[Strip].Points.Num() != B.Strips[Strip].Points.Num() ) return false;
			// 中心線の中で比較する点の番号。
			for ( int32 Index = 0; Index < A.Strips[Strip].Points.Num(); ++Index )
			{
				// 最初の形状から取った点。
				const FEffectPathGeometry::FPoint& Left = A.Strips[Strip].Points[Index];
				// 二つ目の形状から取った対応する点。
				const FEffectPathGeometry::FPoint& Right = B.Strips[Strip].Points[Index];
				if ( Left.Position != Right.Position || Left.Width != Right.Width || Left.Color != Right.Color || Left.U != Right.U ) return false;
			}
		}
		return true;
	}

	// すべての点が有限で、点数と幅が安全な範囲に収まるかを確かめる。
	bool SafeGeometry( const FEffectPathGeometry& Geometry )
	{
		// 全中心線の点数を足した合計。
		int32 Count = 0;
		// 幅と色を持つ各中心線。
		for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips )
		{
			Count += Strip.Points.Num();
			if ( Strip.Points.Num() < 2 || Strip.Points.Num() > FEffectPathDefinition::MaxSegments + 1 ) return false;
			// 座標・幅・距離を調べる点。
			for ( const FEffectPathGeometry::FPoint& Point : Strip.Points )
			{
				if ( !std::isfinite( Point.Position.x ) || !std::isfinite( Point.Position.y ) || !std::isfinite( Point.Position.z ) || !std::isfinite( Point.Width ) || !std::isfinite( Point.U ) || Point.Width < 0.0f ) return false;
			}
		}
		return Count <= FEffectPathGeometry::MaxPoints && Geometry.Strips.Num() <= FEffectPathDefinition::MaxBranches + 1;
	}

	// 新しい項目の保存と読み込み、既定の互換性と全項目の変更識別を確認する。
	void TestPathData( NAssetTest::FChecker& C )
	{
		C.Group = "effect-path-data";
		// 新項目を設定しない既存形式の定義。
		FParticleEffectDefinition Legacy;
		Legacy.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( !Legacy.ToJson()["Emitters"][0].contains( "Path" ), "既定の None は Path を書かず、既存の定義形式を保つ" );
		// 全項目を既定から変えた、保存対象の表示設定。
		FEffectPathDefinition Full;
		Full.Kind = FEffectPathDefinition::EKind::Lightning;
		Full.End = FVector3( 1.1234567f, 4.2345678f, 2.3456789f );
		Full.Segments = 24;
		Full.Branches = 6;
		Full.Jitter = 0.1234567f;
		Full.BranchLength = 0.654321f;
		Full.RefreshHz = 9.876543f;
		Full.UVRepeat = 2.345678f;
		Full.BreakDistance = 3.456789f;
		// 書き戻しと読み戻しを調べる定義。
		FEffectPathDefinition Back;
		C.Check( FEffectPathDefinition::FromJson( NlohmannJson::parse( Full.ToJson().dump() ), Back ) && Back.ComputeHash() == Full.ComputeHash() && Back.ToJson() == Full.ToJson() && Back.End == Full.End && Back.UVRepeat == Full.UVRepeat, "帯と雷の全項目を、JSON の文字列を経由して精度を失わず往復できる" );
		Legacy.Emitters[0].Path = Full;
		Legacy.Emitters[0].MaxParticles = 64;
		// エミッター経由の往復で、警告も確認する一覧。
		TArray<FString> Warnings;
		// エミッター内へ保存した項目の読み戻し先。
		FParticleEffectDefinition Parsed;
		C.Check( FParticleEffectDefinition::FromJson( NlohmannJson::parse( Legacy.ToJson().dump() ), Parsed, &Warnings ) && Warnings.IsEmpty() && Parsed.ComputeHash() == Legacy.ComputeHash() && Parsed.Emitters[0].Path.ToJson() == Full.ToJson(), "エミッターの Path にも全項目を保存し、定義全体から読み戻せる" );
		Full.Kind = FEffectPathDefinition::EKind::None;
		Legacy.Emitters[0].Path = Full;
		C.Check( Legacy.ToJson()["Emitters"][0].contains( "Path" ) && FParticleEffectDefinition::FromJson( Legacy.ToJson(), Parsed ) && Parsed.Emitters[0].Path.End == Full.End && Parsed.Emitters[0].Path.Kind == FEffectPathDefinition::EKind::None, "None に戻しても、後で使うために変えてある設定を捨てない" );
		Back = Full;
		C.Check( !FEffectPathDefinition::FromJson( NlohmannJson::array(), Back ) && Back.ComputeHash() == Full.ComputeHash(), "オブジェクトでない表示設定は、出力を変えず読み込みに失敗する" );
		Warnings.Reset();
		C.Check( FEffectPathDefinition::FromJson( NlohmannJson::object( { { "Kind", "rIbBoN" } } ), Back, &Warnings ) && Back.Kind == FEffectPathDefinition::EKind::Ribbon && Back.Segments == 32 && Back.RefreshHz == 12.0f && Warnings.IsEmpty(), "表示方式は大文字小文字を区別せず、不足項目には既定値を使う" );
		C.Check( FEffectPathDefinition::FromJson( NlohmannJson::object( { { "Kind", "Unknown" } } ), Back, &Warnings ) && Back.Kind == FEffectPathDefinition::EKind::None && Warnings.Num() == 1, "未知の表示方式は警告を出して None を使う" );

		// 各項目を一つずつ変更する確認項目。
		struct FChange
		{
			// 失敗した項目を識別する名前。
			const char* Name;
			// 一つだけ異なる値へ変える操作。
			std::function<void( FEffectPathDefinition& )> Apply;
		};
		// 種類・終点の各軸・残りのすべての値を対象にする変更。
		const FChange Changes[] = {
			{ "Kind", []( FEffectPathDefinition& D ) { D.Kind = FEffectPathDefinition::EKind::Ribbon; } },
			{ "End.x", []( FEffectPathDefinition& D ) { D.End.x += 0.1f; } },
			{ "End.y", []( FEffectPathDefinition& D ) { D.End.y += 0.1f; } },
			{ "End.z", []( FEffectPathDefinition& D ) { D.End.z += 0.1f; } },
			{ "Segments", []( FEffectPathDefinition& D ) { ++D.Segments; } },
			{ "Branches", []( FEffectPathDefinition& D ) { ++D.Branches; } },
			{ "Jitter", []( FEffectPathDefinition& D ) { D.Jitter += 0.1f; } },
			{ "BranchLength", []( FEffectPathDefinition& D ) { D.BranchLength += 0.1f; } },
			{ "RefreshHz", []( FEffectPathDefinition& D ) { D.RefreshHz += 0.1f; } },
			{ "UVRepeat", []( FEffectPathDefinition& D ) { D.UVRepeat += 0.1f; } },
			{ "BreakDistance", []( FEffectPathDefinition& D ) { D.BreakDistance += 0.1f; } },
		};
		// 変更を識別する前の、表示設定の基準。
		const FEffectPathDefinition Base;
		// 変更を識別する前の、エミッター全体の基準。
		FParticleEffectDefinition Original;
		Original.Emitters.Add( FParticleEmitterDefinition() );
		// 項目ごとの変更が保存内容と両方の識別値に反映されるかを調べる。
		for ( const FChange& Change : Changes )
		{
			// 一つの項目だけを変えたエミッター。
			FParticleEffectDefinition Changed = Original;
			Change.Apply( Changed.Emitters[0].Path );
			C.Check( Changed.Emitters[0].Path.ComputeHash() != Base.ComputeHash() && Changed.ComputeHash() != Original.ComputeHash() && Changed.ToJson() != Original.ToJson(), "表示設定 %s の単独変更を、保存内容と両方の識別値へ反映する", Change.Name );
		}
	}

	// 種類を問わず全項目を検証し、雷の描画数上限も事前に説明することを確認する。
	void TestPathValidation( NAssetTest::FChecker& C )
	{
		C.Group = "effect-path-validation";
		// 既定値が問題ないことを確認する定義。
		FEffectPathDefinition Definition;
		C.Check( Definition.Validate().IsEmpty(), "既定の Path はすべて有効な値を持つ" );
		Definition.Kind = FEffectPathDefinition::EKind::Lightning;
		Definition.Segments = 128;
		Definition.Branches = 16;
		Definition.RefreshHz = 0.0f;
		Definition.Jitter = 0.0f;
		Definition.BranchLength = 0.0f;
		C.Check( Definition.Validate().IsEmpty(), "点数と枝数の上限、および静止・揺れなし・枝なしを許す" );
		// 不正な項目だけを一つずつ作る確認項目。
		struct FInvalid
		{
			// 失敗した項目の説明。
			const char* Name;
			// 一つの項目を不正にする操作。
			std::function<void( FEffectPathDefinition& )> Apply;
		};
		// 各検証項目の境界を越える入力。
		const FInvalid Invalid[] = {
			{ "Kind", []( FEffectPathDefinition& D ) { D.Kind = static_cast<FEffectPathDefinition::EKind>( 99 ); } },
			{ "End", []( FEffectPathDefinition& D ) { D.End.x = std::numeric_limits<float32>::quiet_NaN(); } },
			{ "Segments", []( FEffectPathDefinition& D ) { D.Segments = 129; } },
			{ "Branches", []( FEffectPathDefinition& D ) { D.Branches = 17; } },
			{ "Jitter", []( FEffectPathDefinition& D ) { D.Jitter = -0.1f; } },
			{ "BranchLength", []( FEffectPathDefinition& D ) { D.BranchLength = -0.1f; } },
			{ "RefreshHz", []( FEffectPathDefinition& D ) { D.RefreshHz = -0.1f; } },
			{ "UVRepeat", []( FEffectPathDefinition& D ) { D.UVRepeat = 0.0f; } },
			{ "BreakDistance", []( FEffectPathDefinition& D ) { D.BreakDistance = 0.0f; } },
		};
		// 一つずつの不正が、エミッターのエラーまで届くかを調べる。
		for ( const FInvalid& Case : Invalid )
		{
			// 検証対象を一つだけ持つエフェクト。
			FParticleEffectDefinition Effect;
			Effect.Emitters.Add( FParticleEmitterDefinition() );
			Case.Apply( Effect.Emitters[0].Path );
			C.Check( Effect.Emitters[0].Path.Validate().Num() == 1 && FParticleEffectDefinition::HasErrors( Effect.Validate() ), "Path.%s の不正を単独の問題として検出し、エフェクト全体もエラーにする", Case.Name );
		}
		Definition.End = FVector3::Zero;
		C.Check( !Definition.Validate().IsEmpty(), "雷の終点が始点と同じならエラーにする" );
		// 描画数の負荷制限を確認する雷。
		FParticleEffectDefinition Crowd;
		Crowd.Emitters.Add( FParticleEmitterDefinition() );
		Crowd.Emitters[0].Path.Kind = FEffectPathDefinition::EKind::Lightning;
		Crowd.Emitters[0].MaxParticles = 65;
		C.Check( !FParticleEffectDefinition::HasErrors( Crowd.Validate() ) && Crowd.Validate().Num() == 1 && Crowd.Validate()[0].Message.Contains( "64" ), "64 個を超える雷はエラーにはせず、描画を制限する警告を出す" );
		Crowd.Emitters[0].MaxParticles = 64;
		C.Check( Crowd.Validate().IsEmpty(), "64 個ちょうどの雷では負荷制限の警告を出さない" );
	}

	// 両端・枝の接続・幅・累積距離と、生成の決定性を独立した条件で確認する。
	void TestLightningGeometry( NAssetTest::FChecker& C )
	{
		C.Group = "effect-path-geometry";
		// 主幹と複数の枝を持つ雷の設定。
		FEffectPathDefinition Definition;
		Definition.Kind = FEffectPathDefinition::EKind::Lightning;
		Definition.Segments = 12;
		Definition.Branches = 3;
		// 端点が原点に固定されていないことも確かめる開始位置。
		const FVector3 Start( 3.0f, 4.0f, 5.0f );
		// 入力されたワールド座標をそのまま保つ終了位置。
		const FVector3 End( 3.0f, 9.0f, 5.0f );
		// 主幹では変更せず、枝では先端へ消える色。
		const FColor32 Color( 100, 160, 240, 200 );
		// 最初に生成した中心線。
		FEffectPathGeometry Geometry;
		NEffectPath::BuildLightning( Definition, 77u, 0.0f, Start, End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.Num() == 4 && Geometry.Strips[0].Points.Num() == 13 && Geometry.Strips[0].Points[0].Position == Start && Geometry.Strips[0].Points.Last().Position == End && SafeGeometry( Geometry ), "主幹の両端を厳密に入力位置へ固定し、指定数の枝を有限な点で作る" );
		// 枝の始点と幅が、主幹から連続しているか。
		bool bBranches = Geometry.Strips.Num() == 4;
		// 主幹の途中につながる枝の番号。
		for ( int32 Branch = 1; bBranches && Branch < Geometry.Strips.Num(); ++Branch )
		{
			// 今回の枝の中心線。
			const FEffectPathGeometry::FStrip& Strip = Geometry.Strips[Branch];
			// 始点が主幹の端以外の点と一致したか。
			bool bAttached = false;
			// 枝の付け根に使える、主幹の途中の点。
			for ( int32 Index = 1; Index < Geometry.Strips[0].Points.Num() - 1; ++Index ) bAttached = bAttached || Strip.Points[0].Position == Geometry.Strips[0].Points[Index].Position;
			bBranches = bAttached && Strip.Points[0].Width > 0.0f && Strip.Points[0].Width < 0.4f && Strip.Points.Last().Width == 0.0f && Strip.Points.Last().Color.a == 0;
		}
		C.Check( bBranches, "枝は主幹の途中から隙間なく分かれ、先端で幅と不透明度が 0 になる" );
		// 幅・色・距離が主幹の各点で正しいか。
		bool bMain = !Geometry.Strips.IsEmpty();
		// 主幹の始点から積み上げ直す距離。
		float32 Distance = 0.0f;
		// 主幹の点の見た目と累積距離を確かめる番号。
		for ( int32 Index = 0; bMain && Index < Geometry.Strips[0].Points.Num(); ++Index )
		{
			if ( Index > 0 ) Distance += ( Geometry.Strips[0].Points[Index].Position - Geometry.Strips[0].Points[Index - 1].Position ).Length();
			bMain = Geometry.Strips[0].Points[Index].Width == 0.4f && Geometry.Strips[0].Points[Index].Color == Color && std::fabs( Geometry.Strips[0].Points[Index].U - Distance ) < 1.0e-5f;
		}
		C.Check( bMain && Distance >= 5.0f, "主幹の幅と色を保ち、画像用の U は揺れた線に沿う累積ワールド距離にする" );
		// 同一入力を繰り返して作った中心線。
		FEffectPathGeometry Again;
		NEffectPath::BuildLightning( Definition, 77u, 0.0f, Start, End, 0.4f, Color, Again );
		C.Check( SameGeometry( Geometry, Again ), "同じシード・時刻・両端では、全点と見た目が同じになる" );
		NEffectPath::BuildLightning( Definition, 78u, 0.0f, Start, End, 0.4f, Color, Again );
		C.Check( !SameGeometry( Geometry, Again ) && Again.Strips[0].Points[0].Position == Start && Again.Strips[0].Points.Last().Position == End, "別のシードでは途中の形を変え、主幹の両端は保つ" );
		Definition.UVRepeat = 7.0f;
		NEffectPath::BuildLightning( Definition, 77u, 0.0f, Start, End, 0.4f, Color, Again );
		C.Check( SameGeometry( Geometry, Again ), "UVRepeat は描画側で累積距離へ掛けるため、中心線の U や形状は変えない" );
		Definition.Segments = 24;
		NEffectPath::BuildLightning( Definition, 77u, 0.0f, Start, End, 0.4f, Color, Again );
		// 区間を二倍にした場合、元の各点の位置で形が変わっていないか。
		bool bSameShape = Again.Strips[0].Points.Num() == 25;
		// 低い区間数と共有する、主幹上の位置。
		for ( int32 Index = 0; bSameShape && Index <= 12; ++Index ) bSameShape = Geometry.Strips[0].Points[Index].Position.Equals( Again.Strips[0].Points[Index * 2].Position, 1.0e-6f );
		C.Check( bSameShape, "区間数を増やしても、粗い曲がりと細い折れを持つ元の形の共有点を保つ" );
		Definition.Segments = 12;
		Definition.Jitter = 0.0f;
		Definition.Branches = 0;
		NEffectPath::BuildLightning( Definition, 77u, 0.0f, Start, End, 0.4f, Color, Again );
		// 揺れなしで主幹が厳密に直線へ戻るか。
		bool bStraight = Again.Strips.Num() == 1;
		// 直線上の各区間の番号。
		for ( int32 Index = 0; bStraight && Index <= 12; ++Index ) bStraight = Again.Strips[0].Points[Index].Position.Equals( Start + ( End - Start ) * ( static_cast<float32>( Index ) / 12.0f ), 1.0e-6f );
		C.Check( bStraight, "揺れ 0・枝 0 なら、両端を結ぶ一本の直線になる" );
	}

	// 更新境界を挟んだ連続性、静止設定と、不正な値での安全性を確認する。
	void TestLightningTimeAndLimits( NAssetTest::FChecker& C )
	{
		C.Group = "effect-path-time-limits";
		// 更新の間を 0.5 秒刻みで調べる設定。
		FEffectPathDefinition Definition;
		Definition.Kind = FEffectPathDefinition::EKind::Lightning;
		Definition.RefreshHz = 2.0f;
		// 最初の更新時点の形状。
		FEffectPathGeometry First;
		// 次の更新時点の形状。
		FEffectPathGeometry Second;
		// 更新区間の中央の形状。
		FEffectPathGeometry Middle;
		NEffectPath::BuildLightning( Definition, 41u, 0.0f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), First );
		NEffectPath::BuildLightning( Definition, 41u, 0.5f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), Second );
		NEffectPath::BuildLightning( Definition, 41u, 0.25f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), Middle );
		C.Check( !SameGeometry( First, Second ) && Middle.Strips[0].Points[16].Position.Equals( ( First.Strips[0].Points[16].Position + Second.Strips[0].Points[16].Position ) * 0.5f, 1.0e-5f ), "更新点の間では形を滑らかに補間し、区間中央の主幹は二つの形状の中間になる" );
		NEffectPath::BuildLightning( Definition, 41u, 0.5f - 1.0e-5f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), First );
		NEffectPath::BuildLightning( Definition, 41u, 0.5f + 1.0e-5f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), Second );
		// 境界をまたいだ全点の位置の最大の変化。
		float32 MaximumChange = 0.0f;
		// 主幹と枝を含めた比較対象の番号。
		for ( int32 Strip = 0; Strip < First.Strips.Num(); ++Strip )
		{
			// 各点の更新境界での変化を調べる番号。
			for ( int32 Index = 0; Index < First.Strips[Strip].Points.Num(); ++Index ) MaximumChange = std::max( MaximumChange, ( First.Strips[Strip].Points[Index].Position - Second.Strips[Strip].Points[Index].Position ).Length() );
		}
		C.Check( MaximumChange < 0.001f, "形状の更新境界でも主幹と枝が飛ばない（最大変化 %.8f）", MaximumChange );
		Definition.RefreshHz = 0.0f;
		NEffectPath::BuildLightning( Definition, 41u, 0.0f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), First );
		NEffectPath::BuildLightning( Definition, 41u, 100.0f, FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), Second );
		C.Check( SameGeometry( First, Second ), "更新速度 0 では、時刻が進んでも雷の形を変えない" );
		Definition.RefreshHz = 12.0f;
		Definition.Segments = std::numeric_limits<int32>::max();
		Definition.Branches = std::numeric_limits<int32>::max();
		NEffectPath::BuildLightning( Definition, 0u, std::numeric_limits<float32>::max(), FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), First );
		C.Check( First.Strips.Num() == 17 && First.Strips[0].Points.Num() == 129 && SafeGeometry( First ), "巨大な区間数・枝数・時刻でも、点数上限と有限な座標を保つ" );
		Definition.Segments = -1;
		Definition.Branches = -1;
		Definition.Jitter = std::numeric_limits<float32>::quiet_NaN();
		NEffectPath::BuildLightning( Definition, 0u, std::numeric_limits<float32>::quiet_NaN(), FVector3::Zero, Definition.End, 1.0f, FColor32( 255, 255, 255 ), First );
		C.Check( First.Strips.Num() == 1 && First.Strips[0].Points.Num() == 3 && SafeGeometry( First ), "不正な区間数・枝数・揺れ・時刻を直接渡しても、安全な値で処理する" );
		NEffectPath::BuildLightning( Definition, 0u, 0.0f, FVector3::Zero, FVector3::Zero, 1.0f, FColor32(), First );
		C.Check( First.Strips.IsEmpty(), "同じ始点と終点は、前の出力を消して空の形状にする" );
		NEffectPath::BuildLightning( Definition, 0u, 0.0f, FVector3::Zero, Definition.End, 0.0f, FColor32(), First );
		C.Check( First.Strips.IsEmpty(), "幅 0 の雷は点を作らない" );
		NEffectPath::BuildLightning( Definition, 0u, 0.0f, FVector3( std::numeric_limits<float32>::infinity(), 0.0f, 0.0f ), Definition.End, 1.0f, FColor32(), First );
		C.Check( First.Strips.IsEmpty(), "非有限の始点は、描画側へ不正な座標を渡さず拒否する" );
		Definition.BranchLength = 1.0e-10f;
		Definition.Branches = 4;
		NEffectPath::BuildLightning( Definition, 0u, 0.0f, FVector3( 10000.0f, 10000.0f, 10000.0f ), FVector3( 10000.0f, 10003.0f, 10000.0f ), 1.0f, FColor32(), First );
		C.Check( First.Strips.Num() == 1 && SafeGeometry( First ), "大きな座標で丸まって長さ 0 になる枝は省き、主幹の描画を保つ" );
		Definition.Kind = FEffectPathDefinition::EKind::Ribbon;
		NEffectPath::BuildLightning( Definition, 0u, 0.0f, FVector3::Zero, Definition.End, 1.0f, FColor32(), First );
		C.Check( First.Strips.IsEmpty(), "軌跡表示を雷として生成しない" );
	}
}

// 帯と雷の定義の往復・検証、および描画に依存しない雷の形状を確認する。
void NWorldTest::RunEffectPathTests( NAssetTest::FChecker& C )
{
	TestPathData( C );
	TestPathValidation( C );
	TestLightningGeometry( C );
	TestLightningTimeAndLimits( C );
}

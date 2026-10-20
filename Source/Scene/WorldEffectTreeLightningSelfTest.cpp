// SPDX-License-Identifier: NOASSERTION
#include "WorldEffectSelfTestSupport.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "Library/Framework/Effect/ParticleEffectEditing.h"
#include "Library/Framework/Effect/EffectPathGeometry.h"

namespace
{
	// TreeLightning全点の位置・見た目・累積距離が同じかを比べる。
	bool SameGeometry( const FEffectPathGeometry& A, const FEffectPathGeometry& B )
	{
		if ( A.Strips.Num() != B.Strips.Num() ) return false;
		for ( int32 Strip = 0; Strip < A.Strips.Num(); ++Strip )
		{
			if ( A.Strips[Strip].Points.Num() != B.Strips[Strip].Points.Num() ) return false;
			for ( int32 Index = 0; Index < A.Strips[Strip].Points.Num(); ++Index )
			{
				// 二つの生成結果で比較する対応点。
				const FEffectPathGeometry::FPoint& Left = A.Strips[Strip].Points[Index];
				// もう一方の生成結果で比較する対応点。
				const FEffectPathGeometry::FPoint& Right = B.Strips[Strip].Points[Index];
				if ( Left.Position != Right.Position || Left.Width != Right.Width || Left.Color != Right.Color || Left.U != Right.U ) return false;
			}
		}
		return true;
	}

	// 各stripと点の上限を守り、全ての出力値が有限かを調べる。
	bool IsSafeGeometry( const FEffectPathGeometry& Geometry )
	{
		// 全stripの点数を足した量。
		int32 TotalPoints = 0;
		for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips )
		{
			TotalPoints += Strip.Points.Num();
			if ( Strip.Points.Num() < 2 || Strip.Points.Num() > FEffectPathDefinition::MaxSegments + 1 ) return false;
			for ( const FEffectPathGeometry::FPoint& Point : Strip.Points ) if ( !std::isfinite( Point.Position.x ) || !std::isfinite( Point.Position.y ) || !std::isfinite( Point.Position.z ) || !std::isfinite( Point.Width ) || !std::isfinite( Point.U ) || Point.Width < 0.0f ) return false;
		}
		return TotalPoints <= FEffectPathGeometry::MaxPoints && Geometry.Strips.Num() <= 81;
	}

	// 点が主幹の元chordからどれだけ横へ離れたかを測る。
	float32 DistanceFromChord( const FVector3& Point, const FVector3& Start, const FVector3& End )
	{
		// chord上でPointへ最も近い位置を求める射影率。
		const FVector3 Delta = End - Start;
		// 射影率の分母に使うchord長の二乗。
		const float32 Denominator = FVector3::Dot( Delta, Delta );
		if ( Denominator <= 0.0f ) return std::numeric_limits<float32>::infinity();
		// Pointからchord方向へ進む最近点の比率。
		const float32 T = FVector3::Dot( Point - Start, Delta ) / Denominator;
		return ( Point - ( Start + Delta * T ) ).Length();
	}

	// 枝の両端から、その完成chordの長さを返す。
	float32 StripChordLength( const FEffectPathGeometry::FStrip& Strip )
	{
		return Strip.Points.Num() >= 2 ? ( Strip.Points.Last().Position - Strip.Points[0].Position ).Length() : 0.0f;
	}

	// 定義と形状を検査するTreeLightningの既定シナリオを作る。
	FEffectPathDefinition MakeTreePath()
	{
		// 成長する幹と親子枝を持つTreeLightning設定。
		FEffectPathDefinition Definition;
		Definition.Kind = FEffectPathDefinition::EKind::TreeLightning;
		Definition.End = FVector3( 0.0f, 4.0f, 0.0f );
		Definition.Segments = 12;
		Definition.Branches = 2;
		Definition.Jitter = 0.15f;
		Definition.BranchLength = 0.5f;
		Definition.Tree.Depth = 2;
		Definition.Tree.ChildBranches = 2;
		Definition.Tree.GrowthSeconds = 0.0f;
		Definition.Tree.BranchDelaySeconds = 0.0f;
		Definition.Tree.StrokeInterval = 0.2f;
		Definition.Tree.StrokeSeconds = 0.05f;
		return Definition;
	}

	// 保存互換・不正値・hashと編集履歴を確認する。
	void TestTreeDefinition( NAssetTest::FChecker& C )
	{
		C.Group = "effect-tree-lightning-definition";
		// 旧JSONと旧hashのまま保つNone定義。
		FEffectPathDefinition LegacyPath;
		FParticleEffectDefinition LegacyEffect;
		LegacyEffect.Emitters.Add( FParticleEmitterDefinition() );
		C.Check( LegacyPath.ComputeHash() == 0xDD139293FA3B301Dull && LegacyEffect.ComputeHash() == 0xA811CEF10E93C86Eull && !LegacyEffect.ToJson()["Emitters"][0].contains( "Path" ), "既定Pathとeffectの旧hash・JSONをそのまま保つ" );
		// 後方互換用に既定のTree設定を出さないpath。
		FEffectPathDefinition DefaultTreePath;
		DefaultTreePath.Kind = FEffectPathDefinition::EKind::TreeLightning;
		C.Check( !DefaultTreePath.ToJson().contains( "Tree" ) && DefaultTreePath.Tree.IsDefault(), "既定Tree設定はPath JSONから省略する" );
		// 既存LightningにもTree既定値を保存せず、旧hashを維持するpath。
		FEffectPathDefinition LegacyLightning = FEffectPathDefinition();
		LegacyLightning.Kind = FEffectPathDefinition::EKind::Lightning;
		C.Check( !LegacyLightning.ToJson().contains( "Tree" ) && LegacyLightning.ComputeHash() == 0x67A80EDB1E1C5D23ull, "既存LightningはTreeを省略し従来hashを保つ" );
		// 精度・hash・型名の往復を調べる設定。
		FEffectPathDefinition Full = MakeTreePath();
		Full.Tree.BranchAngleDegrees = 37.123456f;
		Full.Tree.GrowthSeconds = 0.1234567f;
		Full.Tree.BranchDelaySeconds = 0.01234567f;
		Full.Tree.StrokeCount = 5;
		Full.Tree.StrokeInterval = 0.1234567f;
		Full.Tree.StrokeSeconds = 0.03456789f;
		const NlohmannJson Json = NlohmannJson::parse( Full.ToJson().dump() );
		// JSON textから復元したTree Path。
		FEffectPathDefinition Back;
		C.Check( FEffectPathDefinition::FromJson( Json, Back ) && Back.ComputeHash() == Full.ComputeHash() && Back.ToJson() == Full.ToJson() && Back.Tree.StrokeSeconds == Full.Tree.StrokeSeconds && Back.Kind == FEffectPathDefinition::EKind::TreeLightning && FString( ToString( Back.Kind ) ) == "TreeLightning", "TreeLightningの全設定をJSON float精度のまま往復する" );
		// Treeの全field変更を識別する変更関数。
		struct FTreeChange
		{
			// 失敗箇所を示すfield名。
			const char* Name;
			// 基準設定を一項目だけ変える操作。
			void ( *Apply )( FEffectLightningTreeDefinition& );
		};
		const FTreeChange Changes[] = {
			{ "Depth", []( FEffectLightningTreeDefinition& T ) { T.Depth = 1; } },
			{ "ChildBranches", []( FEffectLightningTreeDefinition& T ) { ++T.ChildBranches; } },
			{ "BranchAngleDegrees", []( FEffectLightningTreeDefinition& T ) { T.BranchAngleDegrees += 0.25f; } },
			{ "GrowthSeconds", []( FEffectLightningTreeDefinition& T ) { T.GrowthSeconds += 0.01f; } },
			{ "BranchDelaySeconds", []( FEffectLightningTreeDefinition& T ) { T.BranchDelaySeconds += 0.01f; } },
			{ "StrokeCount", []( FEffectLightningTreeDefinition& T ) { ++T.StrokeCount; } },
			{ "StrokeInterval", []( FEffectLightningTreeDefinition& T ) { T.StrokeInterval += 0.01f; } },
			{ "StrokeSeconds", []( FEffectLightningTreeDefinition& T ) { T.StrokeSeconds += 0.001f; } },
		};
		for ( const FTreeChange& Change : Changes )
		{
			// 一項目だけ基準値から変更したTree設定。
			FEffectLightningTreeDefinition Changed = FEffectLightningTreeDefinition();
			Change.Apply( Changed );
			C.Check( Changed.ComputeHash() != FEffectLightningTreeDefinition().ComputeHash() && Changed.ToJson() != FEffectLightningTreeDefinition().ToJson(), "Tree.%sの単独変更をhashとJSONへ反映する", Change.Name );
		}
		// Treeを持つ Path kind以外でも設定を検証する。
		FEffectPathDefinition RetainedTree;
		RetainedTree.Tree.Depth = 3;
		C.Check( !RetainedTree.Validate().IsEmpty(), "Path kindに関係なくTreeの不正値を保存時に拒否する" );
		// 欠落ではなく誤型を持つTree設定。
		NlohmannJson BadTree = Full.ToJson();
		BadTree["Tree"]["Depth"] = "two";
		FEffectPathDefinition BadDepth;
		TArray<FString> Warnings;
		C.Check( FEffectPathDefinition::FromJson( BadTree, BadDepth, &Warnings ) && !BadDepth.Validate().IsEmpty() && !Warnings.IsEmpty(), "Tree.Depthの誤型を既定値へ修復せず保存エラーにする" );
		// Tree object自体がnullのJSON。
		BadTree = Full.ToJson();
		BadTree["Tree"] = nullptr;
		C.Check( FEffectPathDefinition::FromJson( BadTree, BadDepth ) && !BadDepth.Validate().IsEmpty(), "Tree objectのnullを保存可能な既定値へ変えない" );
		// Treeの実数誤型、null、非有限値をJSON往復後も不正として残すfield。
		struct FBadTreeFloat
		{
			// 不正値を入れるTree内の実数field。
			const char* Name;
			// 誤型または非有限値のJSON表現。
			NlohmannJson Value;
		};
		const FBadTreeFloat BadTreeFloats[] = {
			{ "GrowthSeconds", "bad" },
			{ "BranchAngleDegrees", nullptr },
			{ "GrowthSeconds", std::numeric_limits<float32>::quiet_NaN() },
		};
		for ( const FBadTreeFloat& Case : BadTreeFloats )
		{
			// Tree fieldを一つだけ壊した読込元。
			NlohmannJson InvalidJson = Full.ToJson();
			InvalidJson["Tree"][Case.Name] = Case.Value;
			// 読込後とUndo相当のJSON往復先。
			FEffectPathDefinition InvalidDefinition;
			FEffectPathDefinition UndoDefinition;
			const bool bReadInvalid = FEffectPathDefinition::FromJson( InvalidJson, InvalidDefinition );
			const bool bReadUndo = bReadInvalid && FEffectPathDefinition::FromJson( NlohmannJson::parse( InvalidDefinition.ToJson().dump() ), UndoDefinition );
			C.Check( bReadInvalid && !InvalidDefinition.Validate().IsEmpty() && bReadUndo && !UndoDefinition.Validate().IsEmpty(), "Tree.%sの誤型/null/非有限値を読込とJSON往復後も保存拒否する", Case.Name );
		}
		// Treeを伴う未知kindの読取後も、Undo用JSONで不正状態を保つ。
		BadTree = Full.ToJson();
		BadTree["Kind"] = "FutureLightning";
		C.Check( FEffectPathDefinition::FromJson( BadTree, BadDepth ) && !BadDepth.Validate().IsEmpty() && BadDepth.ToJson()["Kind"] == "InvalidTreeLightning", "Tree付き未知kindを不正sentinelとしてJSON往復後も拒否する" );
		// TreeLightningの共通数値fieldで誤型を単独検査するケース。
		struct FBadField
		{
			// 不正にするJSON field名。
			const char* Name;
			// 値の型違いまたはshape違い。
			NlohmannJson Value;
		};
		const FBadField BadFields[] = {
			{ "End", nullptr },
			{ "End", NlohmannJson::array( { 0.0, 3.0 } ) },
			{ "End", NlohmannJson::array( { 0.0, 3.0, 0.0, 1.0 } ) },
			{ "End", NlohmannJson::array( { 0.0, nullptr, 0.0 } ) },
			{ "Segments", 12.5 },
			{ "Branches", 3.5 },
			{ "Segments", std::numeric_limits<uint64>::max() },
			{ "Jitter", "bad" },
			{ "BranchLength", nullptr },
			{ "RefreshHz", "bad" },
			{ "UVRepeat", nullptr },
			{ "BreakDistance", "bad" },
		};
		for ( const FBadField& Case : BadFields )
		{
			// 共通fieldを一つだけ壊したTreeLightning JSON。
			NlohmannJson BadJson = Full.ToJson();
			BadJson[Case.Name] = Case.Value;
			// 誤型が保存検証へ届く読取先。
			FEffectPathDefinition BadDefinition;
			C.Check( FEffectPathDefinition::FromJson( BadJson, BadDefinition ) && !BadDefinition.Validate().IsEmpty(), "TreeLightningのPath.%s誤型を検証エラーに残す", Case.Name );
		}
		// 逆順Undoで保存不可なNaNを再び読み込む文書。
		FParticleEffectDefinition InvalidEffect;
		InvalidEffect.Emitters.Add( FParticleEmitterDefinition() );
		InvalidEffect.Emitters[0].Path.Kind = FEffectPathDefinition::EKind::TreeLightning;
		InvalidEffect.Emitters[0].Path.Jitter = std::numeric_limits<float32>::quiet_NaN();
		TParticleEffectDocument Document;
		Document.Reset( InvalidEffect );
		Document.BeginEdit().Emitters[0].Path.Tree.ChildBranches = 3;
		Document.EndEditSession();
		const bool bUndo = Document.Undo();
		C.Check( bUndo && std::isnan( Document.Get().Emitters[0].Path.Jitter ) && !Document.Get().Emitters[0].Path.Validate().IsEmpty(), "Undoで不正NaNを既定値へ消さず検証可能な状態に戻す" );
	}

	// 固定端点、枝接続、枝角、幅と累積Uを確認する。
	void TestTreeShape( NAssetTest::FChecker& C )
	{
		C.Group = "effect-tree-lightning-shape";
		// 決定論的な親子枝の入力。
		FEffectPathDefinition Definition = MakeTreePath();
		Definition.Jitter = 1.0f;
		const FVector3 Start( 2.0f, 3.0f, 1.0f );
		const FVector3 End( 2.0f, 7.0f, 1.0f );
		const FColor32 Color( 70, 130, 210, 200 );
		FEffectPathGeometry Geometry;
		NEffectPath::BuildTreeLightning( Definition, 991u, 0.0f, Start, End, 0.8f, Color, Geometry );
		C.Check( IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 7 && Geometry.Strips[0].Points.Num() == 13 && Geometry.Strips[0].Points[0].Position == Start && Geometry.Strips[0].Points.Last().Position == End, "深さ2・主幹枝2・子枝2で固定端点と7本の有限な折線を作る" );
		if ( IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 7 && Geometry.Strips[0].Points.Num() == 13 )
		{
			// 主幹上へ接続した一次枝と子枝を検査する状態。
			bool bAttachments = true;
			// 二本の一次枝が親接線から離れる角度。
			float32 FirstPrimaryAngle = 0.0f;
			float32 SecondPrimaryAngle = 0.0f;
			for ( const int32 Branch : { 1, 4 } )
			{
				// 主幹から分かれる一次枝。
				const FEffectPathGeometry::FStrip& Primary = Geometry.Strips[Branch];
				if ( Primary.Points.Num() < 2 ) { bAttachments = false; continue; }
				int32 Attachment = INDEX_NONE;
				for ( int32 Point = 1; Point < Geometry.Strips[0].Points.Num() - 1; ++Point ) if ( Geometry.Strips[0].Points[Point].Position == Primary.Points[0].Position ) Attachment = Point;
				bAttachments = bAttachments && Attachment >= 2 && Attachment <= 10 && std::fabs( Primary.Points[0].U - Geometry.Strips[0].Points[Attachment].U ) < 1.0e-5f;
				if ( Attachment >= 1 && Attachment + 1 < Geometry.Strips[0].Points.Num() )
				{
					// 主幹接続点の折線接線。
					const FVector3 Tangent = ( Geometry.Strips[0].Points[Attachment + 1].Position - Geometry.Strips[0].Points[Attachment - 1].Position ).Normalized();
					// 枝の方向が主幹接線と平行でないこと。
					const FVector3 BranchDirection = ( Primary.Points.Last().Position - Primary.Points[0].Position ).Normalized();
					bAttachments = bAttachments && std::fabs( FVector3::Dot( Tangent, BranchDirection ) ) < 0.95f && Primary.Points[0].Width > 0.0f && Primary.Points.Last().Width == 0.0f && Primary.Points.Last().Color.a == 0 && Primary.Points[0].Color.r == Color.r && Primary.Points[0].Color.g == Color.g && Primary.Points[0].Color.b == Color.b;
					if ( Branch == 1 ) FirstPrimaryAngle = std::acos( std::clamp( FVector3::Dot( Tangent, BranchDirection ), -1.0f, 1.0f ) );
					if ( Branch == 4 ) SecondPrimaryAngle = std::acos( std::clamp( FVector3::Dot( Tangent, BranchDirection ), -1.0f, 1.0f ) );
				}
			}
			// 全ての子枝接続とU連続を個別に数える状態。
			int32 VerifiedChildConnections = 0;
			for ( const int32 Child : { 2, 3, 5, 6 } )
			{
				// 主枝列の後ろにある二次枝。
				const FEffectPathGeometry::FStrip& Secondary = Geometry.Strips[Child];
				if ( Secondary.Points.Num() < 2 ) { bAttachments = false; continue; }
				// この子枝を持つ一次枝と、配列を読む前の安全条件。
				const int32 Parent = Child < 4 ? 1 : 4;
				const bool bParentSafe = Geometry.Strips.IsValidIndex( Parent ) && Geometry.Strips[Parent].Points.Num() >= 3;
				if ( !bParentSafe ) { bAttachments = false; continue; }
				// 親の位置と累積Uが同じ接続頂点を見つけたか。
				bool bChildPositionAttached = false;
				bool bChildUvAttached = false;
				// 生成順から対応する一次枝を求める。
				for ( int32 Point = 1; Point < Geometry.Strips[Parent].Points.Num() - 1; ++Point )
				{
					// 根元位置が一致した親頂点でU連続も確かめる。
					const FEffectPathGeometry::FPoint& ParentPoint = Geometry.Strips[Parent].Points[Point];
					if ( ParentPoint.Position != Secondary.Points[0].Position ) continue;
					bChildPositionAttached = true;
					bChildUvAttached = ParentPoint.U > 0.0f && Secondary.Points[0].U > 0.0f && std::fabs( ParentPoint.U - Secondary.Points[0].U ) < 1.0e-5f;
					break;
				}
				const bool bChildShapeValid = bChildPositionAttached && bChildUvAttached && Secondary.Points.Last().Width == 0.0f && Secondary.Points.Last().Color.a == 0;
				bAttachments = bAttachments && bChildShapeValid;
				if ( bChildShapeValid ) ++VerifiedChildConnections;
			}
			C.Check( VerifiedChildConnections == 4, "親位置と正のUが一致する子枝接続を4本すべて検査する (verified=%d)", VerifiedChildConnections );
			// 直線の同じ親から反対側へ開く2本の子枝。
			FEffectPathDefinition ChildSplit = Definition;
			ChildSplit.Jitter = 0.0f;
			ChildSplit.Branches = 1;
			FEffectPathGeometry SplitGeometry;
			NEffectPath::BuildTreeLightning( ChildSplit, 991u, 0.0f, Start, End, 0.8f, Color, SplitGeometry );
			const bool bSplitHasChildren = IsSafeGeometry( SplitGeometry ) && SplitGeometry.Strips.Num() == 4 && SplitGeometry.Strips[2].Points.Num() >= 2 && SplitGeometry.Strips[3].Points.Num() >= 2;
			C.Check( bSplitHasChildren, "直線の同じ親から二本の子枝を作る" );
			if ( bSplitHasChildren )
			{
				// 同じ親から左右へ開く二つの子枝方向。
				const FVector3 ChildDirectionA = ( SplitGeometry.Strips[2].Points.Last().Position - SplitGeometry.Strips[2].Points[0].Position ).Normalized();
				const FVector3 ChildDirectionB = ( SplitGeometry.Strips[3].Points.Last().Position - SplitGeometry.Strips[3].Points[0].Position ).Normalized();
				bAttachments = bAttachments && std::fabs( FVector3::Dot( ChildDirectionA, ChildDirectionB ) ) < 0.8f;
			}
			// seed差を比べる一つ目の一次枝長。
			const float32 FirstPrimaryLength = StripChordLength( Geometry.Strips[1] );
			// seed差を比べる二つ目の一次枝長。
			const float32 SecondPrimaryLength = StripChordLength( Geometry.Strips[4] );
			// 二本の一次枝長に視認できる差があるか。
			const bool bDifferentPrimaryLengths = std::fabs( FirstPrimaryLength - SecondPrimaryLength ) > 0.05f;
			// 二本の一次枝角度にseed由来の差があるか。
			const bool bDifferentPrimaryAngles = std::fabs( FirstPrimaryAngle - SecondPrimaryAngle ) > 0.005f;
			// 同じ親から出る二次枝長にも差があるか。
			const bool bDifferentSecondaryLengths = std::fabs( StripChordLength( Geometry.Strips[2] ) - StripChordLength( Geometry.Strips[3] ) ) > 0.02f;
			bAttachments = bAttachments && bDifferentPrimaryLengths && bDifferentPrimaryAngles && bDifferentSecondaryLengths;
			C.Check( bAttachments, "枝を親の内側頂点とUへ接続し、角度・長さをseedで変えながら先端を絞る" );
			// 直線主幹では一次枝2本が正面投影で必ず左右へ分かれる。
			FEffectPathDefinition PrimarySplit = Definition;
			PrimarySplit.Jitter = 0.0f;
			PrimarySplit.Branches = 2;
			PrimarySplit.Tree.ChildBranches = 0;
			// 正面左右の分岐だけを確かめる生成結果。
			FEffectPathGeometry PrimarySplitGeometry;
			NEffectPath::BuildTreeLightning( PrimarySplit, 991u, 0.0f, Start, End, 0.8f, Color, PrimarySplitGeometry );
			// 一次枝の左右点を読む前に形状の本数・点数を確認する。
			const bool bPrimarySplitSafe = IsSafeGeometry( PrimarySplitGeometry ) && PrimarySplitGeometry.Strips.Num() == 3 && PrimarySplitGeometry.Strips[1].Points.Num() >= 2 && PrimarySplitGeometry.Strips[2].Points.Num() >= 2;
			C.Check( bPrimarySplitSafe, "直線主幹から二本の一次枝を生成する" );
			if ( bPrimarySplitSafe )
			{
				// 左右に反対向きへ開く一次枝の正面方向成分。
				const float32 FirstSide = PrimarySplitGeometry.Strips[1].Points.Last().Position.x - PrimarySplitGeometry.Strips[1].Points[0].Position.x;
				// もう一方の一次枝の正面方向成分。
				const float32 SecondSide = PrimarySplitGeometry.Strips[2].Points.Last().Position.x - PrimarySplitGeometry.Strips[2].Points[0].Position.x;
				C.Check( FirstSide * SecondSide < 0.0f && std::fabs( FirstSide ) > 0.5f && std::fabs( SecondSide ) > 0.5f, "直線主幹の兄弟枝は正面から見て左右へ有意に開く" );
			}
			// 同じseedで再生成した完成形。
			FEffectPathGeometry Again;
			NEffectPath::BuildTreeLightning( Definition, 991u, 0.0f, Start, End, 0.8f, Color, Again );
			C.Check( IsSafeGeometry( Again ) && SameGeometry( Geometry, Again ), "同じseedと入力で完成形・色・Uを再現する" );
			NEffectPath::BuildTreeLightning( Definition, 992u, 0.0f, Start, End, 0.8f, Color, Again );
			// 異なるseedの途中点を安全に比較できる形か。
			const bool bDifferentSeedSafe = IsSafeGeometry( Again ) && Again.Strips.Num() == 7 && Again.Strips[0].Points.Num() == 13;
			C.Check( bDifferentSeedSafe, "異なるseedでも比較対象の主幹と枝形状を作る" );
			if ( bDifferentSeedSafe ) C.Check( Geometry.Strips[0].Points[0].Position == Again.Strips[0].Points[0].Position && Geometry.Strips[0].Points.Last().Position == Again.Strips[0].Points.Last().Position && Geometry.Strips[0].Points[5].Position != Again.Strips[0].Points[5].Position, "別seedで途中の折線だけ変え、主幹両端を固定する" );
		}
		// 複数seedで中央線の横ずれと折れが読み取れることを確認する。
		bool bStrongIrregularity = true;
		// 異seedの中点位置を比較するための先行値。
		FVector3 FirstSeedMidpoint = FVector3::Zero;
		// 複数seedの粗い折れ幅の最小値。
		float32 MinimumCoarseDisplacement = std::numeric_limits<float32>::max();
		// 複数seedの粗い折れ幅の最大値。
		float32 MaximumCoarseDisplacement = 0.0f;
		// 形状差を確認するseed群。
		for ( uint32 Seed : { 991u, 992u, 993u, 4096u } )
		{
			// 指定seedの主幹と枝を生成した結果。
			FEffectPathGeometry SeedGeometry;
			NEffectPath::BuildTreeLightning( Definition, Seed, 0.0f, Start, End, 0.8f, Color, SeedGeometry );
			// まず主幹の点数を検査し、安全な時だけ途中点を読む。
			const bool bSeedGeometrySafe = IsSafeGeometry( SeedGeometry ) && SeedGeometry.Strips.Num() == 7 && SeedGeometry.Strips[0].Points.Num() == 13;
			C.Check( bSeedGeometrySafe, "seed %uで乱れ評価用の全枝形状を作る", Seed );
			if ( !bSeedGeometrySafe ) { bStrongIrregularity = false; continue; }
			// 主幹中央の横ずれを原chord長と比べる。
			float32 MaximumChordDistance = 0.0f;
			// 局所的な折れ角を一つでも確認したか。
			bool bHasSharpTurn = false;
			for ( int32 Point = 1; Point < SeedGeometry.Strips[0].Points.Num() - 1; ++Point ) MaximumChordDistance = std::max( MaximumChordDistance, DistanceFromChord( SeedGeometry.Strips[0].Points[Point].Position, Start, End ) );
			for ( int32 Point = 1; Point < SeedGeometry.Strips[0].Points.Num() - 1; ++Point )
			{
				// 隣接線分の向きを比べ、鋭い折れが存在するか調べる。
				const FVector3 Before = ( SeedGeometry.Strips[0].Points[Point].Position - SeedGeometry.Strips[0].Points[Point - 1].Position ).Normalized();
				const FVector3 After = ( SeedGeometry.Strips[0].Points[Point + 1].Position - SeedGeometry.Strips[0].Points[Point].Position ).Normalized();
				bHasSharpTurn = bHasSharpTurn || FVector3::Dot( Before, After ) < 0.97f;
			}
			bStrongIrregularity = bStrongIrregularity && MaximumChordDistance > 0.15f * ( End - Start ).Length() && bHasSharpTurn;
			// seedごとに異なる粗い中央折れの実距離。
			const float32 CoarseDisplacement = DistanceFromChord( SeedGeometry.Strips[0].Points[6].Position, Start, End );
			MinimumCoarseDisplacement = std::min( MinimumCoarseDisplacement, CoarseDisplacement );
			MaximumCoarseDisplacement = std::max( MaximumCoarseDisplacement, CoarseDisplacement );
			if ( Seed == 991u ) FirstSeedMidpoint = SeedGeometry.Strips[0].Points[6].Position;
			if ( Seed == 992u ) bStrongIrregularity = bStrongIrregularity && FirstSeedMidpoint != SeedGeometry.Strips[0].Points[6].Position;
		}
		C.Check( bStrongIrregularity, "複数seedで主幹はchordから十分に曲がり、異seedは異なる折線を作る" );
		C.Check( MaximumCoarseDisplacement - MinimumCoarseDisplacement > 0.2f, "複数seedで主幹の大きな折れ幅自体も変化する" );
		// 角度範囲の両端で、奥行き成分が親接線との設定角を崩さない定義。
		FEffectPathDefinition AngleLimits = Definition;
		AngleLimits.Jitter = 0.0f;
		AngleLimits.Branches = 1;
		AngleLimits.Tree.ChildBranches = 0;
		// 実角度の下端と上端を試す設定値。
		for ( const float32 RequestedAngle : { 5.0f, 85.0f } )
		{
			// 指定角の一次枝を作る入力。
			AngleLimits.Tree.BranchAngleDegrees = RequestedAngle;
			// 指定角で生成した幹と枝。
			FEffectPathGeometry AngleGeometry;
			NEffectPath::BuildTreeLightning( AngleLimits, 8u, 0.0f, Start, End, 0.8f, Color, AngleGeometry );
			// 角度検査で参照するstripと点が揃ったか。
			const bool bAngleGeometrySafe = IsSafeGeometry( AngleGeometry ) && AngleGeometry.Strips.Num() == 2 && AngleGeometry.Strips[0].Points.Num() >= 3 && AngleGeometry.Strips[1].Points.Num() >= 2;
			C.Check( bAngleGeometrySafe, "指定角度%.1f度の枝形状を作る", RequestedAngle );
			if ( bAngleGeometrySafe )
			{
				// 直線主幹に対する実際の枝角度を度へ換算する。
				const FVector3 Tangent = ( AngleGeometry.Strips[0].Points[2].Position - AngleGeometry.Strips[0].Points[0].Position ).Normalized();
				const FVector3 BranchDirection = ( AngleGeometry.Strips[1].Points.Last().Position - AngleGeometry.Strips[1].Points[0].Position ).Normalized();
				// 親接線との角度を度へ変換した実測値。
				const float32 ActualAngle = std::acos( std::clamp( FVector3::Dot( Tangent, BranchDirection ), -1.0f, 1.0f ) ) * 57.29577951308232f;
				// hash倍率と設定角clampが許す最小角。
				const float32 MinimumAngle = std::max( RequestedAngle * 0.8f, 5.0f );
				// hash倍率と設定角clampが許す最大角。
				const float32 MaximumAngle = std::min( RequestedAngle * 1.2f, 85.0f );
				C.Check( ActualAngle >= MinimumAngle - 0.05f && ActualAngle <= MaximumAngle + 0.05f, "奥行き乱れを含む実角度を許容範囲内へ保つ (actual=%.3f min=%.3f max=%.3f)", ActualAngle, MinimumAngle, MaximumAngle );
			}
		}
		// Ageだけを変えた成長中の同じseed形状。
		Definition.Tree.GrowthSeconds = 0.2f;
		Definition.Branches = 0;
		FEffectPathGeometry Early;
		FEffectPathGeometry Later;
		NEffectPath::BuildTreeLightning( Definition, 44u, 0.04f, Start, End, 0.8f, Color, Early );
		NEffectPath::BuildTreeLightning( Definition, 44u, 0.08f, Start, End, 0.8f, Color, Later );
		// Earlyの最終点は移動中の補間先端なので、完成済み頂点だけを比較する数。
		const int32 StablePointCount = Early.Strips.IsEmpty() ? 0 : std::max( Early.Strips[0].Points.Num() - 1, 0 );
		// 既に完成していた頂点がAge変更で動いていないか。
		bool bStablePrefix = IsSafeGeometry( Early ) && IsSafeGeometry( Later ) && Early.Strips.Num() == 1 && Later.Strips.Num() == 1 && StablePointCount <= Later.Strips[0].Points.Num();
		for ( int32 Point = 0; bStablePrefix && Point < StablePointCount; ++Point ) bStablePrefix = Early.Strips[0].Points[Point].Position == Later.Strips[0].Points[Point].Position;
		C.Check( bStablePrefix, "成長時刻を変えても既に現れた主幹頂点は移動しない" );
		// 成長先端の累積距離が前の時刻より先へ進んだか。
		const bool bTipAdvanced = IsSafeGeometry( Early ) && IsSafeGeometry( Later ) && Early.Strips.Num() == 1 && Later.Strips.Num() == 1 && Later.Strips[0].Points.Last().U > Early.Strips[0].Points.Last().U && Later.Strips[0].Points.Last().Position != Early.Strips[0].Points.Last().Position;
		C.Check( bTipAdvanced, "Ageの進行で主幹の補間先端だけが前進する" );
	}

	// 成長先端・放電窓・階層と安全上限を確かめる。
	void TestTreeTimingAndLimits( NAssetTest::FChecker& C )
	{
		C.Group = "effect-tree-lightning-time-limits";
		// 直線主幹で成長先端を固定検査する設定。
		FEffectPathDefinition Growth = MakeTreePath();
		Growth.Branches = 0;
		Growth.Jitter = 0.0f;
		Growth.Segments = 8;
		Growth.Tree.GrowthSeconds = 1.0f;
		Growth.Tree.StrokeCount = 2;
		Growth.Tree.StrokeInterval = 0.25f;
		Growth.Tree.StrokeSeconds = 0.015625f;
		FEffectPathGeometry Geometry;
		const FColor32 Color( 20, 80, 160, 255 );
		NEffectPath::BuildTreeLightning( Growth, 5u, 0.5f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.Num() == 1 && Geometry.Strips[0].Points.Num() == 5 && Geometry.Strips[0].Points.Last().Position == FVector3( 0.0f, 2.0f, 0.0f ) && Geometry.Strips[0].Points.Last().U == 2.0f && Geometry.Strips[0].Points[0].Color.a == 31 && Geometry.Strips[0].Points.Last().Color.r == Color.r, "成長0.5秒で幹の半分まで伸び、Uを再正規化せずalphaだけ0.12倍する" );
		NEffectPath::BuildTreeLightning( Growth, 5u, 1.0f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 1 && Geometry.Strips[0].Points.Num() == 9 && Geometry.Strips[0].Points.Last().Position == Growth.End && Geometry.Strips[0].Points[0].Color.a == 255, "成長完了時の最初の放電は完成した主幹を明るく出す" );
		NEffectPath::BuildTreeLightning( Growth, 5u, 1.0078125f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 1 && Geometry.Strips[0].Points[0].Color.a == 128 && Geometry.Strips[0].Points[0].Color.r == Color.r, "放電窓の中間でalphaだけ半減しRGBを保つ" );
		NEffectPath::BuildTreeLightning( Growth, 5u, 1.015625f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "放電窓の終端と次の間隔は非表示にする" );
		NEffectPath::BuildTreeLightning( Growth, 5u, 1.25f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 1 && Geometry.Strips[0].Points[0].Color.a == 255, "StrokeInterval境界で次の放電を開始する" );
		NEffectPath::BuildTreeLightning( Growth, 5u, 1.265625f, FVector3::Zero, Growth.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "最後の放電窓以降はstripを出さない" );
		// 主幹U=2、速度0.4、親枝内U差=1の子が9.1秒時点で伸びている設定。
		FEffectPathDefinition NestedGrowth = MakeTreePath();
		NestedGrowth.End = FVector3( 0.0f, 4.0f, 0.0f );
		NestedGrowth.Segments = 2;
		NestedGrowth.Branches = 1;
		NestedGrowth.Jitter = 0.0f;
		NestedGrowth.BranchLength = 0.5f;
		NestedGrowth.Tree.ChildBranches = 1;
		NestedGrowth.Tree.GrowthSeconds = 10.0f;
		NestedGrowth.Tree.BranchDelaySeconds = 0.1f;
		NEffectPath::BuildTreeLightning( NestedGrowth, 9u, 9.1f, FVector3::Zero, NestedGrowth.End, 0.4f, Color, Geometry );
		const bool bNestedChildVisible = IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 3 && Geometry.Strips[1].Points.Num() >= 2 && Geometry.Strips[2].Points.Num() >= 2 && Geometry.Strips[2].Points[0].Position == Geometry.Strips[1].Points[1].Position;
		C.Check( bNestedChildVisible, "子枝の成長開始は親枝先頭Uではなく親接続点までの距離を使う" );
		// 有効な全上限設定から得る点数と本数。
		FEffectPathDefinition Maximum = MakeTreePath();
		Maximum.Segments = 128;
		Maximum.Branches = 16;
		Maximum.Jitter = 1.0f;
		Maximum.BranchLength = 2.0f;
		Maximum.Tree.Depth = 2;
		Maximum.Tree.ChildBranches = 4;
		Maximum.Tree.GrowthSeconds = 0.0f;
		Maximum.Tree.BranchDelaySeconds = 0.0f;
		Maximum.Tree.StrokeInterval = 0.001f;
		Maximum.Tree.StrokeSeconds = 0.0001f;
		NEffectPath::BuildTreeLightning( Maximum, 7u, 0.0f, FVector3::Zero, Maximum.End, 0.4f, Color, Geometry );
		int32 PointCount = 0;
		for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) PointCount += Strip.Points.Num();
		C.Check( Geometry.Strips.Num() == 81 && PointCount == 1777 && IsSafeGeometry( Geometry ), "最大設定でも81strip・1777点以内へ収め、全値を有限に保つ (strips=%d points=%d)", Geometry.Strips.Num(), PointCount );
		// 不正な直接呼び出しの入力を安全に空へする。
		Maximum.Tree.BranchAngleDegrees = std::numeric_limits<float32>::quiet_NaN();
		NEffectPath::BuildTreeLightning( Maximum, 7u, 0.0f, FVector3::Zero, Maximum.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "NaN設定は空形状へ安全に拒否する" );
		Maximum = MakeTreePath();
		NEffectPath::BuildTreeLightning( Maximum, 7u, std::numeric_limits<float32>::max(), FVector3::Zero, Maximum.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "巨大な有限Ageを整数変換せず放電後の空形状にする" );
		NEffectPath::BuildTreeLightning( Maximum, 7u, std::numeric_limits<float32>::quiet_NaN(), FVector3::Zero, Maximum.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "非有限Ageを空形状へ安全に拒否する" );
		NEffectPath::BuildTreeLightning( Maximum, 7u, 0.0f, FVector3::Zero, FVector3::Zero, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "同じ始点と終点では空形状を返す" );
		NEffectPath::BuildTreeLightning( Maximum, 7u, 0.0f, FVector3::Zero, Maximum.End, 0.0f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "幅0では空形状を返す" );
		Maximum.Kind = FEffectPathDefinition::EKind::Lightning;
		NEffectPath::BuildTreeLightning( Maximum, 7u, 0.0f, FVector3::Zero, Maximum.End, 0.4f, Color, Geometry );
		C.Check( Geometry.Strips.IsEmpty(), "既存LightningをTree生成関数へ誤って渡さない" );
	}

	// 全seedの形状が保守sphereに入り、無効入力を拒否する。
	void TestTreeBounds( NAssetTest::FChecker& C )
	{
		C.Group = "effect-tree-lightning-bounds";
		// 最長の枝階層と最大Jitterを使う検査定義。
		FEffectPathDefinition Definition = MakeTreePath();
		Definition.Segments = 128;
		Definition.Branches = 16;
		Definition.Jitter = 1.0f;
		Definition.BranchLength = 2.0f;
		Definition.Tree.Depth = 2;
		Definition.Tree.ChildBranches = 4;
		// 最大構成で計測する始点。
		const FVector3 Start( -3.0f, 2.0f, 1.0f );
		// 最大構成で計測する終点。
		const FVector3 End( 4.0f, 8.0f, -2.0f );
		// 結果球の外へ確保する余白。
		const float32 Padding = 0.75f;
		// bounds APIが返す中心。
		FVector3 Center;
		// bounds APIが返す半径。
		float32 Radius = 0.0f;
		// 入力を受理して球を作れたか。
		const bool bBoundsValid = NEffectPath::GetTreeLightningBounds( Definition, Start, End, Padding, Center, Radius );
		C.Check( bBoundsValid && Center == Start + ( End - Start ) * 0.5f && Radius > 0.5f * ( End - Start ).Length() + Padding, "全階層を含む有限なsphere boundsを計算する" );
		// 全seedの全点が余白込みで球へ収まったか。
		bool bEverySeedInside = bBoundsValid;
		for ( uint32 Seed : { 1u, 991u, 992u, 0xDEADBEEFu } )
		{
			// 指定seedの枝形状。
			FEffectPathGeometry Geometry;
			NEffectPath::BuildTreeLightning( Definition, Seed, 0.0f, Start, End, 0.8f, FColor32( 10, 80, 150, 255 ), Geometry );
			// Geometry生成失敗を通常の検査失敗として残す。
			const bool bGeometrySafe = IsSafeGeometry( Geometry ) && Geometry.Strips.Num() == 81;
			C.Check( bGeometrySafe, "seed %uでbounds対象の最大形状を生成する", Seed );
			if ( !bGeometrySafe ) { bEverySeedInside = false; continue; }
			for ( const FEffectPathGeometry::FStrip& Strip : Geometry.Strips ) for ( const FEffectPathGeometry::FPoint& Point : Strip.Points ) bEverySeedInside = bEverySeedInside && ( Point.Position - Center ).Length() + Padding <= Radius + 1.0e-4f;
		}
		C.Check( bEverySeedInside, "複数seedの全主幹・枝・孫枝点をpadding込みbounds内へ収める" );
		// paddingを除いた幹だけの球は固定値2.75になる検査定義。
		FEffectPathDefinition Straight = MakeTreePath();
		Straight.Jitter = 0.0f;
		Straight.Branches = 0;
		Straight.Tree.Depth = 0;
		// 独立期待の幹だけの球中心。
		FVector3 StraightCenter;
		// 独立期待の幹だけの球半径。
		float32 StraightRadius = 0.0f;
		const bool bStraightBounds = NEffectPath::GetTreeLightningBounds( Straight, FVector3::Zero, FVector3( 0.0f, 4.0f, 0.0f ), 0.75f, StraightCenter, StraightRadius );
		C.Check( bStraightBounds && StraightCenter == FVector3( 0.0f, 2.0f, 0.0f ) && StraightRadius == 2.75f, "直線のDepth0 boundsは独立期待値でCenter(0,2,0)とRadius2.75になる" );
		// 中点がfloat丸めで始点へ寄る、4096付近の隣接座標。
		// 4096付近でfloatの中点丸めを受け取る中心。
		FVector3 RoundedCenter;
		// 終点と余白を含む丸め後の半径。
		float32 RoundedRadius = 0.0f;
		// 中点丸めfixtureの始点。
		const FVector3 RoundedStart( 4096.0f, 0.0f, 0.0f );
		// 中点丸めfixtureの隣接終点。
		const FVector3 RoundedEnd( 4096.00048828125f, 0.0f, 0.0f );
		// 丸め誤差の検査でも球へ加える余白。
		const float32 RoundedPadding = 0.125f;
		// 丸め後の中心と端点を含む球が計算できたか。
		const bool bRoundedBounds = NEffectPath::GetTreeLightningBounds( Straight, RoundedStart, RoundedEnd, RoundedPadding, RoundedCenter, RoundedRadius );
		// 公開centerから終点までdouble精度で測る実距離。
		const float64 RoundedEndDistance = std::fabs( static_cast<float64>( RoundedEnd.x ) - static_cast<float64>( RoundedCenter.x ) );
		// 公開centerから始点までdouble精度で測る実距離。
		const float64 RoundedStartDistance = std::fabs( static_cast<float64>( RoundedStart.x ) - static_cast<float64>( RoundedCenter.x ) );
		C.Check( bRoundedBounds && RoundedStartDistance + static_cast<float64>( RoundedPadding ) <= static_cast<float64>( RoundedRadius ) && RoundedEndDistance + static_cast<float64>( RoundedPadding ) <= static_cast<float64>( RoundedRadius ), "float中心の丸め後も両端距離とpaddingをbounds内へ含める" );
		// 偶数・奇数segmentで再帰二分が同じ偏差上限を守る設定。
		bool bDisplacementBounded = true;
		// 偶数と奇数の両方の再帰分割数。
		for ( const int32 SegmentCount : { 24, 25 } )
		{
			// 対象分割数の直線幹設定。
			FEffectPathDefinition Trunk = Straight;
			Trunk.Segments = SegmentCount;
			Trunk.Jitter = 1.0f;
			// 同一分割数で変位境界を確認するseed群。
			for ( uint32 Seed : { 17u, 991u, 0xDEADBEEFu } )
			{
				// 指定分割数・seedの主幹だけを生成した結果。
				FEffectPathGeometry TrunkGeometry;
				// 偶数・奇数分割の幹が一つの安全なstripか確認する。
				NEffectPath::BuildTreeLightning( Trunk, Seed, 0.0f, FVector3::Zero, FVector3( 0.0f, 4.0f, 0.0f ), 0.8f, FColor32( 10, 80, 150, 255 ), TrunkGeometry );
				const bool bTrunkSafe = IsSafeGeometry( TrunkGeometry ) && TrunkGeometry.Strips.Num() == 1 && TrunkGeometry.Strips[0].Points.Num() == SegmentCount + 1;
				C.Check( bTrunkSafe, "segments=%d seed=%uで全幹点を生成する", SegmentCount, Seed );
				if ( !bTrunkSafe ) { bDisplacementBounded = false; continue; }
				for ( const FEffectPathGeometry::FPoint& Point : TrunkGeometry.Strips[0].Points ) bDisplacementBounded = bDisplacementBounded && DistanceFromChord( Point.Position, FVector3::Zero, FVector3( 0.0f, 4.0f, 0.0f ) ) <= 4.0001f;
			}
		}
		C.Check( bDisplacementBounded, "偶数・奇数segmentと複数seedで幹chord偏差をLength×Jitter以内へ保つ" );
		// 不正入力ごとにfalseを確認する定義状態。
		FVector3 InvalidCenter;
		float32 InvalidRadius = 0.0f;
		C.Check( !NEffectPath::GetTreeLightningBounds( Definition, Start, End, -0.1f, InvalidCenter, InvalidRadius ), "負paddingを拒否する" );
		C.Check( !NEffectPath::GetTreeLightningBounds( Definition, Start, Start, Padding, InvalidCenter, InvalidRadius ), "zero lengthのchordを拒否する" );
		C.Check( !NEffectPath::GetTreeLightningBounds( Definition, FVector3( std::numeric_limits<float32>::quiet_NaN(), 0.0f, 0.0f ), End, Padding, InvalidCenter, InvalidRadius ), "非有限座標を拒否する" );
		Definition.Jitter = std::numeric_limits<float32>::infinity();
		C.Check( !NEffectPath::GetTreeLightningBounds( Definition, Start, End, Padding, InvalidCenter, InvalidRadius ), "非有限Jitterを拒否する" );
	}
}

// TreeLightningの定義互換、枝形状、成長、放電と上限を確認する。
void NWorldTest::RunEffectTreeLightningTests( NAssetTest::FChecker& C )
{
	TestTreeDefinition( C );
	TestTreeShape( C );
	TestTreeTimingAndLimits( C );
	TestTreeBounds( C );
}

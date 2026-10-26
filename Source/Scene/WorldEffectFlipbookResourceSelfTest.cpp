// SPDX-License-Identifier: NOASSERTION
#include <algorithm>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <vector>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Asset/AssetManager.h"
#include "Library/Framework/Asset/SpriteSheet.h"
#include "Library/Framework/Effect/EffectFlipbookFrames.h"
#include "WorldEffectSelfTestSupport.h"

namespace
{
	// 同じテスト内で異なる画像とシートを作る。
	bool WriteSheetFixture( const FString& Dir, const FString& Stem, FSpriteSheetDefinition Definition )
	{
		// シートが参照するBMP名。
		Definition.TexturePath = Stem + ".bmp";
		// fixture用画像のバイト列。
		const std::vector<uint8> Image = NAssetTest::MakeSheetBmp();
		// JSONを書き込む文字列。
		const std::string Json = Definition.ToJson().dump();
		return NAssetTest::WriteFileBytes( Dir / Definition.TexturePath, Image ) && NAssetTest::WriteFileText( Dir / ( Stem + ".sheet.json" ), Json.c_str() );
	}

	// 範囲とclipを指定した小さなシート定義を作る。
	FSpriteSheetDefinition MakeDefinition( const std::vector<int32>& FrameIndices, float32 Fps = 4.0f, const std::vector<float32>& Durations = {} )
	{
		// 生成するsheet定義。
		FSpriteSheetDefinition Definition;
		// 追加する固定枚数のテスト矩形番号。
		for ( int32 Index = 0; Index < 2; ++Index )
		{
			// 互いに重ならない16x16の矩形。
			FSpriteFrame Frame;
			Frame.X = 3 + Index * 18;
			Frame.Y = 3;
			Frame.Width = 16;
			Frame.Height = 16;
			Definition.Frames.Add( Frame );
		}
		// 再生順を保持するclip。
		FSpriteClip Clip;
		Clip.Name = "main";
		Clip.Fps = Fps;
		Clip.Loop = EAnimLoopMode::Loop;
		// clipへ追加する元frame番号。
		for ( const int32 Index : FrameIndices ) Clip.Frames.Add( Index );
		// clipへ追加する明示duration。
		for ( const float32 Duration : Durations ) Clip.Durations.Add( Duration );
		Definition.Clips.Add( Clip );
		return Definition;
	}

	// fixtureシートを有効にするflipbook設定。
	FEffectFlipbookDefinition MakeFlipbook( const FString& Stem )
	{
		// 有効化済みのシート参照。
		FEffectFlipbookDefinition Definition;
		Definition.bEnabled = true;
		Definition.Sheet = Stem + ".sheet.json";
		return Definition;
	}

	// AssetManagerの期限切れ弱参照と遅延解放を掃除する。
	void CollectReleasedTextures( CAssetManager& Assets )
	{
		Assets.ReleaseUnused();
		Assets.Tick( 0.0f );
	}

	// 初期から存在したtexture以外が全て解放されたか調べる。
	bool HasOnlyInitialTextures( const NAssetTest::FFakeAssetBackend& Fake, const std::set<int32>& Initial )
	{
		return std::all_of( Fake.Textures.begin(), Fake.Textures.end(), [&Initial]( int32 Handle ) { return Initial.count( Handle ) != 0; } );
	}

	// シートの読み込みと派生領域の所有、失敗時rollbackを調べる。
	void TestBuildFramesAndRollback( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
	{
		// 呼出し元のログ分類を最後に戻す値。
		const char* PreviousGroup = C.Group;
		C.Group = "effect-flipbook-resources";
		// texture寸法とregion capabilityをテスト区間だけ保存する。
		const int32 PreviousWidth = Fake.TextureWidth;
		const int32 PreviousHeight = Fake.TextureHeight;
		const bool PreviousSupport = Fake.bSupportTextureRegions;
		const int32 PreviousCreateFailure = Fake.FailTextureRegionCall;
		const int32 PreviousUvFailure = Fake.FailTextureUvCall;
		Fake.TextureWidth = NAssetTest::SheetWidth;
		Fake.TextureHeight = NAssetTest::SheetHeight;
		Fake.bSupportTextureRegions = false;
		Fake.FailTextureRegionCall = 0;
		Fake.FailTextureUvCall = 0;
		// テスト開始時に別の確認が所有していたhandle集合。
		const std::set<int32> InitialTextures = Fake.Textures;

		// 既定のfakeは派生領域非対応で、親textureだけを一時作成する。
		WriteSheetFixture( Dir, "flipbook-resource-default", MakeDefinition( { 0, 1 } ) );
		{
			// default設定で作ったシートと出力先。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-default.sheet.json" );
			FEffectFlipbookFrames Out;
			// 対応外backendへ渡したregion作成回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 親画像を読み込んだ後のhandle数。
			const int32 HandlesBefore = static_cast<int32>( Fake.Textures.size() );
			// BuildFramesが返す失敗理由。
			FString Error;
			// 対応外backendで全frameを準備できたか。
			const bool bBuilt = Sheet.IsValid() && NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-default" ), Assets, Out, &Error );
			C.Check( Sheet.IsValid() && !bBuilt && Fake.NumTextureRegionCalls == CallsBefore && static_cast<int32>( Fake.Textures.size() ) == HandlesBefore, "region非対応の既定fakeでは親以外を作らずBuildFramesを失敗させる" );
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "region非対応の試行後に親textureを解放する" );

		// 2枚目のregion作成を失敗させ、既存Outと最初の子handleを守る。
		Fake.bSupportTextureRegions = true;
		WriteSheetFixture( Dir, "flipbook-resource-create-failure", MakeDefinition( { 0, 1 } ) );
		// 失敗前に作られ、rollbackで消える先頭child handle。
		int32 FirstCreatedHandle = -1;
		{
			// failure fixtureの親と既存snapshot相当の出力。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-create-failure.sheet.json" );
			FEffectFlipbookFrames Out;
			TArray<float32> SentinelDuration;
			SentinelDuration.Add( 0.75f );
			Out.Timeline = FAnimTimeline::Build( SentinelDuration, EAnimLoopMode::Once );
			if ( Sheet.IsValid() ) Out.Frames.Add( Sheet->GetTexture() );
			// 失敗時にも残るべき先頭snapshot画像。
			const TSharedPtr<CTexture> PreviousFrame = Out.Frames.IsEmpty() ? nullptr : Out.Frames[0];
			// 失敗時にも残るべきsnapshot時間長。
			const float32 PreviousTotal = Out.Timeline.Total;
			// この試行が追加する領域recordの開始位置。
			const int32 RecordStart = static_cast<int32>( Fake.TextureRegionRecords.size() );
			Fake.FailTextureRegionCall = Fake.NumTextureRegionCalls + 2;
			// region作成失敗の理由を受ける文字列。
			FString Error;
			// 二枚目を意図的に失敗させた構築結果。
			const bool bBuilt = Sheet.IsValid() && NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-create-failure" ), Assets, Out, &Error );
			if ( Fake.TextureRegionRecords.size() > static_cast<size_t>( RecordStart ) ) FirstCreatedHandle = Fake.TextureRegionRecords[RecordStart].Handle;
			C.Check( Sheet.IsValid() && !bBuilt && Fake.TextureRegionRecords.size() == static_cast<size_t>( RecordStart + 2 ) && FirstCreatedHandle >= 0 && Fake.Textures.count( FirstCreatedHandle ) == 0, "2枚目の領域作成失敗で先に作った子画像も解放する" );
			C.Check( Out.Frames.Num() == 1 && Out.Frames[0].Get() == PreviousFrame.Get() && Out.Timeline.Total == PreviousTotal, "部分失敗時に呼出し側のsnapshotを変更しない" );
		}
		Fake.FailTextureRegionCall = PreviousCreateFailure;
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "作成失敗後に親と子の全texture handleを解放する" );

		// UV取得失敗時に作成済み子handleを削除し、既存出力を保つ。
		WriteSheetFixture( Dir, "flipbook-resource-uv-failure", MakeDefinition( { 0, 1 } ) );
		// UV取得失敗で削除される子handle。
		int32 UvFailedHandle = -1;
		{
			// UV失敗fixtureの親と既存出力。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-uv-failure.sheet.json" );
			FEffectFlipbookFrames Out;
			TArray<float32> SentinelDuration;
			SentinelDuration.Add( 0.5f );
			Out.Timeline = FAnimTimeline::Build( SentinelDuration, EAnimLoopMode::Once );
			if ( Sheet.IsValid() ) Out.Frames.Add( Sheet->GetTexture() );
			// 失敗時にも残るべき先頭snapshot画像。
			const TSharedPtr<CTexture> PreviousFrame = Out.Frames.IsEmpty() ? nullptr : Out.Frames[0];
			// 失敗時にも残るべきsnapshot時間長。
			const float32 PreviousTotal = Out.Timeline.Total;
			// この試行が追加する領域recordの開始位置。
			const int32 RecordStart = static_cast<int32>( Fake.TextureRegionRecords.size() );
			Fake.FailTextureUvCall = Fake.NumTextureUvCalls + 1;
			// UV失敗の理由を受ける文字列。
			FString Error;
			// 最初のchild UV取得を意図的に失敗させた構築結果。
			const bool bBuilt = Sheet.IsValid() && NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-uv-failure" ), Assets, Out, &Error );
			if ( Fake.TextureRegionRecords.size() > static_cast<size_t>( RecordStart ) ) UvFailedHandle = Fake.TextureRegionRecords[RecordStart].Handle;
			C.Check( Sheet.IsValid() && !bBuilt && UvFailedHandle >= 0 && Fake.Textures.count( UvFailedHandle ) == 0 && std::find( Fake.DeletedTextures.begin(), Fake.DeletedTextures.end(), UvFailedHandle ) != Fake.DeletedTextures.end(), "UV取得失敗時は作成済み子handleを削除する" );
			C.Check( Out.Frames.Num() == 1 && Out.Frames[0].Get() == PreviousFrame.Get() && Out.Timeline.Total == PreviousTotal, "UV取得失敗でも既存snapshotを維持する" );
		}
		Fake.FailTextureUvCall = PreviousUvFailure;
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "UV失敗後に親textureを解放する" );

		// 同じシートを繰り返し記録して、region共有とsnapshotの値固定を調べる。
		WriteSheetFixture( Dir, "flipbook-resource-snapshot", MakeDefinition( { 0, 1 } ) );
		// child以外のownerを外した親textureの弱参照。
		TWeakPtr<CTexture> ParentWeak;
		// 最終解放順を確かめる親handle。
		int32 ParentHandle = -1;
		// snapshotの最初のregion handle。
		int32 SnapshotRegion0 = -1;
		// snapshotの二番目のregion handle。
		int32 SnapshotRegion1 = -1;
		{
			// snapshotを共有している間は同じ矩形のregionが再利用される。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-snapshot.sheet.json" );
			FEffectFlipbookFrames First;
			FEffectFlipbookFrames Second;
			FString Error;
			// 最初のsnapshot前のregion作成回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 最初のsnapshot構築結果。
			const bool bFirst = Sheet.IsValid() && NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-snapshot" ), Assets, First, &Error );
			// 最初のsnapshot後のregion作成回数。
			const int32 CallsAfterFirst = Fake.NumTextureRegionCalls;
			// 同じsheetから二度目に構築した結果。
			const bool bSecond = Sheet.IsValid() && NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-snapshot" ), Assets, Second, &Error );
			// region handleからfake backendが返す矩形寸法。
			int32 RegionWidth = 0;
			int32 RegionHeight = 0;
			// 親handleからfake backendが返す画像全体の寸法。
			int32 ParentWidth = 0;
			int32 ParentHeight = 0;
			if ( First.Frames.Num() >= 2 && First.Frames[0].IsValid() && First.Frames[1].IsValid() ) { SnapshotRegion0 = First.Frames[0]->GetHandle(); SnapshotRegion1 = First.Frames[1]->GetHandle(); }
			// 元sheetを変えても維持すべきsnapshot長さ。
			const float32 SnapshotTotal = First.Timeline.Total;
			if ( Sheet.IsValid() && Sheet->GetTexture().IsValid() ) { ParentWeak = Sheet->GetTexture(); ParentHandle = Sheet->GetTexture()->GetHandle(); }
			C.Check( bFirst && bSecond && First.Frames.Num() == 2 && Second.Frames.Num() == 2 && First.Frames[0].Get() == Second.Frames[0].Get() && First.Frames[1].Get() == Second.Frames[1].Get() && CallsAfterFirst == CallsBefore + 2 && Fake.NumTextureRegionCalls == CallsAfterFirst, "BuildFramesの再記録は同じ親矩形の生存regionを共有する" );
			C.Check( First.Frames.Num() >= 1 && First.Frames[0].IsValid() && First.Frames[0]->GetWidth() == 16 && First.Frames[0]->GetHeight() == 16 && First.Frames[0]->GetMemoryBytes() == 0 && SnapshotRegion0 >= 0 && SnapshotRegion1 >= 0, "派生regionは親textureの重複メモリを数えず寸法を返す" );
			// 元sheetへ後から設定する別の矩形定義。
			FSpriteSheetDefinition Changed = Sheet.IsValid() ? Sheet->GetDefinition() : FSpriteSheetDefinition();
			if ( Changed.Frames.IsValidIndex( 0 ) ) Changed.Frames[0].X = 40;
			if ( Sheet.IsValid() ) Sheet->SetDefinition( Changed );
			C.Check( First.Frames.Num() == 2 && First.Frames[0].IsValid() && First.Frames[0]->GetHandle() == SnapshotRegion0 && First.Timeline.Total == SnapshotTotal, "元シートの定義を差し替えても既存snapshotの矩形とtimelineを混ぜない" );
			Sheet.Reset();
			Second = FEffectFlipbookFrames();
			// 領域handleからfake backendが返した矩形寸法。
			const bool bRegionSize = SnapshotRegion0 >= 0 && Fake.GetTextureSize( SnapshotRegion0, RegionWidth, RegionHeight );
			// 親handleからfake backendが返した画像全体寸法。
			const bool bParentSize = ParentHandle >= 0 && Fake.GetTextureSize( ParentHandle, ParentWidth, ParentHeight );
			C.Check( bRegionSize && RegionWidth == 16 && RegionHeight == 16 && bParentSize && ParentWidth == NAssetTest::SheetWidth && ParentHeight == NAssetTest::SheetHeight, "fake backendは子に領域寸法、親に画像全体寸法を返す" );
			C.Check( ParentWeak.Pin().IsValid(), "派生regionが親textureの寿命を保持する" );
			First = FEffectFlipbookFrames();
		}
		CollectReleasedTextures( Assets );
		// 親handleが解放された履歴位置。
		const auto ParentDelete = std::find( Fake.DeletedTextures.begin(), Fake.DeletedTextures.end(), ParentHandle );
		// 先頭child handleが解放された履歴位置。
		const auto Region0Delete = std::find( Fake.DeletedTextures.begin(), Fake.DeletedTextures.end(), SnapshotRegion0 );
		// 二番目child handleが解放された履歴位置。
		const auto Region1Delete = std::find( Fake.DeletedTextures.begin(), Fake.DeletedTextures.end(), SnapshotRegion1 );
		C.Check( !ParentWeak.Pin().IsValid() && Fake.Textures.count( ParentHandle ) == 0 && Fake.Textures.count( SnapshotRegion0 ) == 0 && Fake.Textures.count( SnapshotRegion1 ) == 0 && ParentDelete != Fake.DeletedTextures.end() && Region0Delete != Fake.DeletedTextures.end() && Region1Delete != Fake.DeletedTextures.end() && Region0Delete < ParentDelete && Region1Delete < ParentDelete, "全snapshotを手放すと全子handleを親より先に解放する" );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "snapshot所有検査後に作成資源を残さない" );

		// 同一pathの別source handleを誤って同じcache keyにしない。
		{
			// 同じ表示pathを持つ別々の親texture。
			// 一つ目のfake親handle。
			const int32 SourceHandleA = Fake.LoadTexture( "manual-same-path", false );
			// 二つ目のfake親handle。
			const int32 SourceHandleB = Fake.LoadTexture( "manual-same-path", false );
			// 一つ目の独立したtexture owner。
			TSharedPtr<CTexture> SourceA = MakeShared<CTexture>( "manual-same-path", SourceHandleA, false );
			// 同じpathでも別handleを持つtexture owner。
			TSharedPtr<CTexture> SourceB = MakeShared<CTexture>( "manual-same-path", SourceHandleB, false );
			// RegionAが保持する親の弱参照。
			TWeakPtr<CTexture> SourceWeak;
			if ( SourceA.IsValid() ) SourceWeak = SourceA;
			// 最初のregion作成前の回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// SourceAの最初の矩形region。
			TSharedPtr<CTexture> RegionA = Assets.CreateTextureRegion( SourceA, 1, 2, 8, 9 );
			// 生存中の同じ矩形から再利用するregion。
			TSharedPtr<CTexture> RegionAgain = Assets.CreateTextureRegion( SourceA, 1, 2, 8, 9 );
			// SourceBの同じpath/矩形から作る別region。
			TSharedPtr<CTexture> RegionB = Assets.CreateTextureRegion( SourceB, 1, 2, 8, 9 );
			// 三要求後のbackend作成回数。
			const int32 CallsAfterCreates = Fake.NumTextureRegionCalls;
			// 子の生存による親保持を確かめる一時pin。
			TSharedPtr<CTexture> ParentPin = SourceWeak.Pin();
			C.Check( RegionA.IsValid() && RegionAgain.Get() == RegionA.Get() && RegionB.IsValid() && RegionB.Get() != RegionA.Get() && CallsAfterCreates == CallsBefore + 2, "同じ親矩形だけを再利用し同じpathの別親は分離する" );
			C.Check( Fake.TextureRegionRecords.size() >= 2 && Fake.TextureRegionRecords[Fake.TextureRegionRecords.size() - 2].Source == SourceHandleA && Fake.TextureRegionRecords.back().Source == SourceHandleB && Fake.TextureRegionRecords.back().X == 1 && Fake.TextureRegionRecords.back().Y == 2, "fakeが親handleと領域矩形を作成記録へ残す" );
			C.Check( RegionA.IsValid() && ParentPin.IsValid() && RegionA->GetMemoryBytes() == 0 && RegionA->GetWidth() == 8 && RegionA->GetHeight() == 9, "子画像の共有所有中は親を保持しregion寸法を返す" );
			ParentPin.Reset();
			SourceA.Reset();
			// RegionAがまだある間の親保持結果。
			const bool bParentRetained = SourceWeak.Pin().IsValid();
			RegionA.Reset();
			RegionAgain.Reset();
			// RegionAを全て手放した後の親保持結果。
			const bool bParentRetainedBySecondSource = SourceWeak.Pin().IsValid();
			C.Check( bParentRetained && !bParentRetainedBySecondSource, "最後のregion ownerを手放すと元texture参照が切れる" );
			SourceB.Reset();
			RegionB.Reset();
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "同一pathの親分離検査後に全textureを解放する" );

		// 不正なboundsはbackendへ渡さずregion作成も記録しない。
		{
			// bounds拒否の親texture。
			// bounds検査用の親handle。
			const int32 SourceHandle = Fake.LoadTexture( "manual-bounds", false );
			// 無効矩形が参照する親texture。
			TSharedPtr<CTexture> Source = MakeShared<CTexture>( "manual-bounds", SourceHandle, false );
			// 無効矩形を渡す前のregion作成数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 左端外へ出る矩形。
			const TSharedPtr<CTexture> Negative = Assets.CreateTextureRegion( Source, -1, 0, 8, 8 );
			// 右端外へ出る矩形。
			const TSharedPtr<CTexture> Outside = Assets.CreateTextureRegion( Source, NAssetTest::SheetWidth - 4, 0, 8, 8 );
			// 幅が零の矩形。
			const TSharedPtr<CTexture> Zero = Assets.CreateTextureRegion( Source, 0, 0, 0, 8 );
			C.Check( !Negative.IsValid() && !Outside.IsValid() && !Zero.IsValid() && Fake.NumTextureRegionCalls == CallsBefore, "負値・画像外・零寸法のregionを作成前に拒否する" );
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "bounds拒否後に親textureを解放する" );

		// clip/frame/durationとfallback FPSを派生画像作成前に検証する。
		// 不正定義を保存し、BuildFramesの失敗と事前拒否を共通検査する。
		const auto CheckRejectedFixture = [&]( const FString& Stem, const FSpriteSheetDefinition& Definition, const char* Description )
		{
			// 検証対象のシートfile。
			FSpriteSheetDefinition Copy = Definition;
			const bool bWritten = WriteSheetFixture( Dir, Stem, Copy );
			// 失敗前のregion作成回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 失敗時に空のまま残す出力snapshot。
			FEffectFlipbookFrames Out;
			// 読み込み/検証失敗の理由。
			FString Error;
			// 不正sheetを構築できたか。
			const bool bBuilt = NEffectFlipbook::BuildFrames( MakeFlipbook( Stem ), Assets, Out, &Error );
			C.Check( bWritten && !bBuilt && !Error.IsEmpty() && Fake.NumTextureRegionCalls == CallsBefore, Description );
			CollectReleasedTextures( Assets );
			C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "不正clip検査は派生textureを残さない" );
		};
		// クリップを持たないsheet。
		FSpriteSheetDefinition MissingClip;
		CheckRejectedFixture( "flipbook-resource-no-clip", MissingClip, "clip無しのsheetを拒否する" );
		CheckRejectedFixture( "flipbook-resource-bad-index", MakeDefinition( { 99 } ), "存在しないframe indexを拒否する" );
		// 画像境界を越えるrectangleを持つsheet。
		FSpriteSheetDefinition OutsideFrame = MakeDefinition( { 0 } );
		OutsideFrame.Frames[0].X = NAssetTest::SheetWidth - 4;
		CheckRejectedFixture( "flipbook-resource-outside-frame", OutsideFrame, "texture外へ出るframe矩形を拒否する" );
		CheckRejectedFixture( "flipbook-resource-zero-duration", MakeDefinition( { 0 }, 4.0f, { 0.0f } ), "零durationをfallbackへ置き換えず拒否する" );
		CheckRejectedFixture( "flipbook-resource-invalid-fps", MakeDefinition( { 0 }, 0.0f ), "duration省略時の非正FPSを拒否する" );
		CheckRejectedFixture( "flipbook-resource-negative-duration", MakeDefinition( { 0 }, 4.0f, { -0.25f } ), "負のdurationを拒否する" );
		CheckRejectedFixture( "flipbook-resource-negative-fps", MakeDefinition( { 0 }, -4.0f ), "duration省略時の負FPSを拒否する" );

		// 同じpathで読み込んだ定義へNaN durationを直接入れ、region作成前の拒否を調べる。
		WriteSheetFixture( Dir, "flipbook-resource-nan-duration", MakeDefinition( { 0 }, 4.0f, { 0.25f } ) );
		{
			// 同じpathのsheet cacheを保持する。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-nan-duration.sheet.json" );
			// NaNを直接設定する定義。
			FSpriteSheetDefinition Changed = Sheet.IsValid() ? Sheet->GetDefinition() : FSpriteSheetDefinition();
			// NaN durationがあっても追加regionを作らない回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// BuildFramesが変更済みcacheを取得したか調べる再読み込み。
			TSharedPtr<CSpriteSheet> CachedAgain = Assets.LoadSpriteSheet( "flipbook-resource-nan-duration.sheet.json" );
			// NaN durationを含むsnapshot。
			FEffectFlipbookFrames Out;
			// 検証失敗の理由。
			FString Error;
			const bool bHasClip = Changed.Clips.IsValidIndex( 0 );
			const bool bHasDuration = bHasClip && Changed.Clips[0].Durations.IsValidIndex( 0 );
			if ( bHasDuration ) Changed.Clips[0].Durations[0] = TNumericLimits<float32>::QuietNaN();
			if ( Sheet.IsValid() ) Sheet->SetDefinition( Changed );
			const bool bBuilt = NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-nan-duration" ), Assets, Out, &Error );
			C.Check( bHasDuration && CachedAgain.IsValid() && CachedAgain.Get() == Sheet.Get() && !bBuilt && !Error.IsEmpty() && Fake.NumTextureRegionCalls == CallsBefore, "同pathのcached sheetへ直接入れたNaN durationを領域作成前に拒否する" );
			Sheet.Reset();
			CachedAgain.Reset();
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "NaN durationの拒否後に親textureを解放する" );

		// duration省略時は同pathのcached sheetへ直接入れたNaN FPSを拒否する。
		WriteSheetFixture( Dir, "flipbook-resource-nan-fps-missing-duration", MakeDefinition( { 0, 1 }, 4.0f ) );
		{
			// durationが空のsheet cache。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-nan-fps-missing-duration.sheet.json" );
			// NaN FPSを設定する定義。
			FSpriteSheetDefinition Changed = Sheet.IsValid() ? Sheet->GetDefinition() : FSpriteSheetDefinition();
			// BuildFramesが同じcached sheetを使うか確認する参照。
			TSharedPtr<CSpriteSheet> CachedAgain = Assets.LoadSpriteSheet( "flipbook-resource-nan-fps-missing-duration.sheet.json" );
			// region作成を増やしてはいけない開始回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 失敗時に空のまま残すsnapshot。
			FEffectFlipbookFrames Out;
			// 検証失敗の理由。
			FString Error;
			const bool bHasClip = Changed.Clips.IsValidIndex( 0 );
			const bool bNoDurations = bHasClip && Changed.Clips[0].Durations.IsEmpty();
			if ( bHasClip ) Changed.Clips[0].Fps = TNumericLimits<float32>::QuietNaN();
			if ( Sheet.IsValid() ) Sheet->SetDefinition( Changed );
			const bool bBuilt = NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-nan-fps-missing-duration" ), Assets, Out, &Error );
			C.Check( bNoDurations && CachedAgain.IsValid() && CachedAgain.Get() == Sheet.Get() && !bBuilt && !Error.IsEmpty() && Fake.NumTextureRegionCalls == CallsBefore, "duration未指定時のNaN fallback FPSを領域作成前に拒否する" );
			Sheet.Reset();
			CachedAgain.Reset();
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "NaN fallback FPSの拒否後に親textureを解放する" );

		// 全durationが有効なら未使用のNaN FPSを参照せず受理する。
		WriteSheetFixture( Dir, "flipbook-resource-nan-fps-explicit", MakeDefinition( { 0, 1 }, 4.0f, { 0.25f, 0.5f } ) );
		{
			// 明示durationを持つsheet cache。
			TSharedPtr<CSpriteSheet> Sheet = Assets.LoadSpriteSheet( "flipbook-resource-nan-fps-explicit.sheet.json" );
			// FPSだけNaNへ変えた有効duration定義。
			FSpriteSheetDefinition Changed = Sheet.IsValid() ? Sheet->GetDefinition() : FSpriteSheetDefinition();
			// 成功時に作るsnapshot。
			FEffectFlipbookFrames Out;
			// 成功前のregion作成回数。
			const int32 CallsBefore = Fake.NumTextureRegionCalls;
			// 構築時の警告文字列。
			FString Error;
			if ( Changed.Clips.IsValidIndex( 0 ) ) Changed.Clips[0].Fps = TNumericLimits<float32>::QuietNaN();
			if ( Sheet.IsValid() ) Sheet->SetDefinition( Changed );
			const bool bBuilt = NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-nan-fps-explicit" ), Assets, Out, &Error );
			C.Check( Sheet.IsValid() && Changed.Clips.IsValidIndex( 0 ) && Changed.Clips[0].Durations.Num() == 2 && bBuilt && Out.Timeline.StepEnd.Num() == 2 && Out.Timeline.StepEnd[0] == 0.25f && Out.Timeline.StepEnd[1] == 0.75f && Fake.NumTextureRegionCalls == CallsBefore + 2, "全duration指定時は未使用のNaN FPSを許可する" );
			Out = FEffectFlipbookFrames();
			Sheet.Reset();
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "明示durationのNaN FPS成功後にsnapshotを解放する" );

		// duration省略時だけ正のfallback FPSを秒数へ変換する。
		WriteSheetFixture( Dir, "flipbook-resource-fallback-fps", MakeDefinition( { 0, 1 }, 4.0f ) );
		{
			// fallback FPSを読むsheetとtimeline。
			// 成功時に作るsnapshot。
			FEffectFlipbookFrames Out;
			// BuildFramesの警告を受ける文字列。
			FString Error;
			// 正のfallback FPSでsnapshotを作れたか。
			const bool bBuilt = NEffectFlipbook::BuildFrames( MakeFlipbook( "flipbook-resource-fallback-fps" ), Assets, Out, &Error );
			C.Check( bBuilt && Out.Timeline.StepEnd.Num() == 2 && Out.Timeline.StepEnd[0] == 0.25f && Out.Timeline.StepEnd[1] == 0.5f, "正のfallback FPSから各コマの表示秒を作る" );
		}
		CollectReleasedTextures( Assets );
		C.Check( HasOnlyInitialTextures( Fake, InitialTextures ), "fallback FPS成功後もsnapshotを解放する" );
		Fake.TextureWidth = PreviousWidth;
		Fake.TextureHeight = PreviousHeight;
		Fake.bSupportTextureRegions = PreviousSupport;
		Fake.FailTextureRegionCall = PreviousCreateFailure;
		Fake.FailTextureUvCall = PreviousUvFailure;
		C.Group = PreviousGroup;
	}
}

// fake backendで派生画像の作成・共有・失敗時の所有解放を確認する。
void NWorldTest::RunEffectFlipbookResourceTests( NAssetTest::FChecker& C, CAssetManager& Assets, NAssetTest::FFakeAssetBackend& Fake, const FString& Dir )
{
	TestBuildFramesAndRollback( C, Assets, Fake, Dir );
}

// SPDX-License-Identifier: MIT
#include "EffectEditorCurveInput.h"


#include "Library/Core/Types/Numeric.h"
#include "Library/DebugUI/EditorCommon.h"
#include "Library/Math/MathUtils.h"

namespace
{
	// 操作を離したとき、同じ入力欄への連続変更を一つの履歴sessionに保つ。
	bool TrackInputEnd( bool bChanged, TParticleEffectDocument& Document )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() ) Document.EndEditSession();
		return bChanged;
	}

	// 入力終了を呼出側へ通知し、particle/world双方の履歴所有者を切り離す。
	bool TrackInputEnd( bool bChanged, bool* bInputEnded )
	{
		if ( ImGui::IsItemDeactivatedAfterEdit() && bInputEnded != nullptr ) *bInputEnded = true;
		return bChanged;
	}

	// scalar curveの最大空き区間へ、有限かつ重複しないキーを追加する。
	bool AddScalarCurveKey( FEffectScalarCurve& Curve, float32 DefaultValue )
	{
		if ( Curve.Keys.Num() >= FEffectScalarCurve::MaxKeys ) return false;
		// 各曲線keyを一つずつ検査する。
		for ( int32 Index = 0; Index < Curve.Keys.Num(); ++Index )
		{
			// 検証する既存キーの時刻。
			const float32 Time = Curve.Keys[Index].Time;
			if ( !NMath::IsFinite( Time ) || Time < 0.0f || Time > 1.0f || ( Index > 0 && Time <= Curve.Keys[Index - 1].Time ) ) return false;
		}
		// 最大の空き区間の先頭時刻。
		float32 BestStart = 0.0f;
		// 最大の空き区間の末尾時刻。
		float32 BestEnd = Curve.Keys.IsEmpty() ? 1.0f : Curve.Keys[0].Time;
		// 現在選んでいる空き区間の幅。
		float32 BestWidth = BestEnd - BestStart;
		// 各曲線keyを一つずつ検査する。
		for ( int32 Index = 0; Index < Curve.Keys.Num(); ++Index )
		{
			// 比較する空き区間の先頭。
			const float32 Start = Curve.Keys[Index].Time;
			// 比較する空き区間の末尾。
			const float32 End = Index + 1 < Curve.Keys.Num() ? Curve.Keys[Index + 1].Time : 1.0f;
			if ( End - Start > BestWidth ) { BestStart = Start; BestEnd = End; BestWidth = End - Start; }
		}
		// 最大区間の中央に置く新しいキー時刻。
		const float32 Time = BestStart + BestWidth * 0.5f;
		if ( !NMath::IsFinite( Time ) || Time <= BestStart || Time >= BestEnd ) return false;
		// 新しいscalar keyへ時刻と初期値を格納する。
		FEffectScalarCurve::FKey Key;
		Key.Time = Time;
		Key.Value = NMath::Clamp( DefaultValue, 0.0f, 32.0f );
		// 昇順を保つ挿入先index。
		int32 InsertAt = 0;
		while ( InsertAt < Curve.Keys.Num() && Curve.Keys[InsertAt].Time < Time ) ++InsertAt;
		Curve.Keys.Insert( Key, InsertAt );
		return true;
	}

	// 色曲線の最大空き区間へ、現在の評価色を初期値としてキーを追加する。
	bool AddColorCurveKey( FEffectColorCurve& Curve )
	{
		if ( Curve.Keys.Num() >= FEffectColorCurve::MaxKeys ) return false;
		// 各曲線keyを一つずつ検査する。
		for ( int32 Index = 0; Index < Curve.Keys.Num(); ++Index )
		{
			const FEffectColorCurve::FKey& Key = Curve.Keys[Index];
			if ( !NMath::IsFinite( Key.Time ) || Key.Time < 0.0f || Key.Time > 1.0f || ( Index > 0 && Key.Time <= Curve.Keys[Index - 1].Time ) ) return false;
		}
		// 最大の空き区間の先頭時刻。
		float32 BestStart = 0.0f;
		// 最大の空き区間の末尾時刻。
		float32 BestEnd = Curve.Keys.IsEmpty() ? 1.0f : Curve.Keys[0].Time;
		// 現在選んでいる空き区間の幅。
		float32 BestWidth = BestEnd - BestStart;
		// 各曲線keyを一つずつ検査する。
		for ( int32 Index = 0; Index < Curve.Keys.Num(); ++Index )
		{
			// 比較する空き区間の先頭。
			const float32 Start = Curve.Keys[Index].Time;
			// 比較する空き区間の末尾。
			const float32 End = Index + 1 < Curve.Keys.Num() ? Curve.Keys[Index + 1].Time : 1.0f;
			if ( End - Start > BestWidth ) { BestStart = Start; BestEnd = End; BestWidth = End - Start; }
		}
		// 最大区間の中央に置く新しいキー時刻。
		const float32 Time = BestStart + BestWidth * 0.5f;
		if ( !NMath::IsFinite( Time ) || Time <= BestStart || Time >= BestEnd ) return false;
		// 新しいcolor keyへ時刻と既存曲線の評価色を格納する。
		FEffectColorCurve::FKey Key;
		Key.Time = Time;
		Key.Value = Curve.Evaluate( Time, FColor::White );
		// 昇順を保つ挿入先index。
		int32 InsertAt = 0;
		while ( InsertAt < Curve.Keys.Num() && Curve.Keys[InsertAt].Time < Time ) ++InsertAt;
		Curve.Keys.Insert( Key, InsertAt );
		return true;
	}

	// 入力へ渡す倍率を表示範囲へ収め、定義の不正値は表示中に変更しない。
	bool IsFiniteNormalizedColor( const FColor& Color )
	{
		return NMath::IsFinite( Color.r ) && NMath::IsFinite( Color.g ) && NMath::IsFinite( Color.b ) && NMath::IsFinite( Color.a ) && Color.r >= 0.0f && Color.r <= 1.0f && Color.g >= 0.0f && Color.g <= 1.0f && Color.b >= 0.0f && Color.b <= 1.0f && Color.a >= 0.0f && Color.a <= 1.0f;
	}
}

// 一つのscalar curveを補間方法・キー・グラフ付きで編集する。
namespace
{
	// 履歴の所有者を受け取らず、一つのscalar curve欄を描く。
	bool DrawScalarCurveValues( const char* Label, FEffectScalarCurve& Curve, float32 DefaultValue, float32 Minimum, float32 Maximum, bool* bInputEnded )
{
	// この曲線で保存定義に反映する変更の有無。
	bool bChanged = false;
	ImGui::PushID( Label );
	ImGui::TextUnformatted( Label );
	// 保存enumの順に並ぶ補間方式の表示名。
	static const char* InterpolationNames[] = { "Linear", "Step", "Smooth" };
	// Comboへ渡す現在の補間方式番号。
	int32 Interpolation = static_cast<int32>( Curve.Interpolation );
	if ( Interpolation < 0 || Interpolation >= 3 ) Interpolation = 0;
	ImGui::SetNextItemWidth( 140.0f );
	if ( ImGui::Combo( "Interpolation", &Interpolation, InterpolationNames, 3 ) )
	{
		Curve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( Interpolation );
		bChanged = true;
		if ( bInputEnded != nullptr ) *bInputEnded = true;
	}
	ImGui::SameLine();
	ImGui::BeginDisabled( Curve.Keys.Num() >= FEffectScalarCurve::MaxKeys );
	if ( ImGui::SmallButton( "Add key" ) )
	{
		const bool bAdded = AddScalarCurveKey( Curve, DefaultValue );
		bChanged |= bAdded;
		if ( bAdded && bInputEnded != nullptr ) *bInputEnded = true;
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	// Resetやキー削除で修復できる曲線の問題一覧。
	const TArray<FString> CurveIssues = Curve.Validate();
	// 既定曲線と違う値が保存されているか。
	const bool bCurveNeedsReset = !Curve.Keys.IsEmpty() || Curve.Interpolation != FEffectScalarCurve::EInterpolation::Linear || !CurveIssues.IsEmpty();
	if ( ImGui::SmallButton( "Reset" ) && bCurveNeedsReset ) { Curve = FEffectScalarCurve(); bChanged = true; if ( bInputEnded != nullptr ) *bInputEnded = true; }
	if ( !CurveIssues.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Curve data is invalid. Remove a key or reset this curve." );
	if ( Curve.Keys.IsEmpty() ) ImGui::TextDisabled( "No keys: uses %.3g for the whole lifetime", DefaultValue );
	if ( Curve.Keys.Num() > FEffectScalarCurve::MaxKeys )
	{
		ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Only the first %d keys are shown.", FEffectScalarCurve::MaxKeys );
		if ( ImGui::SmallButton( "Keep first 32 keys" ) ) { while ( Curve.Keys.Num() > FEffectScalarCurve::MaxKeys ) Curve.Keys.RemoveAt( Curve.Keys.Num() - 1 ); bChanged = true; if ( bInputEnded != nullptr ) *bInputEnded = true; }
	}
	// 表示走査の後に削除するkey index。
	int32 RemoveIndex = INDEX_NONE;
	for ( int32 Index = 0; Index < NMath::Min( Curve.Keys.Num(), FEffectScalarCurve::MaxKeys ); ++Index )
	{
		ImGui::PushID( Index );
		// 編集中のscalar key。
		FEffectScalarCurve::FKey& Key = Curve.Keys[Index];
		// 前のキーが有限なときに使う左境界。
		const float32 Previous = Index > 0 && NMath::IsFinite( Curve.Keys[Index - 1].Time ) ? NMath::Clamp( Curve.Keys[Index - 1].Time, 0.0f, 1.0f ) : 0.0f;
		// 次のキーが有限なときに使う右境界。
		const float32 Next = Index + 1 < Curve.Keys.Num() && NMath::IsFinite( Curve.Keys[Index + 1].Time ) ? NMath::Clamp( Curve.Keys[Index + 1].Time, 0.0f, 1.0f ) : 1.0f;
		// 隣の時刻と重ならない最小入力値。
		const float32 Earliest = Index > 0 ? NMath::NextAfter( Previous, 1.0f ) : 0.0f;
		// 隣の時刻と重ならない最大入力値。
		const float32 Latest = Index + 1 < Curve.Keys.Num() ? NMath::NextAfter( Next, 0.0f ) : 1.0f;
		// TimeとValueが安全な数値として保存されているか。
		const bool bFiniteKey = NMath::IsFinite( Key.Time ) && NMath::IsFinite( Key.Value );
		// 前後のキーとの厳密な順序が保たれているか。
		const bool bOrderedKey = ( Index == 0 || ( NMath::IsFinite( Curve.Keys[Index - 1].Time ) && Curve.Keys[Index - 1].Time < Key.Time ) ) && ( Index + 1 >= Curve.Keys.Num() || ( NMath::IsFinite( Curve.Keys[Index + 1].Time ) && Key.Time < Curve.Keys[Index + 1].Time ) );
		if ( !bFiniteKey ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Invalid key values" );
		if ( bFiniteKey && bOrderedKey && Earliest <= Latest )
		{
			ImGui::SetNextItemWidth( 100.0f );
			if ( TrackInputEnd( ImGui::DragFloat( "Time", &Key.Time, 0.001f, Earliest, Latest, "%.9g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded ) ) { Key.Time = NMath::Clamp( Key.Time, Earliest, Latest ); bChanged = true; }
			ImGui::SameLine();
			ImGui::SetNextItemWidth( 100.0f );
			bChanged |= TrackInputEnd( ImGui::DragFloat( "Value", &Key.Value, 0.01f, Minimum, Maximum, "%.9g", ImGuiSliderFlags_AlwaysClamp ), bInputEnded );
			ImGui::SameLine();
		}
		else if ( bFiniteKey ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Invalid key order; remove or reset." );
		if ( ImGui::SmallButton( "Remove" ) ) { RemoveIndex = Index; if ( bInputEnded != nullptr ) *bInputEnded = true; }
		ImGui::PopID();
	}
	if ( RemoveIndex != INDEX_NONE ) { Curve.Keys.RemoveAt( RemoveIndex ); bChanged = true; }
	// PlotLinesへ渡す曲線の64点標本。
	float32 Samples[64];
	for ( int32 Index = 0; Index < 64; ++Index ) Samples[Index] = Curve.Evaluate( static_cast<float32>( Index ) / 63.0f, DefaultValue );
	ImGui::PlotLines( "##CurveGraph", Samples, 64, 0, nullptr, Minimum, Maximum, ImVec2( -1.0f, 42.0f ) );
	ImGui::PopID();
	return bChanged;
}
}

// 既存の粒子editor用wrapperはdocumentの履歴sessionを閉じる。
bool NEffectEditorUI::DrawScalarCurve( const char* Label, FEffectScalarCurve& Curve, TParticleEffectDocument& Document, float32 DefaultValue, float32 Minimum, float32 Maximum )
{
	// 現在のImGui入力が終わったことをgeneric helperから受け取る。
	bool bInputEnded = false;
	const bool bChanged = DrawScalarCurveValues( Label, Curve, DefaultValue, Minimum, Maximum, &bInputEnded );
	if ( bInputEnded ) Document.EndEditSession();
	return bChanged;
}

// World editorなど別の履歴documentからcurve入力終了を処理できるようにする。
bool NEffectEditorUI::DrawScalarCurve( const char* Label, FEffectScalarCurve& Curve, float32 DefaultValue, float32 Minimum, float32 Maximum, bool& bInputEnded )
{
	// 同じpanel内の後続curve欄が先行欄の入力終了を消さないよう個別に集める。
	bool bThisInputEnded = false;
	const bool bChanged = DrawScalarCurveValues( Label, Curve, DefaultValue, Minimum, Maximum, &bThisInputEnded );
	bInputEnded |= bThisInputEnded;
	return bChanged;
}

// RGBA curveの各キーをfloat精度で編集し、時刻順を壊さない。
bool NEffectEditorUI::DrawColorCurve( const char* Label, FEffectColorCurve& Curve, TParticleEffectDocument& Document )
{
	// この曲線で保存定義に反映する変更の有無。
	bool bChanged = false;
	ImGui::PushID( Label );
	ImGui::TextUnformatted( Label );
	// 保存enumの順に並ぶ補間方式の表示名。
	static const char* InterpolationNames[] = { "Linear", "Step", "Smooth" };
	// Comboへ渡す現在の補間方式番号。
	int32 Interpolation = static_cast<int32>( Curve.Interpolation );
	if ( Interpolation < 0 || Interpolation >= 3 ) Interpolation = 0;
	ImGui::SetNextItemWidth( 140.0f );
	if ( ImGui::Combo( "Interpolation", &Interpolation, InterpolationNames, 3 ) ) { Curve.Interpolation = static_cast<FEffectScalarCurve::EInterpolation>( Interpolation ); bChanged = true; }
	ImGui::SameLine();
	ImGui::BeginDisabled( Curve.Keys.Num() >= FEffectColorCurve::MaxKeys );
	if ( ImGui::SmallButton( "Add key" ) ) bChanged |= AddColorCurveKey( Curve );
	ImGui::EndDisabled();
	ImGui::SameLine();
	// Resetやキー削除で修復できる曲線の問題一覧。
	const TArray<FString> CurveIssues = Curve.Validate();
	// 既定曲線と違う値が保存されているか。
	const bool bCurveNeedsReset = !Curve.Keys.IsEmpty() || Curve.Interpolation != FEffectScalarCurve::EInterpolation::Linear || !CurveIssues.IsEmpty();
	if ( ImGui::SmallButton( "Reset" ) && bCurveNeedsReset ) { Curve = FEffectColorCurve(); bChanged = true; }
	if ( !CurveIssues.IsEmpty() ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Color curve data is invalid. Remove a key or reset this curve." );
	if ( Curve.Keys.IsEmpty() ) ImGui::TextDisabled( "No keys: uses white multiplier for the whole lifetime" );
	if ( Curve.Keys.Num() >= FEffectColorCurve::MaxKeys ) ImGui::TextDisabled( "Maximum 32 color keys" );
	// 表示走査の後に削除するkey index。
	int32 RemoveIndex = INDEX_NONE;
	for ( int32 Index = 0; Index < NMath::Min( Curve.Keys.Num(), FEffectColorCurve::MaxKeys ); ++Index )
	{
		ImGui::PushID( Index );
		// 編集中のcolor key。
		FEffectColorCurve::FKey& Key = Curve.Keys[Index];
		// 前のキーが有限なときに使う左境界。
		const float32 Previous = Index > 0 && NMath::IsFinite( Curve.Keys[Index - 1].Time ) ? NMath::Clamp( Curve.Keys[Index - 1].Time, 0.0f, 1.0f ) : 0.0f;
		// 次のキーが有限なときに使う右境界。
		const float32 Next = Index + 1 < Curve.Keys.Num() && NMath::IsFinite( Curve.Keys[Index + 1].Time ) ? NMath::Clamp( Curve.Keys[Index + 1].Time, 0.0f, 1.0f ) : 1.0f;
		// 隣の時刻と重ならない最小入力値。
		const float32 Earliest = Index > 0 ? NMath::NextAfter( Previous, 1.0f ) : 0.0f;
		// 隣の時刻と重ならない最大入力値。
		const float32 Latest = Index + 1 < Curve.Keys.Num() ? NMath::NextAfter( Next, 0.0f ) : 1.0f;
		// ColorEditと時刻欄へ渡せる有限時刻か。
		const bool bFiniteTime = NMath::IsFinite( Key.Time );
		// 前後のキーとの厳密な順序が保たれているか。
		const bool bOrderedKey = ( Index == 0 || ( NMath::IsFinite( Curve.Keys[Index - 1].Time ) && Curve.Keys[Index - 1].Time < Key.Time ) ) && ( Index + 1 >= Curve.Keys.Num() || ( NMath::IsFinite( Curve.Keys[Index + 1].Time ) && Key.Time < Curve.Keys[Index + 1].Time ) );
		if ( !bFiniteTime || !IsFiniteNormalizedColor( Key.Value ) ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Invalid key values; remove or reset." );
		if ( bFiniteTime && bOrderedKey && Earliest <= Latest )
		{
			ImGui::SetNextItemWidth( 100.0f );
			if ( TrackInputEnd( ImGui::DragFloat( "Time", &Key.Time, 0.001f, Earliest, Latest, "%.9g", ImGuiSliderFlags_AlwaysClamp ), Document ) ) { Key.Time = NMath::Clamp( Key.Time, Earliest, Latest ); bChanged = true; }
			ImGui::SameLine();
			if ( ImGui::SmallButton( "Remove" ) ) RemoveIndex = Index;
			if ( IsFiniteNormalizedColor( Key.Value ) )
			{
				// ColorEdit4が編集する一時RGBA。
				float32 Value[4] = { Key.Value.r, Key.Value.g, Key.Value.b, Key.Value.a };
				// 入力欄で色成分が変更されたか。
				const float32 ColorLabelWidth = ImGui::CalcTextSize( "Multiplier" ).x + ImGui::GetStyle().ItemInnerSpacing.x;
				// 表示ラベル分を除き、色入力全体がpanel幅に収まるようにする。
				ImGui::SetNextItemWidth( NMath::Max( 1.0f, ImGui::GetContentRegionAvail().x - ColorLabelWidth ) );
				const bool bColorChanged = ImGui::ColorEdit4( "Multiplier", Value, ImGuiColorEditFlags_AlphaPreview | ImGuiColorEditFlags_Float );
				// ImGuiから返った色4成分が有限か。
				bool bFiniteColor = true;
				// 各成分を保存する前に有限値だけを許す。
				for ( const float32 Component : Value ) if ( !NMath::IsFinite( Component ) ) bFiniteColor = false;
				if ( TrackInputEnd( bColorChanged, Document ) && bFiniteColor ) { Key.Value = FColor( NMath::Clamp( Value[0], 0.0f, 1.0f ), NMath::Clamp( Value[1], 0.0f, 1.0f ), NMath::Clamp( Value[2], 0.0f, 1.0f ), NMath::Clamp( Value[3], 0.0f, 1.0f ) ); bChanged = true; }
				else if ( bColorChanged ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Color input must be finite." );
			}
		}
		else if ( bFiniteTime ) ImGui::TextColored( ImVec4( 1.0f, 0.65f, 0.3f, 1.0f ), "Invalid key order; remove or reset." );
		if ( !( bFiniteTime && bOrderedKey && Earliest <= Latest ) && ImGui::SmallButton( "Remove" ) ) RemoveIndex = Index;
		ImGui::PopID();
	}
	if ( RemoveIndex != INDEX_NONE ) { Curve.Keys.RemoveAt( RemoveIndex ); bChanged = true; }
	ImGui::PopID();
	return bChanged;
}

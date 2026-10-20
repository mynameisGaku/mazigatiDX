#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/SpriteSheet.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Color.h"
#include "Library/Math/Vector2.h"

class FTransformView;

//=============================================================================
// FSpriteDrawSpace
//   ワールド座標（CTransformComponent の値）から画面のピクセル座標への変換。
//     画面の X = Origin.x + ワールドの X * PixelsPerUnit.x
//     画面の Y = Origin.y + ワールドの Y * PixelsPerUnit.y
//   縦を上向きにしたいとき（物理ワールドなど）は、PixelsPerUnit.y を負にして Origin.y を画面の下端にする。
//=============================================================================
struct FSpriteDrawSpace
{
	FVector2 Origin = FVector2( 0.0f, 0.0f );
	FVector2 PixelsPerUnit = FVector2( 1.0f, 1.0f );
};

//=============================================================================
// FSpriteAnimator
//   スプライトシートのアニメーションを再生する。1 つのオブジェクトの 1 つのスプライト分の状態。
//
//   // オブジェクトに付けるだけで、毎フレーム自動で更新・描画される（ECS のデータ。並列に処理される）
//   void CHero::OnSpawn()
//   {
//       AddComponent<CTransformComponent>();
//       FSpriteAnimator Animator( Sheet );
//       Animator.Space.Origin = FVector2( 0.0f, 720.0f );  Animator.Space.PixelsPerUnit = FVector2( 50.0f, -50.0f );
//       Animator.Play( "Idle" );
//       AddData( Animator );
//   }
//   void CHero::Tick( float32 DeltaTime )
//   {
//       FSpriteAnimator* Animator = GetData<FSpriteAnimator>();
//       Animator->bFlipX = FacingLeft;
//       if ( Jumped ) Animator->Play( "Jump" );
//       if ( Animator->ConsumeEvent( "Footstep" ) ) Audio->Play( Step );
//   }
//
//   // 自分で更新・描画する（ECS を使わない）
//   Animator.Update( DeltaTime );                      // 時間を進める
//   Animator.Record( Draw, ScreenX, ScreenY );         // 描画システムの中で、または FDrawContext に記録して実行
//
//   - 再生位置は秒で持つので、フレームレートに依存しない。Speed で速さを変えられ、負なら逆再生
//   - 出来事（イベント）は FSpriteClip::AddEvent で足す。Update のたびに、その更新でコマに入った分が溜まり直される
//     （次の Update まで有効。PreUpdate に動く組み込みシステムの場合、そのフレームの Tick から読める）
//=============================================================================
struct FSpriteAnimator
{
	FSpriteAnimator() = default;
	explicit FSpriteAnimator( const TSharedPtr<CSpriteSheet>& InSheet ) : Sheet( InSheet ) {}

	//-------------------------------------------------------------------------
	// 設定（いつ変えてもよい）
	//-------------------------------------------------------------------------
	TSharedPtr<CSpriteSheet> Sheet;
	float32 Speed = 1.0f;         // 再生の速さの倍率。負で逆再生、0 で止まる
	bool bFlipX = false;          // 基準点を軸に左右反転
	bool bFlipY = false;
	bool bVisible = true;
	bool bAutoDraw = true;        // true なら、組み込みの描画システムが CTransformComponent の位置に描く
	FVector2 Offset = FVector2( 0.0f, 0.0f );   // 表示位置のずらし（画面のピクセル）
	FVector2 Scale = FVector2( 1.0f, 1.0f );   // 表示の拡大率（CTransformComponent のスケールにさらに掛ける）
	float32 AngleOffset = 0.0f;   // 回転のずらし（ラジアン。CTransformComponent の Z 軸まわりの回転に足す）
	FColor32 Tint = FColor32::White;   // 色を掛ける（白でそのまま）
	float32 Alpha = 1.0f;         // 不透明度（0..1）
	FSpriteDrawSpace Space;       // ワールド -> 画面の変換（bAutoDraw のとき）

	//-------------------------------------------------------------------------
	// 再生の操作
	//-------------------------------------------------------------------------
	// クリップを最初から（Speed が負なら最後から）再生する。bRestart が false で、すでに同じクリップを再生中なら何もしない。
	// 見つからなければ false（いまの再生は変えない）
	bool Play( const FString& ClipName, bool bRestart = true );
	bool PlayIndex( int32 ClipIndex, bool bRestart = true );
	// クリップの NormalizedStart（0..1）の位置から再生する
	bool PlayFrom( const FString& ClipName, float32 NormalizedStart );
	void Pause() { bPlaying = false; }
	void Resume() { if ( ClipIndex != INDEX_NONE && !bFinished ) bPlaying = true; }
	// 止めて、クリップの最初のコマに戻す
	void Stop();
	// アニメーションを使わず、スプライトシートのコマ 1 つを表示したままにする
	void ShowFrame( int32 SheetFrameIndex );

	// 時間を進める（DeltaTime は秒）。Speed と bPlaying を考慮する
	void Update( float32 DeltaTime );
	// 再生位置を、クリップの 0..1 の位置に動かす（出来事は起きない。エディタのシークバー用）。クリップを再生していなければ何もしない
	void Seek( float32 NormalizedTime );

	//-------------------------------------------------------------------------
	// 状態
	//-------------------------------------------------------------------------
	FORCEINLINE bool IsPlaying() const { return bPlaying; }
	// Once のクリップが最後まで再生された（Play すると false に戻る）
	FORCEINLINE bool IsFinished() const { return bFinished; }
	// 直近の Update で、Once のクリップが最後まで再生された
	FORCEINLINE bool DidFinishThisUpdate() const { return bFinishedThisUpdate; }
	FORCEINLINE int32 GetClipIndex() const { return ClipIndex; }
	// 再生中のクリップの名前（無ければ空）
	FString GetClipName() const;
	// いま表示しているスプライトシートのコマの番号（無ければ INDEX_NONE）
	int32 GetFrameIndex() const;
	// いま表示しているのが、クリップの何コマ目か（再生順、0 から。クリップが無ければ INDEX_NONE）
	FORCEINLINE int32 GetStep() const { return ClipIndex != INDEX_NONE ? Step : INDEX_NONE; }
	// クリップの中の位置（0..1。PingPong は往路と復路で 0..1..0 にはならず、1 周の中の位置）
	float32 GetNormalizedTime() const;

	//-------------------------------------------------------------------------
	// 出来事
	//-------------------------------------------------------------------------
	// 直近の Update で入ったコマの出来事の名前（起きた順）
	FORCEINLINE const TArray<FString>& GetEvents() const { return Events; }
	FORCEINLINE bool HasEvent( const FString& Name ) const { return Events.Contains( Name ); }
	// Name の出来事があれば、消して true（同じフレームで 2 回読まれないように）
	bool ConsumeEvent( const FString& Name );

	//-------------------------------------------------------------------------
	// 描画
	//-------------------------------------------------------------------------
	// 画面の (Position) に基準点が来るように、いまのコマを記録する。Scale は Animator の Scale に掛かる
	void Record( FDrawContext& Draw, const FVector2& Position, const FVector2& InScale = FVector2( 1.0f, 1.0f ), float32 AngleRadians = 0.0f ) const;
	// CTransformComponent の位置・拡大・回転（Z 軸まわり）から、Space で画面の位置に変換して記録する（組み込みの描画システムが使う）
	void RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const;

private:
	// 再生位置をクリップの先頭／末尾に置く
	void Rewind( float64 NormalizedStart );

	int32 ClipIndex = INDEX_NONE;   // 再生中のクリップ（INDEX_NONE なら ManualFrame を表示）
	int32 ManualFrame = INDEX_NONE;
	int32 Step = 0;
	int64 LastAbsoluteStep = -1;
	float64 Playhead = 0.0;         // 秒（Speed が負なら減る）
	bool bPlaying = false;
	bool bFinished = false;
	bool bFinishedThisUpdate = false;
	TArray<FString> Events;
};

#pragma once
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/Asset/Texture.h"
#include "Library/Framework/Render/DrawContext.h"
#include "Library/Math/Color.h"
#include "Library/Math/Transform.h"
#include "Library/Math/Vector2.h"

class FTransformView;

//=============================================================================
// FBillboard
//   画像を貼った、ワールドの四角形（看板・草・木・光の玉・床の模様・壁の絵）。ECS のデータで、オブジェクトに AddData で付けるだけで、
//   毎フレーム自動で描かれる（組み込みの描画システム。CTransformComponent のワールドの位置・向き・拡大に従う）。
//
//   TSharedPtr<CTexture> Glow = Assets->LoadTexture( "Sprites/Glow.png" );
//   FBillboard Board;
//   Board.Texture = Glow;
//   Board.Size = FVector2( 2.0f, 2.0f );              // ワールドでの大きさ（幅 x 高さ）。オブジェクトの拡大が掛かる
//   Board.Orient = EDrawQuadOrient::CameraY;          // 立ったまま、カメラの方を向く
//   Board.Tint = FColor32( 255, 200, 100, 180 );      // 色と不透明度
//   Object->AddData( Board );
//
//   向きの決め方（Orient）:
//     Camera   常にカメラの正面を向く（画面に平ら）。オブジェクトの Roll（Z まわりの回転）だけ、視線まわりの回転として効く
//     CameraY  Y 軸だけ回って、カメラの方を向く（地面に立ったまま、傾かない）
//     Fixed    オブジェクトの向きのまま（回転・拡大もオブジェクトに従う）
//   Camera / CameraY は、記録のときにはカメラを知らないので、描くとき（IDrawExecutor）のカメラで向きが決まる。
//
//   自分で描きたいときは bAutoDraw を false にして、描画システムの中で Record / RecordAt を呼ぶ。
//   画像（Texture）を持っている間は、ハンドルが有効。
//=============================================================================
struct FBillboard
{
	TSharedPtr<CTexture> Texture;
	FVector2 Size = FVector2( 1.0f, 1.0f );
	FVector2 Pivot = FVector2( 0.5f, 0.5f );        // 画像の中の、オブジェクトの位置に来る点（0..1。左上が 0, 0）
	EDrawQuadOrient Orient = EDrawQuadOrient::Camera;
	FColor32 Tint = FColor32( 255, 255, 255, 255 );
	// 画像の使う範囲（0..1。左上が 0, 0）。スプライトシートの 1 コマだけ、などに（SetRect が計算する）
	float32 U0 = 0.0f;
	float32 V0 = 0.0f;
	float32 U1 = 1.0f;
	float32 V1 = 1.0f;
	bool bFlipX = false;
	bool bFlipY = false;
	bool bVisible = true;
	bool bAutoDraw = true;

	// 画像のピクセルの範囲 (X, Y, Width, Height) だけを使う。Texture の大きさが分かってから（読み込みが終わってから）呼ぶ。
	// 大きさが分からない・範囲が空のときは何もしない（false）
	bool SetRect( int32 X, int32 Y, int32 Width, int32 Height );
	void ResetRect();

	// World（ワールドの位置・向き・拡大）で記録する。Texture が無い・見えない設定なら何も記録しない
	void Record( FDrawContext& Draw, const FTransform& World ) const;
	void RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const;
};

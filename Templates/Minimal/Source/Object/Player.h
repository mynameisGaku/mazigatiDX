#pragma once
#include "Library/Framework/Object/GameObject.h"
#include "Library/Math/Math.h"

//=============================================================================
// CPlayer
//   矢印キー / WASD で動かせる四角形。オブジェクトの書き方の見本。
//   CGameObject を継承して、Tick で動かし、Draw で描く。
//
//   SpawnObject<CPlayer>( FVector2( 640.0f, 360.0f ) );
//=============================================================================
class CPlayer : public CGameObject
{
public:
	// InPosition は最初の位置（画面の座標。ピクセル）
	explicit CPlayer( const FVector2& InPosition );

	void Tick( float32 DeltaTime ) override;
	void Draw() override;

private:
	// 画面の座標（ピクセル。左上が原点）
	FVector2 Position;

	// 1 秒に進む距離（ピクセル）
	float32 Speed = 320.0f;

	// 四角形の半分の大きさ（ピクセル）
	int32 HalfSize = 16;
};

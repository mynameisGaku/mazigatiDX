// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/IShadowMap.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Math/Matrix4x4.h"

//=============================================================================
// CShadowMap
//   シャドウマップ（影の絵）。光の向きから見た「いちばん手前の深度」を覚えておく画像で、これを使って描くと、光が遮られる場所に影ができる（DxLib のシャドウマップ）。
//   縦横の大きさ（ピクセル）は、作ったあと変わらない。大きいほど影の輪郭が細かく、メモリを使う。
//   ファイルから読むアセットではないので、CAssetManager のキャッシュには入らない。持っている間は有効で、消えるとき、解放する（メインスレッド以外で消えたら、次の更新で解放する）。
//   作るのも、行列を読むのも、メインスレッドだけ（DxLib がスレッドセーフでないため）。それ以外のスレッドからは、nullptr / false を返す。
//   影の絵を描くのも、影を受けるのも、FDrawContext に記録する: BeginShadowMap〜EndShadowMap の間に影を落とすものを描き、UseShadowMap のあとに影を受けるものを描く。
//   記録してから描かれるまで、シャドウマップを手放さないこと。
//
//   TSharedPtr<CShadowMap> ShadowMap = CShadowMap::Create( 1024, 1024 );             // 作れなければ nullptr
//   {
//       FShadowMapPassScope Pass( Draw, ShadowMap, FShadowMapView::MakeForArea( Light, Min, Max ) );
//       Draw.Cube3D( ... );                                                        // 影を落とすもの
//   }
//   FShadowMapUseScope Shadow( Draw, 0, ShadowMap );
//   Draw.Cube3D( ... );                                                            // 影を受けるもの
//=============================================================================
class CShadowMap final : public IShadowMap
{
public:
	// 縦横それぞれの大きさの上限（DxLib がこれを超えると作れない）
	static constexpr int32 MaxSize = NDrawShadowMap::MaxSize;

	// 縦横 Width x Height のシャドウマップを作る。それぞれ 1 未満か MaxSize を超える・メインスレッドでない・作れなかったときは nullptr
	static TSharedPtr<CShadowMap> Create( int32 Width, int32 Height );

	~CShadowMap() override;
	CShadowMap( const CShadowMap& ) = delete;
	CShadowMap& operator=( const CShadowMap& ) = delete;

	int32 GetWidth() const { return Width; }
	int32 GetHeight() const { return Height; }

	// 最後に影を描いた（BeginShadowMap を実行した）ときの、光から見た行列（ビュー行列 x 射影行列。DxLib と同じ、行ベクトルの流儀）。
	// 影を描くときに決まる値で、描画を実行したあとに読める。まだ一度も描いていない・メインスレッドでないときは false
	bool GetViewProjection( FMatrix4x4& OutMatrix ) const;

	FShadowMapHandle GetShadowMapHandle() const override;

private:
	CShadowMap( int32 InShadowMap, int32 InWidth, int32 InHeight )
		: ShadowMap( InShadowMap )
		, Width( InWidth )
		, Height( InHeight )
	{
	}

	int32 ShadowMap = -1;
	int32 Width = 0;
	int32 Height = 0;
};

// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Interfaces/Render/IRenderCanvas2D.h"
#include "Library/Core/Interfaces/Render/IRenderGeometry3D.h"
#include "Library/Core/Thread/Atomic.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"

class CRenderCommandThread;

// ゲーム側の命令を所有コピーし、APIを知らずに描画スレッドへ渡す接続。Targetは描画スレッド専用の実行先。
class CThreadedRenderSurface : public IRenderSurface, private IRenderCanvas2D, private IRenderGeometry3D
{
  public:
	// TargetとThreadはこの接続の全命令と破棄が終わるまで生存させる。
	CThreadedRenderSurface( IRenderSurface &InTarget, CRenderCommandThread &InThread );
	// 残りの命令を実行してから借用参照を返す。作成したゲームスレッドで破棄する。
	~CThreadedRenderSurface();
	// GPU側のフレーム開始後、ゲームスレッドで呼ぶ。前フレーム終了前・不正寸法はfalse。
	bool BeginFrame( int32 Width, int32 Height );
	// コピーした命令を全件実行して受付を閉じる。命令拒否・失敗があればfalse。
	bool EndFrame();
	// 現在までの2D命令を描画スレッドへ渡す。CPU完了を待たない。
	bool Flush();
	// フレーム内だけゲーム側の記録窓口を貸す。
	IRenderCanvas2D *GetCanvas2D() override;
	// フレーム内だけゲーム側の3D窓口を貸す。
	IRenderGeometry3D *GetGeometry3D() override;
	// 消去を前後の描画と同じ順序に積む。
	void RecordClear( FColor32 Color ) override;
	// 全CPU命令を渡してからGPU待機を実行する。
	bool WaitForGPU() override;
	// それ以前の描画を確定し、同期で撮影先へ切り替える。
	bool BeginOffscreenCapture( const FOffscreenCaptureDesc &Desc ) override;
	// 撮影の描画を確定し、画素を同期で返して通常寸法へ戻す。
	bool EndOffscreenCapture( TArray<FColor32> &Out ) override;
	// それ以前の描画を確定し、同期で所有済みの描画先パスへ切り替える。
	bool BeginRenderTargetPass( const FRenderTargetPassDesc &Desc ) override;
	// 描画先パスの描画を確定して同じGPUキューへ提出し、通常寸法へ戻す。
	bool EndRenderTargetPass() override;
	// 先行する2D命令のあとに、同期して色の矩形を画像へ写す。
	bool CopyColorRegion( int32 X0, int32 Y0, int32 X1, int32 Y1, FTextureResourceHandle Dest ) override;
	// パス間の画像コピーを、先行する命令に続けて描画スレッドに記録する。
	bool RecordTextureCopy( FTextureResourceHandle Source, FTextureResourceHandle Destination ) override;
	// 先行する2D命令を確定し、現在の記録先（主画面または描画先パス）へGPUタイムスタンプの開始点を積む。
	bool BeginGpuTimestamp( FGpuTimestampHandle Query ) override;
	// 先行する2D命令を確定し、開始済みのGPUタイムスタンプを閉じて解決を記録する。
	bool EndGpuTimestamp( FGpuTimestampHandle Query ) override;
	// 実行列と借用先の寿命を複製しない。
	CThreadedRenderSurface( const CThreadedRenderSurface & ) = delete;
	// 実行中に所有先を代入で差し替えない。
	CThreadedRenderSurface &operator=( const CThreadedRenderSurface & ) = delete;

  private:
	// フロント側で記録できる2D操作。GPUの型や参照を含めない。
	enum class ECommand : uint8
	{
		Box, // 四角形。
		Line, // 線分。
		Circle, // 円。
		Triangle, // 三角形。
		Quad, // 凸四角形。
		Polygon, // 凸な点列。
		Text, // 所有したUTF-8文字列。
		TextFont, // フォントを指定した、所有したUTF-8文字列。
		Image, // 借用画像の点列とUV。
		ImagePolygon, // 借用画像を貼る凸な多角形（点列は x, y, u, v）。
		PushClip, // クリップの追加。
		PopClip, // クリップの復元。
		AntiAlias, // 輪郭の補間設定。
		Blend, // 描画先との混ぜ方。
		CustomBlend, // 利用者が決めた混ぜ方の係数。
		Mask, // 2Dの描画を薄めるマスクの画像。
		ClearColor, // 色の消去。
		ClearDepth, // 深度の消去。
		TextureFilter, // 画像の補間（bEnabled が線形）。
	};
	// 64件ずつ移動する操作値。文字列以外は固定長で持つ。
	struct FCommand
	{
		// 実行する操作。
		ECommand Type = ECommand::Box;
		// 点列・UV・半径・線幅など、その操作の浮動小数値。
		float32 Values[12] = {};
		// クリップの四辺または円の分割数。
		int32 Integers[4] = {};
		// 描画色または消去色。
		FColor32 Color;
		// 塗りつぶしまたは補間の有効設定。
		bool bEnabled = false;
		// 描画実装が解釈する画像の値。GPU寿命は既存の所有先が保持する。
		FTextureDrawView Texture;
		// 文字列は終端を含めて所有する。
		FString String;
		// 凸な点列（XYの順）。点の数が決まっていない操作だけが使う。
		TArray<float32> Points;
		// フォントを指定した文字列の、フォントの値と縁取りの色。
		FRenderTextFont Font;
		FColor32 EdgeColor;
		// 利用者が決めた混ぜ方の係数。
		FGeometryCustomBlend CustomBlend;
	};
	// 描画スレッドだけで、所有する操作値を順に実行する。
	void ExecuteBatch( const TArray<FCommand> &Commands );
	// 受付上限を調べ、64件ごとにまとめて渡す。ゲーム側からだけ呼ぶ。
	void Queue( FCommand Command );
	// 先行する2D命令を渡して、結果を返す処理を直列に実行する。
	bool Execute( TCallable<void()> Command );
	// 描画先の現在の物理幅と高さを返す。
	int32 GetWidth() const override;
	// 現在の描画先の物理高さを、記録側の値だけから返す。
	int32 GetHeight() const override;
	// 頂点の借用は同期実行が終わるまで保持し、既存の検査結果を返す。
	bool DrawGeometry( const FGeometryVertex *Vertices, int32 Count, const FGeometryDrawState &State ) override;
	// 画像の借用値と頂点を同期で渡し、実行側の成否を返す。
	bool DrawTexturedGeometry( const FGeometryVertex *Vertices, int32 Count, const FGeometryDrawState &State, const FTextureDrawView &Texture ) override;
	// GPU上の範囲と任意の画像を同じ順序で渡し、既存の所有参照保持と検査を使う。
	bool DrawBufferGeometry( const FGeometryBufferDraw &Draw, const FGeometryDrawState &State, const FTextureDrawView *Texture ) override;
	// 登録済みカスタムパイプラインを使ったGPU描画を同じ順序で実行する。
	bool DrawGraphicsPipeline( const FGraphicsPipelineDraw &Draw ) override;
	// 一時データの描画も、同じ順序で同期して実行する（借りた配列は、呼び出しの間だけ有効）。
	bool DrawGraphicsPipelineTransient( const FGraphicsPipelineTransientDraw &Draw ) override;
	// 深度消去を同じ順序で渡す。
	void ClearDepth() override;
	// 図形の引数は所有コピーし、頂点への展開とGPU記録を描画スレッドで行う。
	void Box( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, bool bFill, float32 Thickness ) override;
	// 線分の位置・色・太さを所有する命令として記録する。
	void Line( float32 X0, float32 Y0, float32 X1, float32 Y1, FColor32 Color, float32 Thickness ) override;
	// 円の位置・半径・分割数と描画状態を値で記録する。
	void Circle( float32 X, float32 Y, float32 Radius, FColor32 Color, bool bFill, float32 Thickness, int32 Segments ) override;
	// 三角形の点列を呼出し中に複写する。
	void Triangle( const float32 ( &Points )[6], FColor32 Color, bool bFill, float32 Thickness ) override;
	// 四角形の点列を呼出し中に複写する。
	void Quad( const float32 ( &Points )[8], FColor32 Color, bool bFill, float32 Thickness ) override;
	// 凸な点列を呼出し中に複写する。3〜512点でなければ記録しない。
	void Polygon( const float32 *Points, int32 Count, FColor32 Color, bool bFill, float32 Thickness ) override;
	// UTF-8を最大64KiBまで所有コピーする。Endがnullptrなら終端まで読む。1フレーム8MiBを越すと失敗。
	void Text( float32 X, float32 Y, FColor32 Color, const char *Begin, const char *End ) override;
	// フォントの値・変換・UTF-8（最大64KiB）を所有コピーする。文字列の合計は、Textと同じ1フレーム8MiBまで。
	void TextFont( const FRenderTextFont &Font, const float32 ( &Transform )[6], FColor32 Color, FColor32 EdgeColor, const char *Begin, const char *End ) override;
	// 画像のbindingと点列を値で渡す。画像のGPU寿命は既存の所有者が管理する。
	void Image( const FTextureDrawView &Texture, const float32 ( &Points )[8], const float32 ( &UV )[4], FColor32 Color ) override;
	void ImagePolygon( const FTextureDrawView &Texture, const float32 *Vertices, int32 Count, FColor32 Color ) override;
	// 切り取りと描画状態も、描画と同じ順序で実行する。
	void PushClip( int32 X0, int32 Y0, int32 X1, int32 Y1 ) override;
	// 先行する描画の後で、直前の切り取り範囲へ戻す。
	void PopClip() override;
	// 補間設定に対応するかは、描画先が変わるたびの最初の 1 回だけ、先行する命令を確定して同期で確かめる。対応していれば、以降は他の2D命令と同じ順序に積むだけで、すぐ true を返す。
	bool SetTextureFilter( bool bLinear ) override;
	// 輪郭の補間設定を後続の描画と同じ順序へ積む。
	void SetAntiAlias( bool bEnabled ) override;
	// 描画先との混ぜ方を後続の描画と同じ順序へ積む。
	void SetBlend( EGeometryBlend Blend ) override;
	void SetCustomBlend( const FGeometryCustomBlend &Blend ) override;
	// マスクを後続の描画と同じ順序へ積む。実行先が2Dを持たなければfalse。
	bool SetMask( const FTextureDrawView *Mask, int32 Channel, bool bInvert ) override;
	// API側の実行先。内容には描画スレッドだけが触る。
	IRenderSurface &Target;
	// CPU命令と同期処理を同じ順序へ集める既存の専用スレッド。
	CRenderCommandThread &Thread;
	// 次にまとめて渡す命令。ゲームスレッドだけが変更する。
	TArray<FCommand> Pending;
	// 両スレッドから記録する、このフレームの失敗印。
	TAtomic<bool> bFailed{ false };
	// ゲーム側のフレーム・撮影・描画先パスの受付状態。
	bool bActive = false, bCapture = false, bRenderTargetPass = false;
	// 現在の実行先が提供する窓口。描画スレッドで調べた結果だけを保持する。
	bool bHasCanvas = false, bHasGeometry = false;
	// 補間設定への対応を確かめた印と、その答え。フレーム・描画先パス・撮影の切り替えで確かめ直す。ゲームスレッドだけ。
	bool bFilterSupportKnown = false, bFilterSupported = false;
	// 現在の描画先と、通常フレームへ復元する寸法。
	int32 Width = 0, Height = 0, FrameWidth = 0, FrameHeight = 0;
	// 1フレーム65536命令、文字列8MiBの受付を数える。
	int32 NumCommands = 0, TextBytes = 0;
};

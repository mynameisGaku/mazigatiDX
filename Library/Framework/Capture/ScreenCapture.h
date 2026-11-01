// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Interfaces/Platform/FrameCapture.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Core/Memory/UniquePtr.h"
#include "Library/Framework/Capture/CaptureCommandLine.h"
#include "Library/Framework/Capture/CaptureTypes.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

class FGifEncoder;
class IPlatform;

//=============================================================================
// CScreenCapture
//   画面を撮る（スクリーンショット・GIF の記録）。プラットフォームの窓口（IFrameCaptureSink）で、画面に出す直前の画素を受け取る。
//   保存先は Saved/Screenshots。名前を指定しなければ Screenshot_00001 のような番号の名前（番号は、フォルダにあるもの + 1）。
//   既定はゲームの絵だけ（デバッグ用の UI は写さない）。PNG・JPEG・BMP・GIF で保存できる。
//
//   CScreenCapture* Capture = GetAppSubsystem<CScreenCapture>();
//   Capture->TakeScreenshot();                               // Saved/Screenshots/Screenshot_00001.png
//   Capture->TakeScreenshot( "Title.jpg" );                  // 名前と形式を指定
//   Capture->StartRecording( "Walk" );                       // Saved/Screenshots/Walk.gif（StopRecording で保存）
//
//   キー: F9 でスクリーンショット、Shift + F9 で GIF の記録を始める / 止める（フレームワークの決まり）。起動引数は NCaptureCommandLine。
//=============================================================================
class CScreenCapture : public CAppSubsystem, public ITickable, public IFrameCaptureSink
{
public:
	CScreenCapture();
	~CScreenCapture() override;

	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	void Tick( float32 DeltaTime ) override;
	ETickGroup GetTickGroup() const override
	{
		return ETickGroup::PostUpdate;
	}

	// 次に描き終えたフレーム（DelayFrames があれば、その分あと）を撮る。保存するパスを返す（撮れない環境・すでに撮る予定があるときは空）
	FString TakeScreenshot( const FString& Name = FString(), const FScreenshotOptions& Options = FScreenshotOptions() );

	// GIF の記録を始める。保存するパスを返す（すでに記録中・撮れない環境なら空）
	FString StartRecording( const FString& Name = FString(), const FGifRecordOptions& Options = FGifRecordOptions() );
	// 記録を止めて、GIF のファイルにする（記録していなければ false）
	bool StopRecording();

	FORCEINLINE bool IsRecording() const
	{
		return bRecording;
	}

	// 最後に保存したファイルのパス（まだ無ければ空）と、最後の保存が成功したか
	FORCEINLINE const FString& GetLastSavedPath() const
	{
		return LastSavedPath;
	}

	FORCEINLINE bool DidLastSaveSucceed() const
	{
		return bLastSaveSucceeded;
	}

	// F9 / Shift + F9 で撮るときの設定（デバッグ用の UI を含めるか・形式・縮小・矩形など）。既定は FScreenshotOptions() / FGifRecordOptions()
	FORCEINLINE void SetHotkeyScreenshotOptions( const FScreenshotOptions& Options )
	{
		HotkeyScreenshotOptions = Options;
	}

	FORCEINLINE const FScreenshotOptions& GetHotkeyScreenshotOptions() const
	{
		return HotkeyScreenshotOptions;
	}

	FORCEINLINE void SetHotkeyRecordOptions( const FGifRecordOptions& Options )
	{
		HotkeyRecordOptions = Options;
	}

	FORCEINLINE const FGifRecordOptions& GetHotkeyRecordOptions() const
	{
		return HotkeyRecordOptions;
	}

	// F9 / Shift + F9 を受け付けるか（既定は受け付ける。ゲームが F9 を別の用に使うときは false に）
	FORCEINLINE void SetHotkeysEnabled( bool bEnabled )
	{
		bHotkeysEnabled = bEnabled;
	}

	// 保存先のフォルダ（既定は Saved/Screenshots）を替える
	void SetDirectory( const FString& InDirectory );

	FORCEINLINE const FString& GetDirectory() const
	{
		return Directory;
	}

	// IFrameCaptureSink
	bool WantsFrame( uint64 FrameIndex, EFrameCaptureStage Stage ) const override;
	void OnFrame( const FFramePixels& Frame ) override;

private:
	void SaveScreenshot( const FFramePixels& Frame );
	void RecordFrame( const FFramePixels& Frame );
	void ApplyCommandLine( uint64 FrameIndex );

	IPlatform* Platform = nullptr;
	FString Directory;
	FString LastSavedPath;
	// 最後に使った番号（Screenshot_00001 の 1）。同じフレームの 2 つめが、同じ番号にならないように
	int32 LastNumber = 0;
	bool bLastSaveSucceeded = false;
	FScreenshotOptions HotkeyScreenshotOptions;
	FGifRecordOptions HotkeyRecordOptions;
	bool bHotkeysEnabled = true;

	// 撮る予定のスクリーンショット
	bool bScreenshotPending = false;
	uint64 ScreenshotFrame = 0;
	FString ScreenshotPath;
	FScreenshotOptions ScreenshotOptions;

	// GIF の記録
	bool bRecording = false;
	uint64 RecordStartFrame = 0;
	FString RecordPath;
	FGifRecordOptions RecordOptions;
	TUniquePtr<FGifEncoder> Encoder;
	int32 RecordedFrames = 0;
	// 表示時間の端数（1/100 秒）を次の 1 枚へ持ち越す
	float64 DelayCarry = 0.0;

	// 起動引数の指示
	FCaptureCommandLine CommandLine;
	bool bCommandScreenshotIssued = false;
	bool bCommandRecordIssued = false;
};

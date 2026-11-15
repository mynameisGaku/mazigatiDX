// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Input/ITextInput.h"
#include "Library/Core/Interfaces/Platform/INativeWindow.h"
#include "Library/Core/Interfaces/Tickable/ITickable.h"
#include "Library/Framework/Subsystem/AppSubsystem.h"

//=============================================================================
// CWindowsTextInputSubsystem
//   Windows の文字入力（WM_CHAR・キーの自動反復・かな漢字変換 IME）を、ITextInput として貸す。描画 API を知らない。
//   窓に届いたメッセージを INativeWindowMessageObserver で受け取り、フレームごとに、届いた文字と編集の操作を並べ直す。
//
//   - SetTextInputActive( true ) の間だけ、メッセージを受ける（false の間は、何も受けず、OS・ImGui へそのまま渡す）
//   - IME は、OS の変換の窓・候補の窓を出さず、途中の文字列と候補を FImeComposition として返す（描くのは UI の側）
//   - 文字列は UTF-8。制御文字（Enter・Tab など）は、文字ではなく編集の操作（ETextEditCommand）で返す
//=============================================================================
class CWindowsTextInputSubsystem : public CAppSubsystem, public ITextInput, public INativeWindowMessageObserver, public ITickable
{
public:
	// 現段階では DX12 のとき作る（DxLib は、DxLib の入力が ITextInput を持つ）
	bool ShouldCreateSubsystem( CEngine& Engine ) const override;
	// OS の窓を借りて、メッセージの受け取り手になる
	void Initialize( FSubsystemCollection& Collection ) override;
	void Deinitialize() override;
	// 届いたものを、このフレームの入力として確定する
	void Tick( float32 DeltaTime ) override;

	void SetTextInputActive( bool bActive ) override;
	bool IsTextInputActive() const override;
	const TArray<FTextInputEvent>& GetTextInputEvents() const override;
	const FImeComposition& GetImeComposition() const override;

	bool OnNativeWindowMessage( void* Window, uint32 Message, uint64 WParam, int64 LParam, int64& OutResult ) override;

private:
	// WM_CHAR の 1 文字（UTF-16 の 1 単位）。サロゲートペアは、2 つ届いたところで 1 つの文字にする
	void HandleChar( uint16 Unit );
	// WM_KEYDOWN の、編集の操作になるキー
	void HandleKeyDown( uint32 VirtualKey );
	// WM_IME_COMPOSITION: 確定した文字と、途中の文字列
	void HandleImeComposition( void* Window, int64 LParam );
	// WM_IME_NOTIFY: 変換の候補
	void HandleImeNotify( void* Window, uint64 Command );

	INativeWindow* Window = nullptr; // 借用する OS の窓
	bool bActive = false;
	TArray<FTextInputEvent> Pending; // メッセージで届いた分（Tick で Events へ）
	TArray<FTextInputEvent> Events; // このフレームの分
	FImeComposition PendingIme; // メッセージで更新する、変換の状態
	FImeComposition Ime; // このフレームの変換の状態
	uint16 HighSurrogate = 0; // 前の WM_CHAR で届いた、サロゲートペアの前半
};

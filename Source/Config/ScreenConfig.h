#pragma once
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Json/JsonObject.h"
#include "Library/Platform/WindowSettings.h"

class FScreenConfig : public NJson::CJsonObject
{
public:
	explicit FScreenConfig( const FString& Filename );

	FORCEINLINE int32 GetWidth() const { return Width; }
	FORCEINLINE int32 GetHeight() const { return Height; }
	FORCEINLINE int32 GetOffsetX() const { return OffsetX; }
	FORCEINLINE int32 GetOffsetY() const { return OffsetY; }
	FORCEINLINE FString GetTitle() const { return Title; }
	// 未指定・不正な値なら空
	FORCEINLINE TOptional<EWindowMode> GetWindowMode() const { return WindowMode; }
	FORCEINLINE float64 GetExtendRate() const { return ExtendRate; }

	// CDxLibBackend::Run / Init に渡す設定へ変換（その他の項目は Base の値を使う）
	FWindowSettings ToWindowSettings( const FWindowSettings& Base = FWindowSettings() ) const;

private:
	int32 Width = 0;
	int32 Height = 0;
	int32 OffsetX = 0;
	int32 OffsetY = 0;
	FString Title;
	TOptional<EWindowMode> WindowMode;
	float64 ExtendRate = 0.0;
};

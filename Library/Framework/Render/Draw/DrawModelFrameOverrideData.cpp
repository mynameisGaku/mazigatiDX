// SPDX-License-Identifier: MIT
#include "DrawModelFrameOverrideData.h"

#include "Library/Math/Matrix4x4.h"

FDrawModelFrameOverrideData::FDrawModelFrameOverrideData( int32 InFrame, const FMatrix4x4& LocalToParent )
	: Frame( InFrame )
{
	// source matrixの行を読む位置。
	for ( int32 Row = 0; Row < 4; ++Row )
	{
		// 現在行のcolumn位置。
		for ( int32 Column = 0; Column < 4; ++Column )
		{
			M[Row * 4 + Column] = LocalToParent.m[Row][Column];
		}
	}
}

// SPDX-License-Identifier: MIT
#include "Library/Framework/PixelEffect/PixelMask.h"

#include "Library/Core/Types/CString.h"
#include "Library/Math/MathUtils.h"

FPixelMask NPixelMask::FromRows( TArrayView<const char* const> Rows, int32 Scale )
{
	FPixelMask Mask;
	if ( Rows.Num() <= 0 || Rows[0] == nullptr )
	{
		return Mask;
	}
	const int32 Columns = static_cast<int32>( NCString::Length( Rows[0] ) );
	const int32 Factor = NMath::Max( 1, Scale );
	if ( Columns <= 0 )
	{
		return Mask;
	}
	Mask.Width = Columns * Factor;
	Mask.Height = Rows.Num() * Factor;
	Mask.Data.SetNum( Mask.Width * Mask.Height, 0 );
	for ( int32 Row = 0; Row < Rows.Num(); ++Row )
	{
		const char* Line = Rows[Row];
		if ( Line == nullptr )
		{
			continue;
		}
		for ( int32 Column = 0; Column < Columns && Line[Column] != '\0'; ++Column )
		{
			if ( Line[Column] == '.' || Line[Column] == ' ' )
			{
				continue;
			}
			for ( int32 SubY = 0; SubY < Factor; ++SubY )
			{
				for ( int32 SubX = 0; SubX < Factor; ++SubX )
				{
					Mask.Data[( Row * Factor + SubY ) * Mask.Width + Column * Factor + SubX] = 1;
				}
			}
		}
	}
	return Mask;
}

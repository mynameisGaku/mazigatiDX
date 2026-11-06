#include "ImmediateDraw.h"
#include "Library/Core/Interfaces/Render/IDrawExecutor.h"

void FImmediateDraw::Flush()
{
	if ( Storage.Words.Num() == 0 )
	{
		return;
	}

	if ( Executor != nullptr )
	{
		Executor->Execute( FDrawSpan { Storage.Words.GetData(), Storage.Words.Num() } );
	}
	Storage.Clear();
}

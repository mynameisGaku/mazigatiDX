// SPDX-License-Identifier: MIT
#include "BufferResourceHandle.h"
#include "RenderResourceSerial.h"

FBufferResourceHandle FBufferResourceHandle::Allocate()
{
	return FBufferResourceHandle( NRenderResourceSerial::Allocate() );
}

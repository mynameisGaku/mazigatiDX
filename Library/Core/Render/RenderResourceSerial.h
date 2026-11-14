// SPDX-License-Identifier: MIT
#pragma once
#include "Library/Core/Types/TypeDefinition.h"

namespace NRenderResourceSerial
{
	// プロセス内で再利用しない資源・提出先の番号を発行する。枯渇時は0。
	uint64 Allocate();
}

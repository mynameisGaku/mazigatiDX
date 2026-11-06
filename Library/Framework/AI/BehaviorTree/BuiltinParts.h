// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Framework/AI/Registry/AIRegistry.h"

//=============================================================================
// 標準の部品（ビヘイビアツリーの Task）
//   NAI::RegisterBuiltinParts( Registry ) で登録される。アプリのレジストリ（NAI::GetRegistry()）には、最初に取り出すときに自動で登録される。
//
//   Wait              Seconds 秒たつまで Running、そのあと Success
//   SetBlackboard     Key に Value を書いて Success（Value は、キーの型に合わせて読む。Bool は true / false、Enum は選択肢の名前。型に合わないと Failure）
//   Log               Message をログに出して Success
//   Succeed / Fail    すぐ Success / Failure（動作の確認や、枝の仮置きに）
//=============================================================================
namespace NAI
{
	void RegisterBuiltinParts( CAIRegistry& Registry );
}

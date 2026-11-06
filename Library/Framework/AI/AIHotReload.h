// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/String.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Framework/AI/Blackboard/Blackboard.h"

class CScene;
class CGameObject;

//=============================================================================
// AI のホットリロード
//   エディタで保存し直したアセット（.gxstate / .gxbt / .gxblackboard）を、動いているコンポーネントへ反映する。
//   - 読み込めなければ（壊れたファイルなど）、元の定義のまま動き続ける
//   - ブラックボードは、作り直して、キー名と型が同じ値を引き継ぐ（キーが増えたら既定値、無くなったら捨てる）
//   - ステートマシン・ビヘイビアツリーは、動いていたものは、最初から動かし直す（今いるステートやノードが、新しい定義に残っているとは限らないため）
//   - 同じオブジェクトのステートマシンとツリーが、同じブラックボードを共有していれば、共有したまま作り直す
//   - ゲームスレッドで使う
//=============================================================================
// 同じオブジェクトの部品どうしが、作り直したブラックボードを共有するための入れ物（NAI::ReloadObject が使う）
struct FAIReloadContext
{
	FString BlackboardPath;
	TSharedPtr<CBlackboard> Blackboard;
};

namespace NAI
{
	// Path（アセットのルートからの相対。区切りは / でも \ でもよい）のアセットを使っているコンポーネントを、すべて読み直す。読み直した（読み直そうとした）コンポーネントの数を返す。
	// Path は、ステートマシン・ビヘイビアツリー・ブラックボードのどれでもよい（ブラックボードなら、それを指すコンポーネントすべて）
	int32 ReloadAsset( CScene& Scene, const FString& Path );

	// オブジェクトの、ステートマシンとビヘイビアツリーを、読み直す（ブラックボードの共有を保つ）。読み直せた部品の数を返す
	int32 ReloadObject( CGameObject& Object );
}

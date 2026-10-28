#include "__NAME__Sample.h"

#include "Library/Framework/Render/ImmediateDraw.h"
#include "Source/Catalog/SampleCatalog.h"

namespace
{
	// サンプル集への登録（一覧に出す名前・説明・順番）
	const TSampleRegistrar<C__NAME__Sample> Registrar( FSampleInfo { "__NAME__", "__TITLE__", "__DESCRIPTION__", __ORDER__ } );
}

void C__NAME__Sample::OnEnter()
{
	// ここで、オブジェクトを置く:  SpawnObject<CMyObject>( ... );
}

void C__NAME__Sample::OnSampleDraw( FImmediateDraw& Screen )
{
	Screen.Text( 16, 64, "This is the __NAME__ sample.", FColor32::White );
}

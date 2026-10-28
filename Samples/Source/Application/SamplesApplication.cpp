#include "SamplesApplication.h"

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Log/Log.h"
#include "Library/Framework/Scene/SceneManager.h"
#include "Library/Platform/CommandLine.h"
#include "Source/Browser/BrowserScene.h"
#include "Source/Catalog/SampleCatalog.h"

DEFINE_LOG_CATEGORY_STATIC( LogSamples, Log )

void CSamplesApplication::OnInit()
{
	Overlay.Init();
	Overlay.SetDebugWindowVisible( false );
	Overlay.SetStatusLineVisible( false );

	CSampleCatalog& Catalog = CSampleCatalog::Get();
	for ( const FString& Reason : Catalog.GetRegistrationErrors() )
	{
		LOG( LogSamples, Error, "%s", *Reason );
	}

	CSceneManager& Scenes = GetSceneManager();
	Scenes.RegisterScene<CBrowserScene>( CSampleCatalog::GetBrowserSceneName() );
	Catalog.RegisterAll( Scenes );

	TArray<FString> Names;
	for ( const FSampleInfo& Sample : Catalog.GetSorted() ) Names.Add( Sample.Name );
	LOG( LogSamples, Log, "Samples (%d): %s", Names.Num(), *FString::Join( Names, ", " ) );

	// --sample=Name で、そのサンプルから始める（無い名前なら、警告を出して一覧から始める）
	const TOptional<FString> Requested = NCommandLine::GetValue( "sample" );
	if ( Requested.IsSet() && Catalog.Find( Requested.GetValue() ) != nullptr )
	{
		Catalog.SetLastSelected( Requested.GetValue() );
		Scenes.ChangeScene( Requested.GetValue() );
	}
	else
	{
		if ( Requested.IsSet() ) LOG( LogSamples, Warning, "Unknown sample '%s'. Starting from the list.", *Requested.GetValue() );
		Scenes.ChangeScene( CSampleCatalog::GetBrowserSceneName() );
	}
}

void CSamplesApplication::OnTick( float32 DeltaTime )
{
	Overlay.Tick();
}

void CSamplesApplication::OnDraw()
{
	Overlay.Draw( *this );
}

void CSamplesApplication::OnTerm()
{
	Overlay.Term();
}

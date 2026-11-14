// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: アセット
void NConsole::NBuiltin::RegisterAssetCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "assets", "Show the asset statistics: counts, memory, load times, video memory",
		 []()
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 return Assets != nullptr ? DescribeAssets( *Assets ) : FString( NotRunning );
		 },
		 {}, CategoryAssets );

	Add( "assets.list", "List the loaded assets (type, path, references, memory, load time)",
		 []( FConsoleContext& Context, TOptional<FString> Filter )
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr ) return false;
			 TArray<FAssetInfo> Infos = Assets->GetLoadedAssets();
			 Infos.StableSort( []( const FAssetInfo& A, const FAssetInfo& B ) { return A.MemoryBytes > B.MemoryBytes; } );
			 int32 Count = 0;
			 for ( const FAssetInfo& Info : Infos )
			 {
				 if ( Filter.IsSet() && !Info.Path.Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) && !FString( ToString( Info.Type ) ).Contains( Filter.GetValue(), ESearchCase::IgnoreCase ) ) continue;
				 Context.Print( FString::Printf( "  %-14s %-10s refs %-3d load %6.1f ms  %s%s", ToString( Info.Type ), *FormatBytes( Info.MemoryBytes ), Info.RefCount, Info.LoadMs, *Info.Path, Info.bReady ? "" : "  (loading)" ) );
				 ++Count;
			 }
			 Context.Print( FString::Printf( "%d asset(s)", Count ) );
			 return true;
		 },
		 { { "filter", "Only the assets whose path or type contains this text (for example Texture)." } }, CategoryAssets );

	Add( "assets.dump", "Write the asset list and statistics to the log",
		 []( FConsoleContext& Context )
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr ) return false;
			 Assets->DumpToLog();
			 Context.Print( "written to the log (category LogAsset)" );
			 return true;
		 },
		 {}, CategoryAssets );

	Add( "assets.release", "Forget the records of assets nobody uses any more",
		 []( FConsoleContext& Context )
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr ) return false;
			 Assets->ReleaseUnused();
			 Context.Print( "done" );
			 return true;
		 },
		 {}, CategoryAssets );

	Add( "assets.root", "Show the asset root folder",
		 []()
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 return Assets != nullptr ? Assets->GetRootDirectory() : FString( NotRunning );
		 },
		 {}, CategoryAssets );

	Add( "assets.export", "Convert the models under the asset root (.fbx .obj .x .pmx ...) to .mv1",
		 []( FConsoleContext& Context, TOptional<bool> Force )
		 {
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Assets == nullptr ) return false;
			 const FModelBatchExportResult Batch = Assets->ExportAllModels( Force.IsSet() && Force.GetValue() );
			 for ( const FModelExportResult& Item : Batch.Items )
			 {
				 if ( Item.bSuccess ) Context.Print( "  " + Item.GetSummary() );
				 else Context.Error( "  " + Item.GetSummary() );
			 }
			 Context.Print( Batch.GetSummary() );
			 return Batch.NumFailed == 0;
		 },
		 { { "force", "true to convert everything again. By default only the missing or outdated .mv1 files are made." } }, CategoryAssets );
}

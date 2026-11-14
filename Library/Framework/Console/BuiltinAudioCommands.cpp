// SPDX-License-Identifier: MIT
#include "BuiltinCommandHelpers.h"

// 組み込みのコマンド: 音
void NConsole::NBuiltin::RegisterAudioCommands( FConsoleRegistry& Registry )
{
	const auto Add = [&Registry]( const char* Name, const char* Description, auto&& Function, const TArray<FConsoleArgSpec>& Args, const char* Category )
	{
		AddCommand( Registry, Name, Description, Forward<decltype( Function )>( Function ), Args, Category );
	};

	Add( "audio", "Show the audio status: voices, pause, listener and the bus volumes",
		 []( FConsoleContext& Context )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 if ( Audio == nullptr ) return false;
			 const FAudioListener& Listener = Audio->GetListener();
			 Context.Print( FString::Printf( "%d voice(s)%s, listener at %s facing %s", Audio->GetNumVoices(), Audio->IsPaused() ? " (paused)" : "", *Vec3Text( Listener.Position ), *Vec3Text( Listener.Forward ) ) );
			 for ( int32 i = 0; i < static_cast<int32>( EAudioBus::Count ); ++i )
			 {
				 const EAudioBus Bus = static_cast<EAudioBus>( i );
				 Context.Print( FString::Printf( "  %-8s volume %s%s", ToString( Bus ), *NConsole::FormatFloat( Audio->GetBusVolume( Bus ) ), Audio->IsBusMuted( Bus ) ? "  (muted)" : "" ) );
			 }
			 return true;
		 },
		 {}, CategoryAudio );

	Add( "audio.voices", "List the sounds that are playing",
		 []( FConsoleContext& Context )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 if ( Audio == nullptr ) return false;
			 const TArray<FVoiceInfo> Voices = Audio->GetVoiceInfos();
			 for ( const FVoiceInfo& Voice : Voices )
			 {
				 Context.Print( FString::Printf( "  %-8s vol %.2f pan %+.2f pitch %.2f age %5.1f s %s%s%s%s  %s", ToString( Voice.Bus ), Voice.Volume, Voice.Pan, Voice.Pitch, Voice.Age, Voice.bLooping ? "loop " : "",
												 Voice.bPaused ? "paused " : "", Voice.bFading ? "fading " : "", Voice.bSpatial ? "3D" : "", *Voice.SoundPath ) );
			 }
			 Context.Print( FString::Printf( "%d voice(s)", Voices.Num() ) );
			 return true;
		 },
		 {}, CategoryAudio );

	Add( "audio.volume", "Show or set the volume of an audio bus",
		 []( FConsoleContext& Context, const FString& BusName, TOptional<float32> Volume )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 EAudioBus Bus = EAudioBus::Master;
			 if ( Audio == nullptr ) return false;
			 if ( !ParseBus( BusName, Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Volume.IsSet() ) Audio->SetBusVolume( Bus, NMath::Max( Volume.GetValue(), 0.0f ) );
			 Context.Print( FString( ToString( Bus ) ) + " volume " + NConsole::FormatFloat( Audio->GetBusVolume( Bus ) ) );
			 return true;
		 },
		 { { "bus", "Master, Music, Sfx, Ambient, Voice or Ui." }, { "volume", "0 = silent, 1 = normal. Without it, shows the current volume." } }, CategoryAudio );
	Registry.SetArgChoices( "audio.volume", 0, GetBusNames() );

	Add( "audio.mute", "Mute or unmute an audio bus",
		 []( FConsoleContext& Context, const FString& BusName, TOptional<bool> On )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 EAudioBus Bus = EAudioBus::Master;
			 if ( Audio == nullptr ) return false;
			 if ( !ParseBus( BusName, Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 const bool bMute = On.IsSet() ? On.GetValue() : !Audio->IsBusMuted( Bus );
			 Audio->SetBusMuted( Bus, bMute );
			 Context.Print( FString( ToString( Bus ) ) + ( bMute ? " muted" : " unmuted" ) );
			 return true;
		 },
		 { { "bus", "Master, Music, Sfx, Ambient, Voice or Ui." }, { "on", "true to mute, false to unmute. Without it, switches." } }, CategoryAudio );
	Registry.SetArgChoices( "audio.mute", 0, GetBusNames() );

	Add( "audio.pause", "Pause or resume all sounds",
		 []( FConsoleContext& Context, TOptional<bool> On )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 if ( Audio == nullptr ) return false;
			 const bool bPause = On.IsSet() ? On.GetValue() : !Audio->IsPaused();
			 Audio->SetPaused( bPause );
			 Context.Print( bPause ? "audio paused" : "audio resumed" );
			 return true;
		 },
		 { { "on", "true to pause, false to resume. Without it, switches." } }, CategoryAudio );

	Add( "audio.stop", "Stop all sounds",
		 []( FConsoleContext& Context, TOptional<float32> Fade )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 if ( Audio == nullptr ) return false;
			 Audio->StopAll( Fade.IsSet() ? NMath::Max( Fade.GetValue(), 0.0f ) : 0.0f );
			 Context.Print( "stopped" );
			 return true;
		 },
		 { { "fade", "Fade-out time in seconds (default 0)." } }, CategoryAudio );

	Add( "audio.play", "Play a sound file once (or in a loop) to try it",
		 []( FConsoleContext& Context, const FString& Path, TOptional<FString> BusName, TOptional<float32> Volume, TOptional<float32> Pitch, TOptional<bool> Loop )
		 {
			 CAudioSubsystem* Audio = GetAppSubsystem<CAudioSubsystem>();
			 CAssetSubsystem* Assets = GetAppSubsystem<CAssetSubsystem>();
			 if ( Audio == nullptr || Assets == nullptr ) return false;
			 FSoundPlayParams Params;
			 if ( BusName.IsSet() && !ParseBus( BusName.GetValue(), Params.Bus ) )
			 {
				 Context.Error( "unknown bus '" + BusName.GetValue() + "' (" + FString::Join( GetBusNames(), ", " ) + ")" );
				 return false;
			 }
			 if ( Volume.IsSet() ) Params.Volume = Volume.GetValue();
			 if ( Pitch.IsSet() ) Params.Pitch = Pitch.GetValue();
			 Params.bLoop = Loop.IsSet() && Loop.GetValue();
			 const TSharedPtr<CSound> Sound = Assets->LoadSound( Path );
			 if ( !Sound.IsValid() )
			 {
				 Context.Error( "could not load the sound '" + Path + "' (see the log)" );
				 return false;
			 }
			 const FSoundHandle Handle = Audio->Play( Sound, Params );
			 Context.Print( Audio->IsPlaying( Handle ) ? "playing " + Path : "could not start " + Path + " (too many sounds?)" );
			 return Audio->IsPlaying( Handle );
		 },
		 { { "file", "Sound file path, relative to the asset root." }, { "bus", "Which bus to play on (default Sfx)." }, { "volume", "Volume (default 1)." }, { "pitch", "Pitch multiplier (default 1)." }, { "loop", "true to loop (stop it with audio.stop)." } },
		 CategoryAudio );
	Registry.SetArgChoices( "audio.play", 1, GetBusNames() );
}

// SPDX-License-Identifier: MIT
#include "SaveGameStorage.h"

#include "Library/Core/File/File.h"
#include "Library/Core/Log/Log.h"
#include "Library/Core/Types/Checksum.h"
#include "Library/Platform/System/FileSystem.h"

DEFINE_LOG_CATEGORY_STATIC( LogSaveGame, Log )

namespace
{
	// ファイルの形: 識別子 "GXSV"（4）+ バージョン（4）+ 中身の大きさ（8）+ CRC-32（4）+ 中身
	constexpr uint32 FormatVersion = 1;
	constexpr int32 HeaderBytes = 20;
	constexpr const char* Extension = ".gxsave";

	void PutUInt32( TArray<uint8>& Out, uint32 Value )
	{
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			Out.Add( static_cast<uint8>( ( Value >> ( 8 * Index ) ) & 0xFFu ) );
		}
	}

	void PutUInt64( TArray<uint8>& Out, uint64 Value )
	{
		for ( int32 Index = 0; Index < 8; ++Index )
		{
			Out.Add( static_cast<uint8>( ( Value >> ( 8 * Index ) ) & 0xFFull ) );
		}
	}

	uint32 GetUInt32( const uint8* Data )
	{
		return static_cast<uint32>( Data[0] ) | ( static_cast<uint32>( Data[1] ) << 8 ) | ( static_cast<uint32>( Data[2] ) << 16 ) | ( static_cast<uint32>( Data[3] ) << 24 );
	}

	uint64 GetUInt64( const uint8* Data )
	{
		return static_cast<uint64>( GetUInt32( Data ) ) | ( static_cast<uint64>( GetUInt32( Data + 4 ) ) << 32 );
	}

	// ファイルの全体から、中身を取り出す。識別子・バージョン・大きさ・CRC のどれかが合わなければ false
	bool ExtractPayload( const TArray<uint8>& File, TArray<uint8>& OutPayload )
	{
		if ( File.Num() < HeaderBytes || File[0] != 'G' || File[1] != 'X' || File[2] != 'S' || File[3] != 'V' )
		{
			return false;
		}
		if ( GetUInt32( File.GetData() + 4 ) != FormatVersion )
		{
			return false;
		}
		const uint64 PayloadSize = GetUInt64( File.GetData() + 8 );
		if ( PayloadSize != static_cast<uint64>( File.Num() - HeaderBytes ) )
		{
			return false;
		}
		const uint32 StoredCrc = GetUInt32( File.GetData() + 16 );
		if ( NChecksum::Crc32( File.GetData() + HeaderBytes, static_cast<usize>( PayloadSize ) ) != StoredCrc )
		{
			return false;
		}
		OutPayload.Reset();
		OutPayload.Append( File.GetData() + HeaderBytes, static_cast<int32>( PayloadSize ) );
		return true;
	}

	enum class EFileState : uint8
	{
		Missing,
		Corrupt,
		Valid,
	};

	// ファイルを読んで、中身を取り出す
	EFileState ReadFile( const FString& Path, TArray<uint8>& OutPayload )
	{
		if ( !NFileSystem::FileExists( Path ) )
		{
			return EFileState::Missing;
		}
		TArray<uint8> File;
		if ( !NFile::ReadAllBytes( Path, File ) )
		{
			return EFileState::Corrupt;
		}
		return ExtractPayload( File, OutPayload ) ? EFileState::Valid : EFileState::Corrupt;
	}

	FDateTime ToDateTime( const FFileTime& Time )
	{
		FDateParts Parts;
		Parts.Year = Time.Year;
		Parts.Month = Time.Month;
		Parts.Day = Time.Day;
		Parts.Hour = Time.Hour;
		Parts.Minute = Time.Minute;
		Parts.Second = Time.Second;
		FDateTime Result;
		FDateTime::FromLocalParts( Parts, Result );
		return Result;
	}
}

CSaveGameStorage::CSaveGameStorage( const FString& InDirectory )
	: Directory( InDirectory )
{
}

bool CSaveGameStorage::IsValidSlotName( const FString& Slot )
{
	if ( Slot.IsEmpty() || Slot.Len() > 64 )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Slot.Len(); ++Index )
	{
		const char Character = Slot[Index];
		const bool bAllowed = ( Character >= 'a' && Character <= 'z' ) || ( Character >= 'A' && Character <= 'Z' ) || ( Character >= '0' && Character <= '9' ) || Character == '_' || Character == '-';
		if ( !bAllowed )
		{
			return false;
		}
	}
	return true;
}

FString CSaveGameStorage::GetMainPath( const FString& Slot ) const
{
	return Directory / ( Slot + Extension );
}

FString CSaveGameStorage::GetBackupPath( const FString& Slot ) const
{
	return Directory / ( Slot + Extension + ".bak" );
}

FString CSaveGameStorage::GetTemporaryPath( const FString& Slot ) const
{
	return Directory / ( Slot + Extension + ".tmp" );
}

bool CSaveGameStorage::Write( const FString& Slot, const TArray<uint8>& Data ) const
{
	if ( !IsValidSlotName( Slot ) || !NFileSystem::EnsureDirectory( Directory ) )
	{
		return false;
	}

	// 識別子 + バージョン + 大きさ + CRC-32 + 中身 を、一時ファイルに全部書く
	TArray<uint8> File;
	File.Reserve( HeaderBytes + Data.Num() );
	File.Add( 'G' );
	File.Add( 'X' );
	File.Add( 'S' );
	File.Add( 'V' );
	PutUInt32( File, FormatVersion );
	PutUInt64( File, static_cast<uint64>( Data.Num() ) );
	PutUInt32( File, NChecksum::Crc32( Data.GetData(), static_cast<usize>( Data.Num() ) ) );
	File.Append( Data );

	const FString Main = GetMainPath( Slot );
	const FString Backup = GetBackupPath( Slot );
	const FString Temporary = GetTemporaryPath( Slot );
	if ( !NFile::WriteAllBytes( Temporary, File.GetData(), static_cast<usize>( File.Num() ) ) )
	{
		LOG( LogSaveGame, Error, "Failed to write the save slot '%s' (temporary file).", *Slot );
		NFileSystem::RemoveFile( Temporary );
		return false;
	}

	// いまのセーブが壊れていなければ、1 つ前のセーブ（バックアップ）にする。壊れていれば、前のバックアップを守る
	TArray<uint8> Current;
	if ( ReadFile( Main, Current ) == EFileState::Valid && !NFileSystem::OverwriteFile( Main, Backup ) )
	{
		LOG( LogSaveGame, Warning, "Could not keep the previous save of '%s' as a backup.", *Slot );
	}
	if ( !NFileSystem::OverwriteFile( Temporary, Main ) )
	{
		LOG( LogSaveGame, Error, "Failed to replace the save slot '%s'.", *Slot );
		NFileSystem::RemoveFile( Temporary );
		return false;
	}
	return true;
}

ESaveLoadResult CSaveGameStorage::Read( const FString& Slot, TArray<uint8>& Out ) const
{
	if ( !IsValidSlotName( Slot ) )
	{
		return ESaveLoadResult::InvalidSlotName;
	}
	TArray<uint8> Payload;
	const EFileState MainState = ReadFile( GetMainPath( Slot ), Payload );
	if ( MainState == EFileState::Valid )
	{
		Out = MoveTemp( Payload );
		return ESaveLoadResult::Ok;
	}
	const EFileState BackupState = ReadFile( GetBackupPath( Slot ), Payload );
	if ( BackupState == EFileState::Valid )
	{
		LOG( LogSaveGame, Warning, "The save slot '%s' is %s; loaded the previous save (backup).", *Slot, MainState == EFileState::Missing ? "missing" : "corrupt" );
		Out = MoveTemp( Payload );
		return ESaveLoadResult::RecoveredFromBackup;
	}
	return MainState == EFileState::Missing && BackupState == EFileState::Missing ? ESaveLoadResult::NotFound : ESaveLoadResult::Corrupt;
}

TArray<FSaveSlotInfo> CSaveGameStorage::List() const
{
	TArray<FSaveSlotInfo> Result;
	const FString Pattern = FString( "*" ) + Extension;
	TArray<FString> Names = NFileSystem::ListFiles( Directory, Pattern );
	// ワイルドカードは ".gxsave.bak" や ".gxsave.tmp" にも当たることがあるので、拡張子がちょうど一致するものだけ使う
	for ( const FString& FileName : Names )
	{
		if ( !FileName.EndsWith( Extension, ESearchCase::IgnoreCase ) )
		{
			continue;
		}
		FSaveSlotInfo Info;
		Info.Name = FileName.Left( FileName.Len() - FString( Extension ).Len() );
		if ( !IsValidSlotName( Info.Name ) )
		{
			continue;
		}
		TArray<uint8> Payload;
		Info.bValid = ReadFile( GetMainPath( Info.Name ), Payload ) == EFileState::Valid;
		Info.PayloadBytes = Info.bValid ? Payload.Num() : 0;
		Info.bHasBackup = NFileSystem::FileExists( GetBackupPath( Info.Name ) );
		FFileTime Time;
		if ( NFileSystem::GetLastWriteTime( GetMainPath( Info.Name ), Time ) )
		{
			Info.Modified = ToDateTime( Time );
		}
		Result.Add( Info );
	}
	Result.Sort( []( const FSaveSlotInfo& A, const FSaveSlotInfo& B ) { return A.Name.Compare( B.Name, ESearchCase::IgnoreCase ) < 0; } );
	return Result;
}

bool CSaveGameStorage::Delete( const FString& Slot ) const
{
	if ( !IsValidSlotName( Slot ) )
	{
		return false;
	}
	bool bOk = true;
	for ( const FString& Path : { GetMainPath( Slot ), GetBackupPath( Slot ), GetTemporaryPath( Slot ) } )
	{
		if ( NFileSystem::FileExists( Path ) )
		{
			bOk = NFileSystem::RemoveFile( Path ) && bOk;
		}
	}
	return bOk;
}

bool CSaveGameStorage::Exists( const FString& Slot ) const
{
	return IsValidSlotName( Slot ) && ( NFileSystem::FileExists( GetMainPath( Slot ) ) || NFileSystem::FileExists( GetBackupPath( Slot ) ) );
}

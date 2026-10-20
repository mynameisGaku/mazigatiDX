#pragma once
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "Library/Core/Interfaces/Asset/AssetBackend.h"
#include "Library/Core/Interfaces/Audio/AudioBackend.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Log/Log.h"
#include "Library/Platform/DxLib/DxLibInclude.h"

//=============================================================================
// アセット・アニメーション・オーディオの自己確認で使う道具（CAssetSelfTestScene）
//   確認の結果を数える FChecker、読み込み・再生を偽物にするバックエンド、描画コマンドを集める実行先、
//   確認用のファイル（BMP / WAV / フォント）を作る関数。
//=============================================================================
DECLARE_LOG_CATEGORY_EXTERN( LogAssetSelfTest, Log )

class CAssetManager;
class CScene;

namespace NAssetTest
{
	//=========================================================================
	// 確認の結果を数える
	//=========================================================================
	struct FChecker
	{
		int64 NumChecked = 0;
		int64 NumFailed = 0;
		int64 NumSkipped = 0;
		const char* Group = "";

		void Check( bool bOk, const char* Format, ... )
		{
			++NumChecked;
			if ( bOk ) return;
			++NumFailed;
			if ( NumFailed > 400 ) return;

			char Message[512];
			va_list Args;
			va_start( Args, Format );
			std::vsnprintf( Message, sizeof( Message ), Format, Args );
			va_end( Args );
			LOG( LogAssetSelfTest, Error, "[%s] %s", Group, Message );
		}

		void Near( float64 Actual, float64 Expected, float64 Tolerance, const char* What )
		{
			Check( std::fabs( Actual - Expected ) <= Tolerance, "%s: expected %.5f, got %.5f", What, Expected, Actual );
		}

		void Skip( const char* What )
		{
			++NumSkipped;
			LOG( LogAssetSelfTest, Warning, "[%s] skipped: %s", Group, What );
		}
	};

	//=========================================================================
	// 偽物のバックエンド（読み込み・再生の呼び出しを数えるだけ）
	//=========================================================================
	class FFakeAssetBackend final : public IAssetBackend
	{
	public:
		struct FModelData
		{
			std::vector<FString> Names;
			std::vector<float32> Lengths;
			int32 MeshCount = 1;                   // 0 ならアニメーションだけのファイル
			std::vector<FString> FrameNames;       // 骨の名前
			bool bHasBounds = false;               // GetModelBounds が答える大きさ
			float32 BoundsMin[3] = {};
			float32 BoundsMax[3] = {};
			std::vector<float32> MeshPositions;    // GetModelCollisionMesh が答える三角形（空なら、答えない）
			std::vector<uint32> MeshIndices;
		};

		int32 Next = 1000;
		std::set<int32> Textures, Models, Fonts, Sounds;
		std::set<int32> VertexBuffers, IndexBuffers;   // 作って、まだ解放していないバッファ
		struct FMeshBufferRecord { int32 NumVertices = 0; int32 NumIndices = 0; std::vector<FMeshVertex> Vertices; std::vector<uint32> Indices; };
		std::map<int32, FMeshBufferRecord> MeshRecords;   // 頂点バッファのハンドル -> 渡された中身
		int32 NumMeshBuffersMade = 0;
		std::map<int32, FModelData> ModelData;
		std::map<int32, std::set<int32>> Attached;   // モデル -> 付けているアニメーションの番号
		std::map<int32, int32> NextAttach;
		std::set<int32> Loading;                    // 非同期で、まだ読み込み中のハンドル
		std::vector<FString> FontFilesAdded, FontFilesRemoved;
		std::vector<FFontDesc> FontsMade;
		std::vector<FString> LoadedModelPaths;     // LoadModel に渡されたパス（読み込んだ順）
		bool bCaptureLoadedFiles = false;          // true なら、LoadModel のときにファイルの中身を控える（一時ファイルは、あとで消えるので）
		std::map<std::string, std::string> LoadedFileContents;   // LoadModel に渡されたパス -> そのときのファイルの中身
		struct FSavedModel { int32 Model; FString Path; FModelSaveOptions Options; };
		std::vector<FSavedModel> SavedModels;      // SaveModelToMV1 の記録
		bool bSaveFails = false;
		FString SaveWarning;
		int32 NumAttachCalls = 0;
		int32 NumDetachCalls = 0;
		int32 NumTextureLoads = 0;
		// 指定番号のtexture読込でInvalidHandleを返す。0以下なら失敗させない。
		int32 FailTextureLoadCall = 0;
		// assetの読み込み待ちを問い合わせた回数。
		int32 NumLoadingQueries = 0;
		int32 NumSoundLoads = 0;
		int32 NumDuplicates = 0;
		int32 TextureWidth = 128;
		int32 TextureHeight = 64;
		// true の間だけ派生領域の作成を再現する。
		bool bSupportTextureRegions = false;
		// この番号の領域作成を失敗させる。0以下なら失敗を注入しない。
		int32 FailTextureRegionCall = 0;
		// この番号のUV取得を失敗させる。0以下なら失敗を注入しない。
		int32 FailTextureUvCall = 0;
		// 派生領域を作成した呼出し回数。
		int32 NumTextureRegionCalls = 0;
		// 派生領域のUVを問い合わせた回数。
		int32 NumTextureUvCalls = 0;
		// 作成試行へ渡された親ハンドルと矩形。
		struct FTextureRegionRecord
		{
			// 派生元texture handle。
			int32 Source = -1;
			// 親画像内の左上X座標。
			int32 X = 0;
			// 親画像内の左上Y座標。
			int32 Y = 0;
			// 領域の幅。
			int32 Width = 0;
			// 領域の高さ。
			int32 Height = 0;
			// 作成成功時の派生handle。
			int32 Handle = -1;
		};
		std::vector<FTextureRegionRecord> TextureRegionRecords;
		// region handleに対応する寸法。
		std::map<int32, std::pair<int32, int32>> TextureRegionSizes;
		// UVを問い合わせたtexture handle。
		std::vector<int32> TextureUvRecords;
		// 解放されたtexture handleの履歴。
		std::vector<int32> DeletedTextures;
		float32 SoundLength = 1.0f;
		FModelData NextModelData;                  // 次の LoadModel が返すモデルのアニメーション

		void FinishLoading() { Loading.clear(); }

		bool CreateMeshBuffers( const FMeshVertex* Vertices, int32 NumVertices, const uint32* Indices, int32 NumIndices, int32& OutVertexBuffer, int32& OutIndexBuffer ) override
		{
			OutVertexBuffer = Next++;
			OutIndexBuffer = Next++;
			VertexBuffers.insert( OutVertexBuffer );
			IndexBuffers.insert( OutIndexBuffer );
			FMeshBufferRecord& Record = MeshRecords[OutVertexBuffer];
			Record.NumVertices = NumVertices;
			Record.NumIndices = NumIndices;
			Record.Vertices.assign( Vertices, Vertices + NumVertices );
			Record.Indices.assign( Indices, Indices + NumIndices );
			++NumMeshBuffersMade;
			return true;
		}
		void DeleteVertexBuffer( int32 VertexBuffer ) override
		{
			VertexBuffers.erase( VertexBuffer );
			MeshRecords.erase( VertexBuffer );
		}
		void DeleteIndexBuffer( int32 IndexBuffer ) override { IndexBuffers.erase( IndexBuffer ); }

		int32 LoadTexture( const FString&, bool bAsync ) override
		{
			++NumTextureLoads;
			if ( NumTextureLoads == FailTextureLoadCall ) return NAsset::InvalidHandle;
			const int32 Handle = Next++;
			Textures.insert( Handle );
			if ( bAsync ) Loading.insert( Handle );
			return Handle;
		}
		int32 CreateTextureFromMemory( const void*, usize ) override
		{
			const int32 Handle = Next++;
			Textures.insert( Handle );
			return Handle;
		}
		// テスト設定が有効な場合だけ派生画像を作り、親と矩形を記録する。
		int32 CreateTextureRegion( int32 Source, int32 X, int32 Y, int32 Width, int32 Height ) override
		{
			if ( !bSupportTextureRegions ) return NAsset::InvalidHandle;
			++NumTextureRegionCalls;
			// 作成要求を履歴へ残す初期record。
			FTextureRegionRecord Record;
			Record.Source = Source;
			Record.X = X;
			Record.Y = Y;
			Record.Width = Width;
			Record.Height = Height;
			if ( NumTextureRegionCalls == FailTextureRegionCall ) { TextureRegionRecords.push_back( Record ); return NAsset::InvalidHandle; }
			// 成功した領域だけがtexture handleと寸法を持つ。
			Record.Handle = Next++;
			Textures.insert( Record.Handle );
			TextureRegionSizes[Record.Handle] = std::make_pair( Width, Height );
			TextureRegionRecords.push_back( Record );
			return Record.Handle;
		}
		// 派生画像のUV範囲を返し、失敗注入と問い合わせ履歴を記録する。
		bool GetTextureUvBounds( int32 Texture, float32 ( &OutUV )[4] ) override
		{
			if ( !bSupportTextureRegions ) return false;
			++NumTextureUvCalls;
			// UV照会の対象handleを追跡する。
			TextureUvRecords.push_back( Texture );
			if ( NumTextureUvCalls == FailTextureUvCall || TextureRegionSizes.count( Texture ) == 0 ) return false;
			OutUV[0] = 0.01f;
			OutUV[1] = 0.02f;
			OutUV[2] = 0.99f;
			OutUV[3] = 0.98f;
			return true;
		}
		// 派生handleは領域寸法、通常handleは親画像寸法を返す。
		bool GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight ) override
		{
			if ( Textures.count( Texture ) == 0 || Loading.count( Texture ) > 0 ) return false;
			// 派生画像では親全体ではなく領域そのものの寸法を返す。
			const auto Found = TextureRegionSizes.find( Texture );
			if ( Found != TextureRegionSizes.end() ) { OutWidth = Found->second.first; OutHeight = Found->second.second; return true; }
			OutWidth = TextureWidth;
			OutHeight = TextureHeight;
			return true;
		}
		// 解放順を記録し、fake backendが持つtexture状態を消す。
		void DeleteTexture( int32 Texture ) override { DeletedTextures.push_back( Texture ); TextureRegionSizes.erase( Texture ); Textures.erase( Texture ); }

		int32 LoadModel( const FString& Path, bool bAsync ) override
		{
			LoadedModelPaths.push_back( Path );
			if ( bCaptureLoadedFiles )
			{
				FILE* File = nullptr;
				if ( fopen_s( &File, *Path, "rb" ) == 0 && File != nullptr )
				{
					std::string Content;
					char Buffer[4096];
					size_t Read = 0;
					while ( ( Read = fread( Buffer, 1, sizeof( Buffer ), File ) ) > 0 ) Content.append( Buffer, Read );
					fclose( File );
					LoadedFileContents[*Path] = Content;
				}
			}
			const int32 Handle = Next++;
			Models.insert( Handle );
			ModelData[Handle] = NextModelData;
			if ( bAsync ) Loading.insert( Handle );
			return Handle;
		}
		int32 DuplicateModel( int32 Source ) override
		{
			const int32 Handle = Next++;
			Models.insert( Handle );
			ModelData[Handle] = ModelData[Source];
			return Handle;
		}
		void DeleteModel( int32 Model ) override
		{
			Models.erase( Model );
			ModelData.erase( Model );
			Attached.erase( Model );
		}
		int32 GetModelAnimCount( int32 Model ) override { return static_cast<int32>( ModelData[Model].Names.size() ); }
		FString GetModelAnimName( int32 Model, int32 AnimIndex ) override { return ModelData[Model].Names[AnimIndex]; }
		float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) override { return ModelData[Model].Lengths[AnimIndex]; }
		int32 AttachModelAnim( int32 Model, int32, int32 ) override
		{
			++NumAttachCalls;
			const int32 Index = NextAttach[Model]++;
			Attached[Model].insert( Index );
			return Index;
		}
		void DetachModelAnim( int32 Model, int32 AttachIndex ) override
		{
			++NumDetachCalls;
			Attached[Model].erase( AttachIndex );
		}
		int32 GetModelMeshCount( int32 Model ) override { return ModelData[Model].MeshCount; }
		bool GetModelCollisionMesh( int32 Model, TArray<float32>& OutPositions, TArray<uint32>& OutIndices ) override
		{
			const FModelData& Data = ModelData[Model];
			if ( Data.MeshPositions.empty() || Data.MeshIndices.empty() || Data.MeshCount <= 0 ) return false;
			OutPositions.Reset();
			OutIndices.Reset();
			for ( const float32 Value : Data.MeshPositions ) OutPositions.Add( Value );
			for ( const uint32 Value : Data.MeshIndices ) OutIndices.Add( Value );
			return true;
		}
		bool GetModelBounds( int32 Model, float32 ( &OutMin )[3], float32 ( &OutMax )[3] ) override
		{
			const FModelData& Data = ModelData[Model];
			if ( !Data.bHasBounds || Data.MeshCount <= 0 ) return false;
			for ( int32 Axis = 0; Axis < 3; ++Axis )
			{
				OutMin[Axis] = Data.BoundsMin[Axis];
				OutMax[Axis] = Data.BoundsMax[Axis];
			}
			return true;
		}
		int32 GetModelFrameCount( int32 Model ) override { return static_cast<int32>( ModelData[Model].FrameNames.size() ); }
		FString GetModelFrameName( int32 Model, int32 FrameIndex ) override { return ModelData[Model].FrameNames[FrameIndex]; }
		bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) override
		{
			if ( bSaveFails ) return false;
			SavedModels.push_back( FSavedModel { Model, Path, Options } );
			OutWarning = SaveWarning;
			// 本物のファイルを作る（上書き・更新日時の判定を確かめられるように）
			FILE* File = nullptr;
			if ( fopen_s( &File, *Path, "wb" ) == 0 && File != nullptr )
			{
				fwrite( "FAKEMV1", 1, 7, File );
				fclose( File );
			}
			return true;
		}

		int32 MakeFont( const FFontDesc& Desc ) override
		{
			const int32 Handle = Next++;
			Fonts.insert( Handle );
			FontsMade.push_back( Desc );
			return Handle;
		}
		void DeleteFont( int32 Font ) override { Fonts.erase( Font ); }
		int32 MeasureText( int32, const char* Text ) override { return static_cast<int32>( std::strlen( Text ) ) * 10; }
		bool AddFontFile( const FString& Path ) override
		{
			FontFilesAdded.push_back( Path );
			return true;
		}
		void RemoveFontFile( const FString& Path ) override { FontFilesRemoved.push_back( Path ); }

		int32 LoadSound( const FString&, ESoundLoadMode, bool bAsync ) override
		{
			++NumSoundLoads;
			const int32 Handle = Next++;
			Sounds.insert( Handle );
			if ( bAsync ) Loading.insert( Handle );
			return Handle;
		}
		int32 DuplicateSound( int32 ) override
		{
			++NumDuplicates;
			const int32 Handle = Next++;
			Sounds.insert( Handle );
			return Handle;
		}
		void DeleteSound( int32 Sound ) override { Sounds.erase( Sound ); }
		float32 GetSoundLength( int32 ) override { return SoundLength; }

		FBackendMemoryInfo GetMemoryInfo() override
		{
			FBackendMemoryInfo Info;
			Info.VideoMemoryTotal = 1000000;
			Info.VideoMemoryUsed = 250000;
			Info.NumTextureHandles = static_cast<int32>( Textures.size() );
			Info.NumModelHandles = static_cast<int32>( Models.size() );
			Info.NumSoundHandles = static_cast<int32>( Sounds.size() );
			Info.NumFontHandles = static_cast<int32>( Fonts.size() );
			return Info;
		}
		int64 EstimateModelBytes( int32 ) override { return 10000; }
		int64 EstimateSoundBytes( int32, ESoundLoadMode Mode ) override { return Mode == ESoundLoadMode::Stream ? 1000 : 40000; }

		// texture待ち状態を返し、ready問い合わせ回数も記録する。
		bool IsLoading( int32 Handle ) override { ++NumLoadingQueries; return Loading.count( Handle ) > 0; }
		int32 GetNumLoading() override { return static_cast<int32>( Loading.size() ); }
	};

	class FRecordingAudioBackend final : public IAudioBackend
	{
	public:
		struct FVoiceState
		{
			bool bPlaying = false;
			bool bLoop = false;
			float32 Volume = -1.0f;
			float32 Pan = 0.0f;
			float32 Pitch = 1.0f;
			float32 Position = 0.0f;
			int32 PlayCalls = 0;
			int32 ResumeCalls = 0;
			int32 StopCalls = 0;
		};
		std::map<int32, FVoiceState> Voices;
		int32 LastStarted = -1;   // 最後に Play されたボイス

		bool Play( int32 Voice, bool bLoop, bool bFromStart ) override
		{
			FVoiceState& State = Voices[Voice];
			LastStarted = Voice;
			State.bPlaying = true;
			State.bLoop = bLoop;
			++State.PlayCalls;
			if ( bFromStart ) State.Position = 0.0f;
			else ++State.ResumeCalls;
			return true;
		}
		void Stop( int32 Voice ) override
		{
			FVoiceState& State = Voices[Voice];
			if ( State.bPlaying ) ++State.StopCalls;
			State.bPlaying = false;
		}
		bool IsPlaying( int32 Voice ) override { return Voices[Voice].bPlaying; }
		void SetVolume( int32 Voice, float32 Volume ) override { Voices[Voice].Volume = Volume; }
		void SetPan( int32 Voice, float32 Pan ) override { Voices[Voice].Pan = Pan; }
		void SetPitch( int32 Voice, float32 Pitch ) override { Voices[Voice].Pitch = Pitch; }
		float32 GetPlayPosition( int32 Voice ) override { return Voices[Voice].Position; }
		void SetPlayPosition( int32 Voice, float32 Seconds ) override { Voices[Voice].Position = Seconds; }

		// 再生が最後まで終わったことにする
		void Finish( int32 Voice ) { Voices[Voice].bPlaying = false; }

		int32 CountPlaying() const
		{
			int32 Count = 0;
			for ( const auto& Entry : Voices )
			{
				if ( Entry.second.bPlaying ) ++Count;
			}
			return Count;
		}
	};

	// 描画コマンドを集めるだけの実行先
	class FCommandLog final : public IDrawExecutor
	{
	public:
		std::vector<uint32> Words;

		void Execute( const FDrawSpan& Span ) override { Words.insert( Words.end(), Span.Words, Span.Words + Span.NumWords ); }
		void Clear() { Words.clear(); }

		template<typename TFunction>
		void ForEach( TFunction&& Function ) const
		{
			ForEachDrawCommand( FDrawSpan { Words.data(), static_cast<int32>( Words.size() ) }, Function );
		}

		int32 Count( EDrawCommand Type ) const
		{
			int32 Count = 0;
			ForEach( [&]( const FDrawCommandView& Command ) { if ( Command.Type == Type ) ++Count; } );
			return Count;
		}
	};

	//=========================================================================
	// ファイルの用意
	//=========================================================================
	inline FString MakeTempDirectory()
	{
		char Path[MAX_PATH + 64] = {};
		GetTempPathA( MAX_PATH, Path );
		char Name[64];
		std::snprintf( Name, sizeof( Name ), "AssetSelfTest_%lu", static_cast<unsigned long>( GetCurrentProcessId() ) );
		strcat_s( Path, Name );
		CreateDirectoryA( Path, nullptr );
		return FString( Path );
	}

	// フォルダを、中のファイルごと消す（確認用の一時フォルダの後片付け）
	inline void RemoveDirectoryRecursive( const FString& Directory )
	{
		WIN32_FIND_DATAA Data;
		HANDLE Handle = FindFirstFileA( *( Directory + "\\*" ), &Data );
		if ( Handle != INVALID_HANDLE_VALUE )
		{
			do
			{
				const FString Name( Data.cFileName );
				if ( Name == "." || Name == ".." ) continue;
				const FString Child = Directory + "\\" + Name;
				if ( ( Data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY ) != 0 ) RemoveDirectoryRecursive( Child );
				else DeleteFileA( *Child );
			}
			while ( FindNextFileA( Handle, &Data ) );
			FindClose( Handle );
		}
		RemoveDirectoryA( *Directory );
	}

	inline bool WriteFileBytes( const FString& Path, const std::vector<uint8>& Bytes )
	{
		FILE* File = nullptr;
		if ( fopen_s( &File, *Path, "wb" ) != 0 || File == nullptr ) return false;
		if ( !Bytes.empty() ) std::fwrite( Bytes.data(), 1, Bytes.size(), File );
		std::fclose( File );
		return true;
	}

	// ファイルの更新日時を、今から SecondsFromNow 秒後にする（出力が新しい・古いの確認用）
	inline bool SetLastWriteOffset( const FString& Path, int64 SecondsFromNow )
	{
		HANDLE Handle = CreateFileA( *Path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr );
		if ( Handle == INVALID_HANDLE_VALUE ) return false;
		FILETIME Now;
		GetSystemTimeAsFileTime( &Now );
		ULARGE_INTEGER Value;
		Value.LowPart = Now.dwLowDateTime;
		Value.HighPart = Now.dwHighDateTime;
		Value.QuadPart += static_cast<ULONGLONG>( SecondsFromNow * 10000000LL );
		FILETIME Time;
		Time.dwLowDateTime = Value.LowPart;
		Time.dwHighDateTime = Value.HighPart;
		const BOOL bOk = SetFileTime( Handle, nullptr, nullptr, &Time );
		CloseHandle( Handle );
		return bOk != FALSE;
	}

	inline bool WriteFileText( const FString& Path, const char* Text )
	{
		return WriteFileBytes( Path, std::vector<uint8>( reinterpret_cast<const uint8*>( Text ), reinterpret_cast<const uint8*>( Text ) + std::strlen( Text ) ) );
	}

	inline void PutU16BE( std::vector<uint8>& Bytes, uint32 Value )
	{
		Bytes.push_back( static_cast<uint8>( Value >> 8 ) );
		Bytes.push_back( static_cast<uint8>( Value ) );
	}
	inline void PutU32BE( std::vector<uint8>& Bytes, uint32 Value )
	{
		PutU16BE( Bytes, Value >> 16 );
		PutU16BE( Bytes, Value & 0xFFFFu );
	}
	inline void PutU16LE( std::vector<uint8>& Bytes, uint32 Value )
	{
		Bytes.push_back( static_cast<uint8>( Value ) );
		Bytes.push_back( static_cast<uint8>( Value >> 8 ) );
	}
	inline void PutU32LE( std::vector<uint8>& Bytes, uint32 Value )
	{
		PutU16LE( Bytes, Value & 0xFFFFu );
		PutU16LE( Bytes, Value >> 16 );
	}

	// 24bit の BMP。Pixels は上から下、左から右の (R, G, B)
	inline std::vector<uint8> MakeBmp( int32 Width, int32 Height, const std::vector<uint32>& Rgb )
	{
		const int32 RowBytes = ( Width * 3 + 3 ) & ~3;
		const uint32 ImageBytes = static_cast<uint32>( RowBytes * Height );
		std::vector<uint8> Bytes;
		Bytes.push_back( 'B' );
		Bytes.push_back( 'M' );
		PutU32LE( Bytes, 54 + ImageBytes );
		PutU32LE( Bytes, 0 );
		PutU32LE( Bytes, 54 );
		PutU32LE( Bytes, 40 );
		PutU32LE( Bytes, static_cast<uint32>( Width ) );
		PutU32LE( Bytes, static_cast<uint32>( Height ) );
		PutU16LE( Bytes, 1 );
		PutU16LE( Bytes, 24 );
		PutU32LE( Bytes, 0 );
		PutU32LE( Bytes, ImageBytes );
		PutU32LE( Bytes, 2835 );
		PutU32LE( Bytes, 2835 );
		PutU32LE( Bytes, 0 );
		PutU32LE( Bytes, 0 );
		for ( int32 Row = Height - 1; Row >= 0; --Row )   // BMP は下の行から
		{
			int32 Written = 0;
			for ( int32 Column = 0; Column < Width; ++Column )
			{
				const uint32 Color = Rgb[static_cast<size_t>( Row * Width + Column )];
				Bytes.push_back( static_cast<uint8>( Color & 0xFF ) );          // B
				Bytes.push_back( static_cast<uint8>( ( Color >> 8 ) & 0xFF ) );  // G
				Bytes.push_back( static_cast<uint8>( ( Color >> 16 ) & 0xFF ) ); // R
				Written += 3;
			}
			while ( Written < RowBytes ) { Bytes.push_back( 0 ); ++Written; }
		}
		return Bytes;
	}

	constexpr uint32 Rgb( uint32 R, uint32 G, uint32 B ) { return ( R << 16 ) | ( G << 8 ) | B; }

	// 4 x 2 コマ（1 コマ 16 x 16、余白 3、隙間 2）のスプライトシートの画像。コマごとに違う色。余白と隙間は紫
	constexpr int32 SheetCell = 16;
	constexpr int32 SheetMargin = 3;
	constexpr int32 SheetSpacing = 2;
	constexpr int32 SheetColumns = 4;
	constexpr int32 SheetRows = 2;
	constexpr int32 SheetWidth = 2 * SheetMargin + SheetColumns * SheetCell + ( SheetColumns - 1 ) * SheetSpacing;   // 76
	constexpr int32 SheetHeight = 2 * SheetMargin + SheetRows * SheetCell + ( SheetRows - 1 ) * SheetSpacing;        // 40

	inline uint32 SheetCellColor( int32 Column, int32 Row )
	{
		return Rgb( static_cast<uint32>( 40 + Column * 50 ), static_cast<uint32>( 40 + Row * 100 ), static_cast<uint32>( 200 - ( Row * SheetColumns + Column ) * 20 ) );
	}

	inline std::vector<uint8> MakeSheetBmp()
	{
		std::vector<uint32> Pixels( static_cast<size_t>( SheetWidth * SheetHeight ), Rgb( 255, 0, 255 ) );
		for ( int32 Row = 0; Row < SheetRows; ++Row )
		{
			for ( int32 Column = 0; Column < SheetColumns; ++Column )
			{
				const int32 X0 = SheetMargin + Column * ( SheetCell + SheetSpacing );
				const int32 Y0 = SheetMargin + Row * ( SheetCell + SheetSpacing );
				for ( int32 Y = 0; Y < SheetCell; ++Y )
				{
					for ( int32 X = 0; X < SheetCell; ++X ) Pixels[static_cast<size_t>( ( Y0 + Y ) * SheetWidth + X0 + X )] = SheetCellColor( Column, Row );
				}
			}
		}
		return MakeBmp( SheetWidth, SheetHeight, Pixels );
	}

	// 16bit モノラルの正弦波の WAV
	inline std::vector<uint8> MakeWav( float32 Seconds, int32 SampleRate )
	{
		const int32 NumSamples = static_cast<int32>( Seconds * static_cast<float32>( SampleRate ) );
		std::vector<uint8> Bytes;
		const char* Riff = "RIFF";
		Bytes.insert( Bytes.end(), Riff, Riff + 4 );
		PutU32LE( Bytes, static_cast<uint32>( 36 + NumSamples * 2 ) );
		const char* Wave = "WAVEfmt ";
		Bytes.insert( Bytes.end(), Wave, Wave + 8 );
		PutU32LE( Bytes, 16 );
		PutU16LE( Bytes, 1 );
		PutU16LE( Bytes, 1 );
		PutU32LE( Bytes, static_cast<uint32>( SampleRate ) );
		PutU32LE( Bytes, static_cast<uint32>( SampleRate * 2 ) );
		PutU16LE( Bytes, 2 );
		PutU16LE( Bytes, 16 );
		const char* Data = "data";
		Bytes.insert( Bytes.end(), Data, Data + 4 );
		PutU32LE( Bytes, static_cast<uint32>( NumSamples * 2 ) );
		for ( int32 i = 0; i < NumSamples; ++i )
		{
			const float32 Sample = std::sin( 6.2831853f * 440.0f * static_cast<float32>( i ) / static_cast<float32>( SampleRate ) ) * 0.3f;
			PutU16LE( Bytes, static_cast<uint32>( static_cast<int32>( Sample * 32767.0f ) & 0xFFFF ) );
		}
		return Bytes;
	}

	// name テーブルだけを持つ最小の TrueType フォント（フォント名の読み取りの確認用）
	inline std::vector<uint8> MakeFakeFont( const wchar_t* English, const wchar_t* Japanese )
	{
		struct FName { uint32 Language; const wchar_t* Text; };
		std::vector<FName> Names;
		if ( Japanese != nullptr ) Names.push_back( { 0x0411, Japanese } );
		if ( English != nullptr ) Names.push_back( { 0x0409, English } );

		std::vector<uint8> Strings;
		std::vector<uint8> Records;
		for ( const FName& Name : Names )
		{
			const uint32 Offset = static_cast<uint32>( Strings.size() );
			uint32 Length = 0;
			for ( const wchar_t* p = Name.Text; *p != L'\0'; ++p, Length += 2 ) PutU16BE( Strings, static_cast<uint32>( *p ) );
			PutU16BE( Records, 3 );          // platform: Windows
			PutU16BE( Records, 1 );          // encoding: Unicode BMP
			PutU16BE( Records, Name.Language );
			PutU16BE( Records, 1 );          // nameID: ファミリー名
			PutU16BE( Records, Length );
			PutU16BE( Records, Offset );
		}

		std::vector<uint8> NameTable;
		PutU16BE( NameTable, 0 );
		PutU16BE( NameTable, static_cast<uint32>( Names.size() ) );
		PutU16BE( NameTable, static_cast<uint32>( 6 + Records.size() ) );
		NameTable.insert( NameTable.end(), Records.begin(), Records.end() );
		NameTable.insert( NameTable.end(), Strings.begin(), Strings.end() );

		std::vector<uint8> Font;
		PutU32BE( Font, 0x00010000 );
		PutU16BE( Font, 1 );
		PutU16BE( Font, 16 );
		PutU16BE( Font, 0 );
		PutU16BE( Font, 0 );
		const char* Tag = "name";
		Font.insert( Font.end(), Tag, Tag + 4 );
		PutU32BE( Font, 0 );
		PutU32BE( Font, 28 );
		PutU32BE( Font, static_cast<uint32>( NameTable.size() ) );
		Font.insert( Font.end(), NameTable.begin(), NameTable.end() );
		return Font;
	}

	// 純粋な計算と、偽のバックエンドでのアセット・アニメーション・オーディオの確認（AssetSelfTestLogic.cpp）
	void RunLogicTests( FChecker& C );
	// デバッグ用のコンソール（構文解析・登録・補完・履歴・組み込みのコマンド）の確認（ConsoleSelfTest.cpp）
	void RunConsoleLogicTests( FChecker& C );
	// 動いているシーン（Scene）に対するコンソールのコマンド（scene / objects / object / teleport / ecs ...）の確認
	void RunConsoleSceneChecks( FChecker& C, ::CScene& Scene );
	// モデルの取り込み・.mv1 への出力・アニメーションだけのファイルの確認（偽のバックエンドで。Dir は、マネージャーのルートにしたフォルダ）
	void RunImportLogicTests( FChecker& C, ::CAssetManager& Assets, FFakeAssetBackend& Fake, const FString& Dir );
}

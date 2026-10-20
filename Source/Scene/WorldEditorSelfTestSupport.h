#pragma once
#include <cmath>

#include "Library/Framework/World/Editor/EditorCamera.h"
#include "Library/Framework/World/Editor/EditorOutliner.h"
#include "Library/Framework/World/Editor/EditorShapes.h"
#include "WorldSelfTestSupport.h"

//=============================================================================
// ワールドエディタの純粋な部分の確認（WorldEditorSelfTest*.cpp）が共有する道具
//=============================================================================
namespace NWorldEditorTest
{
	// 読み取りの窓口: 見つからない・範囲外でも、落ちず（Debug の assert で止まらず）、空の値を返して、確認が失敗するだけにする。
	//   壊したとき（変異テストなど）に、確認の途中でテスト全体が止まらないように。
	inline const FWorldObject& ObjectOrEmpty( const FWorldDefinition& World, int32 Id )
	{
		static const FWorldObject Empty {};
		const FWorldObject* Found = World.FindObject( Id );
		return Found != nullptr ? *Found : Empty;
	}

	inline int32 IdAtOrNone( const TArray<int32>& Ids, int32 Index )
	{
		return Ids.IsValidIndex( Index ) ? Ids[Index] : WorldNoId;
	}

	inline const FEditorOutlinerRow& RowAt( const TArray<FEditorOutlinerRow>& Rows, int32 Index )
	{
		static const FEditorOutlinerRow Empty {};
		return Rows.IsValidIndex( Index ) ? Rows[Index] : Empty;
	}

	inline bool Near3( const FVector3& A, const FVector3& B, float32 Tolerance = 1.0e-3f )
	{
		return std::fabs( A.x - B.x ) <= Tolerance && std::fabs( A.y - B.y ) <= Tolerance && std::fabs( A.z - B.z ) <= Tolerance;
	}

	inline FEditorViewport MakeViewport( int32 Width = 800, int32 Height = 600 )
	{
		FEditorViewport View;
		View.Width = Width;
		View.Height = Height;
		return View;
	}

	inline FEditorCamera MakeCamera( float32 Yaw, float32 Pitch, float32 Distance, const FVector3& Target = FVector3::Zero )
	{
		FEditorCamera Camera;
		Camera.Yaw = Yaw;
		Camera.Pitch = Pitch;
		Camera.Distance = Distance;
		Camera.Target = Target;
		return Camera;
	}

	// ワールドの点を通る、視点からの光線（その点に画面の上でクリックしたときの光線）
	inline FEditorRay RayThrough( const FEditorCamera& Camera, const FEditorViewport& View, const FVector3& Point )
	{
		FVector2 Pixel;
		Camera.WorldToScreen( View, Point, Pixel );
		return Camera.ScreenToRay( View, Pixel.x, Pixel.y );
	}

	// アセットの偽物（大きさと有無を、登録したものだけ答える）
	class FFakeEditorAssets final : public IEditorAssetInfo
	{
	public:
		struct FModel
		{
			FString Path;
			FVector3 Min, Max;
		};
		struct FTexture
		{
			FString Path;
			int32 Width, Height;
		};
		struct FMesh
		{
			FString Path;
			FEditorCollisionMesh Mesh;
		};
		struct FWorldEntry
		{
			FString Path;
			FWorldDefinition World;
		};
		TArray<FModel> Models;
		TArray<FTexture> Textures;
		TArray<FMesh> Meshes;   // GetCollisionMesh が答える三角形
		TArray<FWorldEntry> Worlds;   // GetWorld が答える、入れ子のワールドの中身
		TArray<FString> Files;

		bool Exists( const FString& Path ) const override { return Files.Contains( Path ); }
		bool GetModelBounds( const FString& Path, FVector3& OutMin, FVector3& OutMax ) const override
		{
			for ( const FModel& Model : Models )
			{
				if ( Model.Path == Path )
				{
					OutMin = Model.Min;
					OutMax = Model.Max;
					return true;
				}
			}
			return false;
		}
		const FEditorCollisionMesh* GetCollisionMesh( const FString& Path ) const override
		{
			for ( const FMesh& Entry : Meshes )
			{
				if ( Entry.Path == Path ) return &Entry.Mesh;
			}
			return nullptr;
		}
		const FWorldDefinition* GetWorld( const FString& Path ) const override
		{
			for ( const FWorldEntry& Entry : Worlds )
			{
				if ( Entry.Path == Path ) return &Entry.World;
			}
			return nullptr;
		}
		bool GetTextureSize( const FString& Path, int32& OutWidth, int32& OutHeight ) const override
		{
			for ( const FTexture& Texture : Textures )
			{
				if ( Texture.Path == Path )
				{
					OutWidth = Texture.Width;
					OutHeight = Texture.Height;
					return true;
				}
			}
			return false;
		}
	};
}

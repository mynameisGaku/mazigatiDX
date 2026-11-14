// SPDX-License-Identifier: MIT
#include "GeometryQuadBuilder.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	// 基底と生成座標の有限性を調べる。
	bool Finite( const FVector3& Value )
	{
		return NMath::IsFinite( Value.x ) && NMath::IsFinite( Value.y ) && NMath::IsFinite( Value.z );
	}
	// 四隅へ位置・UV・色と表の法線を与える。
	FDrawVertex3D MakeVertex( const FVector3& Position, const FVector3& Normal, float32 U, float32 V, FDrawRgba Color )
	{
		FDrawVertex3D Result; // 追加画像と鏡面色は使わない。
		Result.Position[0] = Position.x;
		Result.Position[1] = Position.y;
		Result.Position[2] = Position.z;
		Result.Normal[0] = Normal.x;
		Result.Normal[1] = Normal.y;
		Result.Normal[2] = Normal.z;
		Result.U = U;
		Result.V = V;
		Result.Diffuse[0] = Color.R;
		Result.Diffuse[1] = Color.G;
		Result.Diffuse[2] = Color.B;
		Result.Diffuse[3] = Color.A;
		return Result;
	}
	// 左上・右上・右下・左下を、表が見ている人を向く順に結ぶ。
	void Triangulate( const FDrawVertex3D ( &Vertices )[4], TArray<FDrawVertex3D>& Out )
	{
		const int32 Order[] = { 0, 1, 3, 1, 2, 3 }; // 二枚の三角形。
		for ( int32 Index : Order )
		{
			Out.Add( Vertices[Index] );
		}
	}
}

bool CGeometryQuadBuilder::Build( const FDrawQuad3DData& Data, const FCamera& Camera, TArray<FDrawVertex3D>& Out )
{
	const FVector3 Position( Data.Pos[0], Data.Pos[1], Data.Pos[2] ); // 基準点の位置。
	const float32 Values[] = { Data.Width, Data.Height, Data.PivotX, Data.PivotY, Data.Angle, Data.U0, Data.V0, Data.U1, Data.V1 }; // 向きに関係なく使う入力。
	if ( !Finite( Position ) || Data.Orient < 0 || Data.Orient > static_cast<int32>( EDrawQuadOrient::Fixed ) )
	{
		return false;
	}
	for ( float32 Value : Values )
	{
		if ( !NMath::IsFinite( Value ) )
		{
			return false;
		}
	}
	FVector3 Right, Up; // 入力またはカメラから決める基底。
	if ( Data.Orient == static_cast<int32>( EDrawQuadOrient::Fixed ) )
	{
		Right = FVector3( Data.Right[0], Data.Right[1], Data.Right[2] );
		Up = FVector3( Data.Up[0], Data.Up[1], Data.Up[2] );
	}
	else
	{
		if ( !Camera.IsValid() )
		{
			return false;
		}
		if ( Data.Orient == static_cast<int32>( EDrawQuadOrient::CameraY ) )
		{
			const FVector3 Forward( Position.x - Camera.Position.x, 0, Position.z - Camera.Position.z ); // 水平の視線。
			Up = FVector3::Up;
			if ( Forward.LengthSquared() > NMath::SmallNumber )
			{
				Right = FVector3::Cross( Up, Forward.Normalized() );
			}
			else
			{
				const FVector3 CameraRight = Camera.GetRight(); // 真上・真下では横向きを水平へ投影する。
				Right = FVector3( CameraRight.x, 0, CameraRight.z ).Normalized();
				if ( Right.LengthSquared() <= NMath::SmallNumber )
				{
					Right = FVector3::Right;
				}
			}
		}
		else
		{
			const FVector3 CameraRight = Camera.GetRight(), CameraUp = Camera.GetUpVector(); // 画面の横と縦。
			const float32 Cos = NMath::Cos( Data.Angle ), Sin = NMath::Sin( Data.Angle ); // 正は右向きが上へ傾く回転。
			Right = CameraRight * Cos + CameraUp * Sin;
			Up = CameraUp * Cos - CameraRight * Sin;
		}
	}
	const FVector3 Cross = FVector3::Cross( Right, Up ); // 表はこの向きの逆。
	if ( !Finite( Right ) || !Finite( Up ) || !Finite( Cross ) || !NMath::IsFinite( Cross.LengthSquared() ) || Cross.LengthSquared() <= NMath::SmallNumber )
	{
		return false;
	}
	TArray<FDrawVertex3D> Output; // 失敗時の出力保持用。
	if ( Data.Width != 0 && Data.Height != 0 )
	{
		const float32 X[] = { -Data.PivotX * Data.Width, ( 1 - Data.PivotX ) * Data.Width }; // 左と右。
		const float32 Y[] = { Data.PivotY * Data.Height, -( 1 - Data.PivotY ) * Data.Height }; // 上と下。
		FDrawVertex3D Vertices[4]; // 画像の左上から時計回り。
		for ( int32 Index = 0; Index < 4; ++Index )
		{
			const bool bRight = Index == 1 || Index == 2, bBottom = Index >= 2; // 四隅の選択。
			const FVector3 Point = Position + Right * X[bRight] + Up * Y[bBottom]; // 向きと基準点を適用する。
			if ( !Finite( Point ) )
			{
				return false;
			}
			Vertices[Index] = MakeVertex( Point, -Cross.Normalized(), bRight ? Data.U1 : Data.U0, bBottom ? Data.V1 : Data.V0, Data.Color );
		}
		Triangulate( Vertices, Output );
	}
	Out = MoveTemp( Output );
	return true;
}

bool CGeometryQuadBuilder::BuildCorners( const FVector3& Position, const float32 ( &Corners )[8], const FCamera& Camera, TArray<FDrawVertex3D>& Out )
{
	if ( !Finite( Position ) || !Camera.IsValid() )
	{
		return false;
	}
	const FVector3 Right = Camera.GetRight(), Up = Camera.GetUpVector(); // 画面に平行な面。
	FDrawVertex3D Vertices[4]; // 左上から時計回り。
	for ( int32 Index = 0; Index < 4; ++Index )
	{
		if ( !NMath::IsFinite( Corners[Index * 2] ) || !NMath::IsFinite( Corners[Index * 2 + 1] ) )
		{
			return false;
		}
		const FVector3 Point = Position + Right * Corners[Index * 2] + Up * Corners[Index * 2 + 1]; // 自由にずらした角。
		if ( !Finite( Point ) )
		{
			return false;
		}
		Vertices[Index] = MakeVertex( Point, -Camera.GetForward(), Index == 1 || Index == 2 ? 1.0f : 0.0f, Index >= 2 ? 1.0f : 0.0f, FDrawRgba() );
	}
	TArray<FDrawVertex3D> Output; // つぶれた三角形を除いてから出力する。
	const int32 Triangles[2][3] = { { 0, 1, 3 }, { 1, 2, 3 } }; // 既存の四角形と同じ対角線。
	for ( const auto& Triangle : Triangles )
	{
		const FVector3 A( Corners[Triangle[1] * 2] - Corners[Triangle[0] * 2], Corners[Triangle[1] * 2 + 1] - Corners[Triangle[0] * 2 + 1], 0 ); // 一辺。
		const FVector3 B( Corners[Triangle[2] * 2] - Corners[Triangle[0] * 2], Corners[Triangle[2] * 2 + 1] - Corners[Triangle[0] * 2 + 1], 0 ); // もう一辺。
		const float32 Area = A.x * B.y - A.y * B.x; // 符号付き面積。
		if ( !NMath::IsFinite( Area ) )
		{
			return false;
		}
		if ( Area != 0 )
		{
			for ( int32 Index : Triangle )
			{
				Output.Add( Vertices[Index] );
			}
		}
	}
	Out = MoveTemp( Output );
	return true;
}

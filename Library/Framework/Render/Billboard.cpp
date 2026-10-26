#include "Billboard.h"

#include <cmath>

#include "Library/Core/Types/Numeric.h"
#include "Library/Framework/Ecs/SystemParams.h"

bool FBillboard::SetRect( int32 X, int32 Y, int32 Width, int32 Height )
{
	if ( !Texture.IsValid() ) return false;
	const int32 TextureWidth = Texture->GetWidth();
	const int32 TextureHeight = Texture->GetHeight();
	if ( TextureWidth <= 0 || TextureHeight <= 0 || Width <= 0 || Height <= 0 ) return false;
	U0 = static_cast<float32>( X ) / static_cast<float32>( TextureWidth );
	V0 = static_cast<float32>( Y ) / static_cast<float32>( TextureHeight );
	U1 = static_cast<float32>( X + Width ) / static_cast<float32>( TextureWidth );
	V1 = static_cast<float32>( Y + Height ) / static_cast<float32>( TextureHeight );
	return true;
}

void FBillboard::ResetRect()
{
	U0 = 0.0f;
	V0 = 0.0f;
	U1 = 1.0f;
	V1 = 1.0f;
}

void FBillboard::Record( FDrawContext& Draw, const FTransform& World ) const
{
	if ( !bVisible || !Texture.IsValid() || Texture->GetHandle() < 0 ) return;

	FDrawQuad3DData Data;
	Data.Pos[0] = World.Position.x;
	Data.Pos[1] = World.Position.y;
	Data.Pos[2] = World.Position.z;
	// 拡大は、大きさに掛ける（負の拡大で裏返しにはしない。反転は bFlipX / bFlipY で）
	Data.Width = Size.x * NMath::Abs( World.Scale.x );
	Data.Height = Size.y * NMath::Abs( World.Scale.y );
	Data.PivotX = Pivot.x;
	Data.PivotY = Pivot.y;
	Data.Orient = static_cast<int32>( Orient );
	Data.Handle = Texture->GetHandle();
	Data.Color.R = Tint.r;
	Data.Color.G = Tint.g;
	Data.Color.B = Tint.b;
	Data.Color.A = Tint.a;

	// 反転は、使う範囲の左右・上下を入れ替える
	Data.U0 = bFlipX ? U1 : U0;
	Data.U1 = bFlipX ? U0 : U1;
	Data.V0 = bFlipY ? V1 : V0;
	Data.V1 = bFlipY ? V0 : V1;

	if ( Orient == EDrawQuadOrient::Fixed )
	{
		const FVector3 Right = World.Rotation.Rotate( FVector3::Right );
		const FVector3 Up = World.Rotation.Rotate( FVector3::Up );
		Data.Right[0] = Right.x;
		Data.Right[1] = Right.y;
		Data.Right[2] = Right.z;
		Data.Up[0] = Up.x;
		Data.Up[1] = Up.y;
		Data.Up[2] = Up.z;
	}
	else if ( Orient == EDrawQuadOrient::Camera )
	{
		// オブジェクトの Roll（Z まわり）を、視線まわりの回転として使う
		Data.Angle = World.Rotation.ToEuler().z;
	}
	Draw.Quad3D( Data );
}

void FBillboard::RecordAt( FDrawContext& Draw, const FTransformView& Transform ) const
{
	Record( Draw, Transform.GetWorld() );
}

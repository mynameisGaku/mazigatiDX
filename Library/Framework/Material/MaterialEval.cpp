// SPDX-License-Identifier: MIT
#include "MaterialEval.h"

#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"

namespace
{
	float32 Frac( float32 Value )
	{
		return Value - NMath::Floor( Value );
	}

	// HLSL の MatHash と同じ
	float32 Hash( float32 X, float32 Y )
	{
		float32 PX = Frac( X * 123.34f );
		float32 PY = Frac( Y * 456.21f );
		const float32 D = PX * ( PX + 45.32f ) + PY * ( PY + 45.32f );
		PX += D;
		PY += D;
		return Frac( PX * PY );
	}

	// HLSL の3次元格子値と同じ値を返す。
	float32 Hash3D( float32 X, float32 Y, float32 Z )
	{
		float32 PX = Frac( X * 0.1031f ); // 格子座標を周期ごとに混ぜるX成分。
		float32 PY = Frac( Y * 0.1030f ); // 格子座標を周期ごとに混ぜるY成分。
		float32 PZ = Frac( Z * 0.0973f ); // 格子座標を周期ごとに混ぜるZ成分。
		const float32 D = PX * ( PY + 33.33f ) + PY * ( PX + 33.33f ) + PZ * ( PZ + 33.33f ); // 3成分を混ぜる値。
		PX += D; PY += D; PZ += D;
		return Frac( ( PX + PY ) * PZ );
	}

	// 値とXYZ方向の勾配を一度に計算する。
	FVector4 Noise3DGradient( const FVector4& Position )
	{
		const float32 X = NMath::Floor( Position.x ), Y = NMath::Floor( Position.y ), Z = NMath::Floor( Position.z ); // 格子の原点。
		const float32 FX = Position.x - X, FY = Position.y - Y, FZ = Position.z - Z; // 格子内の位置。
		const float32 U = FX * FX * ( 3.0f - 2.0f * FX ), V = FY * FY * ( 3.0f - 2.0f * FY ), W = FZ * FZ * ( 3.0f - 2.0f * FZ ); // 境界で勾配が連続する補間率。
		const auto Lerp = []( float32 A, float32 B, float32 T ) { return A + ( B - A ) * T; };
		const float32 A = Hash3D( X, Y, Z ), B = Hash3D( X + 1, Y, Z ), C = Hash3D( X, Y + 1, Z ), D = Hash3D( X + 1, Y + 1, Z ); // 下側4頂点の値。
		const float32 E = Hash3D( X, Y, Z + 1 ), G = Hash3D( X + 1, Y, Z + 1 ), H = Hash3D( X, Y + 1, Z + 1 ), J = Hash3D( X + 1, Y + 1, Z + 1 ); // 上側4頂点の値。
		const float32 Z0 = Lerp( Lerp( A, B, U ), Lerp( C, D, U ), V ), Z1 = Lerp( Lerp( E, G, U ), Lerp( H, J, U ), V ); // 上下面の補間値。
		const float32 DX = Lerp( Lerp( B - A, D - C, V ), Lerp( G - E, J - H, V ), W ) * 6.0f * FX * ( 1.0f - FX ); // X方向の勾配。
		const float32 DY = Lerp( Lerp( C - A, D - B, U ), Lerp( H - E, J - G, U ), W ) * 6.0f * FY * ( 1.0f - FY ); // Y方向の勾配。
		return FVector4( Lerp( Z0, Z1, W ), DX, DY, ( Z1 - Z0 ) * 6.0f * FZ * ( 1.0f - FZ ) );
	}

	// 非一様な拡大や鏡映を含む変換で局所法線をワールド法線にする。
	FVector3 TransformLocalNormal( const FVector3& LocalNormal, const FMaterialEvalContext& Context )
	{
		const FMatrix4x4& M = Context.ObjectToWorld; // 個体の配置。
		const FVector3 X( M.m[0][0], M.m[0][1], M.m[0][2] ), Y( M.m[1][0], M.m[1][1], M.m[1][2] ), Z( M.m[2][0], M.m[2][1], M.m[2][2] ); // 変換後の局所軸。
		const FVector3 CX = FVector3::Cross( Y, Z ), CY = FVector3::Cross( Z, X ), CZ = FVector3::Cross( X, Y ); // 逆転置の分子。
		const float32 Determinant = FVector3::Dot( X, CX ); // 特異変換と鏡映を判定する値。
		const FVector3 Transformed = ( CX * LocalNormal.x + CY * LocalNormal.y + CZ * LocalNormal.z ) * ( Determinant < 0.0f ? -1.0f : 1.0f ); // 鏡映も考慮した法線。
		const FVector3 Fallback = Context.WorldNormal.LengthSquared() > 1.0e-12f ? Context.WorldNormal.Normalized() : FVector3( 0.0f, 1.0f, 0.0f ); // 特異変換時の代替法線。
		return NMath::Abs( Determinant ) > 1.0e-8f && Transformed.LengthSquared() > 1.0e-12f ? Transformed.Normalized() : Fallback;
	}

	float32 Component( const FVector4& Value, int32 Index )
	{
		switch ( Index )
		{
		case 0:
			return Value.x;
		case 1:
			return Value.y;
		case 2:
			return Value.z;
		default:
			return Value.w;
		}
	}

	void SetComponent( FVector4& Value, int32 Index, float32 Component )
	{
		switch ( Index )
		{
		case 0:
			Value.x = Component;
			break;
		case 1:
			Value.y = Component;
			break;
		case 2:
			Value.z = Component;
			break;
		default:
			Value.w = Component;
			break;
		}
	}

	float32 Clamp01( float32 Value )
	{
		return Value < 0.0f ? 0.0f : ( Value > 1.0f ? 1.0f : Value );
	}
}

float32 NMaterialEval::Noise( float32 X, float32 Y )
{
	const float32 IX = NMath::Floor( X );
	const float32 IY = NMath::Floor( Y );
	const float32 FX = X - IX;
	const float32 FY = Y - IY;
	const float32 A = Hash( IX, IY );
	const float32 B = Hash( IX + 1.0f, IY );
	const float32 C = Hash( IX, IY + 1.0f );
	const float32 D = Hash( IX + 1.0f, IY + 1.0f );
	const float32 UX = FX * FX * ( 3.0f - 2.0f * FX );
	const float32 UY = FY * FY * ( 3.0f - 2.0f * FY );
	const float32 Bottom = A + ( B - A ) * UX;
	const float32 Top = C + ( D - C ) * UX;
	return Bottom + ( Top - Bottom ) * UY;
}

FMaterialEvalContext NMaterialEval::MakeContext( const FMaterialProgram& Program )
{
	FMaterialEvalContext Context;
	for ( int32 Index = 0; Index < Program.Params.Num(); ++Index )
	{
		Context.ParamValues.Add( Program.Params[Index].Default );
	}
	return Context;
}

bool NMaterialEval::Evaluate( const FMaterialProgram& Program, const FMaterialEvalContext& Context, TArray<FVector4>& OutValues )
{
	OutValues.Empty();
	if ( !Program.bValid || Program.bCustomHlsl )
	{
		return false;
	}
	for ( int32 Index = 0; Index < Program.Code.Num(); ++Index )
	{
		const FMaterialInstruction& Instruction = Program.Code[Index];
		const int32 N = NMath::Max( 1, NMaterial::NumComponents( Instruction.Type ) );
		FVector4 Result( 0.0f, 0.0f, 0.0f, 0.0f );
		auto Value = [&]( int32 Operand ) -> const FVector4& { return OutValues[Operand]; };
		auto Num = [&]( int32 Operand ) { return NMath::Max( 1, NMaterial::NumComponents( Program.Code[Operand].Type ) ); };

		switch ( Instruction.Op )
		{
		case EMaterialOp::Const:
			Result = Instruction.Constant;
			break;
		case EMaterialOp::Param:
		{
			const FMaterialParamSlot& Slot = Program.Params[Instruction.Slot];
			Result = Slot.Index < Context.ParamValues.Num() ? Context.ParamValues[Slot.Index] : Slot.Default;
			break;
		}
		case EMaterialOp::Input:
			switch ( static_cast<EMaterialInput>( Instruction.Sub ) )
			{
			case EMaterialInput::Uv:
				Result = FVector4( Context.Uv.x, Context.Uv.y, 0.0f, 0.0f );
				break;
			case EMaterialInput::Time:
				Result.x = Context.Time;
				break;
			case EMaterialInput::Size:
				Result = FVector4( Context.Size.x, Context.Size.y, 0.0f, 0.0f );
				break;
			case EMaterialInput::Hover:
				Result.x = Context.Hover;
				break;
			case EMaterialInput::Press:
				Result.x = Context.Press;
				break;
			case EMaterialInput::Value:
				Result.x = Context.Value;
				break;
			case EMaterialInput::Enabled:
				Result.x = Context.Enabled;
				break;
			case EMaterialInput::VertexColor:
				Result = Context.VertexColor;
				break;
			case EMaterialInput::WorldPosition:
				Result = FVector4( Context.WorldPosition.x, Context.WorldPosition.y, Context.WorldPosition.z, 0.0f );
				break;
			case EMaterialInput::LocalPosition:
				Result = FVector4( Context.LocalPosition.x, Context.LocalPosition.y, Context.LocalPosition.z, 0.0f );
				break;
			case EMaterialInput::LocalNormal:
			{
				const FVector3 Normal = Context.LocalNormal.LengthSquared() > 1.0e-12f ? Context.LocalNormal.Normalized() : FVector3( 0.0f, 1.0f, 0.0f ); // 有効な配置前の単位法線。
				Result = FVector4( Normal.x, Normal.y, Normal.z, 0.0f );
				break;
			}
			case EMaterialInput::WorldNormal:
				Result = FVector4( Context.WorldNormal.x, Context.WorldNormal.y, Context.WorldNormal.z, 0.0f );
				break;
			case EMaterialInput::ParticleLife:
				Result.x = Context.ParticleLife;
				break;
			case EMaterialInput::ParticleAge:
				Result.x = Context.ParticleAge;
				break;
			case EMaterialInput::ParticleRandom:
				Result.x = Context.ParticleRandom;
				break;
			case EMaterialInput::ParticleSpeed:
				Result.x = Context.ParticleSpeed;
				break;
			case EMaterialInput::WorldTangent:
				Result = FVector4( Context.WorldTangent.x, Context.WorldTangent.y, Context.WorldTangent.z, 0.0f );
				break;
			case EMaterialInput::WorldBitangent:
				Result = FVector4( Context.WorldBitangent.x, Context.WorldBitangent.y, Context.WorldBitangent.z, 0.0f );
				break;
			default:
				Result = FVector4( Context.CameraVector.x, Context.CameraVector.y, Context.CameraVector.z, 0.0f );
				break;
			}
			break;
		case EMaterialOp::Unary:
		{
			const FVector4& A = Value( Instruction.A );
			const int32 NA = Num( Instruction.A );
			const EMaterialUnary Operation = static_cast<EMaterialUnary>( Instruction.Sub );
			if ( Operation == EMaterialUnary::Length || Operation == EMaterialUnary::Normalize )
			{
				float32 Sum = 0.0f;
				for ( int32 C = 0; C < NA; ++C )
				{
					Sum += Component( A, C ) * Component( A, C );
				}
				const float32 Length = NMath::Sqrt( Sum );
				if ( Operation == EMaterialUnary::Length )
				{
					Result.x = Length;
				}
				else
				{
					for ( int32 C = 0; C < NA; ++C )
					{
						SetComponent( Result, C, Length > 0.0f ? Component( A, C ) / Length : 0.0f );
					}
				}
				break;
			}
			for ( int32 C = 0; C < N; ++C )
			{
				const float32 X = Component( A, C );
				float32 Out = 0.0f;
				switch ( Operation )
				{
				case EMaterialUnary::Abs:
					Out = NMath::Abs( X );
					break;
				case EMaterialUnary::Saturate:
					Out = Clamp01( X );
					break;
				case EMaterialUnary::OneMinus:
					Out = 1.0f - X;
					break;
				case EMaterialUnary::Negate:
					Out = -X;
					break;
				case EMaterialUnary::Sin:
					Out = NMath::Sin( X );
					break;
				case EMaterialUnary::Cos:
					Out = NMath::Cos( X );
					break;
				case EMaterialUnary::Frac:
					Out = Frac( X );
					break;
				case EMaterialUnary::Floor:
					Out = NMath::Floor( X );
					break;
				case EMaterialUnary::Ceil:
					Out = NMath::Ceil( X );
					break;
				default:
					Out = NMath::Sqrt( X );
					break;
				}
				SetComponent( Result, C, Out );
			}
			break;
		}
		case EMaterialOp::Binary:
		{
			const FVector4& A = Value( Instruction.A );
			const FVector4& B = Value( Instruction.B );
			const int32 NA = Num( Instruction.A );
			const EMaterialBinary Operation = static_cast<EMaterialBinary>( Instruction.Sub );
			if ( Operation == EMaterialBinary::Dot )
			{
				float32 Sum = 0.0f;
				for ( int32 C = 0; C < NA; ++C )
				{
					Sum += Component( A, C ) * Component( B, C );
				}
				Result.x = Sum;
				break;
			}
			if ( Operation == EMaterialBinary::Distance )
			{
				float32 Sum = 0.0f;
				for ( int32 C = 0; C < NA; ++C )
				{
					const float32 Difference = Component( A, C ) - Component( B, C );
					Sum += Difference * Difference;
				}
				Result.x = NMath::Sqrt( Sum );
				break;
			}
			for ( int32 C = 0; C < N; ++C )
			{
				const float32 X = Component( A, C );
				const float32 Y = Component( B, C );
				float32 Out = 0.0f;
				switch ( Operation )
				{
				case EMaterialBinary::Add:
					Out = X + Y;
					break;
				case EMaterialBinary::Subtract:
					Out = X - Y;
					break;
				case EMaterialBinary::Multiply:
					Out = X * Y;
					break;
				case EMaterialBinary::Divide:
					Out = X / Y;
					break;
				case EMaterialBinary::Min:
					Out = X < Y ? X : Y;
					break;
				case EMaterialBinary::Max:
					Out = X > Y ? X : Y;
					break;
				case EMaterialBinary::Power:
					Out = NMath::Pow( X, Y );
					break;
				case EMaterialBinary::Mod:
					Out = X - Y * NMath::Floor( X / Y );
					break;
				default:
					Out = Y >= X ? 1.0f : 0.0f;   // Step: Edge = A、Value = B
					break;
				}
				SetComponent( Result, C, Out );
			}
			break;
		}
		case EMaterialOp::Ternary:
		{
			const FVector4& A = Value( Instruction.A );
			const FVector4& B = Value( Instruction.B );
			const FVector4& C3 = Value( Instruction.C );
			const EMaterialTernary Operation = static_cast<EMaterialTernary>( Instruction.Sub );
			for ( int32 C = 0; C < N; ++C )
			{
				const float32 X = Component( A, C );
				const float32 Y = Component( B, C );
				const float32 Z = Component( C3, C );
				float32 Out = 0.0f;
				switch ( Operation )
				{
				case EMaterialTernary::Lerp:
					Out = X + ( Y - X ) * Z;
					break;
				case EMaterialTernary::Clamp:
					Out = X < Y ? Y : ( X > Z ? Z : X );
					break;
				default:
				{
					// SmoothStep: A = Min、B = Max、C = Value
					const float32 T = Y != X ? Clamp01( ( Z - X ) / ( Y - X ) ) : ( Z >= X ? 1.0f : 0.0f );
					Out = T * T * ( 3.0f - 2.0f * T );
					break;
				}
				}
				SetComponent( Result, C, Out );
			}
			break;
		}
		case EMaterialOp::Splat:
			Result = FVector4( Value( Instruction.A ).x );
			break;
		case EMaterialOp::Append:
		{
			const int32 NA = Num( Instruction.A );
			const int32 NB = Num( Instruction.B );
			for ( int32 C = 0; C < NA; ++C )
			{
				SetComponent( Result, C, Component( Value( Instruction.A ), C ) );
			}
			for ( int32 C = 0; C < NB && NA + C < 4; ++C )
			{
				SetComponent( Result, NA + C, Component( Value( Instruction.B ), C ) );
			}
			break;
		}
		case EMaterialOp::Mask:
			for ( int32 C = 0; C < Instruction.NumSwizzle; ++C )
			{
				SetComponent( Result, C, Component( Value( Instruction.A ), Instruction.Swizzle[C] ) );
			}
			break;
		case EMaterialOp::Sample:
		{
			const FVector4& Uv = Value( Instruction.A );
			Result = Context.SampleTexture ? Context.SampleTexture( Instruction.Slot, FVector2( Uv.x, Uv.y ) ) : FVector4( 1.0f, 1.0f, 1.0f, 1.0f );
			break;
		}
		case EMaterialOp::SceneDepth:
		{
			const FVector4& Uv = Value( Instruction.A );
			Result.x = Context.SceneDepth ? Context.SceneDepth( FVector2( Uv.x, Uv.y ) ) : 1.0f;
			break;
		}
		case EMaterialOp::Noise:
			Result.x = Noise( Value( Instruction.A ).x, Value( Instruction.A ).y );
			break;
		case EMaterialOp::Noise3D:
			Result.x = Noise3DGradient( Value( Instruction.A ) ).x;
			break;
		case EMaterialOp::Noise3DNormal:
		{
			const FVector4 ValueAndGradient = Noise3DGradient( Value( Instruction.A ) ); // 位置のノイズ値と勾配。
			const FVector3 Gradient( ValueAndGradient.y, ValueAndGradient.z, ValueAndGradient.w ); // 凹凸を作る局所勾配。
			const FVector3 N = Context.LocalNormal.LengthSquared() > 1.0e-12f ? Context.LocalNormal.Normalized() : FVector3( 0.0f, 1.0f, 0.0f ); // 面の局所法線。
			const FVector3 Perturbed = N - ( Gradient - N * FVector3::Dot( Gradient, N ) ) * Value( Instruction.B ).x; // 面に沿う勾配で傾けた法線。
			const FVector3 Normal = TransformLocalNormal( Perturbed, Context ); // 有効な最終法線。
			Result = FVector4( Normal.x, Normal.y, Normal.z, 0.0f );
			break;
		}
		case EMaterialOp::LocalNormalToWorld:
		{
			const FVector4 Local = Value( Instruction.A ); // 配置前の法線。
			const FVector3 Normal = TransformLocalNormal( FVector3( Local.x, Local.y, Local.z ), Context ); // 配置後の法線。
			Result = FVector4( Normal.x, Normal.y, Normal.z, 0.0f );
			break;
		}
		}
		OutValues.Add( Result );
	}
	return true;
}

FVector4 NMaterialEval::GetOutput( const FMaterialProgram& Program, const TArray<FVector4>& Values, const FString& Pin, bool* bOutFound )
{
	const FMaterialOutputValue* Output = Program.FindOutput( Pin );
	const bool bFound = Output != nullptr && Values.IsValidIndex( Output->Value );
	if ( bOutFound != nullptr )
	{
		*bOutFound = bFound;
	}
	return bFound ? Values[Output->Value] : FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
}

FVector4 NMaterialEval::GetNodeValue( const FMaterialProgram& Program, const TArray<FVector4>& Values, int32 NodeId, bool* bOutFound )
{
	const int32* Index = Program.NodeValues.Find( NodeId );
	const bool bFound = Index != nullptr && Values.IsValidIndex( *Index );
	if ( bOutFound != nullptr )
	{
		*bOutFound = bFound;
	}
	return bFound ? Values[*Index] : FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
}

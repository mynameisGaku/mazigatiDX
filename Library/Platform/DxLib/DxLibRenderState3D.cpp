// SPDX-License-Identifier: MIT
#include "DxLibRenderState3D.h"

#include "Library/Platform/DxLib/DxLibInclude.h"

namespace
{
	// 描画の終わりに戻す深度バッファの使い方（DxLib に取得関数が無いので、こちらで持つ。メインスレッドだけが読み書きする）
	FDrawDepthData GDefaultDepth;

	// 高さのフォグ（指数）の、「ここより下に掛かる」高さ。DxLib の GetVerticalFogDensity は、この高さを返さない（いつも 0）ので、こちらで持つ
	float GVerticalFogDensityStart = 0.0f;

	// ポリゴンの裏表の描き分け
	int ToDxCulling( int32 Mode )
	{
		switch ( static_cast<EDrawCulling>( Mode ) )
		{
		case EDrawCulling::BackFaces: return DX_CULLING_LEFT;
		case EDrawCulling::FrontFaces: return DX_CULLING_RIGHT;
		default: return DX_CULLING_NONE;
		}
	}

	// 画像の外側の貼り方
	int ToDxTextureAddress( int32 Mode )
	{
		switch ( static_cast<EDrawTextureAddress>( Mode ) )
		{
		case EDrawTextureAddress::Mirror: return DX_TEXADDRESS_MIRROR;
		case EDrawTextureAddress::Clamp: return DX_TEXADDRESS_CLAMP;
		case EDrawTextureAddress::Border: return DX_TEXADDRESS_BORDER;
		default: return DX_TEXADDRESS_WRAP;
		}
	}

	// フォグの濃さの決め方
	int ToDxFogMode( int32 Mode )
	{
		switch ( static_cast<EDrawFogMode>( Mode ) )
		{
		case EDrawFogMode::Exponential: return DX_FOGMODE_EXP;
		case EDrawFogMode::ExponentialSquared: return DX_FOGMODE_EXP2;
		case EDrawFogMode::Linear: return DX_FOGMODE_LINEAR;
		default: return DX_FOGMODE_NONE;
		}
	}

	// 高さのフォグ（指数）の密度と、「ここより下に掛かる」高さを決める。
	// DxLib の SetVerticalFogDensity は、密度がいまと同じだと、高さを無視する（実測）ので、いったん別の密度にしてから、決めたい密度にする
	void ApplyVerticalFogDensity( float Start, float Density )
	{
		SetVerticalFogDensity( Start, Density + 1.0f );
		SetVerticalFogDensity( Start, Density );
		GVerticalFogDensityStart = Start;
	}

	// 深度バッファの使い方を DxLib へ反映する（3D の描画だけに効くほうの設定）
	void ApplyDepth( const FDrawDepthData& Data )
	{
		SetUseZBuffer3D( Data.TestDepth != 0 ? TRUE : FALSE );
		SetWriteZBuffer3D( Data.WriteDepth != 0 ? TRUE : FALSE );
		SetZBufferCmpType3D( NDxLibRenderState3D::ToDxCompare( Data.Compare ) );
		SetZBias3D( Data.Bias );
	}
}

int NDxLibRenderState3D::ToDxCompare( int32 Compare )
{
	switch ( static_cast<EDrawCompare>( Compare ) )
	{
	case EDrawCompare::Never: return DX_CMP_NEVER;
	case EDrawCompare::Less: return DX_CMP_LESS;
	case EDrawCompare::Equal: return DX_CMP_EQUAL;
	case EDrawCompare::LessEqual: return DX_CMP_LESSEQUAL;
	case EDrawCompare::Greater: return DX_CMP_GREATER;
	case EDrawCompare::NotEqual: return DX_CMP_NOTEQUAL;
	case EDrawCompare::GreaterEqual: return DX_CMP_GREATEREQUAL;
	default: return DX_CMP_ALWAYS;
	}
}

void NDxLibRenderState3D::ApplyDefaults( bool bUseDepthBuffer )
{
	GDefaultDepth = FDrawDepthData();
	GDefaultDepth.TestDepth = bUseDepthBuffer ? 1 : 0;
	GDefaultDepth.WriteDepth = bUseDepthBuffer ? 1 : 0;
	ApplyDepth( GDefaultDepth );
	SetTextureAddressMode( DX_TEXADDRESS_WRAP );
	ResetTextureAddressTransform();
	ApplyVerticalFogDensity( 0.0f, 0.0f );
}

FDrawDepthData NDxLibRenderState3D::GetDefaultDepth()
{
	return GDefaultDepth;
}

bool FDxLibRenderState3D::IsStateCommand( EDrawCommand Type )
{
	switch ( Type )
	{
	case EDrawCommand::Depth:
	case EDrawCommand::Culling:
	case EDrawCommand::TextureAddress:
	case EDrawCommand::TextureTransform:
	case EDrawCommand::Fog:
	case EDrawCommand::HeightFog:
	case EDrawCommand::Anisotropy:
		return true;
	default:
		return false;
	}
}

bool FDxLibRenderState3D::Apply( const FDrawCommandView& Command )
{
	switch ( Command.Type )
	{
	case EDrawCommand::Depth:
	{
		if ( Command.CanRead<FDrawDepthData>() )
		{
			ApplyDepth( Command.Read<FDrawDepthData>() );
			bDepthChanged = true;
		}
		return true;
	}
	case EDrawCommand::Culling:
	{
		if ( Command.CanRead<FDrawCullingData>() )
		{
			if ( !bCullingChanged )
			{
				OldCulling = GetUseBackCulling();
				bCullingChanged = true;
			}
			SetUseBackCulling( ToDxCulling( Command.Read<FDrawCullingData>().Mode ) );
		}
		return true;
	}
	case EDrawCommand::TextureAddress:
	{
		if ( Command.CanRead<FDrawTextureAddressData>() )
		{
			const FDrawTextureAddressData Data = Command.Read<FDrawTextureAddressData>();
			SetTextureAddressModeUV( ToDxTextureAddress( Data.U ), ToDxTextureAddress( Data.V ) );
			bTextureAddressChanged = true;
		}
		return true;
	}
	case EDrawCommand::TextureTransform:
	{
		if ( Command.CanRead<FDrawTextureTransformData>() )
		{
			const FDrawTextureTransformData Data = Command.Read<FDrawTextureTransformData>();
			if ( Data.Enabled != 0 )
			{
				SetTextureAddressTransform( Data.TranslationU, Data.TranslationV, Data.ScaleU, Data.ScaleV, Data.CenterU, Data.CenterV, Data.Rotation );
			}
			else
			{
				ResetTextureAddressTransform();
			}
			bTextureTransformChanged = true;
		}
		return true;
	}
	case EDrawCommand::Fog:
	{
		if ( Command.CanRead<FDrawFogData>() )
		{
			const FDrawFogData Data = Command.Read<FDrawFogData>();
			if ( !DistanceFog.bSaved )
			{
				DistanceFog.bSaved = true;
				DistanceFog.Enable = GetFogEnable();
				DistanceFog.Mode = GetFogMode();
				GetFogColor( &DistanceFog.Red, &DistanceFog.Green, &DistanceFog.Blue );
				GetFogStartEnd( &DistanceFog.Start, &DistanceFog.End );
				DistanceFog.Density = GetFogDensity();
			}
			if ( Data.Mode == static_cast<int32>( EDrawFogMode::None ) )
			{
				SetFogEnable( FALSE );
			}
			else
			{
				SetFogMode( ToDxFogMode( Data.Mode ) );
				SetFogColor( Data.Color.R, Data.Color.G, Data.Color.B );
				SetFogStartEnd( Data.Start, Data.End );
				SetFogDensity( Data.Density );
				SetFogEnable( TRUE );
			}
		}
		return true;
	}
	case EDrawCommand::HeightFog:
	{
		if ( Command.CanRead<FDrawFogData>() )
		{
			const FDrawFogData Data = Command.Read<FDrawFogData>();
			if ( !VerticalFog.bSaved )
			{
				VerticalFog.bSaved = true;
				VerticalFog.Enable = GetVerticalFogEnable();
				VerticalFog.Mode = GetVerticalFogMode();
				GetVerticalFogColor( &VerticalFog.Red, &VerticalFog.Green, &VerticalFog.Blue );
				GetVerticalFogStartEnd( &VerticalFog.Start, &VerticalFog.End );
				float IgnoredStart = 0.0f;
				GetVerticalFogDensity( &IgnoredStart, &VerticalFog.Density );
				VerticalFog.DensityStart = GVerticalFogDensityStart;
			}
			if ( Data.Mode == static_cast<int32>( EDrawFogMode::None ) )
			{
				SetVerticalFogEnable( FALSE );
			}
			else
			{
				SetVerticalFogMode( ToDxFogMode( Data.Mode ) );
				SetVerticalFogColor( Data.Color.R, Data.Color.G, Data.Color.B );
				SetVerticalFogStartEnd( Data.Start, Data.End );
				// 指数のフォグは、Start より下に掛かる（密度の設定の「start」が、その高さ）
				ApplyVerticalFogDensity( Data.Start, Data.Density );
				SetVerticalFogEnable( TRUE );
			}
		}
		return true;
	}
	case EDrawCommand::Anisotropy:
	{
		if ( Command.CanRead<FDrawAnisotropyData>() )
		{
			if ( !bAnisotropyChanged )
			{
				OldAnisotropy = GetMaxAnisotropy();
				bAnisotropyChanged = true;
			}
			SetMaxAnisotropy( Command.Read<FDrawAnisotropyData>().Level );
		}
		return true;
	}
	default:
		return false;
	}
}

void FDxLibRenderState3D::Restore()
{
	if ( bDepthChanged )
	{
		ApplyDepth( NDxLibRenderState3D::GetDefaultDepth() );
		bDepthChanged = false;
	}
	if ( bTextureAddressChanged )
	{
		SetTextureAddressMode( DX_TEXADDRESS_WRAP );
		bTextureAddressChanged = false;
	}
	if ( bTextureTransformChanged )
	{
		ResetTextureAddressTransform();
		bTextureTransformChanged = false;
	}
	if ( bCullingChanged )
	{
		SetUseBackCulling( OldCulling );
		bCullingChanged = false;
	}
	if ( bAnisotropyChanged )
	{
		SetMaxAnisotropy( OldAnisotropy );
		bAnisotropyChanged = false;
	}
	if ( DistanceFog.bSaved )
	{
		SetFogMode( DistanceFog.Mode );
		SetFogColor( DistanceFog.Red, DistanceFog.Green, DistanceFog.Blue );
		SetFogStartEnd( DistanceFog.Start, DistanceFog.End );
		SetFogDensity( DistanceFog.Density );
		SetFogEnable( DistanceFog.Enable );
		DistanceFog.bSaved = false;
	}
	if ( VerticalFog.bSaved )
	{
		SetVerticalFogMode( VerticalFog.Mode );
		SetVerticalFogColor( VerticalFog.Red, VerticalFog.Green, VerticalFog.Blue );
		SetVerticalFogStartEnd( VerticalFog.Start, VerticalFog.End );
		ApplyVerticalFogDensity( VerticalFog.DensityStart, VerticalFog.Density );
		SetVerticalFogEnable( VerticalFog.Enable );
		VerticalFog.bSaved = false;
	}
}

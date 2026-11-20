// SPDX-License-Identifier: MIT
#include "RenderGraphBuilder.h"
#include "Library/Core/Interfaces/Render/IRenderSurface.h"
#include "Library/Core/Types/Numeric.h"

namespace
{
	constexpr int32 MaxPasses = 256; // 1グラフで保持する描画パス数。
	constexpr int32 MaxTextures = 1024; // 取り込む画像数と各パスの入力数。

	// 登録時に確認できる設定を調べる。
	bool ValidDesc( const FRenderGraphPassDesc& Desc )
	{
		return !Desc.Name.IsEmpty() && Desc.Reads.Num() <= MaxTextures && Desc.Colors.Num() > 0 && Desc.Colors.Num() <= static_cast<int32>( FRenderTargetPassDesc::MaxColorTargets ) && NMath::IsFinite( Desc.ClearDepth ) && Desc.ClearDepth >= 0.0f && Desc.ClearDepth <= 1.0f;
	}

	// 検査対象のパス名と拒否理由を返す。
	bool Fail( FString* OutError, const FString& Name, const char* Reason )
	{
		if ( OutError ) { *OutError = FString::Printf( "%s: %s", *Name, Reason ); }
		return false;
	}
}

bool CRenderGraphBuilder::ImportTexture( FTextureResourceHandle Texture, bool bHasContents )
{
	if ( bExecuting || !Texture.IsValid() || Textures.Num() >= MaxTextures || Textures.Contains( Texture.Value ) ) { return false; }
	Textures.Add( Texture.Value, bHasContents );
	return true;
}

int32 CRenderGraphBuilder::FindPass( const FString& Name ) const
{
	for ( int32 Index = 0; Index < Passes.Num(); ++Index )
	{
		if ( Passes[Index].Desc.Name == Name ) { return Index; }
	}
	return INDEX_NONE;
}

bool CRenderGraphBuilder::InsertAt( int32 Position, const FRenderGraphPassDesc& Desc, PassCallback Callback )
{
	if ( bExecuting || Position < 0 || Position > Passes.Num() || Passes.Num() >= MaxPasses || !Callback || !ValidDesc( Desc ) || FindPass( Desc.Name ) != INDEX_NONE ) { return false; }
	Passes.Insert( FPass{ Desc, MoveTemp( Callback ) }, Position );
	return true;
}

bool CRenderGraphBuilder::AddPass( const FRenderGraphPassDesc& Desc, PassCallback Callback )
{
	return InsertAt( Passes.Num(), Desc, MoveTemp( Callback ) );
}

bool CRenderGraphBuilder::InsertPassBefore( const FString& Anchor, const FRenderGraphPassDesc& Desc, PassCallback Callback )
{
	return InsertAt( FindPass( Anchor ), Desc, MoveTemp( Callback ) );
}

bool CRenderGraphBuilder::InsertPassAfter( const FString& Anchor, const FRenderGraphPassDesc& Desc, PassCallback Callback )
{
	const int32 Position = FindPass( Anchor ); // 名前がない場合は位置を推測しない。
	return Position != INDEX_NONE && InsertAt( Position + 1, Desc, MoveTemp( Callback ) );
}

bool CRenderGraphBuilder::ReplacePass( const FString& Name, PassCallback Callback )
{
	if ( bExecuting || !Callback ) { return false; }
	const int32 Position = FindPass( Name ); // 登録時の画像と位置を保ち、実行処理だけを更新する。
	if ( Position == INDEX_NONE ) { return false; }
	Passes[Position].Callback = MoveTemp( Callback );
	return true;
}

bool CRenderGraphBuilder::ReplacePass( const FRenderGraphPassDesc& Desc, PassCallback Callback )
{
	if ( bExecuting || !Callback || !ValidDesc( Desc ) ) { return false; }
	const int32 Position = FindPass( Desc.Name ); // 元の前後関係を維持する。
	if ( Position == INDEX_NONE ) { return false; }
	Passes[Position] = FPass{ Desc, MoveTemp( Callback ) };
	return true;
}

bool CRenderGraphBuilder::Validate( FString* OutError ) const
{
	TMap<uint64, bool> Ready = Textures; // 検査は取り込み時の状態を変更しない。
	for ( const FPass& Pass : Passes )
	{
		const FRenderGraphPassDesc& Desc = Pass.Desc; // 同じパスの読み書きを照合する。
		TArray<FTextureResourceHandle> Writes; // 色と明示的な深度の両方を調べる。
		const auto AddWrite = [&]( FTextureResourceHandle Handle, bool bClear )
		{
			const bool* Contents = Ready.Find( Handle.Value ); // 先行パスまたは外部の有効な内容。
			if ( !Contents || ( !*Contents && !bClear ) || Writes.Contains( Handle ) || Desc.Reads.Contains( Handle ) ) { return false; }
			Writes.Add( Handle );
			return true;
		};
		for ( const FRenderTargetColorAttachment& Color : Desc.Colors )
		{
			if ( !AddWrite( Color.Target, Color.bClear ) ) { return Fail( OutError, Desc.Name, "invalid color attachment, overlapping access, or load before initialization" ); }
		}
		if ( Desc.Depth.IsValid() && !AddWrite( Desc.Depth, Desc.bClearDepth ) ) { return Fail( OutError, Desc.Name, "invalid depth attachment, overlapping access, or load before initialization" ); }
		for ( const FTextureResourceHandle Read : Desc.Reads )
		{
			const bool* Contents = Ready.Find( Read.Value ); // 読み取りには別パスまたは外部の先行書き込みが必要。
			if ( !Contents || !*Contents ) { return Fail( OutError, Desc.Name, "read before initialization or texture not imported" ); }
		}
		for ( const FTextureResourceHandle Write : Writes ) { Ready[Write.Value] = true; }
	}
	if ( OutError ) { OutError->Empty(); }
	return true;
}

bool CRenderGraphBuilder::Execute( IRenderSurface& Surface, FString* OutError )
{
	if ( bExecuting ) { return Fail( OutError, "RenderGraph", "already executing" ); }
	if ( !Validate( OutError ) ) { return false; }
	bExecuting = true;
	bool bSuccess = true; // 失敗時にフラグを戻してから返す。
	for ( const FPass& Pass : Passes )
	{
		const FRenderGraphPassDesc& Desc = Pass.Desc; // 既存の描画先パスへ宣言を渡す。
		FRenderTargetPassDesc Target; // GPU固有の状態遷移は描画先の実装が担当する。
		Target.ColorTargets = Desc.Colors;
		Target.DepthTarget = Desc.Depth;
		Target.ClearDepth = Desc.ClearDepth;
		Target.bClearDepth = Desc.bClearDepth;
		if ( !Surface.BeginRenderTargetPass( Target ) )
		{
			bSuccess = Fail( OutError, Desc.Name, "begin render target pass failed" );
			break;
		}
		const FRenderCommandContext Context{ Surface.GetCanvas2D(), Surface.GetGeometry3D() }; // パス中だけ利用できる窓口。
		const bool bRecorded = Pass.Callback( Context ); // 処理の失敗でも開始済みのパスを必ず閉じる。
		const bool bEnded = Surface.EndRenderTargetPass(); // 既存の提出と描画先復帰を使う。
		if ( !bRecorded || !bEnded )
		{
			bSuccess = Fail( OutError, Desc.Name, bRecorded ? "end render target pass failed" : "pass callback failed" );
			break;
		}
	}
	bExecuting = false;
	return bSuccess;
}

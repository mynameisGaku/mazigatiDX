// SPDX-License-Identifier: MIT
#include "CanvasTargetScopes.h"
#include "Library/Platform/Render/Shadow/ShadowMapMath.h"
#include "Library/Core/Types/Numeric.h"
#include "Library/Math/MathUtils.h"
#include "Library/Platform/System/BackendSupport.h"

namespace
{
	using FTargetScope = FCanvasTargetScope;
	using FMaskState = FCanvasMaskState;
}

CCanvasTargetScopes::CCanvasTargetScopes( FCanvasExecution& InRun )
	: Run( InRun )
	, Surface( Run.Surface )
	, Targets( Run.Targets )
	, Shaders( Run.Shaders )
	, Canvas( Run.Canvas )
	, NullCanvas( Run.NullCanvas )
	, Geometry( Run.Geometry )
	, ShaderExecutor( Run.ShaderExecutor )
	, bStopped( Run.bStopped )
	, bAlphaBlend( Run.State.bAlphaBlend )
	, ClipDepth( Run.State.ClipDepth )
	, Transform( Run.State.Transform )
	, MaskStack( Run.State.MaskStack )
	, TargetStack( Run.TargetState.TargetStack )
	, CurrentTarget( Run.TargetState.CurrentTarget )
	, bMrtActive( Run.TargetState.bMrtActive )
	, MrtSignature( Run.TargetState.MrtSignature )
	, SkipTargetDepth( Run.TargetState.SkipTargetDepth )
	, SkipShadowDepth( Run.TargetState.SkipShadowDepth )
	, bShadowPassActive( Run.TargetState.bShadowPassActive )
{
}

void CCanvasTargetScopes::Unsupported( const FDrawCommandView& View, const char* Reason )
{
	FCanvasExecution::Unsupported( View, Reason );
}

void CCanvasTargetScopes::ApplyMask()
{
	Run.ApplyMask();
}

// いまの描画先のパスを始める（主画面なら何もしない）。キャンバスと3Dの実行先を取り直す。
bool CCanvasTargetScopes::EnterTarget( const FRenderTargetDrawView& Target )
{
	bool bEntered = true; // 主画面は、常に成功。
	if ( Target.Color.IsValid() )
	{
		FRenderTargetPassDesc Desc; // すでに描いてある内容を保って、続きを描く。
		Desc.ColorTarget = Target.Color;
		Desc.DepthTarget = Target.Depth;
		Desc.bClearColor = false;
		Desc.bClearDepth = false;
		bEntered = Surface.BeginRenderTargetPass( Desc );
	}
	Canvas = Surface.GetCanvas2D();
	if ( !Canvas )
	{
		Canvas = &NullCanvas; // 浮動小数点の描画先などは、2Dを描けない（3Dとシェーダーだけ）。
	}
	Geometry.SetGeometry( Surface.GetGeometry3D() );
	if ( !MaskStack.IsEmpty() )
	{
		ApplyMask();
	}
	return bEntered;
}

// MRTのパスから、1つの出力のパスへ戻る（描いてある内容は保つ）。
void CCanvasTargetScopes::LeaveMrt()
{
	if ( !bMrtActive )
	{
		return;
	}
	bMrtActive = false;
	Surface.EndRenderTargetPass();
	EnterTarget( CurrentTarget );
}

// シェーダーで描く前に、描画先のパスを整える。書く描画先の数と形式を、Request に入れる。
bool CCanvasTargetScopes::PrepareTargets( FShaderDrawRequest& Request )
{
	FRenderTargetDrawView Outputs[NDrawShader::NumOutputTargets]; // 出力 1〜7 の描画先。
	int32 Highest = 0; // 置いてある、いちばん大きい出力の番号。
	for ( int32 Index = 1; Index < NDrawShader::NumOutputTargets; ++Index )
	{
		const int32 Handle = ShaderExecutor.GetOutputTarget( Index );
		if ( Handle < 0 )
		{
			continue;
		}
		if ( !Targets || !Targets->GetRenderTargetDrawView( Handle, Outputs[Index] ) )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget (not a render target)", "The output target is skipped" );
			return false; // 描画先として読めない番号は、描かない。
		}
		Highest = Index;
	}
	Request.NumTargets = 1;
	Request.TargetFormats[0] = CurrentTarget.Color.IsValid() ? CurrentTarget.Format : ERenderTargetColorFormat::RGBA8;
	if ( Highest == 0 )
	{
		LeaveMrt();
		return true;
	}
	if ( !CurrentTarget.Color.IsValid() )
	{
		NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget", "The screen cannot have multiple outputs; draw into a render target (PushTarget)" );
		return false;
	}
	TArray<uint64> Signature;
	Signature.Add( CurrentTarget.Color.Value );
	for ( int32 Index = 1; Index <= Highest; ++Index )
	{
		if ( !Outputs[Index].Color.IsValid() )
		{
			return false; // 出力の番号は、抜けなく置く。
		}
		Signature.Add( Outputs[Index].Color.Value );
	}
	bool bSame = bMrtActive && Signature.Num() == MrtSignature.Num();
	for ( int32 Index = 0; bSame && Index < Signature.Num(); ++Index )
	{
		bSame = Signature[Index] == MrtSignature[Index];
	}
	if ( !bSame )
	{
		Surface.EndRenderTargetPass(); // いまのパスを閉じて、すべての出力を束ねたパスを開く（描いてある内容は保つ）。
		TArray<FRenderTargetColorAttachment> Attachments;
		FRenderTargetColorAttachment First;
		First.Target = CurrentTarget.Color;
		First.bClear = false;
		Attachments.Add( First );
		for ( int32 Index = 1; Index <= Highest; ++Index )
		{
			FRenderTargetColorAttachment Attachment;
			Attachment.Target = Outputs[Index].Color;
			Attachment.bClear = false;
			Attachments.Add( Attachment );
		}
		FRenderTargetPassDesc Desc;
		Desc.ColorTargets = TArrayView<const FRenderTargetColorAttachment>( Attachments.GetData(), Attachments.Num() );
		Desc.DepthTarget = CurrentTarget.Depth;
		Desc.bClearColor = false;
		Desc.bClearDepth = false;
		if ( !Surface.BeginRenderTargetPass( Desc ) )
		{
			NBackendSupport::ReportUnsupported( "DrawCommands", "ShaderOutputTarget (pass)", "The pass with multiple render targets could not be started" );
			bMrtActive = false;
			EnterTarget( CurrentTarget );
			return false;
		}
		bMrtActive = true;
		MrtSignature = Signature;
		Canvas = Surface.GetCanvas2D();
		if ( !Canvas )
		{
			Canvas = &NullCanvas;
		}
		Geometry.SetGeometry( Surface.GetGeometry3D() );
	}
	Request.NumTargets = Highest + 1;
	for ( int32 Index = 1; Index <= Highest; ++Index )
	{
		Request.TargetFormats[Index] = Outputs[Index].Format;
	}
	return true;
}

// 描画先・影の絵の、始まりと終わりの命令なら実行して true。
bool CCanvasTargetScopes::ExecuteScope( const FDrawCommandView& View )
{
	// 描けない描画先・シャドウマップへ描く範囲は、対になる終わりまで、まとめて飛ばす（画面へ描いてしまわないように）。
	if ( View.Type == EDrawCommand::PushTarget )
	{
		FRenderTargetDrawView Next; // 替える先の描画先。
		if ( SkipTargetDepth > 0 || SkipShadowDepth > 0 || !View.CanRead<FDrawTargetData>() || !Targets || !Targets->GetRenderTargetDrawView( View.Read<FDrawTargetData>().Handle, Next ) )
		{
			++SkipTargetDepth;
			Unsupported( View, "Drawing into this render target is skipped" );
			return true;
		}
		// いまの描画先のパスを閉じてから、次の描画先のパスを始める（パスは入れ子にできない）。
		if ( CurrentTarget.Color.IsValid() )
		{
			Surface.EndRenderTargetPass();
			bMrtActive = false;
		}
		FTargetScope Scope; // 戻すときの状態。
		Scope.Target = CurrentTarget;
		Scope.Transform = Transform;
		Scope.ClipDepth = ClipDepth;
		Scope.MaskStack = MaskStack;
		MaskStack.Reset(); // 替えた先は、マスクなしから始まる。
		if ( !EnterTarget( Next ) )
		{
			// 始められなかったら、元の描画先へ戻して、この範囲を飛ばす。
			MaskStack = Scope.MaskStack;
			EnterTarget( CurrentTarget );
			++SkipTargetDepth;
			Unsupported( View, "The render target pass could not be started; drawing into it is skipped" );
			if ( !Canvas )
			{
				bStopped = true;
			}
			return true;
		}
		TargetStack.Add( Scope );
		CurrentTarget = Next;
		Transform = FCanvasTransform(); // 替えた先は、クリップなし・2Dの変換なしから始まる。
		ClipDepth = 0;
		return true;
	}
	if ( View.Type == EDrawCommand::PopTarget )
	{
		if ( SkipTargetDepth > 0 )
		{
			--SkipTargetDepth;
			return true;
		}
		if ( SkipShadowDepth > 0 || TargetStack.IsEmpty() )
		{
			return true;
		}
		while ( ClipDepth > 0 )
		{
			Canvas->PopClip();
			--ClipDepth;
		}
		Surface.EndRenderTargetPass();
		bMrtActive = false;
		const FTargetScope Scope = TargetStack.Pop(); // 替える前の状態。
		CurrentTarget = Scope.Target;
		Transform = Scope.Transform;
		ClipDepth = Scope.ClipDepth;
		MaskStack = Scope.MaskStack;
		if ( !EnterTarget( CurrentTarget ) )
		{
			bStopped = true; // 元の描画先へ戻れない。これ以上は描けない。
		}
		return true;
	}
	if ( View.Type == EDrawCommand::BeginShadowMap )
	{
		// 描けない（入れ子・使えない値・影の絵でない・カメラが無い）ときは、対の EndShadowMap まで、描く命令を飛ばす（画面へ描いてしまわないように）。
		FRenderTargetDrawView Map; // 影の絵（R32Float の色と深度）。
		FMatrix4x4 LightViewProjection;
		FDrawBeginShadowMapData Data;
		IRenderGeometry3D* Target = Geometry.GetGeometry();
		bool bReady = SkipTargetDepth == 0 && SkipShadowDepth == 0 && !bShadowPassActive && View.CanRead<FDrawBeginShadowMapData>() && Shaders && Targets && Target;
		if ( bReady )
		{
			Data = View.Read<FDrawBeginShadowMapData>();
			const FCameraViewport Viewport = { Target->GetWidth(), Target->GetHeight() };
			bReady = Data.Handle >= 0 && Targets->GetRenderTargetDrawView( Data.Handle, Map ) && Map.Format == ERenderTargetColorFormat::R32Float && Map.Depth.IsValid()
				&& NMath::IsFinite( Data.AdjustDepth ) && Data.AdjustDepth >= 0.0f && Data.AdjustDepth < 1.0f
				&& NShadowMapMath::ComputeLightViewProjection( Data, Geometry.GetCamera(), Viewport, LightViewProjection );
		}
		if ( bReady && CurrentTarget.Color.IsValid() )
		{
			Surface.EndRenderTargetPass(); // いまの描画先のパスを閉じてから、影の絵のパスを始める。
			bMrtActive = false;
		}
		if ( bReady )
		{
			FRenderTargetPassDesc Desc; // 影の絵は、いちばん遠い（1）の深度で始める。
			Desc.ColorTarget = Map.Color;
			Desc.DepthTarget = Map.Depth;
			Desc.ClearColor = FColor32( 255, 255, 255, 255 );
			Desc.bClearColor = true;
			Desc.ClearDepth = 1.0f;
			Desc.bClearDepth = true;
			if ( !Surface.BeginRenderTargetPass( Desc ) )
			{
				EnterTarget( CurrentTarget ); // 元の描画先のパスへ戻す。
				bReady = false;
			}
		}
		if ( !bReady )
		{
			++SkipShadowDepth;
			Unsupported( View, "The shadow map pass could not be started; drawing into it is skipped" );
			return true;
		}
		FTargetScope Scope; // 終わったときに戻す状態。
		Scope.Target = CurrentTarget;
		Scope.Transform = Transform;
		Scope.ClipDepth = ClipDepth;
		Scope.bShadow = true;
		Scope.MaskStack = MaskStack;
		MaskStack.Reset();
		TargetStack.Add( Scope );
		CurrentTarget = Map;
		Transform = FCanvasTransform();
		ClipDepth = 0;
		Canvas = Surface.GetCanvas2D();
		if ( !Canvas )
		{
			Canvas = &NullCanvas;
		}
		Geometry.SetGeometry( Surface.GetGeometry3D() );
		Geometry.BeginShadowCasting( LightViewProjection );
		Shaders->SetShadowPass( Data.Handle, LightViewProjection, Data.AdjustDepth, Map.Width, Map.Height );
		bShadowPassActive = true;
		return true;
	}
	if ( View.Type == EDrawCommand::EndShadowMap )
	{
		if ( SkipShadowDepth > 0 )
		{
			--SkipShadowDepth;
			return true;
		}
		if ( bShadowPassActive && !TargetStack.IsEmpty() )
		{
			Surface.EndRenderTargetPass();
			bMrtActive = false;
			Geometry.EndShadowCasting();
			bShadowPassActive = false;
			const FTargetScope Scope = TargetStack.Pop(); // 始める前の状態。
			CurrentTarget = Scope.Target;
			Transform = Scope.Transform;
			ClipDepth = Scope.ClipDepth;
			MaskStack = Scope.MaskStack;
			if ( !EnterTarget( CurrentTarget ) )
			{
				bStopped = true;
			}
		}
		return true;
	}
	return false;
}

// 影の絵へ描いている間に使えない命令か、描かない範囲の中か。
bool CCanvasTargetScopes::ShouldSkip( EDrawCommand Type ) const
{
	// 影の絵へ描いている間は、描画先・カメラ・クリップ・消去などの命令は、使えない（DxLib と同じ。無視する）。
	if ( bShadowPassActive )
	{
		switch ( Type )
		{
		case EDrawCommand::PushTarget:
		case EDrawCommand::PopTarget:
		case EDrawCommand::PushCamera:
		case EDrawCommand::PopCamera:
		case EDrawCommand::PushClip:
		case EDrawCommand::PopClip:
		case EDrawCommand::PushMask:
		case EDrawCommand::PopMask:
		case EDrawCommand::Clear:
		case EDrawCommand::ClearDepth:
		case EDrawCommand::CopyScreen:
		case EDrawCommand::ImageFilter:
		case EDrawCommand::ImageBlend:
		case EDrawCommand::UseShadowMap:
		case EDrawCommand::PreviewShadowMap:
		case EDrawCommand::ShaderOutputTarget:
			return true;
		default:
			break;
		}
	}
	if ( SkipTargetDepth > 0 || SkipShadowDepth > 0 )
	{
		return true;
	}

	return false;
}

bool CCanvasTargetScopes::ExecuteTargetCommand( const FDrawCommandView& View )
{
	switch ( View.Type )
	{
	case EDrawCommand::CopyScreen:
	{
		if ( !View.CanRead<FDrawCopyScreenData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawCopyScreenData>(); // 固定部を所有コピーする。
		FRenderTargetDrawView Destination; // 写し先の描画先（画像）。
		// DxLibと同じく、画像と矩形の大きさが同じでないと、何もしない。
		if ( !Targets || !Targets->GetRenderTargetDrawView( Data.Handle, Destination ) || Destination.Width != Data.X1 - Data.X0 || Destination.Height != Data.Y1 - Data.Y0 )
		{
			break;
		}
		if ( !Surface.CopyColorRegion( Data.X0, Data.Y0, Data.X1, Data.Y1, Destination.Color ) )
		{
			Unsupported( View, "The screen copy failed (the destination has another pixel format, or the region is outside the screen) and is skipped" );
		}
		break;
	}
	case EDrawCommand::PreviewShadowMap:
	{
		if ( !View.CanRead<FDrawPreviewShadowMapData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawPreviewShadowMapData>(); // 固定部を所有コピーする。
		FRenderTargetDrawView Map;
		// DxLibと同じく、使えない影の絵・空の矩形は、何も描かない。
		if ( Data.Handle >= 0 && Data.X1 > Data.X0 && Data.Y1 > Data.Y0 && Targets && Shaders && Targets->GetRenderTargetDrawView( Data.Handle, Map ) && Map.Format == ERenderTargetColorFormat::R32Float
			&& !ShaderExecutor.DrawShadowPreview( Data, bAlphaBlend ) )
		{
			Unsupported( View, "The shadow map preview failed (the target cannot take a shader draw) and is skipped" );
		}
		break;
	}
	case EDrawCommand::UseShadowMap:
	{
		if ( !View.CanRead<FDrawUseShadowMapData>() )
		{
			bStopped = true;
			break;
		}
		const auto Data = View.Read<FDrawUseShadowMapData>(); // 固定部を所有コピーする。
		if ( Data.Slot < 0 || Data.Slot >= NDrawShadowMap::NumSlots )
		{
			break;
		}
		FRenderTargetDrawView Map;
		const bool bUsable = Data.Handle >= 0 && Targets && Targets->GetRenderTargetDrawView( Data.Handle, Map ) && Map.Format == ERenderTargetColorFormat::R32Float;
		Geometry.SetShadowReceiver( Data.Slot, bUsable ? Data.Handle : -1 ); // 使えない番号は、そのスロットを外す。
		break;
	}
	default:
		return false;
	}
	return true;
}

// 戻し忘れた描画先を、主画面まで戻す。
void CCanvasTargetScopes::UnwindToScreen()
{
while ( !TargetStack.IsEmpty() && Canvas )
{
	while ( ClipDepth > 0 )
	{
		Canvas->PopClip();
		--ClipDepth;
	}
	Surface.EndRenderTargetPass();
	bMrtActive = false;
	const FTargetScope Scope = TargetStack.Pop(); // 替える前の状態。
	if ( Scope.bShadow )
	{
		Geometry.EndShadowCasting();
		bShadowPassActive = false;
	}
	CurrentTarget = Scope.Target;
	ClipDepth = Scope.ClipDepth;
	EnterTarget( CurrentTarget );
}
}

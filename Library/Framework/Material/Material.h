// SPDX-License-Identifier: MIT
#pragma once

#include "Library/Core/Collection/Array.h"
#include "Library/Core/Interfaces/Render/IImage.h"
#include "Library/Core/Memory/SharedPtr.h"
#include "Library/Core/Types/Callable.h"
#include "Library/Framework/Asset/Gpu/Shader.h"
#include "Library/Framework/Material/MaterialParameterCollection.h"
#include "Library/Framework/Material/MaterialCompiler.h"
#include "Library/Framework/Material/MaterialEval.h"
#include "Library/Framework/Render/Draw/DrawContext.h"
#include "Library/Framework/Render/Types/ShaderVertex.h"
#include "Library/Math/Color.h"
#include "Library/Math/Matrix4x4.h"

class CAssetSubsystem;

//=============================================================================
// CMaterial: 動かせるマテリアル
//   定義（FMaterialDefinition）を、コンパイルして（中間表現 → HLSL）、パラメーターの値を持ち、FDrawContext に、描く命令を記録する。
//   UI・描画・エフェクトのどれも知らない。DxLib も知らない（FDrawContext と CShader だけ）。
//
//   // 作る・値を決める
//   TSharedPtr<CMaterial> Material = CMaterial::Create( Definition );
//   Material->SetVector( "Tint", FVector4( 1.0f, 0.5f, 0.0f, 1.0f ) );
//   if ( !Material->IsValid() ) { /* Material->GetDiagnostics() に、場所つきの理由 */ }
//
//   // UI・Particle: 矩形に描く（ブレンドは、呼ぶ側で決める）
//   Material->DrawRect( Draw, X, Y, Width, Height, FColor32( 255, 255, 255, 255 ), FrameState );
//   // Particle: ワールドの四角形（粒子）を描く
//   Material->PrepareShaders();   // 先に、メインスレッドで
//   if ( Material->BeginParticles( Draw, FrameState ) ) { Material->DrawParticleQuad( Draw, Quad ); Material->EndParticles( Draw ); }
//   // PostProcess: シーンの絵（と深度）を読んで、矩形に描く
//   Material->DrawPostProcess( Draw, X, Y, Width, Height, SceneImage, DepthImage, FrameState );
//   // Surface: 球などのメッシュ（FShaderVertex3D）を、標準の頂点シェーダーで描く
//   Material->BeginSurface( Draw, SceneState );
//   Material->DrawSurfaceMesh( Draw, Vertices, NumVertices, Indices, NumIndices, World, Tint );
//   Material->EndSurface( Draw );
//
//   - インスタンス: CMaterial::CreateInstance( 親 ）は、親のコンパイル結果を共有して、パラメーターだけを上書きする（コンパイルし直さない）。上書きしていない値は、親の値に従う。
//   - コレクション（MPC）: BindParameterCollection で結ぶ（または、SetParameterCollectionResolver で、名前から探す関数を渡す。CMaterialSubsystem は、これを自動で行う）。
//   - シェーダーは、最初に描くとき（または PrepareShaders）に、実行のときにコンパイルする（メインスレッドだけ）。コンパイルが失敗したら、描かず false を返し、
//     GetDiagnostics() に、理由が足される。ピクセルシェーダーを実行できないバックエンド（FMaterialCapabilities）では、描かない。
//   - Reload( 新しい定義 ): 同じ名前・同じ型のパラメーターの値を引き継いで、作り直す（ホットリロード）。このマテリアルを使っているブラシなどは、そのまま使える。
//   - 描いたあとの描画の状態（フィルター・テクスチャの端の扱い・使うシェーダー）は、シェーダーを止める以外は、元に戻さない（PostProcess は、フィルターを Bilinear、端を Clamp にする）。
//=============================================================================

// 毎フレームの状態（UI・Particle・PostProcess）。HLSL の g_Time・g_Rect・g_State
struct FMaterialFrameState
{
	float32 Time = 0.0f;
	float32 DeltaTime = 0.0f;
	float32 Hover = 0.0f;      // UI: 0〜1
	float32 Press = 0.0f;
	float32 Value = 0.0f;
	float32 Enabled = 1.0f;
	float32 NearPlane = 0.1f;  // PostProcess: シーンの深度を距離に直すため
	float32 FarPlane = 1000.0f;
};

// 粒子 1 つの状態（Particle ドメインの ParticleLife・ParticleAge・ParticleRandom・ParticleSpeed ノードが読む）
struct FMaterialParticleState
{
	float32 Life = 0.0f;     // 寿命の位置（生まれたときが 0、消えるときが 1）
	float32 Age = 0.0f;      // 生まれてからの秒
	float32 Random = 0.0f;   // 粒子ごとに決まった、0〜1 の数
	float32 Speed = 0.0f;    // 速さ（ワールドの長さ / 秒）
};

// Surface の場面の状態。HLSL の cbScene
struct FMaterialSceneState
{
	FMatrix4x4 ViewProjection = FMatrix4x4::Identity;
	FVector3 LightDirection = FVector3( -0.4969f, -0.7951f, -0.3478f );   // 光が進む向き（長さ 1）
	FVector3 CameraPosition = FVector3( 0.0f, 0.0f, 0.0f );
	float32 Time = 0.0f;
	float32 NearPlane = 0.1f;
	float32 FarPlane = 1000.0f;
};

// コンパイルの結果（マテリアルとインスタンスで共有する）
struct FMaterialCompiled
{
	FMaterialDefinition Definition;
	FMaterialCapabilities Capabilities;
	FMaterialProgram Program;
	FString Hlsl;
	TArray<FMaterialDiagnostic> ShaderDiagnostics;   // シェーダーのコンパイルの失敗など（実行のときに足される）
	TSharedPtr<CShader> Pixel;
	TSharedPtr<CShader> Vertex;                      // Surface だけ
	bool bShadersTried = false;
	bool bShadersOk = false;
};

class CMaterial
{
public:
	using FParameterCollectionResolver = TCallable<TSharedPtr<CMaterialParameterCollection>( const FString& ParameterCollectionName )>;

	// 作る（コンパイルまで行う。エラーがあっても作れる。IsValid / GetDiagnostics で確かめる）
	static TSharedPtr<CMaterial> Create( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities = NMaterial::DefaultCapabilities() );
	// インスタンス: 親のコンパイル結果を共有する。親は、インスタンスより先に消えない（参照で持つ）
	static TSharedPtr<CMaterial> CreateInstance( const TSharedPtr<CMaterial>& Parent );

	// 定義を替えて、作り直す（ホットリロード）。同じ名前・型のパラメーターの値は、引き継ぐ。インスタンスには使えない（false）
	bool Reload( const FMaterialDefinition& Definition );

	bool IsInstance() const { return Parent.IsValid(); }
	const TSharedPtr<CMaterial>& GetParent() const { return Parent; }
	const FMaterialDefinition& GetDefinition() const { return GetCompiled().Definition; }
	const FMaterialProgram& GetProgram() const { return GetCompiled().Program; }
	EMaterialDomain GetDomain() const { return GetCompiled().Program.Domain; }
	// 生成した HLSL（エディタ・確認用。HLSL の直指定は、宣言のあとに本文が付いたもの）
	const FString& GetHlsl() const { return GetCompiled().Hlsl; }
	// コンパイル（と、シェーダーの作成）の診断
	TArray<FMaterialDiagnostic> GetDiagnostics() const;
	// エラーなくコンパイルできたか
	bool IsValid() const { return GetCompiled().Program.bValid; }
	// コンパイルした回数（Reload のたびに増える。表示の更新に使える）
	int32 GetRevision() const { return Revision; }

	// ---- パラメーター（名前で）。型が違う・無い名前は false ----
	bool SetScalar( const FString& Name, float32 Value );
	bool SetVector( const FString& Name, const FVector4& Value );   // Float2〜Float4（使わない成分は、無視される）
	bool SetTexture( const FString& Name, const TSharedPtr<IImage>& Image );
	bool GetScalar( const FString& Name, float32& OutValue ) const;
	bool GetVector( const FString& Name, FVector4& OutValue ) const;
	// インスタンス: 上書きをやめて、親の値に戻す
	bool ResetParameter( const FString& Name );
	// インスタンス: 上書きしているか
	bool IsOverridden( const FString& Name ) const;
	// インスタンス: すべての上書きをやめる
	void ClearOverrides();
	// 既定の画像（ノードの TexturePath）を、アセットから読み込む（まだ画像の無い置き場だけ）
	void LoadTextures( CAssetSubsystem& Assets );

	// ---- CPU での評価（GPU を使わない。テスト・エディタのプレビュー用）----
	// Input（UV・時間・部品の状態などの入力）で、すべての命令を評価する。パラメーターの値は、いまの値（コレクションも含む）で、ここで入れる。無効なマテリアル・HLSL の直指定は false
	bool EvaluateCpu( const FMaterialEvalContext& Input, TArray<FVector4>& OutValues ) const;

	// ---- コレクション ----
	void BindParameterCollection( const TSharedPtr<CMaterialParameterCollection>& ParameterCollection );
	void SetParameterCollectionResolver( const FParameterCollectionResolver& Resolver ) { ParameterCollectionResolver = Resolver; }

	// ---- GPU ----
	// シェーダーを作る（描くとき、自動でも呼ばれる）。メインスレッドだけ。作れなければ false（理由は GetDiagnostics）
	bool PrepareShaders();
	bool IsReady() const { return GetCompiled().bShadersOk; }

	// UI・Particle: 矩形に描く。ドメインが違う・シェーダーが作れないなら、何も記録せず false
	bool DrawRect( FDrawContext& Draw, float32 X, float32 Y, float32 Width, float32 Height, const FColor32& Tint, const FMaterialFrameState& State );
	// Particle: ワールドの四角形（粒子。ビルボード・向きが固定の板）を描く。使うシェーダー・定数・画像を決める → 四角形を描く（何度でも）→ 止める。
	// 四角形の色（FDrawQuad3DData::Color）は、マテリアルの出力に掛かる（VertexColor ノードでも読める）。画像の座標は U0・V0〜U1・V1。Handle は使わない。ブレンドと奥行きは、呼ぶ側で決める。
	// 先に、メインスレッドで PrepareShaders を呼んでおく（ここでは、シェーダーを作らない）。記録は、どのスレッドからでもよい（その間に、パラメーターを変えないこと。
	// 描画システムが、並列に記録するため）。ドメインが Particle でない・シェーダーの用意ができていないなら、何も記録せず false（そのときは、DrawParticleQuad・EndParticles を呼ばない）
	bool BeginParticles( FDrawContext& Draw, const FMaterialFrameState& State ) const;
	// Particle は、粒子ごとの数（ParticleLife・ParticleAge・ParticleRandom・ParticleSpeed ノードが読む）
	bool DrawParticleQuad( FDrawContext& Draw, const FDrawQuad3DData& Quad, const FMaterialParticleState& Particle = FMaterialParticleState() ) const;
	void EndParticles( FDrawContext& Draw ) const;
	// PostProcess: Scene（シーンの絵。ShaderRect で矩形に貼る）と Depth（無ければ null）を読んで、矩形に描く
	bool DrawPostProcess( FDrawContext& Draw, float32 X, float32 Y, float32 Width, float32 Height, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth, const FMaterialFrameState& State );
	// Surface: 使うシェーダー・場面の定数・パラメーター・画像を決める → メッシュを描く（何度でも）→ 止める
	bool BeginSurface( FDrawContext& Draw, const FMaterialSceneState& Scene );
	bool DrawSurfaceMesh( FDrawContext& Draw, const FShaderVertex3D* Vertices, int32 NumVertices, const uint16* Indices, int32 NumIndices, const FMatrix4x4& World, const FColor32& Tint );
	void EndSurface( FDrawContext& Draw );

	// 直近の描画が失敗した理由（成功なら空）
	const FString& GetLastError() const { return LastError; }

private:
	struct FValue
	{
		FString Name;
		FVector4 Value = FVector4( 0.0f, 0.0f, 0.0f, 0.0f );
	};

	struct FTextureValue
	{
		FString Name;
		TSharedPtr<IImage> Image;
	};

	CMaterial() = default;

	const FMaterialCompiled& GetCompiled() const { return Parent ? Parent->GetCompiled() : *Compiled; }
	FMaterialCompiled& GetCompiled() { return Parent ? Parent->GetCompiled() : *Compiled; }
	void Compile( const FMaterialDefinition& Definition, const FMaterialCapabilities& Capabilities );
	// 名前のパラメーターの、いまの値（インスタンスは、上書き → 親）。無ければ false
	bool FindEffectiveValue( const FMaterialParamSlot& Slot, FVector4& OutValue ) const;
	TSharedPtr<IImage> FindEffectiveTexture( const FString& Name ) const;
	TSharedPtr<CMaterialParameterCollection> FindParameterCollection( const FString& ParameterCollectionName ) const;
	// g_Params の中身（パラメーターごとに 4 つ組 1 つ。コレクションの値は、いま読む）
	void BuildParamBlock( TArray<FVector4>& OutBlock ) const;
	void BindTextures( FDrawContext& Draw, const TSharedPtr<IImage>& Scene, const TSharedPtr<IImage>& Depth ) const;
	void UnbindTextures( FDrawContext& Draw ) const;
	bool Fail( const FString& Message );
	FValue* FindValue( TArray<FValue>& Values, const FString& Name );
	const FValue* FindValue( const TArray<FValue>& Values, const FString& Name ) const;

	TSharedPtr<FMaterialCompiled> Compiled;    // 親がいなければ、ここ
	TSharedPtr<CMaterial> Parent;
	TArray<FValue> Values;                     // 上書き（インスタンス）/ 現在の値（親なし）
	TArray<FTextureValue> Textures;
	TArray<TSharedPtr<CMaterialParameterCollection>> ParameterCollections;
	FParameterCollectionResolver ParameterCollectionResolver;
	FString LastError;
	int32 Revision = 1;
};

#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/StaticModelSurfaceData.h"
#include "Library/Core/Interfaces/Render/MeshTypes.h"
#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// アセット（画像・モデル・フォント・サウンド）の読み込みと破棄を行う、プラットフォーム層のインターフェース
//   フレームワーク側（CAssetManager など）はこれだけを知り、DxLib などの実装には依存しない。
//   実装: CDxLibAssetBackend（DxLib）。確認用に、読み込んだふりをするだけの実装を作ってもよい。
//
//   - 資源は int32 のハンドルで表す。負の値（InvalidHandle）は無効
//   - すべての関数はメインスレッドだけから呼ばれる（DxLib はスレッドセーフではない）
//=============================================================================
namespace NAsset
{
	constexpr int32 InvalidHandle = -1;
}

// サウンドの読み込み方
enum class ESoundLoadMode : uint8
{
	Memory,   // 全部をメモリに展開する。応答が速く、同じ音の同時再生ができる（効果音向け）
	Stream,   // 再生しながらファイルから読む。メモリを食わない代わりに、同じ音の同時再生はできない（BGM 向け）
};

// フォントの指定
struct FFontDesc
{
	FString Face;               // フォント名（"MS Gothic" など）。空なら実装の既定フォント
	int32 Size = 24;            // 文字の高さ（ピクセル）
	int32 Thickness = 6;        // 太さ（0..9）
	int32 EdgeSize = 0;         // 0 より大きいと縁取りをつける（ピクセル）
	bool bItalic = false;
	bool bAntialias = true;
};

// モデルを MV1 ファイルに保存するときの指定
enum class EModelSaveType : uint8
{
	Normal,      // メッシュとアニメーションの両方
	MeshOnly,    // メッシュだけ（アニメーションは別のファイル）
	AnimOnly,    // アニメーションだけ（メッシュのないモーション専用ファイル）
};

struct FModelSaveOptions
{
	EModelSaveType Type = EModelSaveType::Normal;
	// true なら小さくなる保存（法線 8bit・座標 16bit・アニメーション 16bit。DxLib の既定）。false なら精度を落とさない（大きい）
	bool bCompress = true;
};

// プラットフォームが持っている資源の全体の状況（0 は不明）
struct FBackendMemoryInfo
{
	int64 VideoMemoryTotal = 0;    // ビデオメモリの容量（バイト）
	int64 VideoMemoryUsed = 0;     // ビデオメモリの使用量
	int64 ProcessWorkingSet = 0;   // このプロセスが使っている物理メモリ
	int32 NumTextureHandles = 0;   // 生きているハンドルの数（アセット以外が作ったものも含む）
	int32 NumModelHandles = 0;
	int32 NumSoundHandles = 0;
	int32 NumFontHandles = 0;
};

class IAssetBackend
{
public:
	virtual ~IAssetBackend() = default;

	//-------------------------------------------------------------------------
	// 画像
	//-------------------------------------------------------------------------
	// 読み込む。bAsync なら、読み込みの完了を待たずにハンドルを返す（完了は IsLoading で調べる）。失敗したら InvalidHandle
	virtual int32 LoadTexture( const FString& Path, bool bAsync ) = 0;
	// ファイルの中身（BMP / PNG など）から作る
	virtual int32 CreateTextureFromMemory( const void* FileImage, usize Size ) = 0;
	// 元画像の矩形を共有する派生画像を作る。非対応の実装は InvalidHandle を返す。
	virtual int32 CreateTextureRegion( int32 /*Source*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/ ) { return NAsset::InvalidHandle; }
	// 3Dポリゴンへ渡す、派生領域の半texel内側UVを返す。
	virtual bool GetTextureUvBounds( int32 /*Texture*/, float32 ( & /*OutUV*/ )[4] ) { return false; }
	// 画像の大きさ。読み込み中・無効なハンドルなら false
	virtual bool GetTextureSize( int32 Texture, int32& OutWidth, int32& OutHeight ) = 0;
	virtual void DeleteTexture( int32 Texture ) = 0;

	//-------------------------------------------------------------------------
	// 3D モデル（アニメーションだけが入ったファイルも、モデルとして読み込む）
	//-------------------------------------------------------------------------
	virtual int32 LoadModel( const FString& Path, bool bAsync ) = 0;
	// 同じデータを共有する、別の状態（位置・アニメーション）を持つモデルを作る
	virtual int32 DuplicateModel( int32 Source ) = 0;
	virtual void DeleteModel( int32 Model ) = 0;

	// モデルが持つアニメーション。名前が無ければ空
	virtual int32 GetModelAnimCount( int32 Model ) = 0;
	virtual FString GetModelAnimName( int32 Model, int32 AnimIndex ) = 0;
	// アニメーションの長さ（実装の時間単位。DxLib ではフレーム数）。不明なら 0
	virtual float32 GetModelAnimLength( int32 Model, int32 AnimIndex ) = 0;
	// Model に、SourceModel（Model 自身でもよい）の AnimIndex 番のアニメーションを付ける。戻り値は付けた番号（失敗したら -1）
	virtual int32 AttachModelAnim( int32 Model, int32 AnimIndex, int32 SourceModel ) = 0;
	virtual void DetachModelAnim( int32 Model, int32 AttachIndex ) = 0;

	// 形の情報。メッシュが 0 個ならアニメーションだけのファイル（モーション専用）。読み込み中なら 0
	virtual int32 GetModelMeshCount( int32 Model ) = 0;
	// 静的モデルの面データを写す。非対応・失敗時は false とし、Outを変更しない。
	// 成功時はframe変換込み・world変換なしの位置とreference法線/UV0/頂点色を写し、三角形順を保つ。
	// sectionはindex列を隙間なく覆い、同一frame/mesh/materialの連続runだけをまとめる。
	// 照明・alpha test・UV変換等native描画の同等性は保証せず、借用画像handleを削除してはならない。
	// 借用handleは元Modelと同じbackendの寿命内だけ有効で、この関数はメインスレッドから呼ぶ。
	virtual bool GetStaticModelSurface( int32 /*Model*/, FStaticModelSurfaceData& /*Out*/ ) { return false; }
	// 指定材質の静的model面をbackend内の不変handleへ登録し、未対応・不正入力では-1を返す。
	// CPU値だけをコピーし、sourceや借用画像の所有権は呼出側が維持する。
	virtual int32 CreateModelMaterialGeometry( const FStaticModelSurfaceData& /*Surface*/, int32 /*MaterialIndex*/ ) { return -1; }
	// CreateModelMaterialGeometryで作った不変形状handleを解放する。呼出側はReleaseModelMaterialGeometryを使う。
	virtual void DeleteModelMaterialGeometry( int32 /*Geometry*/ ) {}
	// モデル全体のローカルでの大きさ（すべてのメッシュを囲む、軸に平行な箱）。メッシュが 0 個・読み込み中・分からないなら false
	virtual bool GetModelBounds( int32 /*Model*/, float32 ( & /*OutMin*/ )[3], float32 ( & /*OutMax*/ )[3] ) { return false; }
	// モデルの三角形（当たり判定用。モデルのローカル座標で、フレームの変換を適用した、描くときと同じ位置）。OutPositions は x, y, z の並び、OutIndices は 3 つで 1 三角形（DxLib の描画と同じ向き）。
	// メッシュが無い・読み込み中・分からないなら false
	virtual bool GetModelCollisionMesh( int32 /*Model*/, TArray<float32>& /*OutPositions*/, TArray<uint32>& /*OutIndices*/ ) { return false; }
	// 骨（フレーム）。アニメーションは名前が合う骨だけを動かすので、モデルとアニメーションの骨の名前が合うかの確認に使う
	virtual int32 GetModelFrameCount( int32 Model ) = 0;
	virtual FString GetModelFrameName( int32 Model, int32 FrameIndex ) = 0;
	// MV1 ファイルに保存する（Path は絶対パス。フォルダは作ってある前提）。成功したら true。
	// 使われていないアニメーションがあった、という警告つきの成功も true（OutWarning に理由）
	virtual bool SaveModelToMV1( int32 Model, const FString& Path, const FModelSaveOptions& Options, FString& OutWarning ) = 0;

	//-------------------------------------------------------------------------
	// メッシュ（頂点バッファ・インデックスバッファ。編集メッシュを描くため）
	//-------------------------------------------------------------------------
	// 頂点（位置・法線・画像の座標）と、三角形のインデックス（3 つで 1 つ）から、描画用のバッファを作る。成功したら true（OutVertexBuffer / OutIndexBuffer が有効）。
	// バッファは、描画コマンド（Mesh3D）が、ハンドルで使う。使い終わったら、DeleteVertexBuffer / DeleteIndexBuffer で解放する
	virtual bool CreateMeshBuffers( const FMeshVertex* /*Vertices*/, int32 /*NumVertices*/, const uint32* /*Indices*/, int32 /*NumIndices*/, int32& /*OutVertexBuffer*/, int32& /*OutIndexBuffer*/ ) { return false; }
	virtual void DeleteVertexBuffer( int32 /*VertexBuffer*/ ) {}
	virtual void DeleteIndexBuffer( int32 /*IndexBuffer*/ ) {}

	//-------------------------------------------------------------------------
	// フォント
	//-------------------------------------------------------------------------
	// ※ CreateFont は Windows のマクロ（CreateFontA / W）と衝突するので使わない
	virtual int32 MakeFont( const FFontDesc& Desc ) = 0;
	virtual void DeleteFont( int32 Font ) = 0;
	// 文字列の描画幅（ピクセル）。無効なハンドルなら 0
	virtual int32 MeasureText( int32 Font, const char* Text ) = 0;
	// フォントファイル（.ttf / .otf）をこのアプリの間だけ使えるようにする／やめる（フォント名で CreateFont できるようになる）
	virtual bool AddFontFile( const FString& Path ) = 0;
	virtual void RemoveFontFile( const FString& Path ) = 0;

	//-------------------------------------------------------------------------
	// サウンド
	//-------------------------------------------------------------------------
	virtual int32 LoadSound( const FString& Path, ESoundLoadMode Mode, bool bAsync ) = 0;
	// 同じデータを共有する、別々に再生できるハンドルを作る（Memory で読み込んだものだけ）
	virtual int32 DuplicateSound( int32 Source ) = 0;
	virtual void DeleteSound( int32 Sound ) = 0;
	// 音の長さ（秒）。読み込み中・不明なら負
	virtual float32 GetSoundLength( int32 Sound ) = 0;

	//-------------------------------------------------------------------------
	// メモリ
	//-------------------------------------------------------------------------
	virtual FBackendMemoryInfo GetMemoryInfo() = 0;
	// モデルが使っているメモリの推定（頂点・テクスチャなど。バイト）
	virtual int64 EstimateModelBytes( int32 Model ) = 0;
	// サウンドが使っているメモリの推定（Memory は展開後の大きさ、Stream は読み込み用の領域。バイト）
	virtual int64 EstimateSoundBytes( int32 Sound, ESoundLoadMode Mode ) = 0;

	//-------------------------------------------------------------------------
	// 非同期の読み込み
	//-------------------------------------------------------------------------
	// bAsync で読み込んだハンドルが、まだ読み込み中なら true（完了・失敗・同期の読み込みなら false）
	virtual bool IsLoading( int32 Handle ) = 0;
	// 読み込み中の数
	virtual int32 GetNumLoading() = 0;
};

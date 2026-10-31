#pragma once
#include "Library/Core/Collection/Array.h"
#include "Library/Core/Collection/String.h"
#include "Library/Core/Interfaces/Asset/FontTypes.h"
#include "Library/Core/Interfaces/Asset/StaticModelSurfaceData.h"
#include "Library/Core/Interfaces/Render/DrawCommand.h"
#include "Library/Core/Interfaces/Render/MeshTypes.h"
#include "Library/Core/Interfaces/Render/RenderTargetTypes.h"
#include "Library/Math/Color.h"
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
	EFontAntialias Antialias = EFontAntialias::Standard;   // 文字のなめらかさ
	int32 LetterSpacing = 0;    // 字間（ピクセル。全部の文字の幅に足す。負の値で詰める）
	int32 LineHeight = -1;      // 行の送り幅（ピクセル。次の行の同じ位置までの距離）。-1 はフォントの既定（1 行の文字の高さ）。0 以上を指定すると、その値
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
	// 描画先（レンダーターゲット）。描き込める画像。解放は DeleteTexture、大きさは GetTextureSize
	//-------------------------------------------------------------------------
	// 作る。Desc が作れない値（IsValid が false）・作れなかったときは InvalidHandle。非対応の実装も InvalidHandle
	virtual int32 CreateRenderTarget( const FRenderTargetDesc& /*Desc*/ ) { return NAsset::InvalidHandle; }
	// 描画先の矩形 (X, Y, Width, Height) の画素を、左上から右へ、上から下へ、OutPixels（Width x Height 個）に読む。
	// 呼ぶ側が、矩形を描画先の中に収めてある。透明度を持たない描画先の A は 255。描画先でない・読めなかったら false
	virtual bool ReadRenderTargetPixels( int32 /*Target*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// 描画先の矩形 (X, Y, Width, Height) を、Path のファイルに保存する。呼ぶ側が、矩形を描画先の中に収めてある。失敗したら false
	virtual bool SaveRenderTarget( int32 /*Target*/, int32 /*X*/, int32 /*Y*/, int32 /*Width*/, int32 /*Height*/, const FString& /*Path*/, const FImageSaveOptions& /*Options*/ ) { return false; }

	//-------------------------------------------------------------------------
	// CPU 側の画像（画素）との行き来。画素は FColor32（透明度は、乗算されていない普通のアルファ）で、左上から右へ、上から下へ並ぶ
	//-------------------------------------------------------------------------
	// 画素（Width x Height 個）から画像を作る。大きさが 1〜16384 の外・Pixels が nullptr・作れなかったときは InvalidHandle。解放は DeleteTexture
	virtual int32 CreateTextureFromPixels( int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/ ) { return NAsset::InvalidHandle; }
	// CreateTextureFromPixels で作った画像の中身を、画素に替える。画像と同じ大きさでなければ false。ハンドルは変わらない
	virtual bool UpdateTexturePixels( int32 /*Texture*/, int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/ ) { return false; }
	// ファイルの中身（PNG・BMP・JPEG など）を、画素に展開する。透明度を持たない形式は、不透明（A = 255）。壊れたデータ・読めない形式なら false
	virtual bool DecodeImage( const void* /*FileImage*/, usize /*Size*/, int32& /*OutWidth*/, int32& /*OutHeight*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// ファイル（フルパス）を、画素に展開する。読めなければ false
	virtual bool DecodeImageFile( const FString& /*Path*/, int32& /*OutWidth*/, int32& /*OutHeight*/, TArray<FColor32>& /*OutPixels*/ ) { return false; }
	// 画素（Width x Height 個）を、Path のファイルに保存する。呼ぶ側が、大きさを 1 以上にしてある。失敗したら false
	virtual bool SavePixels( int32 /*Width*/, int32 /*Height*/, const FColor32* /*Pixels*/, const FString& /*Path*/, const FImageSaveOptions& /*Options*/ ) { return false; }

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

	// 汎用の頂点バッファ・インデックスバッファ（CVertexBuffer / CIndexBuffer）。バッファは、メインスレッドだけで作り・書き換える。解放は DeleteVertexBuffer / DeleteIndexBuffer
	// 頂点 NumVertices 個の空の（0 で埋まった）頂点バッファを作る。作れなければ負（NAsset::InvalidHandle）
	virtual int32 MakeVertexBuffer( int32 /*NumVertices*/ ) { return -1; }
	// 頂点バッファの StartIndex から、頂点 NumVertices 個を書く。範囲が収まらない・無効なハンドルなら false
	virtual bool WriteVertexBuffer( int32 /*VertexBuffer*/, int32 /*StartIndex*/, const FDrawVertex3D* /*Vertices*/, int32 /*NumVertices*/ ) { return false; }
	// 添字 NumIndices 個の空のインデックスバッファを作る（b32Bit が true なら 32 ビット、false なら 16 ビット）。作れなければ負
	virtual int32 MakeIndexBuffer( int32 /*NumIndices*/, bool /*b32Bit*/ ) { return -1; }
	// インデックスバッファの StartIndex から、添字 NumIndices 個（バッファが 16 ビットなら uint16、32 ビットなら uint32 の並び）を書く。範囲が収まらない・無効なハンドルなら false
	virtual bool WriteIndexBuffer( int32 /*IndexBuffer*/, int32 /*StartIndex*/, const void* /*Indices*/, int32 /*NumIndices*/ ) { return false; }

	//-------------------------------------------------------------------------
	// フォント
	//-------------------------------------------------------------------------
	// ※ CreateFont は Windows のマクロ（CreateFontA / W）と衝突するので使わない
	virtual int32 MakeFont( const FFontDesc& Desc ) = 0;
	virtual void DeleteFont( int32 Font ) = 0;
	// 文字列の描画幅（ピクセル）。無効なハンドルなら 0
	virtual int32 MeasureText( int32 Font, const char* Text ) = 0;
	// フォントの寸法。無効なハンドル・非対応の実装は false
	virtual bool GetFontMetrics( int32 /*Font*/, FFontMetrics& /*OutMetrics*/ ) { return false; }
	// 文字列の大きさ（幅・高さ・行数。\n で複数行）。無効なハンドル・非対応の実装は false
	virtual bool MeasureTextSize( int32 /*Font*/, const char* /*Text*/, FTextSize& /*OutSize*/ ) { return false; }
	// 文字 1 つずつの位置と幅（改行も 1 つ）。壊れた UTF-8 は、実装が決めた区切りになる（呼び出す前に、正しい UTF-8 にする）。無効なハンドル・非対応の実装は false
	virtual bool GetTextGlyphs( int32 /*Font*/, const char* /*Text*/, TArray<FTextGlyph>& /*OutGlyphs*/ ) { return false; }
	// その名前のフォントが、この環境にあるか（Face は UTF-8）
	virtual bool IsFontAvailable( const FString& /*Face*/ ) { return false; }
	// この環境にあるフォントの名前の一覧
	virtual void GetFontNames( TArray<FString>& /*OutNames*/ ) {}
	// 指定の 1 文字（Character は UTF-8 の 1 文字）の代わりに、画像 Image を描く（アイコンなどを、文字列の中に入れる）。
	// (DrawX, DrawY) は画像の描く位置のずれ、Advance は次の文字までの幅。登録できたら true
	virtual bool AddFontCharImage( int32 /*Font*/, const char* /*Character*/, int32 /*Image*/, int32 /*DrawX*/, int32 /*DrawY*/, int32 /*Advance*/ ) { return false; }
	virtual void RemoveFontCharImage( int32 /*Font*/, const char* /*Character*/ ) {}
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

#pragma once
#include <cstring>
#include <type_traits>

#include "Library/Core/Types/TypeDefinition.h"

//=============================================================================
// 描画コマンド（描画の「記録」）
//   描画を「今すぐ DxLib を呼ぶ」のではなく、「何を描くか」というデータ（描画コマンド）として溜めておき、あとでまとめて実行する。
//
//   なぜ: DxLib の描画関数は、複数のスレッドから同時に呼べない（描画先・カメラ・ブレンドモード・頂点の溜め場所がプロセスに 1 つで、
//   DX_THREAD_SAFE も無効。Direct3D 11 のデバイスコンテキストも 1 つのスレッドからしか使えない。モデル（MV1）の行列や
//   アニメーション時間もハンドルに書き込む状態）。
//   一方、「何を描くか」を決める計算（ワールド座標からの変換・視錐台カリング・LOD の選択・行列の合成・アニメーション時間・
//   文字列の整形）は、スレッドごとに独立に行える。
//
//     記録（並列、ジョブのワーカーで）: 描画システム（FDrawContext） -> 描画コマンドの並び
//     実行（直列、メインスレッドだけ）: IDrawExecutor が、記録された順番に DxLib を呼ぶ
//
//   実行の順番は記録の順番そのまま（描画システム自身が決める順序で、スレッドの数や実行の順番には依存しない）なので、
//   重なり（2D の前後・3D の半透明の順番）も含めた描画結果は、ふつうに DxLib を呼んだときと同じになる。
//
//   形式: 4 バイト(1 ワード)単位の並び。コマンド 1 つ = ヘッダ 1 ワード + 種類ごとのデータ（FDraw...Data）。
//     ヘッダ = 種類(8bit) | フラグ(8bit) | コマンド全体のワード数(16bit)
//     種類ごとに必要な大きさだけ使うので、2D の四角形は 28 バイト、3D のモデル（行列つき）は 64 バイト。文字列はコマンドの後ろに続ける。
//=============================================================================

enum class EDrawCommand : uint8
{
	// 2D（画面座標）
	Box,        // FDrawBoxData。Fill なら塗りつぶし
	Circle,     // FDrawCircleData。Fill なら塗りつぶし
	Line,       // FDrawLineData
	Text,       // FDrawTextData + 文字列（'\0' 終端、4 バイト境界まで 0 で埋める）
	Sprite,     // FDrawSpriteData。FlipX / FlipY
	Blend,      // FDrawBlendData。以降のブレンドモードを変える
	// 3D（ワールド座標。カメラ・ライト・Zバッファは、呼び出し側が決めたものを使う）
	Line3D,         // FDrawLine3DData
	Triangle3D,     // FDrawTriangle3DData。Fill なら塗りつぶし
	Sphere3D,       // FDrawSphere3DData。Fill なら塗りつぶし
	Capsule3D,      // FDrawCapsule3DData。Fill なら塗りつぶし
	Cube3D,         // FDrawCube3DData（軸に平行な直方体）。Fill なら塗りつぶし
	Billboard3D,    // FDrawBillboard3DData。FlipX / FlipY
	WorldMatrix,    // FDrawMatrixData。以降の 3D 図形（Line3D〜Cube3D）のローカル -> ワールド変換（SetTransformToWorld）
	Model3D,        // FDrawModelData。MV1 モデルを、行列をセットして描画する（アニメーション時間も指定できる）
	// 追加（上のコマンドの形式は変えていない）
	SpriteRect,     // FDrawSpriteRectData。画像の一部分（スプライトシートの 1 コマなど）を、拡大率・回転・基準点を指定して描く。FlipX / FlipY
	Bright,         // FDrawBrightData。以降の描画に掛ける色（SetDrawBright。白 = そのまま）
	TextFont,       // FDrawTextFontData + 文字列。フォントの指定・縁取り・左右の位置揃え（AlignCenter / AlignRight）ができる文字列
	ModelAnim,      // FDrawModelAnimData。次の Model3D で描くモデルに付けたアニメーション 1 つの、再生時間とブレンド率
	Quad3D,         // FDrawQuad3DData。画像を貼った 3D の四角形（カメラの方を向くビルボード・Y 軸だけ回るビルボード・向きが固定の板）
	Mesh3D,         // FDrawMeshData。頂点バッファ・インデックスバッファの三角形（編集メッシュ）。行列をセットして描く。TwoSided なら、裏も描く
	Ribbon3D,       // FDrawRibbonData + 経路の点。カメラへ面を向ける帯。既存の命令番号は変更しない
	MaterialQuad3D, // FDrawMaterialQuadData。画像とマスクを使う材質付きの 3D 四角形
	MaterialRibbon3D, // FDrawMaterialRibbonData + 材質付き経路の点。画像とマスクを使う 3D の帯
	DistortedMaterialQuad3D, // FDrawDistortedMaterialQuadData。ノイズ画像でUVをゆがめる材質付き四角形
	// FDrawDistortedMaterialRibbonDataと点ごとの変位値で、材質付き帯のUVをゆがめる。
	DistortedMaterialRibbon3D,
	// FDrawMaterialMeshDataと頂点・indexを所有コピーした材質メッシュ。
	MaterialMesh3D,
	// FDrawModelMaterialDataを使い、選択材質以外の通常描画も保つモデル命令。
	ModelMaterial3D,
};

// Quad3D の向きの決め方
enum class EDrawQuadOrient : int32
{
	Camera,    // 実行のときのカメラの正面を向く（画面に平ら）。Angle は視線まわりの回転
	CameraY,   // Y 軸だけ回って、実行のときのカメラの方を向く（傾かない）
	Fixed,     // Right / Up の向きのまま
};

// 文字列の位置揃え（TextFont）。X が、左端 / 中央 / 右端になる
enum class EDrawTextAlign : uint8
{
	Left,
	Center,
	Right,
};

enum class EDrawBlend : int32
{
	Opaque,     // ブレンドしない（既定）
	Alpha,      // アルファブレンド（Param = 不透明度 0..255）
	Add,        // 加算
	Subtract,   // 減算
	Multiply,   // 乗算
};

struct FDrawFlag
{
	static constexpr uint8 Fill = 1 << 0;    // 塗りつぶし
	static constexpr uint8 FlipX = 1 << 1;   // 左右反転
	static constexpr uint8 FlipY = 1 << 2;   // 上下反転
	static constexpr uint8 AlignCenter = 1 << 3;   // TextFont: X が文字列の中央
	static constexpr uint8 AlignRight = 1 << 4;    // TextFont: X が文字列の右端
	static constexpr uint8 TwoSided = 1 << 5;      // Mesh3D: 裏も描く
	static constexpr uint8 UseRegularCulling = 1 << 6; // 材質mesh: 通常メッシュの裏面除去設定を使う
};

//-----------------------------------------------------------------------------
// 種類ごとのデータ（すべてのメンバが 4 バイトの倍数で、隙間が無い。コマンドの並びを、ビット単位で比べられるように）
//-----------------------------------------------------------------------------
struct FDrawRgb
{
	uint8 R = 255;
	uint8 G = 255;
	uint8 B = 255;
	uint8 Unused = 0;
};

struct FDrawBoxData { int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0; int32 Thickness = 1; FDrawRgb Color; };
struct FDrawCircleData { int32 X = 0, Y = 0, Radius = 0; int32 Thickness = 1; FDrawRgb Color; };
struct FDrawLineData { int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0; int32 Thickness = 1; FDrawRgb Color; };
struct FDrawTextData { int32 X = 0, Y = 0; FDrawRgb Color; int32 Length = 0; /* 文字列が続く */ };
struct FDrawSpriteData { float32 X = 0.0f, Y = 0.0f, Scale = 1.0f, Angle = 0.0f; int32 Handle = -1; };
struct FDrawBlendData { int32 Mode = 0; int32 Param = 255; };

struct FDrawLine3DData { float32 P0[3] = {}, P1[3] = {}; FDrawRgb Color; };
struct FDrawTriangle3DData { float32 P0[3] = {}, P1[3] = {}, P2[3] = {}; FDrawRgb Color; };
struct FDrawSphere3DData { float32 Center[3] = {}; float32 Radius = 0.0f; int32 Div = 16; FDrawRgb Diffuse, Specular; };
struct FDrawCapsule3DData { float32 P0[3] = {}, P1[3] = {}; float32 Radius = 0.0f; int32 Div = 16; FDrawRgb Diffuse, Specular; };
struct FDrawCube3DData { float32 Min[3] = {}, Max[3] = {}; FDrawRgb Diffuse, Specular; };
struct FDrawBillboard3DData { float32 Pos[3] = {}; float32 Size = 1.0f; float32 Angle = 0.0f; int32 Handle = -1; };
// アフィン変換（DxLib の MATRIX の m[0..3][0..2]。最後の列は 0, 0, 0, 1 と決まっている）
struct FDrawMatrixData { float32 M[12] = {}; };
struct FDrawModelData { int32 Handle = -1; float32 M[12] = {}; int32 AnimIndex = -1; float32 AnimTime = 0.0f; };

// 画像 Handle の矩形 (SrcX, SrcY, SrcW, SrcH) を、矩形の中の点 (PivotX, PivotY)（矩形の左上が原点、ピクセル）が画面の (X, Y) に来るように描く。
// 反転（FlipX / FlipY）は、この基準点を軸にする（反転しても基準点は (X, Y) のまま）。回転も基準点まわり
struct FDrawSpriteRectData
{
	float32 X = 0.0f, Y = 0.0f;
	float32 ScaleX = 1.0f, ScaleY = 1.0f;
	float32 Angle = 0.0f;
	float32 PivotX = 0.0f, PivotY = 0.0f;
	int32 Handle = -1;
	int32 SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0;
};
struct FDrawBrightData { FDrawRgb Color; };
// Text と同じ並びの先頭に、フォントと縁取りの色を足したもの。文字列が続く
struct FDrawTextFontData { int32 X = 0, Y = 0; FDrawRgb Color; int32 Length = 0; int32 Font = -1; FDrawRgb EdgeColor; };
struct FDrawModelAnimData { int32 Handle = -1; int32 AttachIndex = -1; float32 Time = 0.0f; float32 Rate = 1.0f; };

struct FDrawRgba
{
	uint8 R = 255;
	uint8 G = 255;
	uint8 B = 255;
	uint8 A = 255;
};

// 画像 Handle の範囲 (U0, V0)-(U1, V1)（0..1。左上が 0, 0）を貼った、ワールドの四角形。大きさは Width x Height（ワールドの長さ）。
// (PivotX, PivotY)（画像の中の点。0..1、左上が 0, 0）が、Pos に来る。
// Orient が Fixed のとき、四角形の横は Right、縦（上）は Up（どちらも長さ 1）。Camera / CameraY のときは、実行のときのカメラから決めるので、Right / Up は使わない。
// 色 Color は、画像に掛かる色（A は不透明度）。両面が見える（背面のカリングはしない）。照明は、呼び出し側が決めたものに従う
struct FDrawQuad3DData
{
	float32 Pos[3] = {};
	float32 Right[3] = { 1.0f, 0.0f, 0.0f };
	float32 Up[3] = { 0.0f, 1.0f, 0.0f };
	float32 Width = 1.0f, Height = 1.0f;
	float32 PivotX = 0.5f, PivotY = 0.5f;
	float32 Angle = 0.0f;   // Camera のときの、視線まわりの回転（ラジアン）。正の向きは、オブジェクトを視線の軸まわりに正の Roll で回したときと同じ（右向きが上へ傾く）
	float32 U0 = 0.0f, V0 = 0.0f, U1 = 1.0f, V1 = 1.0f;
	int32 Handle = -1;
	int32 Orient = 0;       // EDrawQuadOrient
	FDrawRgba Color;
};


// 頂点バッファ VertexBuffer と、インデックスバッファ IndexBuffer（どちらも IAssetBackend が作ったもの）の、StartIndex から NumIndices 個のインデックス（3 の倍数）の三角形を描く。
// NumVertices は、使う頂点の数（頂点バッファの先頭から）。M はローカル -> ワールドの変換（アフィン変換。法線も、この行列で変わる）。
// Texture は画像のハンドル（負なら、画像なし）。色は、頂点の色（バッファに入っている）。前の状態（カリング）は、描いたあとに元に戻る（行列は、Execute の終わりで単位行列に戻る）
struct FDrawMeshData
{
	int32 VertexBuffer = -1;
	int32 IndexBuffer = -1;
	int32 NumVertices = 0;
	int32 StartIndex = 0;
	int32 NumIndices = 0;
	int32 Texture = -1;
	float32 M[12] = {};
};

#define DRAW_DATA_CHECK( Type ) \
	static_assert( sizeof( Type ) % 4 == 0 && std::is_trivially_copyable_v<Type>, #Type " must be made of 4-byte words with no padding" )
DRAW_DATA_CHECK( FDrawBoxData );
DRAW_DATA_CHECK( FDrawCircleData );
DRAW_DATA_CHECK( FDrawLineData );
DRAW_DATA_CHECK( FDrawTextData );
DRAW_DATA_CHECK( FDrawSpriteData );
DRAW_DATA_CHECK( FDrawBlendData );
DRAW_DATA_CHECK( FDrawLine3DData );
DRAW_DATA_CHECK( FDrawTriangle3DData );
DRAW_DATA_CHECK( FDrawSphere3DData );
DRAW_DATA_CHECK( FDrawCapsule3DData );
DRAW_DATA_CHECK( FDrawCube3DData );
DRAW_DATA_CHECK( FDrawBillboard3DData );
DRAW_DATA_CHECK( FDrawMatrixData );
DRAW_DATA_CHECK( FDrawModelData );
DRAW_DATA_CHECK( FDrawSpriteRectData );
DRAW_DATA_CHECK( FDrawBrightData );
DRAW_DATA_CHECK( FDrawTextFontData );
DRAW_DATA_CHECK( FDrawModelAnimData );
DRAW_DATA_CHECK( FDrawQuad3DData );
DRAW_DATA_CHECK( FDrawMeshData );
#undef DRAW_DATA_CHECK
static_assert( sizeof( FDrawBoxData ) == 24 && sizeof( FDrawSphere3DData ) == 28 && sizeof( FDrawModelData ) == 60, "unexpected padding in a draw command data type" );

//-----------------------------------------------------------------------------
// コマンドの並びの読み書き
//-----------------------------------------------------------------------------
namespace NDrawCommand
{
	// コマンド 1 つの最大の大きさ（ヘッダの 16bit に収まる範囲。長すぎる文字列は切る）
	constexpr int32 MaxWords = 0xFFFF;

	FORCEINLINE uint32 MakeHeader( EDrawCommand Type, uint8 Flags, int32 NumWords )
	{
		return static_cast<uint32>( Type ) | ( static_cast<uint32>( Flags ) << 8 ) | ( static_cast<uint32>( NumWords ) << 16 );
	}
}

// 連続した描画コマンドの並び（1 つのオブジェクトの 1 つの描画システムの出力）
struct FDrawSpan
{
	const uint32* Words = nullptr;
	int32 NumWords = 0;
};

// コマンド 1 つの読み取り用の窓口
struct FDrawCommandView
{
	EDrawCommand Type = EDrawCommand::Box;
	uint8 Flags = 0;
	const uint32* Data = nullptr;   // ヘッダの次のワード
	int32 NumDataWords = 0;

	FORCEINLINE bool Has( uint8 Flag ) const { return ( Flags & Flag ) != 0; }

	template<typename TData>
	FORCEINLINE TData Read() const
	{
		TData Value;
		std::memcpy( &Value, Data, sizeof( TData ) );
		return Value;
	}

	// Text コマンドの文字列（'\0' 終端）
	FORCEINLINE const char* GetTextChars() const { return reinterpret_cast<const char*>( Data + sizeof( FDrawTextData ) / 4 ); }
	// TextFont コマンドの文字列（'\0' 終端）
	FORCEINLINE const char* GetFontTextChars() const { return reinterpret_cast<const char*>( Data + sizeof( FDrawTextFontData ) / 4 ); }
};

// Span のコマンドを先頭から順に Function( const FDrawCommandView& ) へ渡す
template<typename TFunction>
void ForEachDrawCommand( const FDrawSpan& Span, TFunction&& Function )
{
	const uint32* Word = Span.Words;
	const uint32* const End = Span.Words + Span.NumWords;
	while ( Word < End )
	{
		const uint32 Header = *Word;
		const int32 NumWords = static_cast<int32>( Header >> 16 );
		if ( NumWords < 1 || Word + NumWords > End ) break;   // 壊れた並び（起きないはず）

		FDrawCommandView View;
		View.Type = static_cast<EDrawCommand>( Header & 0xFFu );
		View.Flags = static_cast<uint8>( ( Header >> 8 ) & 0xFFu );
		View.Data = Word + 1;
		View.NumDataWords = NumWords - 1;
		Function( View );
		Word += NumWords;
	}
}

//=============================================================================
// IDrawExecutor
//   描画コマンドを、実際に描画する。メインスレッドだけから呼ばれる。
//
//   Execute: Span のコマンドを、先頭から順に実行する。
//     - Span の中で変えた状態（ブレンドモード・WorldMatrix）は、Span の終わりで元（ブレンドなし・単位行列）に戻る
//       （次のオブジェクトに漏れない）
//     - 描画先・カメラ・ライト・Zバッファ・フォントなどは触らない（呼び出し側が決めたものをそのまま使う）
//   実装: CDxLibDrawExecutor（DxLib）。確認用に、記録するだけの実装を作ってもよい。
//=============================================================================
class IDrawExecutor
{
public:
	virtual ~IDrawExecutor() = default;

	virtual void Execute( const FDrawSpan& Span ) = 0;
	// 実行先がネイティブ材質シェーダーを安全に扱えるかを返す。未対応の実行先は従来どおり false。
	virtual bool SupportsEffectMaterials() const { return false; }
	// 実行先が材質付きの帯を描画できるかを返す。未対応の実行先は従来どおり false。
	virtual bool SupportsEffectMaterialRibbons() const { return false; }
	// 実行先がノイズ画像によるUVゆがみを描画できるかを返す。未対応の実行先は従来どおり false。
	virtual bool SupportsEffectUVDistortion() const { return false; }
	// 実行先がノイズ画像による材質帯のUVゆがみを描画できるかを返す。未対応の実行先は false。
	virtual bool SupportsEffectRibbonUVDistortion() const { return false; }
	// 実行先が材質付き頂点メッシュを描画できるかを返す。未対応の実行先は false。
	virtual bool SupportsEffectMaterialMesh() const { return false; }
	// ModelMaterial3Dでモデル内の選択材質へ効果を描けるか返す。
	virtual bool SupportsModelMaterialEffects() const { return false; }
};

#pragma once

#include "Library/Core/Memory/Memory.h"
#include "Library/Core/Types/TypeDefinition.h"
#include "Library/Core/Types/TypeTraits.h"

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
	// 2D の図形の追加（上のコマンドの形式は変えていない。座標は float32 で持ち、なめらかでない描き方では実行のときに四捨五入する）
	Pixel,          // FDrawPixelData
	Triangle,       // FDrawTriangleData。Fill なら塗りつぶし。AntiAlias ならなめらかに描く（DrawTriangleAA。Thickness は枠の太さ）
	Quad,           // FDrawQuadData。Fill / AntiAlias は Triangle と同じ
	Oval,           // FDrawOvalData。Fill / AntiAlias は Triangle と同じ
	RoundBox,       // FDrawRoundBoxData。Fill / AntiAlias は Triangle と同じ
	BoxAA,          // FDrawBoxAAData。Fill なら塗りつぶし（DrawBoxAA）
	CircleAA,       // FDrawCircleAAData。Fill なら塗りつぶし（DrawCircleAA）
	LineAA,         // FDrawLineAAData（DrawLineAA）
	// 2D の画像の描画の追加（上のコマンドの形式は変えていない。座標は float32、画像は Handle で指定する）
	Image,            // FDrawImageData。FlipX / FlipY（DrawGraphF / DrawReverseGraphF）
	ImageStretch,     // FDrawImageStretchData（DrawExtendGraphF）
	ImageQuad,        // FDrawImageQuadData（DrawModiGraphF）
	ImagePart,        // FDrawImagePartData。FlipX / FlipY（DrawRectGraphF）
	ImagePartStretch, // FDrawImagePartStretchData（DrawRectExtendGraphF）
	ImagePartQuad,    // FDrawImagePartQuadData（DrawRectModiGraphF）
	SpritePivot,      // FDrawSpritePivotData。FlipX / FlipY（DrawRotaGraph3F）
	Tile,             // FDrawTileData（DrawTile）
	CircleGauge,      // FDrawCircleGaugeData。FlipX / FlipY（DrawCircleGaugeF）
	// 描画状態の追加（上のコマンドの形式は変えていない。どれも、オブジェクトの分（1 つの Execute）が終わると、元に戻る）
	CustomBlend,      // FDrawCustomBlendData（SetDrawCustomBlendMode）
	AddColor,         // FDrawAddColorData（SetDrawAddColor。描く色に足す色）
	AlphaTest,        // FDrawAlphaTestData（SetDrawAlphaTest）
	Filter,           // FDrawFilterData（SetDrawMode）
	PushClip,         // FDrawClipData。いまのクリップ範囲との共通部分を、新しい範囲にする（SetDrawArea）
	PopClip,          // FDrawPopClipData。1 つ前のクリップ範囲に戻す
	Transform2D,      // FDrawMatrixData。2D の描画に掛かる変換（SetTransformTo2D）
	// 描画先の追加（上のコマンドの形式は変えていない。描画先は、実行の終わりで元に戻る）
	PushTarget,       // FDrawTargetData。描画先を、描き込める画像（レンダーターゲット）に替える（SetDrawScreen）。替えた先は、クリップなし・2D の変換なしから始まる
	PopTarget,        // FDrawPopTargetData。1 つ前の描画先に戻す（クリップ・2D の変換も、替える前に戻る）
	Clear,            // FDrawClearData。いまの描画先の全体を、色（アルファつき）で塗りつぶす（ClearDrawScreen。クリップ範囲に関係なく全体）
	CopyScreen,       // FDrawCopyScreenData。いまの描画先の矩形を、画像（矩形と同じ大きさ）へ写す（GetDrawScreenGraph）
	// 画像の処理とマスクの追加（上のコマンドの形式は変えていない。画像の処理は、描画先・クリップ・ブレンドなどの状態に関係しない。マスクは、実行の終わりで元に戻る）
	ImageFilter,      // FDrawImageFilterData。画像にフィルターを掛ける（GraphFilter / GraphFilterBlt / GraphFilterRectBlt）
	ImageBlend,       // FDrawImageBlendData。2 枚の画像を混ぜて、画像へ出す（GraphBlendBlt / GraphBlend）
	PushMask,         // FDrawMaskData。以降の描画を、マスクの画像の値で薄める（SetMaskScreenGraph）。替えた先の描画先は、マスクなしから始まる
	PopMask,          // FDrawPopMaskData。1 つ前のマスクに戻す
	// 文字の追加（上のコマンドの形式は変えていない）
	TextStyled,       // FDrawTextStyledData + 文字列。拡大・回転・縦書き・小数の位置・基準点つきの文字列（DrawRotaStringFToHandle）
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
	// 追加（既存の値は変えない）
	Invert,         // 描く色を反転する（DX_BLENDMODE_INVSRC）
	MultiplyAlpha,  // アルファ値を考慮した乗算（DX_BLENDMODE_MULA）
	PmaAlpha,       // 乗算済みアルファのアルファブレンド（DX_BLENDMODE_PMA_ALPHA）
	PmaAdd,         // 乗算済みアルファの加算（DX_BLENDMODE_PMA_ADD）
	PmaSubtract,    // 乗算済みアルファの減算（DX_BLENDMODE_PMA_SUB）
	PmaInvert,      // 乗算済みアルファの反転（DX_BLENDMODE_PMA_INVSRC）
	Custom,         // CustomBlend で決めたもの（FDrawContext::GetBlendMode が返すだけで、Blend には渡せない）
};

// カスタムブレンドの係数（DX_BLEND_*。Src = これから描く色、Dest = 描き先にある色。係数を掛けた結果を、EDrawBlendOp で組み合わせる）
enum class EDrawBlendFactor : int32
{
	Zero,           // 0
	One,            // 1
	SrcColor,       // Src の色
	InvSrcColor,    // 1 - Src の色
	SrcAlpha,       // Src のアルファ
	InvSrcAlpha,    // 1 - Src のアルファ
	DestColor,      // Dest の色
	InvDestColor,   // 1 - Dest の色
	DestAlpha,      // Dest のアルファ
	InvDestAlpha,   // 1 - Dest のアルファ
	SrcAlphaSat,    // min( Src のアルファ, 1 - Dest のアルファ )
};

// カスタムブレンドの組み合わせ方（DX_BLENDOP_*）
enum class EDrawBlendOp : int32
{
	Add,              // Src x 係数 + Dest x 係数
	Subtract,         // Src x 係数 - Dest x 係数
	ReverseSubtract,  // Dest x 係数 - Src x 係数
	Min,              // 小さいほう
	Max,              // 大きいほう
};

// アルファテストの比べ方（DX_CMP_*。描く色のアルファ値 と 基準値を比べて、成り立つ画素だけ描く）
enum class EDrawCompare : int32
{
	Never,          // 描かない
	Less,           // アルファ < 基準値
	Equal,          // アルファ == 基準値
	LessEqual,      // アルファ <= 基準値
	Greater,        // アルファ > 基準値
	NotEqual,       // アルファ != 基準値
	GreaterEqual,   // アルファ >= 基準値
	Always,         // 常に描く
};

// 画像を拡大・縮小・回転して描くときの、色の決め方（DX_DRAWMODE_*）
enum class EDrawFilter : int32
{
	Nearest,        // 一番近い画素の色（ギザギザ。既定）
	Bilinear,       // 周りの 4 画素をなめらかに混ぜる
	Anisotropic,    // 異方性フィルタリング（斜めから見たときも、なめらか）
};

// カスタムブレンドの設定（DxLib の SetDrawCustomBlendMode）。既定は、Src をそのまま描く（ブレンドしない）設定
struct FCustomBlend
{
	EDrawBlendFactor SrcColor = EDrawBlendFactor::One;
	EDrawBlendFactor DestColor = EDrawBlendFactor::Zero;
	EDrawBlendOp ColorOp = EDrawBlendOp::Add;
	EDrawBlendFactor SrcAlpha = EDrawBlendFactor::One;
	EDrawBlendFactor DestAlpha = EDrawBlendFactor::Zero;
	EDrawBlendOp AlphaOp = EDrawBlendOp::Add;
	// ブレンドのパラメータ（0..255。SrcAlpha を使う設定では、Src のアルファに掛かる）
	int32 Param = 255;
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
	static constexpr uint8 AntiAlias = 1 << 7;     // Triangle / Quad / Oval / RoundBox: なめらかに描く（座標を四捨五入しない）
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

// 2D の図形（座標は画面のピクセル、左上が原点）。Thickness は枠の太さ（塗りつぶしでは使わない。AntiAlias でない Triangle / Quad / RoundBox にも無い）。
// NumSegments は円・楕円・角の丸みを何角形で近似するか（0 なら、半径から決める。AntiAlias のときだけ使う）
struct FDrawPixelData { int32 X = 0, Y = 0; FDrawRgb Color; };
struct FDrawTriangleData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f; float32 Thickness = 1.0f; FDrawRgb Color; };
struct FDrawQuadData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, X2 = 0.0f, Y2 = 0.0f, X3 = 0.0f, Y3 = 0.0f; float32 Thickness = 1.0f; FDrawRgb Color; };
struct FDrawOvalData { float32 X = 0.0f, Y = 0.0f, Rx = 0.0f, Ry = 0.0f; float32 Thickness = 1.0f; int32 NumSegments = 0; FDrawRgb Color; };
struct FDrawRoundBoxData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f, Rx = 0.0f, Ry = 0.0f; float32 Thickness = 1.0f; int32 NumSegments = 0; FDrawRgb Color; };
struct FDrawBoxAAData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f; float32 Thickness = 1.0f; FDrawRgb Color; };
struct FDrawCircleAAData { float32 X = 0.0f, Y = 0.0f, Radius = 0.0f; float32 Thickness = 1.0f; int32 NumSegments = 0; FDrawRgb Color; };
struct FDrawLineAAData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f; float32 Thickness = 1.0f; FDrawRgb Color; };

// 2D の画像の描画。Handle は画像のハンドル。座標はピクセル（左上が原点）。
// ImageQuad / ImagePartQuad の 4 つの角は、画像の 左上・右上・右下・左下 が行く先（DrawModiGraph と同じ順）
struct FDrawImageData { float32 X = 0.0f, Y = 0.0f; int32 Handle = -1; };
struct FDrawImageStretchData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f; int32 Handle = -1; };
struct FDrawImageQuadData { float32 X[4] = {}, Y[4] = {}; int32 Handle = -1; };
struct FDrawImagePartData { float32 X = 0.0f, Y = 0.0f; int32 SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0; int32 Handle = -1; };
struct FDrawImagePartStretchData { float32 X0 = 0.0f, Y0 = 0.0f, X1 = 0.0f, Y1 = 0.0f; int32 SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0; int32 Handle = -1; };
struct FDrawImagePartQuadData { float32 X[4] = {}, Y[4] = {}; int32 SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0; int32 Handle = -1; };
// 画像の中の点 (PivotX, PivotY)（左上が原点、ピクセル）が画面の (X, Y) に来る。拡大・回転・反転は、その点を軸にする（反転しても、その点は (X, Y) のまま）
struct FDrawSpritePivotData { float32 X = 0.0f, Y = 0.0f, PivotX = 0.0f, PivotY = 0.0f, ScaleX = 1.0f, ScaleY = 1.0f, Angle = 0.0f; int32 Handle = -1; };
// 矩形 (X0, Y0)-(X1, Y1) を、画像の繰り返しで埋める。OffsetX / OffsetY はタイルの並びのずれ（ピクセル）、Scale は画像の拡大率、Angle は画像の回転（ラジアン）
struct FDrawTileData { int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0, OffsetX = 0, OffsetY = 0; float32 Scale = 1.0f, Angle = 0.0f; int32 Handle = -1; };
// 円グラフのように、画像を、真上から時計回りに Percent（0..100）まで描く。(X, Y) は画像の中心。StartPercent（0..100）は描き始める位置
struct FDrawCircleGaugeData { float32 X = 0.0f, Y = 0.0f, Percent = 100.0f, StartPercent = 0.0f, Scale = 1.0f; int32 Handle = -1; };

// 描画状態。値は、EDrawBlendFactor / EDrawBlendOp / EDrawCompare / EDrawFilter の数値
struct FDrawCustomBlendData { int32 SrcColor = 1, DestColor = 0, ColorOp = 0, SrcAlpha = 1, DestAlpha = 0, AlphaOp = 0, Param = 255; };
struct FDrawAddColorData { int32 R = 0, G = 0, B = 0; };
// Enabled が 0 なら、アルファテストをしない（Mode・Reference は使わない）
struct FDrawAlphaTestData { int32 Enabled = 0, Mode = 0, Reference = 0; };
struct FDrawFilterData { int32 Mode = 0; };
// クリップ範囲 (X0, Y0)-(X1, Y1)。右と下の端は含まない
struct FDrawClipData { int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0; };
// PopClip は中身を持たない（命令の大きさを 4 バイトの倍数にするため、1 ワードだけ持つ）
struct FDrawPopClipData { int32 Unused = 0; };
// 描画先。Handle は、描画先にできる画像（CRenderTarget）のハンドル
struct FDrawTargetData { int32 Handle = -1; };
// PopTarget は中身を持たない（命令の大きさを 4 バイトの倍数にするため、1 ワードだけ持つ）
struct FDrawPopTargetData { int32 Unused = 0; };
// 描画先を塗りつぶす色（各 0..255）
struct FDrawClearData { int32 R = 0, G = 0, B = 0, A = 0; };
// いまの描画先の矩形 (X0, Y0)-(X1, Y1)（右と下の端は含まない）を、画像 Handle へ写す
struct FDrawCopyScreenData { int32 X0 = 0, Y0 = 0, X1 = 0, Y1 = 0; int32 Handle = -1; };

// 画像のフィルターの種類（GraphFilter の DX_GRAPH_FILTER_*。既存の値は変えない）
enum class EDrawGraphFilter : int32
{
	Mono,             // 単色化（青色差・赤色差でずらせる）
	Gauss,            // ぼかし
	DownScale,        // 縮小（変換先は、元の 1 / Divisor の大きさ）
	BrightClip,       // 明るさでクリップ
	BrightScale,      // 指定の明るさの範囲を、全体に広げる
	Hsb,              // 色相・彩度・明るさ
	Invert,           // 階調の反転
	Level,            // レベル補正
	TwoColor,         // 2 階調化
	GradientMap,      // グラデーションマップ
	Replacement,      // 色の置換
	PremulAlpha,      // 乗算されていないアルファの画像を、乗算済みアルファにする
	InterpAlpha,      // 乗算済みアルファの画像を、乗算されていないアルファにする
	BicubicScale,     // バイキュービックで、指定の大きさへ拡大・縮小
	Lanczos3Scale,    // Lanczos-3 で、指定の大きさへ拡大・縮小
	FloatColorScale,  // 色の成分ごとに、引いてから掛ける
};

// 2 枚の画像の混ぜ方（GraphBlend の DX_GRAPH_BLEND_*。既存の値は変えない）
enum class EDrawGraphBlend : int32
{
	Normal,             // 通常（混ぜる画像のアルファで混ざる。出力のアルファは元の画像のもの）
	RgbaSelectMix,      // 出力の R / G / B / A を、2 枚の画像の成分から選ぶ（EDrawRgbaSelect）
	Multiply,           // 乗算
	Difference,         // 減算
	Add,                // 加算
	Screen,             // スクリーン
	Overlay,            // オーバーレイ
	Dodge,              // 覆い焼き
	Burn,               // 焼き込み
	Darken,             // 比較（暗）
	Lighten,            // 比較（明）
	SoftLight,          // ソフトライト
	HardLight,          // ハードライト
	Exclusion,          // 除外
	NormalAlphaCh,      // アルファつきの画像どうしの通常合成（アルファも重なる）
	AddAlphaCh,         // アルファつきの画像どうしの加算合成
	MultiplyAlphaOnly,  // アルファチャンネルだけの乗算
	Mask,               // マスク（元の画像に、混ぜる画像を通常に描いて、元の画像のアルファを優先する）
};

// RgbaSelectMix で、出力の成分に選ぶもの（DX_RGBA_SELECT_*。Src = 元の画像、Blend = 混ぜる画像、Inv = 反転）
enum class EDrawRgbaSelect : int32
{
	SrcR, SrcG, SrcB, SrcA,
	BlendR, BlendG, BlendB, BlendA,
	SrcInvR, SrcInvG, SrcInvB, SrcInvA,
	BlendInvR, BlendInvG, BlendInvB, BlendInvA,
};

// マスクの画像の、どの成分をマスクの値にするか（DX_MASKGRAPH_CH_*）
enum class EDrawMaskChannel : int32
{
	Alpha,
	Red,
	Green,
	Blue,
};

// 画像のフィルター。Source を Dest へ掛ける（Source と Dest が同じで HasRect が 0 なら、その場で。矩形があれば、Source の矩形を、Dest の (DestX, DestY) へ）。
// Params は、種類ごとの引数（FGraphFilter を参照。float は、ビットのまま入れる）。Map はグラデーションマップの画像。
// Blend が 1 なら、結果を、転送先に、アルファブレンドで重ねる（0 なら、置き換える）。Premultiplied が 1 なら、乗算済みアルファの画像用の版
struct FDrawImageFilterData
{
	int32 Source = -1, Dest = -1;
	int32 Type = 0;
	int32 Premultiplied = 0;
	int32 Blend = 0;
	int32 HasRect = 0;
	int32 SrcX = 0, SrcY = 0, SrcW = 0, SrcH = 0, DestX = 0, DestY = 0;
	int32 Map = -1;
	int32 Params[8] = {};
};

// 2 枚の画像を混ぜて Dest へ出す。Ratio は混ぜる強さ（0..255）。Select は、RgbaSelectMix の出力の R / G / B / A（EDrawRgbaSelect）
struct FDrawImageBlendData
{
	int32 Base = -1, Blend = -1, Dest = -1;
	int32 Type = 0;
	int32 Ratio = 255;
	int32 Premultiplied = 0;
	int32 Select[4] = { 0, 1, 2, 3 };
};

// マスク。Handle は、アルファを持つ描画先（CRenderTarget）のハンドル。Channel は EDrawMaskChannel。
// Invert が 0 なら、マスクの値が大きいところほど濃く描かれる（0 で描かれず、255 でそのまま）。1 なら逆（0 でそのまま）。マスクの画像の外は、値が 255 として扱われる
struct FDrawMaskData { int32 Handle = -1; int32 Channel = 0; int32 Invert = 0; };
// PopMask は中身を持たない（命令の大きさを 4 バイトの倍数にするため、1 ワードだけ持つ）
struct FDrawPopMaskData { int32 Unused = 0; };

// 拡大・回転・縦書きができる文字列。(X, Y) に、文字列の中の基準点（AnchorX, AnchorY: 文字列の大きさに対する割合。横書きは左上が 0, 0、縦書きは右上が 0, 0 で AnchorX は左へ増える）が来る。
// 拡大と回転は、基準点を中心にする。Angle はラジアン（正の向きは時計回り）。Font が負なら、既定のフォント。Vertical が 1 なら縦書き。
// 文字列（'\0' 終端、4 バイト境界まで 0 で埋める）が続く。Length は、終端を除く長さ（バイト）
struct FDrawTextStyledData
{
	float32 X = 0.0f, Y = 0.0f;
	float32 ScaleX = 1.0f, ScaleY = 1.0f;
	float32 Angle = 0.0f;
	float32 AnchorX = 0.0f, AnchorY = 0.0f;
	FDrawRgb Color;
	FDrawRgb EdgeColor;
	int32 Font = -1;
	int32 Length = 0;
	int32 Vertical = 0;
};

#define DRAW_DATA_CHECK( Type ) \
	static_assert( sizeof( Type ) % 4 == 0 && bIsTriviallyCopyable<Type>, #Type " must be made of 4-byte words with no padding" )
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
DRAW_DATA_CHECK( FDrawPixelData );
DRAW_DATA_CHECK( FDrawTriangleData );
DRAW_DATA_CHECK( FDrawQuadData );
DRAW_DATA_CHECK( FDrawOvalData );
DRAW_DATA_CHECK( FDrawRoundBoxData );
DRAW_DATA_CHECK( FDrawBoxAAData );
DRAW_DATA_CHECK( FDrawCircleAAData );
DRAW_DATA_CHECK( FDrawLineAAData );
DRAW_DATA_CHECK( FDrawImageData );
DRAW_DATA_CHECK( FDrawImageStretchData );
DRAW_DATA_CHECK( FDrawImageQuadData );
DRAW_DATA_CHECK( FDrawImagePartData );
DRAW_DATA_CHECK( FDrawImagePartStretchData );
DRAW_DATA_CHECK( FDrawImagePartQuadData );
DRAW_DATA_CHECK( FDrawSpritePivotData );
DRAW_DATA_CHECK( FDrawTileData );
DRAW_DATA_CHECK( FDrawCircleGaugeData );
DRAW_DATA_CHECK( FDrawCustomBlendData );
DRAW_DATA_CHECK( FDrawAddColorData );
DRAW_DATA_CHECK( FDrawAlphaTestData );
DRAW_DATA_CHECK( FDrawFilterData );
DRAW_DATA_CHECK( FDrawClipData );
DRAW_DATA_CHECK( FDrawPopClipData );
DRAW_DATA_CHECK( FDrawTargetData );
DRAW_DATA_CHECK( FDrawPopTargetData );
DRAW_DATA_CHECK( FDrawClearData );
DRAW_DATA_CHECK( FDrawCopyScreenData );
DRAW_DATA_CHECK( FDrawImageFilterData );
DRAW_DATA_CHECK( FDrawImageBlendData );
DRAW_DATA_CHECK( FDrawMaskData );
DRAW_DATA_CHECK( FDrawPopMaskData );
DRAW_DATA_CHECK( FDrawTextStyledData );
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

	// データが TData 1 つ分あるか（壊れた・短い命令を読まないための確認）
	template<typename TData>
	FORCEINLINE bool CanRead() const { return NumDataWords >= static_cast<int32>( sizeof( TData ) / 4 ); }

	template<typename TData>
	FORCEINLINE TData Read() const
	{
		TData Value;
		FMemory::Memcpy( &Value, Data, sizeof( TData ) );
		return Value;
	}

	// Text コマンドの文字列（'\0' 終端）
	FORCEINLINE const char* GetTextChars() const { return reinterpret_cast<const char*>( Data + sizeof( FDrawTextData ) / 4 ); }
	// TextFont コマンドの文字列（'\0' 終端）
	FORCEINLINE const char* GetFontTextChars() const { return reinterpret_cast<const char*>( Data + sizeof( FDrawTextFontData ) / 4 ); }
	// TextStyled コマンドの文字列（'\0' 終端）
	FORCEINLINE const char* GetStyledTextChars() const { return reinterpret_cast<const char*>( Data + sizeof( FDrawTextStyledData ) / 4 ); }
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
//     - Span の中で変えた状態（ブレンドモード・明るさ・WorldMatrix・加算する色・アルファテスト・描画モード・クリップ範囲・2D の変換）は、
//       Span の終わりで元に戻る（次のオブジェクトに漏れない）。クリップ範囲が空のあいだは、描く命令だけを飛ばす
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

#pragma once

//=============================================================================
// FORCEINLINE
//   小さく頻繁に呼ばれる関数に付ける（アクセサ・演算子・コンテナの要素アクセスなど）。
//   Release (/O2) ではコンパイラの判断より優先してインライン展開される。
//   ※ Debug (/Ob0) ではインライン展開自体が無効なので効果はない
//   ※ 仮想関数・再帰・大きな関数には付けない（展開できない / コードが膨らむだけ）。
//     そういう処理は .cpp（テンプレートなら .inl）に置く
//
//   Windows のヘッダー (winnt.h) も同じ定義を持つので、未定義の場合だけ定義する
//=============================================================================
#ifndef FORCEINLINE
	#if defined( _MSC_VER )
		#define FORCEINLINE __forceinline
	#else
		#define FORCEINLINE inline __attribute__( ( always_inline ) )
	#endif
#endif

//=============================================================================
// FORCENOINLINE
//   インライン展開させたくない関数（エラー処理など、めったに通らない重い経路）
//=============================================================================
#ifndef FORCENOINLINE
	#if defined( _MSC_VER )
		#define FORCENOINLINE __declspec( noinline )
	#else
		#define FORCENOINLINE __attribute__( ( noinline ) )
	#endif
#endif

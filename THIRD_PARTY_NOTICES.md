# 第三者のソフトウェア

mazigatiDX のコードは [MIT ライセンス](LICENSE)です。次の第三者のソフトウェアを使っています。`VendorModules` に置いたものは、同じフォルダーにライセンス文を保持しています。

| 名前 | バージョン | 置き場所 | ライセンス | 著作権者 |
|---|---|---|---|---|
| DxLib | 3.24f | 同梱しない（`FetchDxLib.bat` が、公式サイトから `C:\DxLib` へ取得） | DxLib 独自（使用料なし、商用可。下記の条件あり） | 山田 巧 |
| Jolt Physics | 5.6.0 | `VendorModules/Jolt` | MIT | Jorrit Rouwe |
| Dear ImGui | v1.93.0-docking | `VendorModules/ImGui` | MIT | Omar Cornut |
| SDL | 3.4.18 | `VendorModules/SDL3` | zlib | Sam Lantinga |
| nlohmann/json | 3.12.0 | `VendorModules/nlohmann` | MIT | Niels Lohmann |
| ufbx | 0.23.1 | `VendorModules/ufbx` | MIT または パブリックドメイン（選べる） | Samuli Raivio |
| pixelfx | a841596（2026-11） | `Library/Framework/PixelEffect`（C++ へ移植。元のコードは同梱しない） | MIT | Patch, Hugo Duprez |

ライセンスの組み合わせと配布の条件は、v1.0 の公開前にもう一度確かめます。

## DxLib

DxLib は再配布できないので、このリポジトリには入れていません。DxLib 付属の `DxLib.txt`（3.24f）にある条件は次のとおりです。

- 使用して作ったソフトウェアに、使用料は発生しません（商用・法人を問わず）。DxLib を使った旨の記載は必要ありません。
- 次の機能を使ったときは、配布するソフトウェアの文書などに、機能に応じた著作権表示を含めます。
  - JPEG 画像の読み込み: libjpeg（Thomas G. Lane, Guido Vollbeding）
  - PNG 画像の読み込み: libpng（Glenn Randers-Pehrson）、zlib（Jean-loup Gailly, Mark Adler）
  - TIFF 画像の読み込み: libtiff（Sam Leffler, Silicon Graphics, Inc.）
  - Ogg Vorbis / Ogg Theora の読み込み: Xiph.org Foundation
  - 関数 `GetRand`: Mersenne Twister（Makoto Matsumoto, Takuji Nishimura）
  - PMD + VMD の読み込みで物理演算を使う場合: Bullet（Erwin Coumans）
  - Live2D: Live2D のライセンスに従う
- DxLib のライブラリファイルやソースコード（`DxLib.h` など）を配布するときは、`DX Library Copyright (C) 2001-2025 Takumi Yamada.` を配布物の文書に含めます。

ここでは要点だけを書いています。正確な文面は、DxLib に付属の `DxLib.txt` を参照してください。

## pixelfx

`Library/Framework/PixelEffect` の一部は、pixelfx（https://github.com/patch-gg/pixelfx）を C++ へ移植したものです。ライセンス文は次のとおりです。

```
MIT License

Copyright (c) 2026 Patch
Copyright (c) 2026 Hugo Duprez

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## ゲームを配布するとき

配布物には、このリポジトリの `LICENSE` と、`VendorModules` の各ライセンス文、上の pixelfx のライセンス文と DxLib の著作権表示を同梱してください。

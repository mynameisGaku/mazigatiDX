# 第三者のソフトウェア

mazigatiDX のコードは [MIT ライセンス](LICENSE)です。次の第三者のソフトウェアを使っています。`VendorModules` に置いたものは、同じフォルダーにライセンス文を保持しています。

| 名前 | バージョン | 置き場所 | ライセンス | 著作権者 |
|---|---|---|---|---|
| DxLib | 3.24f | 同梱しない（`C:\DxLib` へ配置） | DxLib 独自（使用料なし、商用可。下記の条件あり） | 山田 巧 |
| Jolt Physics | 5.6.0 | `VendorModules/Jolt` | MIT | Jorrit Rouwe |
| Dear ImGui | v1.93.0-docking | `VendorModules/ImGui` | MIT | Omar Cornut |
| SDL | 3.4.18 | `VendorModules/SDL3` | zlib | Sam Lantinga |
| nlohmann/json | 3.12.0 | `VendorModules/nlohmann` | MIT | Niels Lohmann |
| ufbx | 0.23.1 | `VendorModules/ufbx` | MIT または パブリックドメイン（選べる） | Samuli Raivio |

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

## ゲームを配布するとき

配布物には、このリポジトリの `LICENSE` と、`VendorModules` の各ライセンス文、上の DxLib の著作権表示を同梱してください。

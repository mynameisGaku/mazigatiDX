# SDL3 の x64 の DLL について

`lib/x64/SDL3.dll` と `lib/x64/SDL3.lib` は、SDL の公式の配布物ではなく、**公式のソース（`release-3.4.18`、コミット `829a65d769d935c4852f8159e964312c0957260a`）から、libusb 対応で作り直したもの**です。ヘッダー（`include`）は公式の 3.4.18 のままで、ABI は同じです。

## なぜ作り直したか

公式の Windows 用の `SDL3.dll` は、libusb 対応が外されていて、Nintendo Switch 2 のコントローラーのドライバー（`SDL_hidapi_switch2.c`）が入っていません。Switch 2 Pro Controller は、USB でつないでも、バルク転送の窓口（インターフェース 1）へ初期化の命令を送るまで、入力を返しません。SDL は、その命令を libusb 経由で送ります。

- この機器のインターフェース 1 には、Windows 標準の WinUSB（`winusb.inf`、`USB\MS_COMP_WINUSB`）が自動で割り当てられるので、プレイヤーがドライバーを入れる必要はありません。
- Bluetooth 接続は、SDL が Windows では対応していません。
- その窓口を開けられるのは 1 つのプログラムだけです。Switch2Connect や Steam が開いているときは、読めません。

## 構成

| ファイル | 中身 |
|---|---|
| `lib/x64/SDL3.dll`（SHA-256 `2ff5ea691ad45ea88a6cdc708eff40d3ce297bb25e17252a0fdfd11adf215f3a`） | 公式のソースを、MSVC（VS 2026、`v145`）で、下の設定でビルド。公式と同じく CRT に依存しない |
| `lib/x64/SDL3.lib` | 上の DLL の import ライブラリ |
| `lib/x64/libusb-1.0.dll`（SHA-256 `545802d19a31128cdfca704151eb88453b0009045d98526d2eab49be61b3d730`） | libusb `v1.0.30`（コミット `87a5563`）を、変更せずに、`Release-MT`（静的 CRT。依存は `KERNEL32.dll` だけ）でビルド。SDL が実行時に読み込む |
| `LICENSE-libusb.txt` | libusb のライセンス（LGPL 2.1 以降） |
| `lib/x86`、`lib/arm64` | 公式の配布物のまま。libusb を使わないので、Switch 2 のコントローラーは読めない |

## 確認したこと（2026-11-23、Switch 2 Pro Controller、USB）

Switch2Connect と Steam を終了し、HidHide で機器を隠していない状態で、次を実機で確認しました。

- `CSdlGamepadInputSubsystem`が、`Gamepad connected: #1 Nintendo Switch Pro Controller (VID:0x057e PID:0x2069 Mapped:1)`と認識する（起動に遅れはない）。
- ボタン 15 個・軸 6 本（スティック 2 本とトリガー 2 本）・ハット 1 個が読める。
- 振動（`SDL_RumbleGamepad`）が動く。ジャイロと加速度が読める。
- 次のどれかが動いていると、この機器の窓口を開けないか、入力が読めません。Switch2Connect、Steam（Steam Input）、HidHide でこの機器の HID を隠す設定（インターフェース 0 の入力が「アクセスが拒否されました」になり、接続の直後に切断される）。

## 作り直す手順

libusb（`libusb/libusb` のタグ `v1.0.30`）

```
msbuild msvc\libusb_dll.vcxproj /p:Configuration=Release-MT /p:Platform=x64 /p:PlatformToolset=v145 /p:WindowsTargetPlatformVersion=10.0
```

SDL（`libsdl-org/SDL` のタグ `release-3.4.18`）

```
cmake -S SDL -B build_x64 -G Ninja -DCMAKE_BUILD_TYPE=Release -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_EXAMPLES=OFF -DSDL_TEST_LIBRARY=OFF -DSDL_LIBC=OFF -DSDL_HIDAPI_LIBUSB=ON -DSDL_HIDAPI_LIBUSB_SHARED=ON -DLibUSB_INCLUDE_PATH=<libusb>\libusb -DLibUSB_LIBRARY=<libusb>\build\v145\x64\Release-MT\dll\libusb-1.0.lib
cmake --build build_x64
```

`SDL_LIBC=OFF` は、公式の DLL と同じく、CRT に依存させないための設定です。できた `SDL3.dll` と `SDL3.lib` を `lib/x64` へ、`libusb-1.0.dll` を同じ場所へ置きます。`Build/mazigatiDX.props` が、実行ファイルの隣へ両方をコピーし、`Tools/PackageGame.ps1` の配布物にも入ります。

作り直した DLL が、Switch 2 のドライバーを持つことは、`SDL_JOYSTICK_HIDAPI_SWITCH2` と `libusb-1.0.dll` の文字列が DLL にあることで確かめられます。

## 公式の配布物へ戻すとき

`Tools/FetchSDL.ps1` は、公式の配布物を取得します。それを使うと、`libusb` 対応の DLL は、公式の DLL に置き換わり、Switch 2 のコントローラーは読めなくなります。`libusb-1.0.dll` は、無くても動きます（その機器だけを使いません）。

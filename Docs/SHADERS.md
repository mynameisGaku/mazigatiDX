# シェーダーの管理

固定のシェーダーと共通の生成テンプレートは `EngineContent/Shaders` が正本です。C++ に HLSL の本文を埋め込んで編集しません。新しい HLSL は、最初から `EngineContent/Shaders` のファイルとして作ります（C++ の文字列には書きません）。ただし、`EngineContent` は製品に同梱され、ライブラリへ埋め込まれるので、**製品が使う部品だけ**を置きます。サンプルだけが使うシェーダーは、サンプルの素材（`Samples/Assets/<サンプル名>/`、例: `Samples/Assets/Deferred`）へファイルとして置き、サンプルが実行時に読んで、製品の部品（`NDeferredShaders` など）と連ねてコンパイルします。検査だけが使う HLSL は、検査のリポジトリ（`Source/SelfTest`）へファイルとして置きます。材質グラフから命令や値ごとの式を組み立てる部分は、引き続きマテリアルコンパイラが担当します。

|場所|担当|
|---|---|
|`DX12/ForwardMesh.hlsl`|共有モデル・編集メッシュ・基本図形の変換、頂点／画素照明、UV、距離／高さフォグ|
|`DX12/Geometry.hlsl`|同次座標の図形・画像・マスクと、反転などの合成|
|`DX12/BuiltIn.hlsl`|矩形・四角形、影、画像フィルターと混合、既存の画素照明、材質効果のエントリー|
|`DX12/DepthClear.hlsl`・`ColorClear.hlsl`・`CaptureClear.hlsl`|描画先・撮影先を塗る小さなシェーダー|
|`DxLib/EffectMaterial.hlsl`|DxLib の材質効果|
|`Material/*.hlsli`|材質の共通宣言、ノイズ、深度、表面の頂点、照明、影、Substrate の関数など10個のテンプレート|
|`Deferred/GBufferWrite.hlsli`・`GBufferRead.hlsli`|G-buffer へ書く（`PackGBuffer`）部品と、読んで深度から世界の位置を求める（`LoadGBuffer`・`ReconstructWorldPosition`）部品。[R3 G-buffer と Deferred 照明](R3_DEFERRED.md)|

`Build/EngineShaders.targets` は C++ のコンパイル前に `Tools/GenerateEmbeddedShaders.ps1` を呼びます。Native C++ の `CustomBuildStep` に全シェーダーと生成器の入力、生成ヘッダーの出力を登録するため、Visual Studio の変更検出にも反映されます。生成器は構成ごとの `IntDir/EngineShaders/MazigatiEngineShaderSources.generated.h` へ文字列を出力します。同じ内容なら時刻を更新せず、無関係な再コンパイルを避けます。識別子の衝突、NUL、文字列の区切りとして扱えない入力はエラーにし、有効だった出力を途中の内容で置き換えません。

HLSL のコンパイルは従来の初期化・初回使用時の仕組みを保ちます。今回の生成はシェーダー言語を C++ 文字列へ取り込む処理で、バイトコードの事前コンパイルではありません。描画命令の再生中に新たなファイル検索を行わず、実行時に `EngineContent` の場所へ依存する処理も増やしません。HLSL を編集したらライブラリと利用側をビルドし直します。`PackageGame.ps1` は既存の `EngineContent` のコピーにより、言語ファイルも同梱します。

`mazigatiDX.vcxproj` の `MazigatiSourceRoots` は `EngineContent` を含みます。`GenerateProjectFiles.bat` は HLSL・HLSLI・FX・GLSL・各段の拡張子・Metal・WGSL・USF/USH を `None` 項目として一覧に載せ、フォルダーをフィルターへ反映します。複数のエントリーや断片を持つため、Visual Studio の `FxCompile` による自動コンパイルにはしません。通常の利用側はライブラリのプロジェクト参照を通じて生成が実行されます。DX12 の実装 cpp を直接コンパイルする検査プロジェクトは、同じ `EngineShaders.targets` を取り込みます。

検査は `Source/SelfTest/RunEngineShaderSourceTests.ps1 -FrameworkRoot <ルート>` です。正本の保持、生成名、変更の反映、無変更時の時刻、不正入力、ソリューションの一覧と重複、無関係な項目の保持を調べます。小さな C++ プロジェクトを実際にビルドし、HLSL のみの編集で実行結果が変わること、IDE が参照する tlog に入力が記録されることも検査します。`RunAllChecks.ps1` にも登録しています。新しい言語ファイルを追加・移動したときは、プロジェクト一覧も再生成してください。

参照: [MSBuild の実行順](https://learn.microsoft.com/en-us/visualstudio/msbuild/target-build-order)、[プロジェクト項目](https://learn.microsoft.com/en-us/visualstudio/msbuild/common-msbuild-project-items)、[C++ のカスタムビルド](https://learn.microsoft.com/en-us/cpp/build/how-to-add-a-custom-build-step-to-msbuild-projects?view=msvc-170)。

# mazigatiDX

DxLibを使うWindows向けC++フレームワークです。描画・入力・アセットの窓口をインターフェースに分け、ワールド編集とエフェクト制作を同じアプリで行います。

主な機能:

- 描画命令の記録と実行、ECS、ジョブ、入力、音声、プロファイラー。
- 画像・モデル・アニメーションの管理と編集、ワールドとプレハブの配置・保存。
- エフェクトの複製・編集・保存・配置。粒子、リボン、分岐する雷、固定経路を成長・明滅させる樹状の雷。
- ディゾルブ、境界色、自己発光、UVスクロール、寿命に沿う曲線、四角形粒子のコマ送りとUVディストーション。
- 編集できる組み込みエフェクトと、計算・資源・UI・実GPU画素を調べる自己確認。

エフェクトのメッシュ適用、2D専用の制作操作、HDR・ブルームは今後の工程です。帯と雷のUVディストーションは`Effect`ブランチで開発します。

## ビルド

検証構成はx64のDebug／Releaseです。C++17、MSVCツールセットv145、Windows SDK 10.0.26100.0を使用します。DxLib 3.24fのヘッダーとライブラリを`C:\DxLib`へ配置してください。別の場所を使う場合は`project.vcxproj`のインクルード・ライブラリ検索先を合わせます。

必要な第三者ライブラリは`VendorModules`に配置しています。SDLのシンボルファイルは含みません。各ライブラリのライセンスは同じフォルダー内に保持しています。

1. `GenerateProjectFiles.bat`を実行します。
2. `project.sln`を開き、x64のDebugまたはReleaseをビルドします。
3. 作業ディレクトリーをこのリポジトリのルートにして起動します。`Assets`と`Config`を使います。

Developer PowerShellからの例:

```powershell
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
.\Tools\RunSelfTests.ps1 -Suite world -Configuration Debug
```

自己確認の実行は専用の一時フォルダーを使います。通常利用とは別に、詳細な起動・確認・開発規約を[開発手順](Docs/DEVELOPMENT.md)へまとめています。

## エフェクトを使う

Effect Editorの`Preset`から元になる表現を選び、未保存のコピーを編集します。`File > Save as...`で`Effects/MyEffect.effect.json`などへ保存し、World Editorのアセット一覧から配置します。

`Flowing energy veil`ではノイズの移動と強さの曲線によるUVディストーションを試せます。`Realistic lightning tree`と`Growing lightning tree`では枝・孫枝・成長・明滅を編集できます。サンプル原本は`Assets/Effects`にあります。

## ブランチ

|ブランチ|役割|
|---|---|
|`main`|検証を終えた安定版|
|`dev`|全機能を統合して確認する開発版|
|`Effect`|エフェクト機能の開発|
|`Asset`|アセット機能の開発|
|`Core`|基盤・共通インターフェースの開発|

機能ブランチから`dev`へ統合し、必要な確認を終えた区切りを`main`へ反映します。

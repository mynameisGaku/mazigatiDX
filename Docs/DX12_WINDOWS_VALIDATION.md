# DX12 Windows 再開時の検証

確認日: 2026-11-09（日本時間）。製品の対象は `backend-dx12` の `3866b24a98dc7ce20e3572df9e1884739c6fecab`（取り込み元 `a82ccb4`）です。これは初期描画経路の再開確認であり、R0・R1全体や四つの描画方式の完成を示すものではありません。

## 修正

- `c316035` の画像資源の責務分離で `FDX12Device::RecordClear` の定義が欠け、Windows の実行ファイルで LNK2019 が発生しました。記録中だけ画面消去を追加し、所有先と色を ImGui の命令内へコピーします。既存の `ClearCallback` によって、前後の描画との順序を保ちます。
- `FGeometryQuadBuilder::BuildCorners` の UV の 0・1 を浮動小数点リテラルにし、C4244 の2件を解消しました。
- 公開 API・既存の DxLib／Null の選択条件・画像資源の所有境界は変更していません。

## 環境

Windows x64、Visual Studio Community 2026 18.10.12224.181、MSVC v145（14.51.36231）、Windows SDK 10.0.26100.0、DxLib 3.24f を使用しました。DX12 は既定のアダプターと `--dx12-warp=true` を確認しました。選択された実 GPU の名前は現在の実装で記録されないため、機種別・複数ベンダーの検証とは扱いません。

## ビルドと起動

|確認|結果|
|---|---|
|x64 Debug の solution 全体|mazigatiDX／project／Samples／Editor が成功。修正後の実行は警告0・エラー0|
|x64 Release の solution 全体|同じ4 project が成功。警告0・エラー0|
|互換台帳の整合|446件で `UpdateBackendCompatibility.ps1 -Check` が成功。Verified は増やしていない|
|Debug／Release の DX12 既定アダプター|Canvas2D／Geometry3D／StaticModel を各120フレーム実行し、終了コード0・実行時エラーなし|
|Debug／Release の WARP|同じ3 sample を各120フレーム実行し、終了コード0・実行時エラーなし|
|Debug／Release の DxLib|同じ3 sample を実行し、120フレーム目の画像保存と正常終了を確認|
|Debug／Release の Null|同じ3 sample が120フレームで正常終了。StaticModel は既存の未対応診断を出して省略する|
|Canvas2D の実画面|Debug の DX12 で図形・文字・半透明・矩形の切り抜き・市松画像を目視確認|

計24件は起動と終了の確認です。全画素の DxLib／DX12 比較や、Null でのモデル対応を証明する数ではありません。DxLib には `--max-frames` の終了処理がないため、`--screenshot-frame=120 --screenshot-exit` を使いました。DX12 の音声・動画・ImGui viewports の未対応警告は残ります。

## 自己検査の版

既存の隣接する `framework-test` は `6e55308` でした。この版は、製品側 `de3fdc5` の `{Material path}` 対応を検査します。これは対象の DX12 ブランチに含まれておらず、assets の2件が Debug／Release とも失敗しました。GPU ドライバーの不具合や、画面消去の修正による失敗とは扱いません。

元の検査リポジトリには変更を加えず、直前の `2333e417bdbd9df8bb923b5b142e07c35e3f012e` から別の作業コピーを用意しました。検査用ブランチは `codex/dx12-windows-validation`、修正コミットは `480d5ab2655e4132c6e6f8052ada6216a0cf233c` です。将来の UI 機能の検査を削除したのではなく、製品と検査の版を対応させています。

書式検査には、`Library/Platform/DX12/` 内の d3d12／d3dcompiler／dxgi だけを許す判定を追加しました。DX12 からの DxLib・d3d11 参照、Framework からの d3d12 参照、似た名前の別フォルダーからの参照を拒否する検査も追加しています。

実行中の開発用アプリや Visual Studio の中間ファイルと競合したため、自己検査の最終確認は `framework-dx12-verify` の固定したソースと独立した出力を使います。最初に全体ビルドで確認した Debug／Release のライブラリをコピーし、SHA-256 の一致を確認したうえで、対応版の自己検査実行ファイルをこのコピー内で作ります。

### 対応版での結果

|検査|Debug|Release|
|---|---|---|
|自己検査実行ファイルのビルド|警告0・エラー0|警告0・エラー0|
|ecs|7,867,833値を確認。結果のハッシュ `403b39415ba4b9a6`|同じ値とハッシュ|
|assets|9,956件が成功|9,953件が成功。4件省略|
|world|3,635件が成功|3,635件が成功|

各 suite はワーカー数0／1／4／15で確認しました。Debug の一括実行では ecs の4 worker だけ、終了コード0でも結果の行を取得できず `NO RESULT` となりました。このため一括実行の終了コード1は記録として残します。同じ条件だけを他の自己検査と並行させず再実行すると成功し、他の7条件と同じハッシュになりました。初回に結果を取得できなかった理由は未確定で、一括検査を無条件に成功したとは記録しません。

書式、書式検査自身、Clean.bat、互換台帳の各検査は成功しました。書式検査自身では、DX12 の正当な参照を許すだけでなく、DxLib・d3d11 の混入と共通層への d3d12 の流出を拒否することも確認しました。

Release の assets ログで省略した4件は、手元の PMX 資料の不在、明示実行が必要な PCM ストリーム・動画・ディスプレイの実機検査です。これらを成功とは数えません。既存自己検査は DxLib を既定として実行しており、DX12 の全機能の互換検証ではありません。

## 記録と再実行

最初の全体ビルド、24件の起動確認、DxLib の画像は作業用 `framework-dx12/Saved/DX12Handoff/` に保存しました。独立した自己検査のビルドと実行結果は `framework-dx12-verify/Saved/DX12Handoff/` に保存します。これらのローカル生成物は Git や配布物に含めていません。

対応版の自己検査を `Source/SelfTest` へ接続した Windows の作業コピーで、次を実行します。開発中の作業コピーと同じ中間ファイルを使わないよう、別の作業コピーで実行してください。

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateProjectFiles.ps1 -Root .
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\GenerateProjectFiles.ps1 -Root .\Samples
msbuild .\project.sln /p:Configuration=Debug /p:Platform=x64 /m
msbuild .\project.sln /p:Configuration=Release /p:Platform=x64 /m
powershell -NoProfile -ExecutionPolicy Bypass -File Tools\RunAllChecks.ps1 -SkipBuild -Quick
```

## 残る確認と次の範囲

resize と表示モードの切り替え、SDK デバッグ層、GPU の画素・深度の読み戻し、長時間実行、複数ベンダー、性能測定、配布物のクリーンビルド、全 sample・template・Editor 操作の確認は完了していません。`-Quick` では sample／template 全体の検査を省略します。今回の起動確認をもって互換台帳の各機能を Verified に変更しません。

次は [BACKEND_AUDIT.md](BACKEND_AUDIT.md) の R0 の残りを進め、[ROADMAP.md](ROADMAP.md) の R1 に必要な最小の buffer・compute・資源遷移・完了待ち・readback の契約を固めます。基本図形の追加を優先して、この順序を後回しにしません。MV1 形式の DX12 互換は従来どおり v1.0.0 の対象外です。

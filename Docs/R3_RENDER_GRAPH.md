# R3 描画パスの登録と依存検査

`CRenderGraphBuilder` は既存の画像を使う描画パスを集め、宣言を検査してからフレーム内で実行する。各パスは名前、読み取る画像、書き込む色と深度、実行処理を持つ。パスを組み立てるコードを描画方式から分け、利用側は `AddPass`、`InsertPassBefore`、`InsertPassAfter`、`ReplacePass` で組み合わせる。

```cpp
CRenderGraphBuilder Graph;
Graph.ImportTexture( SceneColor, false );
Graph.ImportTexture( ResultColor, false );

FRenderGraphPassDesc Geometry;
Geometry.Name = "Geometry";
Geometry.Colors.Add( { SceneColor, FColor32( 0, 0, 0, 255 ), true } );
Graph.AddPass( Geometry, RecordGeometry );

FRenderGraphPassDesc Composite;
Composite.Name = "Composite";
Composite.Reads.Add( SceneColor );
Composite.Colors.Add( { ResultColor, FColor32( 0, 0, 0, 255 ), true } );
Graph.AddPass( Composite, RecordComposite );
Graph.ReplacePass( "Composite", RecordCustomComposite );

FString Error;
const bool bRecorded = Graph.Execute( RenderSurface, &Error );
```

例の `RecordGeometry` などは `bool( const FRenderCommandContext& )` の処理。既存のフレーム内で実行し、渡された描画先の窓口は処理中だけ使う。各関数の戻り値を確認し、画像とパイプラインの所有を実行まで保つ。宣言と処理はグラフが値で保持する。処理が参照を取り込む場合は、その寿命を呼び出し側が保証する。

登録順が実行順になる。同じ名前の重複を拒否し、前後への挿入は対象の名前を指定する。名前だけで処理を差し替える場合は元の位置と資源宣言を維持する。宣言を変える場合は同名の `FRenderGraphPassDesc` を渡す。取り込み時に内容があると宣言した画像と、先行パスで書いた画像を後続が読める。既存内容を保持する描画先も、先行書き込みか外部の初期内容が必要。重複する描画先、同一パス内の読み書き、未登録・初期化前の読み取りは、全件を検査して実行前に拒否する。グラフは最大256パス、1024画像、各パス色1〜8枚と任意の明示深度を扱う。画像の実在・寸法・形式は既存の描画先が検査する。

処理は宣言した画像だけを使い、パスの開閉を行わない。実行中のグラフ変更と再入は拒否する。開始後に処理が失敗した場合もパスを閉じ、後続は実行しない。開始・終了などの実行時失敗が起きるまでに提出した処理は残る。アプリの `IRenderSurface` を使う場合はゲームスレッドで組み立てて実行する。DX12では既存の窓口が描画スレッドへの引き渡し、単一キューの順序、画像の状態遷移とGPU完了までの保持を担当する。[スレッドの契約](THREADING.md)

現在の入口は既存画像を使うオフスクリーン描画パス。グラフによる資源生成、深度だけのパス、compute、パスの自動並べ替え、標準ForwardとDeferredの名前付きパス群は今後のR3で接続する。互換用の描画命令は従来の順序で動く。

設計の参照 [EpicのRender Dependency Graph](https://dev.epicgames.com/documentation/unreal-engine/render-dependency-graph-in-unreal-engine)。

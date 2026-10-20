# サンプルアセット

`Effects`には、このフレームワークで複製して編集できるエフェクト定義と、その表示用画像を配置しています。

- `campfire`・`sparks`・`aura`: 炎、火花、光の粒子。
- `ribbon_trail`・`material_ribbon_trail`: 軌跡を残す帯と、模様・消失・自己発光を持つ帯。
- `branch_lightning`・`tree_lightning`・`growing_tree_lightning`: 分岐する雷、固定経路を明滅させる雷、成長を観察する雷。
- `material_dissolve`: マスクで穴を開け、境界を光らせる紋様。
- `flipbook_sigil_burst`: 画像のコマ送りと消失を組み合わせた紋様。
- `uv_distortion_energy_veil`: ノイズの移動で青紫の繊維と金線をゆがめる膜。
- `uv_distortion_ribbon`: 軌跡に沿った模様をノイズでゆがめる帯。
- `uv_distortion_tree_lightning`: 幹・枝・孫枝の形を固定し、長い点灯区間でノイズの流れを観察できる雷。

- `mesh_energy_ring`: 中央に穴のある円環メッシュで、周方向の流れと消失を組み合わせる輪。
- `mesh_tilted_veil`: 固定角度の平面メッシュに、流れる模様と寿命に沿う材質を重ねる膜。

Effect Editorの`Preset`は原本を未保存のコピーとして開きます。編集後は別名で保存し、World Editorのアセット一覧から配置できます。

`Models`のモデルと画像、ルートのBMP・WAVは、アセット編集や表示・再生を試す素材として配置しています。エフェクト定義の画像パスは`Assets`からの相対パスです。

`Worlds/mesh_surface_showcase.world`は、通常材質と溶岩を混ぜた床、脈動する結界、通常描画の台を並べた編集メッシュの例です。World Editorの材質ごとに`Pulsing barrier`／`Flowing lava`を適用し、ワールドへ保存できます。`Effects/mesh_surface_barrier.tga`と`mesh_surface_lava.tga`はその表示画像、`material_white.tga`は画像が空の材質効果に使う白画像です。

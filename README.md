# animegif2mono

## 概要

**animegif2mono** は、アニメーションGIF画像をモノクロ（白黒）のアニメーションGIF画像に変換するコマンドラインツールです。  
複数のディザ法に対応し、コントラスト調整、輪郭抽出も入れてあります。

## 特長

- アニメーションGIFファイルのアニメーション構成はそのまま各フレームをモノクロ画像へ変換
- 透過色のあるアニメーションGIFファイルやスクリーンサイズより小さいフレームサイズを持つアニメーションGIFファイルにも対応
- 複数のディザリングアルゴリズムを選択可能  
  （Floyd–Steinberg、Bayer、Atkinson、Stucki、Burkes、Sierra Lite）
- コントラスト調整機能
- 輪郭抽出機能

## 使い方

```sh
animegif2mono [-c contrast] [-d method] [-e] input.gif output.gif
```

## オプション

| オプション    | 内容 |
|---------------|------|
| `-c contrast`	| コントラスト調整値を指定します。範囲は `-100` ～ `100`（デフォルトは `0`）|
| `-d method`   | ディザ法を番号で指定します（デフォルト: `0`）  <br>0: Floyd–Steinberg<br>1: Bayer (4x4)<br>2: Atkinson<br>3: Stucki<br>4: Burkes<br>5: Sierra Lite |
| `-e`          | Sobelフィルタによる輪郭抽出を有効にします |

## 引数

- `input.gif`  : 入力の変換対象のアニメーションGIFファイル
- `output.gif` : 出力のモノクロアニメーションGIFファイル

## 例

```sh
animegif2mono -d 1 input.gif output.gif
```
Bayerディザ法で`input.gif`をモノクロ化し、`output.gif`として保存します。

```sh
animegif2mono -c 50 -e input.gif output.gif
```
コントラストを上げ、輪郭抽出も行って変換します。

## ビルド方法

必要なライブラリ:

- giflib 5.1以降 （要 `DGifSavedExtensionToGCB()`）
  - pkgsrcの場合は `pkg_add giflib`
  - ubuntuの場合は `apt install libgif-dev`

```sh
make
```
ライブラリやヘッダのパスは適当に調整してください。デフォルトでは NetBSD + pkgsrc の設定が書いてあります。

## その他

- 各フレームの待ち時間は入力アニメーションGIFの値がそのままコピーされます
- 入力のアニメーションGIFに透過色が使われている場合でも、出力のモノクロアニメーションGIFは透過色なしで出力します
（GIFにおける透過色は「描画しない」つまり前フレームのデータ保持なので、モノクロ化後は前フレームとのピクセル単位の差分にあまり意味がなく、圧縮にも寄与しない）
- 入力のアニメーションGIFがスクリーンサイズより小さいサイズのフレームを持つ場合でも、出力のモノクロアニメーションGIFはすべてスクリーンサイズ全体の画像データを持つフレームとして出力します
- 輪郭抽出はオマケなのであまりきれいに出ません

## ライセンス

このソフトウェアのライセンスは `LICENSE` ファイルを参照してください。
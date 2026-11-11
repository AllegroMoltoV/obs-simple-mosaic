# 描画の統合テストと GPU 時間の測定

測定日は 2026-11-11 です。対象のコミットは cd2e034 です。

## 検証環境

Windows 11 Home、NVIDIA GeForce RTX 3060 Ti、OBS のビルドは 31.1.1 (`.deps`) です。描画モジュールは `.deps/obs-studio-31.1.1/build_x64` でビルドした `libobs-d3d11.dll` と `libobs-opengl.dll` を使いました。

## 統合テスト

実行したコマンドは次のとおりです。

```
cmake --preset windows-x64 -DENABLE_POST_BUILD_INSTALL=OFF
cmake --build build_x64 --config RelWithDebInfo
cmake --build .deps/obs-studio-31.1.1/build_x64 --config Release --target libobs-d3d11 libobs-opengl
ctest --test-dir build_x64 -C RelWithDebInfo
```

D3D11 と OpenGL の 9 シナリオずつ、計 18 件が合格しました。

```
100% tests passed, 0 tests failed out of 18
```

各シナリオの検査の内容は `tests/integration-test.cpp` の各シナリオのコメントにあります。環境変数 `MOSAIC_TEST_OUTPUT_DIR` を指定すると、基準と出力の BMP を書き出します。

## GPU 時間

1920×1080 の全体にかけたときの、1 フレームあたりの GPU 時間です。フィルターなしで同じソースを描く時間を基準とし、その差をフィルターの時間としています。値は 60 フレームの平均を 5 回測った中央値です。

```
build_x64/RelWithDebInfo/mosaic-benchmark.exe <simple-mosaic.dll> <data> <libobs-d3d11.dll> <libobs の data> 2 16 64 256
```

| マスの大きさ | フィルターの時間 |
| --- | --- |
| 2 px | 0.057 ms |
| 16 px | 0.038 ms |
| 64 px | 0.041 ms |
| 256 px | 0.043 ms |

目標の 1 ms 未満を満たしています。

## 描画方式の経緯

最初に試した 2 つの方式は、それぞれ次の理由で採りませんでした。

- 4×4 点を線形補間で読む 1 パスの方式: 16 px のマスでは 256 画素のうち 64 画素しか読みません。7 px で折り返す青の模様で、平均が最大 7 ずれ、統合テストに不合格でした。
- マスごとに 1 画素のテクスチャへ正確な平均を書く 2 パスの方式: 256 px のマスで 1.679 ms かかりました。描画先が 8×5 の 40 画素しかなく、1 画素あたり約 16,000 回の読み取りを順に行うため、並列に処理できなかったと考えています。

現在の方式では、平均を行ごとと縦の 2 段に分けています。1 画素あたりの読み取りは最大 128 回です。

D3D11 では、回数が実行時に決まるループの中で `Sample` を使うと、`error X3511: unable to unroll loop` でコンパイルに失敗しました。ループの中では `SampleLevel` で読んでいます。

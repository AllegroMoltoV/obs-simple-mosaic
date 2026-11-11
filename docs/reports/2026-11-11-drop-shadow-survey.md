# obs-simple-drop-shadow の構成の調査

調査対象は `D:/work/obs-simple-drop-shadow` (main、HEAD `8e82cb0`) です。読み取りだけで、ファイルの変更、ビルド、テストの実行はしていません。行番号は調査時点の作業ツリーのものです。

---

## 1. ディレクトリ構成

### git 管理下のファイル (`git ls-files`)

```
.clang-format / .gersemirc / .gitignore
.github/actions/{build-plugin,check-changes,package-plugin,run-clang-format,run-gersemi,setup-macos-codesigning}/action.yaml
.github/scripts/{.Aptfile,.Brewfile,build-macos,build-ubuntu,Build-Windows.ps1,package-macos,package-ubuntu,Package-Windows.ps1,Test-ObsLoad-Windows.ps1}
.github/scripts/utils.pwsh/{Ensure-Location,Invoke-External,Logger}.ps1
.github/scripts/utils.zsh/{check_macos,check_ubuntu,log_*,mkcd,set_loglevel,setup_ubuntu}
.github/workflows/{build-project,check-format,dispatch,pr-pull,push,render-test,verify-obs-install}.yaml
.soko-kun/{image.gif,logo.png}
build-aux/{.functions/log_*,.run-format.zsh,run-clang-format,run-gersemi,run-swift-format}
buildspec.json / CMakeLists.txt / CMakePresets.json / LICENSE / README.md
cmake/common/{bootstrap,buildnumber,buildspec_common,ccache,compiler_common,helpers_common,osconfig}.cmake
cmake/linux/{compilerconfig,defaults,helpers}.cmake
cmake/macos/{buildspec,compilerconfig,defaults,helpers,xcode}.cmake + resources/{ccache-launcher-c.in,ccache-launcher-cxx.in,create-package.cmake.in,distribution.in,installer-macos.pkgproj.in}
cmake/windows/{buildspec,compilerconfig,defaults,helpers}.cmake + resources/resource.rc.in
data/effects/drop-shadow.effect
data/locale/{en-US,ja-JP}.ini
docs/INDEX.md
docs/images/{logo.png,logo.svg,obs-simple-drop-shadow.gif}
docs/logs/2026-11-08-stage1-blur-prototype.md
docs/reports/{2026-09-19-initial-setup,2026-09-19-issue-1-implementation,2026-09-22-obs-forum-ai-disclosure,2026-11-07-obs33-install-layout,2026-11-08-low-cost-blur-method,2026-11-08-ui-improvement-survey,2026-11-09-release-1.0.0}.md
src/{drop-shadow-filter.cpp,item-compensation.cpp,item-compensation.hpp,plugin-main.cpp,plugin-support.c.in,plugin-support.h}
tests/{filter-benchmark.cpp,filter-integration-test.cpp,filter-render-fixture.hpp,install-probe.cpp,obs-test-env.hpp}
```

### git 管理外のローカル領域

- `.git/info/exclude` で除外しているもの: `.appendix/ .tmp/ .logs/ .prompts/ AGENTS.md .beads/ .claude CLAUDE.md .idea .vscode` と Dolt 関連。
- `.gitignore` は `/*` ですべてを除外し、`!/.github !/.soko-kun !/docs !/build-aux !/cmake !/data !/src !/tests` と主要ファイルだけを戻す方式です (`.gitignore` 1-21 行)。新しいトップレベルのディレクトリを追跡するには、ここへ `!/` を足す必要があります。`/cmake/.CMakeBuildNumber` も除外しています。
- 実在するローカル領域: `.beads/` (stealth、埋め込み Dolt)、`.logs/` (約 200 件)、`.prompts/{DISCUSSIONS,DRAFT,PLANS,INIT.md,QUESTIONS.md}`、`.tmp/` (配布 zip の検証や ruleset の JSON、`worktrees/ai-disclosure` など)、`.appendix/`、`.deps/`。

### 由来

`git log -- cmake .github ...` には obs-plugintemplate の上流コミット (PatTheMav ほか) が並んでいます。このリポジトリは obs-plugintemplate を fork した履歴をそのまま持っています。プロジェクト固有の変更は `19a8e02`、`156f11b`、`a3e244e` などに限られます。

---

## 2. ビルド

### buildspec.json (全 45 行)

| 項目 | 値 | 行 |
|---|---|---|
| obs-studio | `31.1.1` (ハッシュは macos と windows-x64) | 3-11 |
| prebuilt (obs-deps) | `2025-07-11` | 12-20 |
| qt6 | `2025-07-11` | 21-32 |
| macOS bundleId | `com.example.plugintemplate-for-obs` (テンプレートのまま) | 36 |
| name | `simple-drop-shadow` | 39 |
| displayName | `シンプルなドロップシャドウプラグイン` | 40 |
| version | `1.0.1` | 41 |
| author/website/email | `AllegroMoltoV` / `https://www.allegromoltov.jp` / `contact@allegromoltov.jp` | 42-44 |

`cmake/common/bootstrap.cmake` 46-65 行で buildspec.json を読み、`_name` (プロジェクト名)、`_version`、`PLUGIN_AUTHOR` などを設定します。`CMakeLists.txt:5` の `project(${_name} VERSION ${_version} ...)` によって、ターゲット名、DLL 名、インストール先フォルダー名がすべて `name` から決まります。

### CMakeLists.txt (全 151 行)

- 1 行: `cmake_minimum_required(VERSION 3.28...3.30)`
- 14-20 行: `add_library(${CMAKE_PROJECT_NAME} MODULE)`、C++17、`find_package(libobs)`
- 43-58 行: `src/*.c|*.cpp|*.h` と `data/*.effect`、`data/locale/*.ini` を `file(GLOB ... CONFIGURE_DEPENDS)` で自動的に拾います。ソースを足しても CMakeLists.txt の編集は要りません。
- 63 行: `set_target_properties_plugin(... OUTPUT_NAME ${_name})`
- 66-76 行: `ENABLE_POST_BUILD_INSTALL` (既定 ON)。Windows ではビルド直後に `cmake --install` を実行し、既定の `%ALLUSERSPROFILE%/obs-studio/plugins` へ配置します (`cmake/windows/defaults.cmake` 10-18 行)。
- 81-97 行: テスト用の実行ファイル `drop-shadow-filter-integration-test` と `drop-shadow-filter-benchmark`。Windows では常にビルドします。macOS と Linux では `ENABLE_RENDER_TESTS=ON` のときだけビルドします。Linux では X11 をリンクします。
- 99-135 行: Windows だけ `enable_testing()`。D3D11 と OpenGL の 2 種 × シナリオ 8 種の CTest を登録します。
  - 104 行: `_obs_build = .deps/obs-studio-31.1.1/build_x64`
  - 105-106 行: `libobs-d3d11/Release/libobs-d3d11.dll` と `libobs-opengl/Release/libobs-opengl.dll`
  - 107-117 行: シナリオ名の一覧
  - 120 行: テスト名 `drop-shadow-${_backend}-${_scenario}`
  - 124-125 行: 引数は「プラグイン DLL、data、描画モジュール、`.deps/obs-studio-31.1.1/libobs/data`、シナリオ名」
  - 131 行: `PATH` の先頭に `.deps/bin/64bit` と `.deps/obs-deps-2025-07-11-x64/bin` を足します。
- 137-151 行: `ENABLE_INSTALL_PROBE` (既定 OFF)。`drop-shadow-install-probe` という OBS のモジュールをビルドします。Qt6 と frontend-api を使い、後述の OBS 33 導入検証で使います。

### CMakePresets.json

テンプレートのままです。`windows-x64` (VS 17 2022、`build_x64`)、`windows-ci-x64`、`macos` / `macos-ci` (Xcode、Universal、`build_macos`)、`ubuntu-x86_64` / `ubuntu-ci-x86_64` (Ninja、`build_x86_64`) があります。プラグイン名は埋まっていません。

### cmake/ 配下の役割

- `common/bootstrap.cmake`: buildspec.json を読み、名前と版を変数にします。
- `common/buildspec_common.cmake`: obs-studio のソースと obs-deps を `.deps` へ取得します。obs-studio は `ENABLE_PLUGINS=OFF -DENABLE_FRONTEND=OFF` で構成し、`obs-frontend-api` ターゲットだけを Debug と Release でビルドして、`.deps` に Development コンポーネントをインストールします (50-110 行付近)。`libobs-d3d11` と `libobs-opengl` は自動ではビルドされません。テストに使う 2 つの DLL は `cmake --build .deps/obs-studio-31.1.1/build_x64 --config Release --target libobs-d3d11` (および `--target libobs-opengl`) で別途ビルドしてあります (`CMakeLists.txt` 102-103 行のコメント、`.prompts/PLANS/2026-09-19-blur-test-feasibility.md` 19 行)。
- `windows/helpers.cmake`: プロジェクトで改変した唯一の cmake ファイルです (`19a8e02`)。25-32 行で DLL と PDB のインストール先を、OBS 33 の新配置 `${target}/` (旧 `${target}/bin/64bit`) にしています。78 行で `data/` を `${target}/data` に入れます。57 行で `resource.rc.in` から VERSIONINFO を生成します (中身は `PROJECT_NAME`、`PLUGIN_AUTHOR`、版を参照するだけです)。
- `windows/defaults.cmake`: 既定のインストール先を `$ENV{ALLUSERSPROFILE}/obs-studio/plugins` にしています。
- `macos/*` と `linux/*`: テンプレートのままで、固有名詞はありません。

### 改名時に置換が必要な箇所 (識別子の所在)

`git ls-files` の範囲を grep した結果、`cmake/`、`build-aux/`、`CMakePresets.json`、`.github/actions` に固有名詞はありませんでした。

| 識別子 | 所在 |
|---|---|
| プラグイン名 `simple-drop-shadow` | `buildspec.json:39`。`.github/workflows/render-test.yaml:54,111`。`.github/scripts/Test-ObsLoad-Windows.ps1:18,41-46,51,69,72-73,114,119,122,143`。`tests/install-probe.cpp:67,82`。README の多数の箇所 |
| 表示名 (buildspec) | `buildspec.json:40`。リリース名に使われます (`build-project.yaml:65-72`、`push.yaml:111`) |
| ソース ID `drop_shadow_filter` | `src/drop-shadow-filter.cpp:486`。`tests/filter-integration-test.cpp:540`。`tests/filter-benchmark.cpp:273`。`tests/install-probe.cpp:90,120,139`。`Test-ObsLoad-Windows.ps1:144,159` |
| ロケールキー `DropShadowFilterName` | `src/drop-shadow-filter.cpp:218`。`data/locale/en-US.ini:1`。`ja-JP.ini:1`。`tests/install-probe.cpp:97` |
| 表示名の訳語 | `en-US.ini:1` `Simple Drop Shadow`。`ja-JP.ini:1` `シンプルドロップシャドウ`。`install-probe.cpp:17-18`。`Test-ObsLoad-Windows.ps1:155` |
| エフェクトのファイル名 | `src/drop-shadow-filter.cpp:44,49,70`。`install-probe.cpp:96`。`Test-ObsLoad-Windows.ps1:149`。README 45 行 |
| 登録関数 `register_drop_shadow_filter` | `src/plugin-main.cpp:7,11`。`src/drop-shadow-filter.cpp:482` |
| クラス名 `DropShadowFilter` とログの接頭辞 `[drop_shadow]` | `src/drop-shadow-filter.cpp` 全体 (38, 49, 70, 304 行など) |
| テスト用ターゲット名 `drop-shadow-filter-integration-test` と `-benchmark` | `CMakeLists.txt:79-87,124`。`render-test.yaml:4-5,49,55-56,106,112-113` |
| CTest 名 `drop-shadow-*` | `CMakeLists.txt:120` |
| テスト用ソース ID `drop_shadow_test_source` と `drop_shadow_bench_source` | `tests/filter-render-fixture.hpp:97,101`。`filter-integration-test.cpp:538`。`filter-benchmark.cpp:226,230,247,266`。`install-probe.cpp:113,138` |
| install-probe の名前と環境変数 | `CMakeLists.txt:141-149` の `drop-shadow-install-probe`。`Test-ObsLoad-Windows.ps1:19,34,96-97,130-131,145,172-173` の `DROP_SHADOW_INSTALL_*`。`install-probe.cpp:74,174-175` |
| 旧版の URL とハッシュ | `Test-ObsLoad-Windows.ps1:46,48` (v0.1.1 の zip。旧版から更新する検証は drop-shadow 固有の処理です) |
| OBS と deps の版のパス | `CMakeLists.txt:103-104,125,131`。`render-test.yaml:107,114,122`。`Test-ObsLoad-Windows.ps1:56-61,138` (OBS 33.0.1 の zip とハッシュ) |
| 作者情報 | `buildspec.json:42-44`。README 156 行 |
| macOS bundleId | `buildspec.json:36` (テンプレートの値のまま。未変更でもビルドは通っています) |
| ライセンスのヘッダー | `src/plugin-support.c.in` 1-17 行と `plugin-support.h` 1-17 行はテンプレートの雛形 `Plugin Name / <Year> <Developer>` のまま |

---

## 3. src/drop-shadow-filter.cpp (全 503 行)

### 登録 (482-503 行)

```cpp
static obs_source_info info = {};
info.id = "drop_shadow_filter";
info.type = OBS_SOURCE_TYPE_FILTER;
info.output_flags = OBS_SOURCE_VIDEO;
info.get_name = DropShadowFilter::GetName;        // obs_module_text("DropShadowFilterName")
info.create / destroy / update / get_defaults / get_properties
info.video_render = DropShadowFilter::VideoRender;
info.get_width / get_height                       // 影の余白を足した出力サイズ
info.filter_add / filter_remove                   // 余白の変化に応じてシーンのアイテムを補正する
obs_register_source(&info);
```

- C++ のクラスに静的関数を並べ、`void *data` を `static_cast` で戻す形です (215-288 行)。`Create` は `new (std::nothrow)` を使います (221-224 行)。
- コンストラクタでは `"enable"` シグナルを接続し、デストラクタで切断します (75、81 行)。目的は有効と無効の切替えで余白を補正することです。
- `plugin-main.cpp` は `OBS_DECLARE_MODULE()`、`OBS_MODULE_USE_DEFAULT_LOCALE(PLUGIN_NAME, "en-US")`、`obs_module_load` からの `register_drop_shadow_filter()` 呼び出しだけで構成されます (全 19 行)。`PLUGIN_NAME` と `PLUGIN_VERSION` は `plugin-support.c.in:21-22` で CMake が埋め込みます。`obs_log` はログに `[<プラグイン名>]` を付けます。

### プロパティとスライダー (233-258 行)

```cpp
static void GetDefaults(obs_data_t *settings) {
    obs_data_set_default_double(settings, "offset_x", 4.0);
    ...
    obs_data_set_default_int(settings, "color", 0x80000000);
}
static obs_properties_t *Properties(void *) {
    obs_properties_t *props = obs_properties_create();
    obs_properties_add_float_slider(props, "offset_x", obs_module_text("OffsetX"), -kMaxOffset, kMaxOffset, 1.0);
    obs_properties_add_float_slider(props, "blur_radius", obs_module_text("BlurRadius"), 0.0, kMaxBlurRadius, 0.5);
    obs_properties_add_color(props, "color", obs_module_text("Color"));
    obs_properties_add_float_slider(props, "opacity", obs_module_text("Opacity"), 0.0, 1.0, 0.01);
    return props;
}
```

- `Update` (95-114 行) では、値を `std::clamp` で上限と下限に収めてからメンバーに保持します。
- 108-110 行のコメントによると、update は描画スレッドで描画の前に呼ばれます。
- 余白の補正のため、`std::mutex state_mutex_` で状態を守っています (475 行)。設定の変更は描画スレッドから、有効化の通知は UI スレッドから来るためです。

### 対象の幅と高さ

- 描画時: `obs_filter_get_target(context_)` と `obs_filter_get_parent(context_)` を取り、`obs_source_get_base_width/height(target)` を使います (157-167 行)。0 のときは `obs_source_skip_video_filter`。
- `TargetWidth()` と `TargetHeight()` (310-320 行) も同じ方法です。
- `get_width/get_height` (132-148 行) は、ベースの大きさに余白を足した値を返します。モザイクで出力の大きさが変わらないなら、この 2 つのコールバックは不要です。省略すると OBS が対象の大きさを使います (未確認。OBS の仕様に基づく推測)。

### エフェクトの読み込み (40-73 行)

```cpp
obs_enter_graphics();
char *path = obs_module_file("effects/drop-shadow.effect");
effect_ = gs_effect_create_from_file(path, nullptr);  bfree(path);
param_image_ = gs_effect_get_param_by_name(effect_, "image");  ... (11 個)
source_render_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
gauss_render_  = gs_texrender_create(GS_R8, GS_ZS_NONE);
for (auto &level : levels_) level = gs_texrender_create(GS_R8, GS_ZS_NONE);  // 13 段
obs_leave_graphics();
```

デストラクタ (79-93 行) では、graphics コンテキストの中で texrender と effect を破棄します。`Ready()` (322-336 行) は、全パラメーターと全テクニック (`Mask/Down/Up/Gauss/Composite`) の存在を確認し、欠けがあれば描画をスキップします。

### 描画の方式 (150-213、338-382 行)

このフィルターでは `obs_source_process_filter_begin/end` を使っていません。代わりに次の手順で描画します。

1. `RenderSource` (338-359 行): 対象を `gs_texrender_begin_with_color_space(source_render_, w, h, GS_CS_SRGB)` で RGBA の texrender に描きます。描画の前に `gs_blend_function_separate(SRCALPHA, INVSRCALPHA, ONE, INVSRCALPHA)`、`gs_clear`、`gs_ortho(0,w,0,h,-100,100)` を設定します。描画は次の分岐です。
   ```cpp
   if (target == parent && !(parent_flags & OBS_SOURCE_CUSTOM_DRAW) && !(parent_flags & OBS_SOURCE_ASYNC))
       obs_source_default_render(target);
   else
       obs_source_video_render(target);
   ```
2. `DrawPass` (362-382 行): 中間のパスを描く共通関数です。`gs_texrender_reset`、`gs_texrender_begin(output, cx, cy)`、`gs_clear`、`gs_ortho`、`gs_blend_function(ONE, ZERO)` のあと、`while (gs_effect_loop(effect_, technique)) gs_draw_sprite(input, 0, cx, cy);` で描きます。
3. ぼかし (388-448 行): Dual Kawase の縮小と拡大を最大 12 段行い、残差を分離型ガウシアンで足します。403 行のコメントにある制約は流用時にも効きます。`gs_technique_end` がパスごとに全パラメーターの値を消すため、値はパスの直前に毎回設定しています。
4. 最終の合成 (200-212 行): フィルターの出力先 (OBS が用意する描画先) へ直接描きます。
   ```cpp
   gs_blend_state_push();
   gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);   // 乗算済みアルファで出力
   while (gs_effect_loop(effect_, "Composite")) gs_draw_sprite(shadow, 0, canvas_w, canvas_h);
   gs_blend_state_pop();
   ```

mosaic への示唆 (推測、未検証): モザイクは出力の大きさを変えない 1 パスの処理なので、OBS 標準の `obs_source_process_filter_begin(context, GS_RGBA, OBS_ALLOW_DIRECT_RENDERING)` と `obs_source_process_filter_end(context, effect, w, h)` を使う方式が最も単純です。drop-shadow の texrender 方式を流用するのは、多段の処理が要る場合に限られます。

### 余白の補正 (item-compensation.*)

`src/item-compensation.cpp` (264 行) と `.hpp` は、出力の大きさが変わったときにシーンのアイテムの位置とバウンディングボックスを補正する処理です。libobs の `obs-scene.c` の計算を再現しています。これは出力が広がる drop-shadow 固有の処理で、出力の大きさを変えないモザイクには不要です。

---

## 4. data/effects と data/locale

### drop-shadow.effect (全 169 行)

- 冒頭で uniform を宣言しています: `float4x4 ViewProj`、`texture2d image`、`texture2d mask_image`、各種 `float2`、`float`、`int` (5-21 行)。
- `sampler_state linearClamp { Filter = Linear; AddressU = Clamp; AddressV = Clamp; };` (23-27 行)
- `VertInOut { float4 pos : POSITION; float2 uv : TEXCOORD0; }` と `VSDefault` は `mul(float4(v_in.pos.xyz, 1.0), ViewProj)` を使います (29-40 行)。
- ピクセルシェーダーは `float4 PSxxx(VertInOut v_in) : TARGET`、テクニックは `technique Name { pass { vertex_shader = VSDefault(v_in); pixel_shader = PSxxx(v_in); } }` の形です (126-169 行)。
- D3D11 と OpenGL の両方で動かすための工夫は次の 3 点です。
  1. 型変換を C 形式のキャストで書かず、コンストラクタ形式 `float(i)` で書きます (91、106 行のコメント)。C 形式のキャストは HLSL では通りますが、OpenGL では `error C7503: OpenGL does not allow C-style casts` でコンパイルに失敗します。v0.1.2 では実際に macOS と Linux で影が出ていなかった可能性があります (`docs/logs/2026-11-08-stage1-blur-prototype.md` 62-64、88 行。README 69 行)。
  2. テクスチャの参照は `image.Sample(linearClamp, uv)` だけに限っています。OBS の OpenGL 変換は `Sample`、`SampleLevel`、`Load` を GLSL の関数へ置き換えます (`docs/reports/2026-11-08-low-cost-blur-method.md` 56、68 行)。
  3. 補間された UV に大きさを掛けた位置は、描画器によって画素中心から僅かにずれます。そのため `floor(px) + 0.5` で画素中心へ丸めています (42-51 行。コミット `23b9a52`)。モザイクのブロックの境界を決める計算にも同じ配慮が要ると考えます (推測)。
- ループの上限を uniform の `int gauss_taps` にした `for` が、両方の描画器で動いています (105 行)。

### data/locale

- `en-US.ini`: `DropShadowFilterName="Simple Drop Shadow"`、`OffsetX`、`OffsetY`、`BlurRadius`、`Color`、`Opacity` (6 行)
- `ja-JP.ini`: 同じキーで日本語 (6 行)
- 既定のロケールは `plugin-main.cpp:5` の `"en-US"` です。

---

## 5. tests/

### CTest の構成

- Windows だけで登録します (`CMakeLists.txt` 99-135 行)。2 種の描画器 (d3d11、opengl) × 8 シナリオで、計 16 件です。シナリオは hard-shadow、negative-offset、color-opacity、blur-small、blur-large、blur-max、settings-update、scene-compensation です。
- 1 つの実行ファイルが引数でシナリオを選びます (`filter-integration-test.cpp` 514-558 行。引数の数は 6、不一致なら終了コード 2)。
- 終了コードは、合格が 0、検査の不合格が 1、起動の失敗が 2 です。
- CI の Windows では CTest を実行していません。`.github` 配下に `ctest` の記述はありません。CI の Windows ビルドでテスト用の実行ファイルはビルドされますが、実行されるのはローカルだけです。

### libobs をフロントエンドなしで起動する仕組み (`tests/obs-test-env.hpp`、92 行)

```cpp
// Linux: XOpenDisplay → obs_set_nix_platform(OBS_NIX_PLATFORM_X11_EGL) → obs_set_nix_platform_display
obs_startup("en-US", nullptr, nullptr);
obs_add_data_path(core_data + "/");          // 非推奨 API。MSVC は warning 4996、GCC/Clang は -Wdeprecated-declarations を pragma で抑止 (37-52 行)
obs_video_info video = {}; video.graphics_module = <引数の DLL/so/dylib>; base/output = width×height; VIDEO_FORMAT_RGBA;
obs_reset_video(&video);
while (obs_get_total_frames() < 2) sleep 5ms;  // OpenGL で最初のフレーム前に描くと空になる問題の回避 (67-77 行)
obs_open_module(&module, plugin_path, module_data); obs_init_module(module);  // load_plugin (82-90 行)
```

- 描画モジュールのパスを引数で切り替えることで、同じ実行ファイルを D3D11 と OpenGL の両方で動かします。Windows の OpenGL 版は、`.deps` の obs-studio から別途ビルドした `libobs-opengl.dll` です。
- libobs の既定エフェクト (`default.effect` など) は `.deps/obs-studio-31.1.1/libobs/data` から読みます。
- `obs.dll` などは、CTest の `ENVIRONMENT_MODIFICATION` で `.deps/bin/64bit` と `.deps/obs-deps-2025-07-11-x64/bin` を PATH に足して解決します (`CMakeLists.txt:131`)。

### オフスクリーンの描画と画素の検査 (`tests/filter-render-fixture.hpp`、199 行)

- テスト用の入力ソースを登録します (`register_source`、94-109 行)。`SourceSpec { width, height, paint }` で描いた RGBA を `gs_texture_create` し、`video_render` では `gs_effect` の `image` に設定して `gs_draw_sprite` します。描き方の補助として `single_pixel` と `fill_opaque` があります (111-133 行)。
- 出力の読み出しは `fixture::render(source, Image&)` で行います (136-175 行)。
  ```cpp
  obs_enter_graphics();
  w = obs_source_get_width(source); h = obs_source_get_height(source);
  gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
  gs_stagesurf_t *staging = gs_stagesurface_create(w, h, GS_RGBA);
  gs_texrender_begin(output, w, h); gs_clear(...); gs_ortho(0,w,0,h,-100,100);
  gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
  obs_source_video_render(source);          // フィルター付きソースを描く
  gs_texrender_end(output);
  gs_stage_texture(staging, gs_texrender_get_texture(output));
  gs_stagesurface_map(staging, &mapped, &stride);  // 行ごとに stride を考慮してコピー
  ```
- `Checker` (178-197 行) は `expect` と `expect_near(実測, 期待, 許容差)` で失敗を数えます。
- 画像ファイルへの書き出しはありません。`tests/` を `fopen|ofstream|.ppm|.png|stbi` で検索して、該当はありませんでした。検査は画素値の数値比較だけです。
- 設定の変更は `update_and_wait` で行います (`filter-integration-test.cpp` 143-157 行)。`"update"` シグナルを接続して `obs_source_update` を呼び、最大 2 秒待ちます。フレームの経過は `wait_frames` で待ちます (160-171 行)。
- 後始末の注意 (384-389 行): OBS 31 以降はキャンバスがシーンを保持します。そのため、`obs_scene_create` したシーンは `obs_source_remove` してから release します。

### tests/filter-benchmark.cpp (全 317 行) の GPU 時間の測り方

- CTest には登録していません。環境で値が変わるためです (`CMakeLists.txt:80`)。
- time モード (90-142 行): 1920x1080 のソースを使います。`gs_timer_range_create`、`gs_timer_create`、warmup の 10 フレームのあと、`gs_timer_range_begin`、`gs_timer_begin`、60 フレームの描画、`gs_timer_end`、`gs_timer_range_end`、`gs_flush` の順に処理します。`gs_timer_range_get_data(range, &disjoint, &frequency)` と `gs_timer_get_data(timer, &ticks)` から、`ticks*1000/frequency/60` で 1 フレームのミリ秒を出します。これを 5 回繰り返した中央値を取り (198-210 行)、フィルターなしのソースとの差を `filter_ms` として CSV で出力します (257-306 行)。
- profile モード (144-196 行): 1 行のアルファを読んで σ と extent を判定し、外れたら終了コード 1 を返します。CI の macOS と Linux でも、描画の検証として使っています。
- 実行例は `docs/logs/2026-11-08-stage1-blur-prototype.md` 22-29 行にあります。PATH に `.deps/bin/64bit` と `.deps/obs-deps-2025-07-11-x64/bin` を足し、引数は絶対パスで渡します。

### tests/install-probe.cpp (187 行)

OBS 33.0.1 の実物に読み込ませる検証用のモジュールです。環境変数 `DROP_SHADOW_INSTALL_RESULT_DIR` と `DROP_SHADOW_INSTALL_PHASE` が無ければ、何もせず load に失敗します。`OBS_FRONTEND_EVENT_FINISHED_LOADING` の 1 秒後に、フィルターの登録数、エフェクトとロケールの解決先、訳語、保存された設定、画素を検査し、`result.json` を書いて OBS を閉じます。`fixture` を流用しています (1 行)。

---

## 6. .github/workflows/

| ファイル | 起動条件 | 役割 |
|---|---|---|
| `push.yaml` | push (master、main、`release/**`、全タグ) | main では check-format。常に build-project。タグでは create-release (37-119 行)。タグ名を `+([0-9]).+([0-9]).+([0-9])` (または `-beta`、`-rc`) で判定し (44-56 行)、ドラフトの GitHub Release を作ります |
| `pr-pull.yaml` | main/master への PR (`**.md` は除外) | check-format と build-project |
| `dispatch.yaml` | 手動 | build-project だけ |
| `build-project.yaml` | workflow_call | テンプレートの標準ワークフローです。macOS 15 (Xcode 16.1)、Ubuntu 24.04、Windows 2022 でビルドとパッケージ化を行い、artifact `<name>-<version>-<os>-<hash>` を作ります。名前と版は buildspec.json から取ります (65-72、109-113、200-204、273-278 行)。タグの push では config を Release、notarize を true にします (45-48 行) |
| `check-format.yaml` | workflow_call | clang-format-19 (`run-clang-format`) と gersemi (`run-gersemi`) |
| `render-test.yaml` | 手動、`feature/ui-improvement` への push、`CMakeLists.txt/src/data/tests/render-test.yaml` を変えた PR | macOS と Linux の描画検証 (後述) |
| `verify-obs-install.yaml` | 手動、src や cmake などを変えた PR | Windows 2022 で `Build-Windows.ps1 -EnableInstallProbe`、`Package-Windows.ps1`、`Test-ObsLoad-Windows.ps1` を実行します。OBS 33.0.1 の公式 zip をハッシュで照合して展開し、`%ProgramData%/obs-studio/plugins` へ導入して、新規導入、v0.1.1 の導入、更新の 3 段で OBS を起動して検証します |

### macOS と Linux の描画検証 (render-test.yaml)

- Ubuntu (25-79 行): build-plugin アクションでビルドします。続けて `apt-get install xvfb xauth libgl1-mesa-dri libegl-mesa0 libx11-dev`、`cmake -S . -B build_x86_64 -DENABLE_RENDER_TESTS=ON`、テスト用ターゲットのビルドを行います。描画モジュールは PPA の obs-studio に含まれる `find /usr/lib -name 'libobs-opengl.so*'`、コアのデータは `/usr/share/obs/libobs` です。各シナリオを `xvfb-run -a -s '-screen 0 1280x720x24'` で実行し、benchmark の profile も実行します。ログは artifact として上げます。PPA の obs-studio は `.github/scripts/utils.zsh/setup_ubuntu:38` の `add-apt-repository ppa:obsproject/obs-studio` で入ります。
- macOS (81-141 行): 追加で `cmake --build .deps/obs-studio-31.1.1/build_universal --config Release --target libobs-opengl` を実行し、OpenGL のモジュールを作ります。実行時には `DYLD_FRAMEWORK_PATH=$PWD/.deps/Frameworks` と、`DYLD_LIBRARY_PATH` に `libavcodec.dylib` のあるディレクトリを渡します (116-121 行。コミット `4bb2f28`、`9a3d0b4`)。描画は Apple Software Renderer の OpenGL 4.1、Ubuntu は Mesa llvmpipe の OpenGL 4.5 です (`docs/logs/...prototype.md` 74-75 行)。
- 22-23 行: `env.SCENARIOS` と `PROFILE_RADII` は drop-shadow 固有の値です。
- 11 行: push のトリガーに旧作業ブランチ `feature/ui-improvement` が残っています。

### リリースの実態 (事実と推測を分けて記載)

- 事実: 実際のタグは `v1.0.0` のように `v` を付けています (`git tag`)。公開記録 (`docs/reports/2026-11-09-release-1.0.0.md` 14-16 行) によると、添付は `simple-drop-shadow-1.0.0-windows-x64.zip` だけです。
- 推測 (未確認): `push.yaml` 44-56 行の extglob は先頭の `v` を許しません。そのため `v` 付きのタグでは validTag が false になり、create-release は Release を作らないはずです。Release は手動 (`gh release`) で作ったと推測します。一方、`build-project.yaml:45` の正規表現はアンカーが無いため、`v1.0.0` でも Release 構成になります。mosaic では、タグの命名と push.yaml を揃える判断が要ります。

---

## 7. README、LICENSE、.soko-kun、docs、版の置き場所

### README.md (182 行) の構成

1. タイトルと日英の概要 (1-5 行)
2. GIF (7 行、`docs/images/obs-simple-drop-shadow.gif`)
3. Features / 機能 (9-19 行)
4. Installation (21-69 行): Windows の新規導入 (`%PROGRAMDATA%\obs-studio\plugins\<name>\` と構成図)、旧版からの更新、ポータブル版、macOS / Linux (CI で検証しており実機では未確認である旨)
5. Usage (71-76 行)
6. Parameters の表 (78-86 行)
7. Known Behavior (88-99 行)
8. Build (101-128 行)
9. AI 利用開示 (130-146 行、日英)
10. License (148-152 行): GPL-2.0、obs-plugintemplate がベースである旨
11. Author (154-156 行)
12. Changelog (158-182 行、版ごと)

### LICENSE

GPL v2 の全文です (338 行)。

### .soko-kun/

- `logo.png`: 512×512 の RGBA PNG、25733 バイト
- `image.gif`: 1280×663、935992 バイト

どちらも `docs/images/logo.png` と `docs/images/obs-simple-drop-shadow.gif` とバイト数が一致しており、同じファイルのコピーと見られます (ハッシュは未照合)。配信プラグイン倉庫くんに掲載するための素材で、v1.0.1 で追加しました (README 160-162 行、コミット `8e82cb0`)。掲載手順はスキル `soko-kun-publish` にあります。

### docs/

- `INDEX.md`: reports 7 件、logs 1 件、images 3 件の索引
- `reports/`: 初回セットアップ、issue #1、Forum の AI 開示要件、OBS 33 の配置移行、UI 改善の調査、低負荷のぼかし方式の比較、1.0.0 の公開記録
- `logs/2026-11-08-stage1-blur-prototype.md`: ベンチマークの実行手順と測定結果、OpenGL の落とし穴
- `images/`: `logo.svg` (「影」の字に影を付けたもの)、`logo.png`、GIF

### 版番号の置き場所

- 正本は `buildspec.json:41` の `"version": "1.0.1"` です。CMake の `PROJECT_VERSION`、`plugin-support.c.in` の `PLUGIN_VERSION`、`resource.rc.in` の VERSIONINFO、CI の artifact 名、zip 名はすべてここから決まります。
- そのほか手で直す箇所: README の Changelog と本文中の版の言及 (25、67、90、94 行など)、`tests/install-probe.cpp` 20-21 行のコメント、`Test-ObsLoad-Windows.ps1:154` のコメント。コミット `a3e244e` (版の変更) はこの 4 ファイルを触っています。

---

## 8. git log の流れ (直近 25 件)

```
8e82cb0 配信プラグイン倉庫くん用の画像を追加し1.0.1へ更新する (#9)
26d9834 READMEに1.0.0の操作の様子のGIFを掲載 (#8)
821c04f 1.0.0の公開記録を追加 (#7)
a3e244e 版を0.2.0から1.0.0へ変更 (#6)
781c54b 0.2.0の影の大半径化とUI改善をmainへ統合 (#5)   ← merge commit (feature/ui-improvement)
845fa48 設定の変更による補正のログをデバッグ用の水準に下げる
f4745d3 フィルタの追加・削除・有効化・無効化でもアイテムの位置を補正する
c9a9e97 位置補正のテストでシーンをキャンバスから外してから解放する
9aa715b 影の余白が変わったときにシーン上のアイテムの位置を補正する
0852789 0.2.0の版とREADMEを更新
23b9a52 元画像を読む位置を画素中心へ丸める
156f11b 統合テストを新しい描画仕様で書き直し、表示名を変更
72596dc macOSとLinuxの描画検証の結果を記録
9a3d0b4 macOSの描画検証でobs-depsのdylibの場所を渡す
4bb2f28 macOSの描画検証でlibobs.frameworkの場所を渡す
50fdd05 ベンチマークの非推奨API警告をGCCとClangでも抑える
719248f 影をDual Kawaseで描画する試作とmacOS/Linuxの描画検証を追加
4d737e3 公開検証報告書をmainへ統合
3a8e6d6 0.1.2の配布確認とForum審査待ちを記録
d9ef3bd OBS33のWindows新配置へ移行
...
```

参考になる点:

- コミットメッセージは日本語で、1 コミットに 1 つの論点です。main へは PR (#5 から #9) で入れています。#5 はマージコミット、#6 以降は直線的な履歴です (squash か rebase と見られますが、方式は未確認)。ブランチは `feature/*`、`release/*`、`docs/*`、`codex/*` があります。
- macOS の描画検証は、CI で何度か試行して直しています (`4bb2f28`、`9a3d0b4`。`.logs/2026110819-` から `27-ci-render-run*` の 4 回)。原因は dylib と framework の場所でした。mosaic では最初から `render-test.yaml` 116-121 行の設定を流用すれば、この試行錯誤を省けます。
- `50fdd05`: GCC と Clang の `-Werror` (CI の preset は `CMAKE_COMPILE_WARNING_AS_ERROR`) で、`obs_add_data_path` の非推奨警告が失敗の原因になりました。`obs-test-env.hpp` の pragma はその対処です。
- `c9a9e97`: OBS 31 以降でシーンを release だけしても破棄されず、後始末が失敗する問題への対処です。
- 公開の手順は、版の変更 PR、タグ、CI、手動の Release、Forum の更新、公開記録 (docs/reports) の順です。main に ruleset (11550302) があり、マージ時に一時的に無効化した記録があります (`docs/reports/2026-11-09-release-1.0.0.md` 12 行)。

---

## 9. ローカルでのビルドとテストの手順 (記載の所在。実行はしていません)

- README 112-128 行:
  ```
  cmake --preset windows-x64 -DENABLE_POST_BUILD_INSTALL=OFF
  cmake --build build_x64 --config RelWithDebInfo
  cmake --install build_x64 --config RelWithDebInfo --prefix release/RelWithDebInfo
  ```
  macOS は `--preset macos` と `build_macos`、Ubuntu は `--preset ubuntu-x86_64` と `build_x86_64` です。
- 描画モジュールの用意は `CMakeLists.txt` 102-103 行のコメントにあります: `cmake --build .deps/obs-studio-31.1.1/build_x64 --config Release --target libobs-opengl`。D3D11 は `--target libobs-d3d11` で、こちらは `.prompts/PLANS/2026-09-19-blur-test-feasibility.md:19` に記載があります。
- CTest の実行コマンド (`ctest --test-dir build_x64 -C RelWithDebInfo` など) は、README と docs のどちらにも書かれていません。`docs/reports/2026-09-19-issue-1-implementation.md:19` に、CTest の合格と `clang-format --dry-run --Werror`、`gersemi --check --no-cache CMakeLists.txt`、`git diff --check` の実施記録があるだけです。
- ベンチマークの実行例は `docs/logs/2026-11-08-stage1-blur-prototype.md` 22-29 行にあります。

---

## mosaic へ流用する際に変更が必要な箇所の一覧

| 区分 | ファイルと行 | 必要な変更 |
|---|---|---|
| 名前と版 | `buildspec.json:39-41` | name を `simple-mosaic` (案)、displayName と version を初期値に変える。作者情報 (42-44 行) は流用 |
| 名前と版 | `buildspec.json:36` | bundleId はテンプレートの値のまま。変更の要否を判断する |
| 名前と版 | `buildspec.json:3-32` | OBS と deps の版を判断する。上げる場合は、下の `.deps/obs-studio-31.1.1` などのパスも連動して変える |
| ビルド | `CMakeLists.txt:78-97` | テスト用のターゲット名 `drop-shadow-filter-*` を改名する |
| ビルド | `CMakeLists.txt:104,125,131` | OBS と obs-deps の版を含むパス (版を変えるときだけ) |
| ビルド | `CMakeLists.txt:107-117,120` | シナリオの一覧とテスト名の接頭辞を mosaic 用にする |
| ビルド | `CMakeLists.txt:137-151` | install-probe を流用するなら改名し、使わないなら削除する |
| ビルド | `cmake/windows/helpers.cmake:25-32` | 変更不要。OBS 33 の新配置の改変を引き継ぐ |
| ソース | `src/drop-shadow-filter.cpp` 全体 | mosaic のフィルターとして書き直す。id、ロケールキー、エフェクトのパス、クラス名、ログの接頭辞、プロパティを変える。出力の大きさが変わらないなら、`get_width/get_height`、`filter_add/remove`、`enable` シグナル、mutex は不要。描画は `obs_source_process_filter_begin/end` を検討する (推測) |
| ソース | `src/item-compensation.cpp/.hpp` | 削除する (出力が広がる drop-shadow 固有の処理) |
| ソース | `src/plugin-main.cpp:7,11` | 登録関数の名前 |
| ソース | `src/plugin-support.c.in`、`plugin-support.h` 1-17 行 | ライセンスのヘッダーはテンプレートの雛形のまま。変更不要。直す場合は任意 |
| シェーダー | `data/effects/drop-shadow.effect` | `mosaic.effect` (案) として新しく書く。`VSDefault`、`linearClamp` (モザイクでは Point の sampler も候補)、`Sample` だけを使う、キャストはコンストラクタ形式、画素中心への丸め、の方針を引き継ぐ |
| ロケール | `data/locale/en-US.ini`、`ja-JP.ini` | キーと訳語を mosaic 用にする |
| テスト | `tests/obs-test-env.hpp` | ほぼそのまま流用できる。固有名詞は無い |
| テスト | `tests/filter-render-fixture.hpp:97,101` | テスト用ソースの id と名前を変える。`single_pixel` と `fill_opaque` の描き方は、モザイクの検査用 (グラデーションや格子など) に足す |
| テスト | `tests/filter-integration-test.cpp` 全体 | シナリオと期待値を mosaic 用に書き直す。`main` の骨格 (514-558 行)、`update_and_wait`、`wait_frames` は流用できる。シーンの補正の検査 (173-390 行) は不要 |
| テスト | `tests/filter-benchmark.cpp` | time モードの計測の骨格 (90-142、198-210 行) は流用できる。profile モード、設定のキー、ソースとフィルターの id を変える |
| テスト | `tests/install-probe.cpp` | 流用するなら id、名前、環境変数、画素の検査、旧版の扱いを書き直す |
| CI | `.github/workflows/render-test.yaml:1-23,49,54-56,106,111-113` | ワークフロー名、`SCENARIOS`、`PROFILE_RADII`、ターゲット名、`.so`/`.plugin` の名前、push のトリガー (11 行の `feature/ui-improvement`) を変える。OBS の版のパス (107、114、122 行) も確認する |
| CI | `.github/workflows/verify-obs-install.yaml`、`.github/scripts/Test-ObsLoad-Windows.ps1` | 製品名、probe 名、環境変数、エフェクト名、表示名、保存された設定の検査、v0.1.1 の zip の URL とハッシュ (46-48 行) を変える。旧版からの更新の段 (old-setup、upgrade) は mosaic に旧版が無いため削る |
| CI | `.github/scripts/Build-Windows.ps1:51-54` | 変更不要 (`-EnableInstallProbe` を使わないなら、そのままでも害は無い) |
| CI | `.github/workflows/push.yaml:44-56` | タグの命名 (`v` の有無) と extglob を揃える判断が要る |
| CI | `build-project.yaml`、`pr-pull.yaml`、`check-format.yaml`、`dispatch.yaml`、`.github/actions/*` | 変更不要 (固有名詞は無い) |
| 除外設定 | `.gitignore` | 流用可。新しいトップレベルのディレクトリを追跡するときは `!/` を足す |
| 文書 | `README.md` 全体 | 構成 (概要、GIF、機能、導入、使い方、パラメーター、既知の挙動、ビルド、AI 開示、ライセンス、作者、更新情報) を流用し、本文を書き直す。導入の構成図 (40-49 行) のフォルダー名とエフェクト名を変える |
| 文書 | `LICENSE` | そのまま流用 (GPL-2.0) |
| 文書 | `docs/` | 中身は drop-shadow 固有。`INDEX.md` の形式だけを流用する |
| 素材 | `.soko-kun/logo.png`、`image.gif` | mosaic 用に作り直す (掲載するとき) |
| ローカル | `.beads/`、`.prompts/`、`.logs/`、`.tmp/`、`.deps/` | コピーしない。`.deps` は configure 時に buildspec から取得される。ただし `libobs-d3d11` と `libobs-opengl` は手動でのビルドが要る |

## 調べた範囲と調べていない範囲

- 調べた範囲: git 管理下の全ファイル一覧、`CMakeLists.txt`、`buildspec.json`、`CMakePresets.json`、`cmake/common/bootstrap.cmake`、`cmake/windows/{helpers,defaults}.cmake`、`cmake/common/buildspec_common.cmake` 40-110 行、`src/` の全ファイル (`item-compensation.cpp` は冒頭 60 行)、`data/` の全ファイル、`tests/` の全ファイル、`.github/workflows/` の全ファイル、`Build-Windows.ps1`、`Package-Windows.ps1`、`Test-ObsLoad-Windows.ps1`、README、`docs/INDEX.md`、docs の 3 件 (初回セットアップ、1.0.0 の公開記録、段階 1 のログ)、`git log`、git の graph とタグ。固有名詞は git 管理下を grep して確かめました。
- 読んでいないもの: `.github/actions/*` の本文 (grep で固有名詞が無いことだけを確認)、`cmake/macos/*`、`cmake/linux/*`、`build-aux/*` の本文、docs/reports の残り 4 件、`.prompts/` と `.logs/` の中身。

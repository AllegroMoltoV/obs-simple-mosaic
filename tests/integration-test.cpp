// モザイクのフィルターの出力を画素で検査する統合テスト。
// フィルターを付ける前の出力を基準の画像として読み、付けたあとの出力と比べる。
// 検査するのは、範囲の外の画素が基準と同じであること、範囲の中がマスごとに同じ色であること、その色がマスの平均に近いことの 3 点。
// 使い方: integration-test <プラグイン> <プラグインの data> <描画モジュール> <libobs の data> <シナリオ>
// 終了コードは、合格が 0、検査の不合格が 1、起動の失敗が 2。

#include "mosaic-fixture.hpp"

#include <cstring>

namespace {

using namespace mosaic_test;

constexpr uint32_t kCanvasWidth = 64;
constexpr uint32_t kCanvasHeight = 48;
// マスの平均の許容差。8 ビットへの丸めと、線形補間の重みの量子化の分だけ許す
constexpr int kAverageTolerance = 2;

struct Rect {
	uint32_t left;
	uint32_t top;
	uint32_t right;
	uint32_t bottom;

	bool contains(uint32_t x, uint32_t y) const { return x >= left && x < right && y >= top && y < bottom; }
};

std::string at(uint32_t x, uint32_t y)
{
	return "(" + std::to_string(x) + ", " + std::to_string(y) + ")";
}

// out が、rect の外で reference と同じ、rect の中で cell ごとに同じ色かつ reference の平均に近いことを検査する
void check_mosaic(Checker &c, const Image &reference, const Image &out, const Rect &rect, uint32_t cell)
{
	if (out.width != reference.width || out.height != reference.height) {
		c.expect(false, "output size " + std::to_string(out.width) + "x" + std::to_string(out.height) +
					" differs from the reference");
		return;
	}
	for (uint32_t y = 0; y < out.height; ++y) {
		for (uint32_t x = 0; x < out.width; ++x) {
			const uint8_t *o = out.at(x, y);
			if (!rect.contains(x, y)) {
				const uint8_t *r = reference.at(x, y);
				for (int ch = 0; ch < 4; ++ch) {
					c.expect(std::abs(o[ch] - r[ch]) <= 1, "outside pixel changed at " + at(x, y));
				}
				continue;
			}
			const uint32_t bx = rect.left + ((x - rect.left) / cell) * cell;
			const uint32_t by = rect.top + ((y - rect.top) / cell) * cell;
			const uint8_t *first = out.at(bx, by);
			c.expect(std::memcmp(o, first, 4) == 0,
				 "pixel " + at(x, y) + " differs from its cell origin " + at(bx, by));
			if (x != bx || y != by) {
				continue;
			}
			// マスの左上の画素で、マスの平均と比べる
			const uint32_t ex = std::min(bx + cell, rect.right);
			const uint32_t ey = std::min(by + cell, rect.bottom);
			for (int ch = 0; ch < 4; ++ch) {
				long sum = 0;
				for (uint32_t yy = by; yy < ey; ++yy) {
					for (uint32_t xx = bx; xx < ex; ++xx) {
						sum += reference.at(xx, yy)[ch];
					}
				}
				const long count = static_cast<long>(ex - bx) * (ey - by);
				const int expected = static_cast<int>((sum + count / 2) / count);
				c.expect(std::abs(first[ch] - expected) <= kAverageTolerance,
					 "cell " + at(bx, by) + " channel " + std::to_string(ch) + ": " +
						 std::to_string(first[ch]) + " (expected " + std::to_string(expected) +
						 ")");
			}
		}
	}
}

// 範囲の中に、隣と色の違うマスが少なくとも 1 つあることを確かめる。全体が 1 色になる誤りを検出する
void check_not_flat(Checker &c, const Image &out, const Rect &rect, uint32_t cell)
{
	if (rect.right - rect.left <= cell) {
		return;
	}
	c.expect(std::memcmp(out.at(rect.left, rect.top), out.at(rect.left + cell, rect.top), 4) != 0,
		 "adjacent cells have the same color");
}

int64_t setting(obs_source_t *filter, const char *name)
{
	obs_data_t *settings = obs_source_get_settings(filter);
	const int64_t value = obs_data_get_int(settings, name);
	obs_data_release(settings);
	return value;
}

// フィルターの付いたソースを 1 つ使うシナリオの共通部分。基準を読み、フィルターを付け、check を呼ぶ
using SourceCheck = std::function<void(Checker &, obs_source_t *source, obs_source_t *filter, const Image &reference)>;

void with_filtered_source(Checker &c, const std::string &name, const SourceSpec &spec, obs_data_t *settings,
			  const SourceCheck &check)
{
	obs_source_t *source = create_test_source(spec);
	obs_source_t *filter = obs_source_create(kFilterId, "mosaic", settings, nullptr);
	Image reference;
	if (!source || !filter) {
		c.expect(false, "failed to create the test source or the filter");
	} else if (!render(source, reference)) {
		c.expect(false, "failed to render the reference");
	} else {
		save_image(name + "-reference", reference);
		obs_source_filter_add(source, filter);
		// 範囲の初期化と設定の反映は video_tick で行われるため、数フレーム待つ
		c.expect(wait_frames(4), "frames did not advance");
		check(c, source, filter, reference);
	}
	obs_source_release(filter);
	obs_source_release(source);
}

bool render_and_save(Checker &c, const std::string &name, obs_source_t *source, Image &out)
{
	if (!render(source, out)) {
		c.expect(false, "failed to render " + name);
		return false;
	}
	save_image(name, out);
	return true;
}

const SourceSpec gradient = {kCanvasWidth, kCanvasHeight, paint_gradient};

struct Scenario {
	const char *name;
	std::function<void(Checker &)> run;
};

const Scenario scenarios[] = {
	// 設定を渡さずに付けると、範囲が対象の全体へ初期化され、全体がモザイクになる
	{"default-full",
	 [](Checker &c) {
		 with_filtered_source(
			 c, "default-full", gradient, nullptr,
			 [](Checker &c, obs_source_t *source, obs_source_t *filter, const Image &ref) {
				 c.expect(setting(filter, "region_x") == 0 && setting(filter, "region_y") == 0 &&
						  setting(filter, "region_width") == kCanvasWidth &&
						  setting(filter, "region_height") == kCanvasHeight,
					  "region was not initialized to the whole target");
				 c.expect(setting(filter, "cell_size") == 16, "default cell size is not 16");
				 Image out;
				 if (render_and_save(c, "default-full", source, out)) {
					 const Rect all = {0, 0, kCanvasWidth, kCanvasHeight};
					 check_mosaic(c, ref, out, all, 16);
					 check_not_flat(c, out, all, 16);
				 }
			 });
	 }},
	// 範囲の外は元のまま、範囲の中はマスの区切りが範囲の左上にそろう
	{"region",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(5, 3, 40, 24, 8);
		 with_filtered_source(c, "region", gradient, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *, const Image &ref) {
					      Image out;
					      if (render_and_save(c, "region", source, out)) {
						      check_mosaic(c, ref, out, {5, 3, 45, 27}, 8);
						      check_not_flat(c, out, {5, 3, 45, 27}, 8);
					      }
				      });
		 obs_data_release(settings);
	 }},
	// 対象からはみ出した範囲は内側だけにかかり、端のマスは欠けた大きさで平均する
	{"overflow",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(41, 30, 100, 100, 8);
		 with_filtered_source(c, "overflow", gradient, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *, const Image &ref) {
					      Image out;
					      if (render_and_save(c, "overflow", source, out)) {
						      check_mosaic(c, ref, out, {41, 30, kCanvasWidth, kCanvasHeight},
								   8);
					      }
				      });
		 obs_data_release(settings);
	 }},
	// 大きいマスでも平均が正しい。マスの一辺 64 px に対し対象の高さが 48 px のため、欠けたマス 1 つになる
	{"large-cell",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(0, 0, kCanvasWidth, kCanvasHeight, 64);
		 with_filtered_source(c, "large-cell", gradient, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *, const Image &ref) {
					      Image out;
					      if (render_and_save(c, "large-cell", source, out)) {
						      check_mosaic(c, ref, out, {0, 0, kCanvasWidth, kCanvasHeight},
								   64);
					      }
				      });
		 obs_data_release(settings);
	 }},
	// 幅か高さが 0 なら何もかけない
	{"zero-size",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(8, 8, 0, 24, 8);
		 with_filtered_source(c, "zero-size", gradient, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *filter, const Image &ref) {
					      Image out;
					      if (render_and_save(c, "zero-size-width", source, out)) {
						      check_mosaic(c, ref, out, {0, 0, 0, 0}, 8);
					      }
					      obs_data_t *next = region_settings(8, 8, 24, 0, 8);
					      c.expect(update_and_wait(filter, next), "settings were not applied");
					      obs_data_release(next);
					      if (render_and_save(c, "zero-size-height", source, out)) {
						      check_mosaic(c, ref, out, {0, 0, 0, 0}, 8);
					      }
				      });
		 obs_data_release(settings);
	 }},
	// 対象の大きさが変わっても範囲は px のまま保たれ、広がった部分はモザイクの外になる
	{"resize-keeps-px",
	 [](Checker &c) {
		 with_filtered_source(
			 c, "resize-keeps-px", gradient, nullptr,
			 [](Checker &c, obs_source_t *source, obs_source_t *filter, const Image &) {
				 obs_data_t *size = obs_data_create();
				 obs_data_set_int(size, "width", 96);
				 obs_data_set_int(size, "height", 64);
				 c.expect(update_and_wait(source, size), "source size was not applied");
				 obs_data_release(size);
				 c.expect(wait_frames(4), "frames did not advance");
				 c.expect(setting(filter, "region_width") == kCanvasWidth &&
						  setting(filter, "region_height") == kCanvasHeight,
					  "region followed the new target size");
				 // 大きさを変えたソースの基準を、フィルターを無効にして読み直す
				 obs_source_set_enabled(filter, false);
				 Image ref;
				 const bool ok_ref = render_and_save(c, "resize-keeps-px-reference", source, ref);
				 obs_source_set_enabled(filter, true);
				 Image out;
				 if (ok_ref && render_and_save(c, "resize-keeps-px", source, out)) {
					 c.expect(out.width == 96 && out.height == 64,
						  "output size did not follow the source");
					 check_mosaic(c, ref, out, {0, 0, kCanvasWidth, kCanvasHeight}, 16);
				 }
			 });
	 }},
	// 半透明の画素を含むマスの色は、アルファを掛けたまま平均する。透明な画素の色 (黒) が混ざって暗くならない
	{"translucent",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(0, 0, kCanvasWidth, kCanvasHeight, 8);
		 with_filtered_source(c, "translucent", {kCanvasWidth, kCanvasHeight, paint_red_checker}, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *, const Image &ref) {
					      Image out;
					      if (!render_and_save(c, "translucent", source, out)) {
						      return;
					      }
					      check_mosaic(c, ref, out, {0, 0, kCanvasWidth, kCanvasHeight}, 8);
					      // アルファを掛けた赤はアルファと同じ値になる。アルファで割り戻すと純粋な赤に戻る
					      const uint8_t *p = out.at(0, 0);
					      c.expect(std::abs(p[3] - 128) <= 2, "cell alpha is not about half");
					      c.expect(std::abs(p[0] - p[3]) <= 2 && p[1] == 0 && p[2] == 0,
						       "cell color is darker than pure red");
				      });
		 obs_data_release(settings);
	 }},
	// シーンに付けると、中の項目を合成した後の画像にかかる
	{"scene",
	 [](Checker &c) {
		 obs_scene_t *scene = obs_scene_create("scene");
		 obs_source_t *background = create_test_source(gradient, "background");
		 obs_source_t *box = create_test_source({16, 16, paint_solid(0, 0, 255)}, "box");
		 obs_sceneitem_t *box_item = nullptr;
		 if (scene && background && box) {
			 obs_scene_add(scene, background);
			 box_item = obs_scene_add(scene, box);
			 vec2 pos;
			 vec2_set(&pos, 20.0f, 10.0f);
			 obs_sceneitem_set_pos(box_item, &pos);
		 }
		 obs_source_t *scene_source = scene ? obs_scene_get_source(scene) : nullptr;
		 obs_data_t *settings = region_settings(8, 8, 32, 24, 8);
		 obs_source_t *filter = obs_source_create(kFilterId, "mosaic", settings, nullptr);
		 obs_data_release(settings);
		 Image ref;
		 if (!scene_source || !box_item || !filter) {
			 c.expect(false, "failed to create the scene or the filter");
		 } else if (wait_frames(2) && render_and_save(c, "scene-reference", scene_source, ref)) {
			 // 青い箱が合成されていることを基準で確かめる
			 c.expect(ref.at(24, 14)[2] == 255 && ref.at(24, 14)[0] == 0, "box is not composited");
			 obs_source_filter_add(scene_source, filter);
			 c.expect(wait_frames(4), "frames did not advance");
			 Image out;
			 if (render_and_save(c, "scene", scene_source, out)) {
				 check_mosaic(c, ref, out, {8, 8, 40, 32}, 8);
				 check_not_flat(c, out, {8, 8, 40, 32}, 8);
			 }
		 }
		 obs_source_release(filter);
		 obs_source_release(box);
		 obs_source_release(background);
		 if (scene) {
			 // OBS 31 以降はキャンバスがシーンを保持するため、キャンバスから外してから解放する
			 obs_source_remove(scene_source);
			 obs_scene_release(scene);
		 }
	 }},
	// 範囲とマスの大きさの変更が出力に反映される
	{"settings-update",
	 [](Checker &c) {
		 obs_data_t *settings = region_settings(0, 0, 32, 24, 8);
		 with_filtered_source(c, "settings-update", gradient, settings,
				      [](Checker &c, obs_source_t *source, obs_source_t *filter, const Image &ref) {
					      Image before;
					      if (render_and_save(c, "settings-update-before", source, before)) {
						      check_mosaic(c, ref, before, {0, 0, 32, 24}, 8);
					      }
					      obs_data_t *next = region_settings(16, 8, 40, 32, 16);
					      c.expect(update_and_wait(filter, next), "settings were not applied");
					      obs_data_release(next);
					      Image after;
					      if (render_and_save(c, "settings-update-after", source, after)) {
						      check_mosaic(c, ref, after, {16, 8, 56, 40}, 16);
					      }
				      });
		 obs_data_release(settings);
	 }},
};

} // namespace

int main(int argc, char **argv)
{
	if (argc != 6) {
		std::cerr
			<< "usage: integration-test <plugin> <plugin data> <graphics module> <libobs data> <scenario>\n";
		return 2;
	}
	const Scenario *scenario = nullptr;
	for (const Scenario &s : scenarios) {
		if (std::strcmp(argv[5], s.name) == 0) {
			scenario = &s;
		}
	}
	if (!scenario) {
		std::cerr << "unknown scenario: " << argv[5] << '\n';
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], kCanvasWidth, kCanvasHeight)) {
		return 2;
	}
	register_test_source();
	int result = 2;
	if (obs_test::load_plugin(argv[1], argv[2])) {
		Checker checker;
		scenario->run(checker);
		result = checker.failures == 0 ? 0 : 1;
		if (result != 0) {
			std::cerr << checker.failures << " check(s) failed in " << scenario->name << '\n';
		} else {
			std::cout << "PASS " << scenario->name << '\n';
		}
	}
	obs_shutdown();
	return result;
}

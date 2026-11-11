#pragma once

// 統合テストとベンチマークで共通に使う部品。テスト用の入力ソースの登録、フィルターの作成、出力の読み出し、
// 目視の確認用の画像の書き出しを行う。

#include "obs-test-env.hpp"

#include <obs.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

namespace mosaic_test {

constexpr const char *kFilterId = "simple_mosaic_filter";
constexpr const char *kTestSourceId = "simple_mosaic_test_source";

// 出力の画素。色はアルファを掛けた値で、フィルターの出力と同じ表現
struct Image {
	uint32_t width = 0;
	uint32_t height = 0;
	std::vector<uint8_t> rgba;

	const uint8_t *at(uint32_t x, uint32_t y) const { return &rgba[(static_cast<size_t>(y) * width + x) * 4]; }
};

// テスト用ソースの描き方。色はアルファを掛けない値で書く
using Painter = std::function<void(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)>;

// 次に作るテスト用ソースの大きさと描き方
struct SourceSpec {
	uint32_t width = 64;
	uint32_t height = 48;
	Painter paint;
};

inline SourceSpec next_spec;

struct TestSource {
	gs_texture_t *texture = nullptr;
	uint32_t width = 0;
	uint32_t height = 0;
	Painter paint;
};

inline bool rebuild_texture(TestSource &source)
{
	std::vector<uint8_t> pixels(static_cast<size_t>(source.width) * source.height * 4, 0);
	if (source.paint) {
		source.paint(pixels, source.width, source.height);
	}
	const uint8_t *data = pixels.data();
	obs_enter_graphics();
	gs_texture_destroy(source.texture);
	source.texture = gs_texture_create(source.width, source.height, GS_RGBA, 1, &data, 0);
	obs_leave_graphics();
	return source.texture != nullptr;
}

// 設定の width と height で大きさを変えられる。対象の大きさが変わったときの挙動の検査に使う
inline void update_source(void *data, obs_data_t *settings)
{
	auto *source = static_cast<TestSource *>(data);
	const auto width = static_cast<uint32_t>(obs_data_get_int(settings, "width"));
	const auto height = static_cast<uint32_t>(obs_data_get_int(settings, "height"));
	if (width > 0 && height > 0 && (width != source->width || height != source->height)) {
		source->width = width;
		source->height = height;
		rebuild_texture(*source);
	}
}

inline void register_test_source()
{
	obs_source_info info = {};
	info.id = kTestSourceId;
	info.type = OBS_SOURCE_TYPE_INPUT;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = [](void *) {
		return "Mosaic test source";
	};
	info.create = [](obs_data_t *, obs_source_t *) -> void * {
		auto *source = new TestSource;
		source->width = next_spec.width;
		source->height = next_spec.height;
		source->paint = next_spec.paint;
		if (!rebuild_texture(*source)) {
			delete source;
			return nullptr;
		}
		return source;
	};
	info.destroy = [](void *data) {
		auto *source = static_cast<TestSource *>(data);
		obs_enter_graphics();
		gs_texture_destroy(source->texture);
		obs_leave_graphics();
		delete source;
	};
	info.update = update_source;
	info.get_width = [](void *data) {
		return static_cast<TestSource *>(data)->width;
	};
	info.get_height = [](void *data) {
		return static_cast<TestSource *>(data)->height;
	};
	info.video_render = [](void *data, gs_effect_t *effect) {
		auto *source = static_cast<TestSource *>(data);
		gs_effect_set_texture(gs_effect_get_param_by_name(effect, "image"), source->texture);
		gs_draw_sprite(source->texture, 0, source->width, source->height);
	};
	obs_register_source(&info);
}

inline obs_source_t *create_test_source(const SourceSpec &spec, const char *name = "source")
{
	next_spec = spec;
	return obs_source_create(kTestSourceId, name, nullptr, nullptr);
}

// 範囲を明示した設定。region_initialized を立て、対象の全体への初期化を行わせない
inline obs_data_t *region_settings(int x, int y, int width, int height, int cell_size)
{
	obs_data_t *settings = obs_data_create();
	obs_data_set_int(settings, "region_x", x);
	obs_data_set_int(settings, "region_y", y);
	obs_data_set_int(settings, "region_width", width);
	obs_data_set_int(settings, "region_height", height);
	obs_data_set_int(settings, "cell_size", cell_size);
	obs_data_set_bool(settings, "region_initialized", true);
	return settings;
}

// 色が x と y で変わる不透明なグラデーション。マスの平均が画素ごとに異なる値になる
inline void paint_gradient(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
{
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
			p[0] = static_cast<uint8_t>((x * 255) / std::max<uint32_t>(1, width - 1));
			p[1] = static_cast<uint8_t>((y * 255) / std::max<uint32_t>(1, height - 1));
			p[2] = static_cast<uint8_t>(((x + y) * 7) % 256);
			p[3] = 255;
		}
	}
}

// 単色の不透明な塗りつぶし
inline Painter paint_solid(uint8_t r, uint8_t g, uint8_t b)
{
	return [=](std::vector<uint8_t> &pixels, uint32_t, uint32_t) {
		for (size_t i = 0; i < pixels.size(); i += 4) {
			pixels[i] = r;
			pixels[i + 1] = g;
			pixels[i + 2] = b;
			pixels[i + 3] = 255;
		}
	};
}

// 不透明な赤と完全な透明 (色は黒) の市松模様。透明な画素の色がマスの色に混ざると暗くなる
inline void paint_red_checker(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height)
{
	for (uint32_t y = 0; y < height; ++y) {
		for (uint32_t x = 0; x < width; ++x) {
			uint8_t *p = &pixels[(static_cast<size_t>(y) * width + x) * 4];
			const bool opaque = (x + y) % 2 == 0;
			p[0] = opaque ? 255 : 0;
			p[1] = 0;
			p[2] = 0;
			p[3] = opaque ? 255 : 0;
		}
	}
}

// OBS の描画スレッドがフレームを count 個進めるまで待つ。設定の変更は次のフレームの video_tick で反映される
inline bool wait_frames(uint64_t count)
{
	const uint64_t target = obs_get_total_frames() + count;
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	while (obs_get_total_frames() < target) {
		if (std::chrono::steady_clock::now() > deadline) {
			std::cerr << "OBS graphics thread did not advance within five seconds\n";
			return false;
		}
		std::this_thread::sleep_for(std::chrono::milliseconds(2));
	}
	return true;
}

inline void mark_updated(void *data, calldata_t *)
{
	static_cast<std::atomic_bool *>(data)->store(true);
}

// 設定を渡し、OBS が update を呼ぶまで最大 2 秒待つ
inline bool update_and_wait(obs_source_t *source, obs_data_t *settings)
{
	std::atomic_bool applied = false;
	signal_handler_t *signals = obs_source_get_signal_handler(source);
	signal_handler_connect(signals, "update", mark_updated, &applied);
	obs_source_update(source, settings);
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
	while (!applied.load() && std::chrono::steady_clock::now() < deadline) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
	}
	signal_handler_disconnect(signals, "update", mark_updated, &applied);
	return applied.load();
}

// ソースの出力を、ソースの大きさのまま読み出す。合成せずに出力をそのまま書き込む
inline bool render(obs_source_t *source, Image &image)
{
	bool ok = false;
	obs_enter_graphics();
	image.width = obs_source_get_width(source);
	image.height = obs_source_get_height(source);
	gs_texrender_t *output = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_stagesurf_t *staging = gs_stagesurface_create(image.width, image.height, GS_RGBA);
	if (output && staging && gs_texrender_begin(output, image.width, image.height)) {
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(image.width), 0.0f, static_cast<float>(image.height), -100.0f,
			 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		obs_source_video_render(source);
		gs_blend_state_pop();
		gs_texrender_end(output);
		gs_stage_texture(staging, gs_texrender_get_texture(output));
		uint8_t *mapped = nullptr;
		uint32_t stride = 0;
		if (gs_stagesurface_map(staging, &mapped, &stride)) {
			image.rgba.resize(static_cast<size_t>(image.width) * image.height * 4);
			for (uint32_t y = 0; y < image.height; ++y) {
				std::copy(mapped + static_cast<size_t>(y) * stride,
					  mapped + static_cast<size_t>(y) * stride + image.width * 4,
					  image.rgba.begin() + static_cast<size_t>(y) * image.width * 4);
			}
			gs_stagesurface_unmap(staging);
			ok = true;
		}
	}
	if (!ok) {
		std::cerr << "failed to render the source\n";
	}
	gs_stagesurface_destroy(staging);
	gs_texrender_destroy(output);
	obs_leave_graphics();
	return ok;
}

// 24 ビットの BMP を書き出す。pixel(x, y) は上から数えた位置の RGB を返す
template<typename PixelFn>
inline bool write_bmp(const std::string &path, uint32_t width, uint32_t height, PixelFn pixel)
{
	const uint32_t row_size = (width * 3 + 3) & ~3u;
	const uint32_t data_size = row_size * height;
	uint8_t header[54] = {'B', 'M'};
	const auto put32 = [&header](size_t offset, uint32_t value) {
		for (int i = 0; i < 4; ++i) {
			header[offset + i] = static_cast<uint8_t>(value >> (8 * i));
		}
	};
	put32(2, 54 + data_size);
	put32(10, 54);
	put32(14, 40);
	put32(18, width);
	put32(22, height);
	header[26] = 1;
	header[28] = 24;
	put32(34, data_size);
	std::ofstream file(path, std::ios::binary);
	if (!file) {
		return false;
	}
	file.write(reinterpret_cast<const char *>(header), sizeof(header));
	std::vector<uint8_t> row(row_size, 0);
	// BMP は下の行から並べる。各画素は B・G・R の順
	for (uint32_t y = height; y-- > 0;) {
		for (uint32_t x = 0; x < width; ++x) {
			const auto rgb = pixel(x, y);
			row[x * 3 + 0] = rgb[2];
			row[x * 3 + 1] = rgb[1];
			row[x * 3 + 2] = rgb[0];
		}
		file.write(reinterpret_cast<const char *>(row.data()), row.size());
	}
	return static_cast<bool>(file);
}

// 環境変数 MOSAIC_TEST_OUTPUT_DIR があるときだけ、目視の確認のために出力を白い背景に合成して書き出す
inline void save_image(const std::string &name, const Image &image)
{
	const char *dir = std::getenv("MOSAIC_TEST_OUTPUT_DIR");
	if (!dir || image.rgba.empty()) {
		return;
	}
	const std::string path = std::string(dir) + "/" + name + ".bmp";
	// 色はアルファを掛けた値のため、白い背景には (1 - アルファ) 分の白を足す
	const bool ok = write_bmp(path, image.width, image.height, [&image](uint32_t x, uint32_t y) {
		const uint8_t *p = image.at(x, y);
		std::array<uint8_t, 3> rgb;
		for (int c = 0; c < 3; ++c) {
			rgb[c] = static_cast<uint8_t>(std::min(255, p[c] + (255 - p[3])));
		}
		return rgb;
	});
	std::cout << (ok ? "saved " : "failed to save ") << path << '\n';
}

// 失敗した検査を数えて報告する。同じ種類の失敗が大量に出ても読めるよう、表示は種類ごとに先頭の数件に絞る
struct Checker {
	int failures = 0;
	int printed = 0;

	void expect(bool condition, const std::string &message)
	{
		if (!condition) {
			if (printed < 20) {
				std::cerr << message << '\n';
				++printed;
			}
			++failures;
		}
	}
};

} // namespace mosaic_test

// モザイクのフィルターの 1 フレームあたりの GPU 時間を、1920x1080 の全体にかけて測るベンチマーク。
// 計測は gs_timer (タイムスタンプのクエリ) で行う。フィルターなしで同じソースを描く時間を基準とし、その差をフィルターの時間とする。
// 使い方: benchmark <プラグイン> <プラグインの data> <描画モジュール> <libobs の data> <マスの大きさ>...
// 環境で値が変わるため CTest には登録しない。

#include "mosaic-fixture.hpp"

#include <iomanip>

namespace {

using namespace mosaic_test;

constexpr uint32_t kWidth = 1920;
constexpr uint32_t kHeight = 1080;
constexpr int kWarmupFrames = 10;
constexpr int kMeasuredFrames = 60;
constexpr int kRepeats = 5;

// 1 フレームあたりの GPU 時間 (ミリ秒) を返す。失敗時は負の値
double measure(obs_source_t *source)
{
	double result = -1.0;
	obs_enter_graphics();
	gs_texrender_t *target = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	gs_timer_range_t *range = gs_timer_range_create();
	gs_timer_t *timer = gs_timer_create();

	auto render_frame = [&]() {
		gs_texrender_reset(target);
		if (!gs_texrender_begin(target, kWidth, kHeight)) {
			return false;
		}
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(kWidth), 0.0f, static_cast<float>(kHeight), -100.0f, 100.0f);
		obs_source_video_render(source);
		gs_texrender_end(target);
		return true;
	};

	// OpenGL の libobs は範囲を作らず NULL を返す。範囲のデータの取得は引数を使わずに周波数を返すため、
	// 範囲が無いときは開始と終了だけを省く
	bool ok = target && timer;
	for (int i = 0; ok && i < kWarmupFrames; ++i) {
		ok = render_frame();
	}
	if (ok) {
		if (range) {
			gs_timer_range_begin(range);
		}
		gs_timer_begin(timer);
		for (int i = 0; ok && i < kMeasuredFrames; ++i) {
			ok = render_frame();
		}
		gs_timer_end(timer);
		if (range) {
			gs_timer_range_end(range);
		}
		gs_flush();
	}
	if (ok) {
		bool disjoint = true;
		uint64_t frequency = 0;
		uint64_t ticks = 0;
		if (gs_timer_range_get_data(range, &disjoint, &frequency) && gs_timer_get_data(timer, &ticks) &&
		    !disjoint && frequency > 0) {
			result = static_cast<double>(ticks) * 1000.0 / static_cast<double>(frequency) / kMeasuredFrames;
		}
	}
	gs_timer_destroy(timer);
	if (range) {
		gs_timer_range_destroy(range);
	}
	gs_texrender_destroy(target);
	obs_leave_graphics();
	return result;
}

double median_of_repeats(obs_source_t *source)
{
	std::vector<double> samples;
	for (int i = 0; i < kRepeats; ++i) {
		const double ms = measure(source);
		if (ms < 0.0) {
			return -1.0;
		}
		samples.push_back(ms);
	}
	std::sort(samples.begin(), samples.end());
	return samples[samples.size() / 2];
}

} // namespace

int main(int argc, char **argv)
{
	if (argc < 6) {
		std::cerr << "usage: benchmark <plugin> <plugin data> <graphics module> <libobs data> <cell size>...\n";
		return 2;
	}
	if (!obs_test::start(argv[3], argv[4], kWidth, kHeight)) {
		return 2;
	}
	register_test_source();
	if (!obs_test::load_plugin(argv[1], argv[2])) {
		obs_shutdown();
		return 2;
	}

	const SourceSpec spec = {kWidth, kHeight, paint_gradient};
	obs_source_t *plain = create_test_source(spec, "plain");
	const double baseline = plain ? median_of_repeats(plain) : -1.0;
	obs_source_release(plain);
	if (baseline <= 0.0) {
		std::cerr << "failed to measure the baseline\n";
		obs_shutdown();
		return 2;
	}

	int exit_code = 0;
	std::cout << std::fixed << std::setprecision(3);
	std::cout << "cell_size,width,height,frame_ms,baseline_ms,filter_ms\n";
	for (int i = 5; i < argc; ++i) {
		obs_source_t *source = create_test_source(spec, "filtered");
		obs_data_t *settings = region_settings(0, 0, kWidth, kHeight, std::atoi(argv[i]));
		obs_source_t *filter = obs_source_create(kFilterId, "mosaic", settings, nullptr);
		obs_data_release(settings);
		double frame = -1.0;
		if (source && filter) {
			obs_source_filter_add(source, filter);
			wait_frames(2);
			frame = median_of_repeats(source);
		}
		std::cout << argv[i] << ',' << kWidth << ',' << kHeight << ',' << frame << ',' << baseline << ','
			  << (frame - baseline) << '\n';
		// 1920x1080 を 60 フレーム描いて 0 になることはない。0 以下はタイムスタンプが取れなかったことを示す
		if (frame <= 0.0) {
			std::cerr << "GPU timestamps are unavailable on this graphics backend\n";
			exit_code = 1;
		}
		obs_source_release(filter);
		obs_source_release(source);
	}

	obs_shutdown();
	return exit_code;
}

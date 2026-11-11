// 四角形の範囲をモザイクにする映像フィルター。
// 範囲 (X・Y・幅・高さ) とマスの大きさを px で受け取り、範囲の中だけをマスごとの平均色にする。範囲の外は元の映像のまま通す。

#include <obs-module.h>
#include <plugin-support.h>

#include <algorithm>
#include <cstdint>
#include <new>

namespace {

constexpr const char *kRegionX = "region_x";
constexpr const char *kRegionY = "region_y";
constexpr const char *kRegionWidth = "region_width";
constexpr const char *kRegionHeight = "region_height";
constexpr const char *kCellSize = "cell_size";
// 範囲を対象の全体へ設定し終えたことを示す。設定画面には出さない
constexpr const char *kRegionInitialized = "region_initialized";

constexpr int kMinCellSize = 2;
constexpr int kMaxCellSize = 256;
constexpr int kDefaultCellSize = 16;
// 対象の大きさが分からないときのスライダーの上限
constexpr int kFallbackSliderMax = 8192;

class MosaicFilter {
public:
	MosaicFilter(obs_data_t *settings, obs_source_t *context) : context_(context)
	{
		obs_enter_graphics();
		char *path = obs_module_file("effects/mosaic.effect");
		if (path) {
			effect_ = gs_effect_create_from_file(path, nullptr);
			bfree(path);
		}
		if (effect_) {
			param_image_ = gs_effect_get_param_by_name(effect_, "image");
			param_rows_ = gs_effect_get_param_by_name(effect_, "rows");
			param_cells_ = gs_effect_get_param_by_name(effect_, "cells");
			param_tex_size_ = gs_effect_get_param_by_name(effect_, "tex_size");
			param_rect_ = gs_effect_get_param_by_name(effect_, "rect");
			param_cell_size_ = gs_effect_get_param_by_name(effect_, "cell_size");
			param_rows_size_ = gs_effect_get_param_by_name(effect_, "rows_size");
			param_cells_size_ = gs_effect_get_param_by_name(effect_, "cells_size");
			param_pair_count_ = gs_effect_get_param_by_name(effect_, "pair_count");
		}
		source_render_ = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		// 途中の平均は 8 ビットに丸めず、丸めの誤差が 2 段で重ならないよう浮動小数点で持つ
		rows_render_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
		cells_render_ = gs_texrender_create(GS_RGBA16F, GS_ZS_NONE);
		obs_leave_graphics();
		if (!Ready()) {
			obs_log(LOG_ERROR, "[mosaic] failed to load effects/mosaic.effect");
		}
		Update(settings);
	}

	~MosaicFilter()
	{
		obs_enter_graphics();
		gs_texrender_destroy(cells_render_);
		gs_texrender_destroy(rows_render_);
		gs_texrender_destroy(source_render_);
		gs_effect_destroy(effect_);
		obs_leave_graphics();
	}

	// OBS は映像のフィルターの update を描画スレッドで、描画の前に呼ぶ。そのため描画と同じスレッドで値を読み書きできる
	void Update(obs_data_t *settings)
	{
		region_x_ = obs_data_get_int(settings, kRegionX);
		region_y_ = obs_data_get_int(settings, kRegionY);
		region_width_ = std::max<long long>(0, obs_data_get_int(settings, kRegionWidth));
		region_height_ = std::max<long long>(0, obs_data_get_int(settings, kRegionHeight));
		cell_size_ = static_cast<int>(
			std::clamp<long long>(obs_data_get_int(settings, kCellSize), kMinCellSize, kMaxCellSize));
		region_initialized_ = obs_data_get_bool(settings, kRegionInitialized);
	}

	// libobs の既定値はインスタンスを受け取れず、対象の大きさを既定値にできない。そこで対象の大きさを初めて
	// 取得できたときに、範囲を対象の全体へ設定する。以後は利用者の設定した px を保つ
	void Tick()
	{
		if (region_initialized_) {
			return;
		}
		uint32_t width = 0;
		uint32_t height = 0;
		if (!TargetSize(width, height)) {
			return;
		}
		region_initialized_ = true;
		obs_data_t *settings = obs_data_create();
		obs_data_set_int(settings, kRegionX, 0);
		obs_data_set_int(settings, kRegionY, 0);
		obs_data_set_int(settings, kRegionWidth, width);
		obs_data_set_int(settings, kRegionHeight, height);
		obs_data_set_bool(settings, kRegionInitialized, true);
		obs_source_update(context_, settings);
		obs_data_release(settings);
	}

	void Render()
	{
		uint32_t width = 0;
		uint32_t height = 0;
		if (!Ready() || !TargetSize(width, height)) {
			obs_source_skip_video_filter(context_);
			return;
		}
		// 範囲を対象の内側へ切り詰める。幅か高さが 0 になれば何もかけない
		const long long left = std::clamp<long long>(region_x_, 0, width);
		const long long top = std::clamp<long long>(region_y_, 0, height);
		const long long right = std::clamp<long long>(region_x_ + region_width_, 0, width);
		const long long bottom = std::clamp<long long>(region_y_ + region_height_, 0, height);
		if (right <= left || bottom <= top) {
			obs_source_skip_video_filter(context_);
			return;
		}
		gs_texture_t *source = RenderTarget(width, height);
		if (!source) {
			obs_source_skip_video_filter(context_);
			return;
		}

		const auto cell = static_cast<long long>(cell_size_);
		const auto cells_x = static_cast<uint32_t>((right - left + cell - 1) / cell);
		const auto cells_y = static_cast<uint32_t>((bottom - top + cell - 1) / cell);
		const auto rows_y = static_cast<uint32_t>(bottom - top);
		vec2 tex_size;
		vec2_set(&tex_size, static_cast<float>(width), static_cast<float>(height));
		vec4 rect;
		vec4_set(&rect, static_cast<float>(left), static_cast<float>(top), static_cast<float>(right),
			 static_cast<float>(bottom));
		vec2 rows_size;
		vec2_set(&rows_size, static_cast<float>(cells_x), static_cast<float>(rows_y));
		vec2 cells_size;
		vec2_set(&cells_size, static_cast<float>(cells_x), static_cast<float>(cells_y));
		// gs_technique_end はパスのたびにパラメーターの値を消すため、パスの直前に毎回すべて設定する
		const auto set_params = [&](gs_texture_t *rows, gs_texture_t *cells) {
			gs_effect_set_texture(param_image_, source);
			gs_effect_set_texture(param_rows_, rows);
			gs_effect_set_texture(param_cells_, cells);
			gs_effect_set_vec2(param_tex_size_, &tex_size);
			gs_effect_set_vec4(param_rect_, &rect);
			gs_effect_set_float(param_cell_size_, static_cast<float>(cell_size_));
			gs_effect_set_vec2(param_rows_size_, &rows_size);
			gs_effect_set_vec2(param_cells_size_, &cells_size);
			gs_effect_set_int(param_pair_count_, (cell_size_ + 1) / 2);
		};

		// 1 パス目: 範囲の各行で、マスの幅ごとの平均を求める
		set_params(nullptr, nullptr);
		if (!DrawPass(rows_render_, "Rows", cells_x, rows_y)) {
			obs_source_skip_video_filter(context_);
			return;
		}
		gs_texture_t *rows = gs_texrender_get_texture(rows_render_);
		// 2 パス目: 行の平均を縦に平均し、マスごとの平均を求める
		set_params(rows, nullptr);
		if (!DrawPass(cells_render_, "Cells", cells_x, cells_y)) {
			obs_source_skip_video_filter(context_);
			return;
		}

		// 3 パス目: フィルターの出力先へ描く。色はアルファを掛けた値のため、そのまま重ねる
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		set_params(rows, gs_texrender_get_texture(cells_render_));
		while (gs_effect_loop(effect_, "Draw")) {
			gs_draw_sprite(source, 0, width, height);
		}
		gs_blend_state_pop();
	}

	// 設定済みのパラメーターで technique を描き、output (cx x cy) に書く
	bool DrawPass(gs_texrender_t *output, const char *technique, uint32_t cx, uint32_t cy)
	{
		gs_texrender_reset(output);
		if (!gs_texrender_begin(output, cx, cy)) {
			return false;
		}
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(cx), 0.0f, static_cast<float>(cy), -100.0f, 100.0f);
		gs_blend_state_push();
		gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);
		while (gs_effect_loop(effect_, technique)) {
			gs_draw_sprite(nullptr, 0, cx, cy);
		}
		gs_blend_state_pop();
		gs_texrender_end(output);
		return true;
	}

	// 対象を中間のテクスチャへ描く。色はアルファを掛けた値になる。OBS の obs_source_process_filter_begin と同じ手順
	gs_texture_t *RenderTarget(uint32_t width, uint32_t height)
	{
		obs_source_t *target = obs_filter_get_target(context_);
		obs_source_t *parent = obs_filter_get_parent(context_);
		if (!target || !parent) {
			return nullptr;
		}
		gs_texrender_reset(source_render_);
		if (!gs_texrender_begin_with_color_space(source_render_, width, height, GS_CS_SRGB)) {
			return nullptr;
		}
		gs_blend_state_push();
		gs_blend_function_separate(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA, GS_BLEND_ONE, GS_BLEND_INVSRCALPHA);
		vec4 clear = {};
		gs_clear(GS_CLEAR_COLOR, &clear, 0.0f, 0);
		gs_ortho(0.0f, static_cast<float>(width), 0.0f, static_cast<float>(height), -100.0f, 100.0f);
		const uint32_t parent_flags = obs_source_get_output_flags(parent);
		if (target == parent && !(parent_flags & OBS_SOURCE_CUSTOM_DRAW) &&
		    !(parent_flags & OBS_SOURCE_ASYNC)) {
			obs_source_default_render(target);
		} else {
			obs_source_video_render(target);
		}
		gs_blend_state_pop();
		gs_texrender_end(source_render_);
		return gs_texrender_get_texture(source_render_);
	}

	bool Ready() const
	{
		return effect_ && param_image_ && param_rows_ && param_cells_ && param_tex_size_ && param_rect_ &&
		       param_cell_size_ && param_rows_size_ && param_cells_size_ && param_pair_count_ &&
		       source_render_ && rows_render_ && cells_render_ && gs_effect_get_technique(effect_, "Rows") &&
		       gs_effect_get_technique(effect_, "Cells") && gs_effect_get_technique(effect_, "Draw");
	}

	bool TargetSize(uint32_t &width, uint32_t &height) const
	{
		obs_source_t *target = obs_filter_get_target(context_);
		if (!target) {
			return false;
		}
		width = obs_source_get_base_width(target);
		height = obs_source_get_base_height(target);
		return width > 0 && height > 0;
	}

	static const char *GetName(void *) { return obs_module_text("SimpleMosaic"); }

	static void *Create(obs_data_t *settings, obs_source_t *context)
	{
		return new (std::nothrow) MosaicFilter(settings, context);
	}

	static void Destroy(void *data) { delete static_cast<MosaicFilter *>(data); }

	static void UpdateCallback(void *data, obs_data_t *settings)
	{
		static_cast<MosaicFilter *>(data)->Update(settings);
	}

	static void Defaults(obs_data_t *settings)
	{
		obs_data_set_default_int(settings, kRegionX, 0);
		obs_data_set_default_int(settings, kRegionY, 0);
		obs_data_set_default_int(settings, kRegionWidth, 0);
		obs_data_set_default_int(settings, kRegionHeight, 0);
		obs_data_set_default_int(settings, kCellSize, kDefaultCellSize);
		obs_data_set_default_bool(settings, kRegionInitialized, false);
	}

	static obs_properties_t *Properties(void *data)
	{
		uint32_t width = 0;
		uint32_t height = 0;
		const bool known = data && static_cast<MosaicFilter *>(data)->TargetSize(width, height);
		const int max_x = known ? static_cast<int>(width) : kFallbackSliderMax;
		const int max_y = known ? static_cast<int>(height) : kFallbackSliderMax;

		obs_properties_t *props = obs_properties_create();
		const auto add_px_slider = [props](const char *name, const char *text, int min, int max) {
			obs_property_t *p =
				obs_properties_add_int_slider(props, name, obs_module_text(text), min, max, 1);
			obs_property_int_set_suffix(p, " px");
		};
		add_px_slider(kRegionX, "RegionX", 0, max_x);
		add_px_slider(kRegionY, "RegionY", 0, max_y);
		add_px_slider(kRegionWidth, "RegionWidth", 0, max_x);
		add_px_slider(kRegionHeight, "RegionHeight", 0, max_y);
		add_px_slider(kCellSize, "CellSize", kMinCellSize, kMaxCellSize);
		return props;
	}

	static void TickCallback(void *data, float) { static_cast<MosaicFilter *>(data)->Tick(); }

	static void RenderCallback(void *data, gs_effect_t *) { static_cast<MosaicFilter *>(data)->Render(); }

private:
	obs_source_t *context_ = nullptr;
	gs_effect_t *effect_ = nullptr;
	gs_eparam_t *param_image_ = nullptr;
	gs_eparam_t *param_rows_ = nullptr;
	gs_eparam_t *param_cells_ = nullptr;
	gs_eparam_t *param_tex_size_ = nullptr;
	gs_eparam_t *param_rect_ = nullptr;
	gs_eparam_t *param_cell_size_ = nullptr;
	gs_eparam_t *param_rows_size_ = nullptr;
	gs_eparam_t *param_cells_size_ = nullptr;
	gs_eparam_t *param_pair_count_ = nullptr;
	gs_texrender_t *source_render_ = nullptr;
	gs_texrender_t *rows_render_ = nullptr;
	gs_texrender_t *cells_render_ = nullptr;

	long long region_x_ = 0;
	long long region_y_ = 0;
	long long region_width_ = 0;
	long long region_height_ = 0;
	int cell_size_ = kDefaultCellSize;
	bool region_initialized_ = false;
};

} // namespace

void register_mosaic_filter(void)
{
	static obs_source_info info = {};
	info.id = "simple_mosaic_filter";
	info.type = OBS_SOURCE_TYPE_FILTER;
	info.output_flags = OBS_SOURCE_VIDEO;
	info.get_name = MosaicFilter::GetName;
	info.create = MosaicFilter::Create;
	info.destroy = MosaicFilter::Destroy;
	info.update = MosaicFilter::UpdateCallback;
	info.get_defaults = MosaicFilter::Defaults;
	info.get_properties = MosaicFilter::Properties;
	info.video_tick = MosaicFilter::TickCallback;
	info.video_render = MosaicFilter::RenderCallback;
	obs_register_source(&info);
}

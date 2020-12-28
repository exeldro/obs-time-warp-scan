#include <obs-module.h>
#include "time-warp-scan.h"
#include "version.h"

struct tws_info {
	obs_source_t *source;

	gs_texrender_t *scan_render;
	gs_texrender_t *line_render;
	uint32_t cx;
	uint32_t cy;
	uint32_t cd;
	bool target_valid;
	bool processed_frame;
	obs_hotkey_pair_id hotkey;
	uint64_t scan_duration;
	uint32_t line_width;
	struct vec4 line_color;
	float line_position;
	double duration;
	float rotation;
	bool transparent;

	double start_x;
	double start_y;
	double scan_x;
	double scan_y;
};

static const char *tws_get_name(void *type_data)
{
	UNUSED_PARAMETER(type_data);
	return obs_module_text("TimeWarpScan");
}

static void free_textures(struct tws_info *f)
{
	if (!f->scan_render && !f->line_render)
		return;
	obs_enter_graphics();
	gs_texrender_destroy(f->scan_render);
	f->scan_render = NULL;
	gs_texrender_destroy(f->line_render);
	f->line_render = NULL;
	obs_leave_graphics();
}

void calc_scan(struct tws_info *tws)
{
	const double line_length_moving_x = tws->cy * cos(RAD(tws->rotation));
	const double line_length_moving_y = tws->cx * sin(RAD(tws->rotation));
	if (fabs(line_length_moving_x) > fabs(line_length_moving_y)) {
		// move y direction
		if (line_length_moving_x > 0.0) {
			// move down
			const double t = tan(RAD((double)tws->rotation));
			tws->scan_y = tws->cy + tws->line_width +
				      (double)tws->cx * fabs(t);
			if (line_length_moving_y > 0.0) {
				tws->start_y = (double)tws->cx * fabs(t) +
					       (double)tws->line_width;
			} else {
				tws->start_y = (double)tws->line_width;
			}
			tws->start_x = 0.0;
		} else {
			// move up
			const double t = tan(RAD((double)tws->rotation + 180));
			tws->scan_y = -1.0 * (tws->cy + tws->line_width +
					      tws->cx * fabs(t));
			if (line_length_moving_y < 0.0) {
				tws->start_y = tws->scan_y;
			} else {
				tws->start_y =
					(double)(tws->cy + tws->line_width) *
					-1.0;
			}
			tws->start_x = (double)tws->cx * -1.0;
		}
		tws->scan_x = 0.0;
	} else {
		// move x direction
		if (line_length_moving_y < 0.0) {
			// move right
			const double t =
				tan(RAD((double)tws->rotation + 270.0));
			tws->scan_x =
				tws->cx + tws->line_width + tws->cy * fabs(t);
			if (line_length_moving_x > 0.0) {
				tws->start_x =
					tws->cy * fabs(t) + tws->line_width;
			} else {
				tws->start_x = tws->line_width;
			}
			tws->start_y = (double)tws->cy * -1.0;
		} else {
			// move left
			const double t = tan(RAD((double)tws->rotation + 90.0));
			tws->scan_x = -1.0 * (tws->cx + tws->cy * fabs(t) +
					      tws->line_width);
			if (line_length_moving_x < 0.0) {
				tws->start_x = tws->scan_x;
			} else {
				tws->start_x =
					(double)(tws->cx + tws->line_width) *
					-1.0;
			}
			tws->start_y = 0.0;
		}
		tws->scan_y = 0.0;
	}
}

static inline bool check_size(struct tws_info *f)
{
	obs_source_t *target = obs_filter_get_target(f->source);

	f->target_valid = !!target;
	if (!f->target_valid)
		return true;

	const uint32_t cx = obs_source_get_base_width(target);
	const uint32_t cy = obs_source_get_base_height(target);

	f->target_valid = !!cx && !!cy;
	if (!f->target_valid)
		return true;

	if (cx != f->cx || cy != f->cy) {
		f->cx = cx;
		f->cy = cy;
		f->cd = ceil(sqrt((cx * cx) + (cy * cy)));
		calc_scan(f);
		free_textures(f);
		return true;
	}
	return false;
}

static void tws_update(void *data, obs_data_t *settings)
{
	struct tws_info *tws = data;

	tws->line_width = (uint32_t)obs_data_get_int(settings, "line_width");
	tws->scan_duration = obs_data_get_int(settings, "scan_duration");
	const uint32_t color =
		(uint32_t)obs_data_get_int(settings, "line_color");
	vec4_from_rgba(&tws->line_color, color);
	tws->line_color.w =
		(float)(obs_data_get_double(settings, "line_opacity") / 100.0);
	tws->rotation =
		(float)fmod(obs_data_get_double(settings, "rotation"), 360.0);
	tws->transparent = obs_data_get_bool(settings, "transparent");
	calc_scan(tws);
}

static void *tws_create(obs_data_t *settings, obs_source_t *source)
{
	struct tws_info *tws = bzalloc(sizeof(struct tws_info));
	tws->source = source;
	tws->hotkey = OBS_INVALID_HOTKEY_PAIR_ID;
	tws_update(tws, settings);
	return tws;
}

static void tws_destroy(void *data)
{
	struct tws_info *tws = data;
	if (tws->hotkey != OBS_INVALID_HOTKEY_PAIR_ID) {
		obs_hotkey_pair_unregister(tws->hotkey);
	}
	free_textures(tws);
	bfree(tws);
}

static void draw_frame(struct tws_info *f)
{
	gs_texture_t *tex = gs_texrender_get_texture(f->scan_render);
	if (tex) {
		gs_effect_t *effect = obs_get_base_effect(OBS_EFFECT_DEFAULT);
		gs_eparam_t *image =
			gs_effect_get_param_by_name(effect, "image");
		gs_effect_set_texture(image, tex);
		while (gs_effect_loop(effect, "Draw"))
			gs_draw_sprite(tex, 0, f->cx, f->cy);
	}
}

static void tws_video_render(void *data, gs_effect_t *effect)
{
	struct tws_info *tws = data;
	obs_source_t *target = obs_filter_get_target(tws->source);
	obs_source_t *parent = obs_filter_get_parent(tws->source);

	if (!tws->target_valid || !target || !parent) {
		obs_source_skip_video_filter(tws->source);
		return;
	}
	if (tws->transparent)
		obs_source_skip_video_filter(tws->source);
	if (tws->processed_frame) {
		draw_frame(tws);
		return;
	}
	if (tws->duration * 1000.0 > (double)tws->scan_duration) {
		tws->processed_frame = true;
		draw_frame(tws);
		return;
	}

	if (!tws->scan_render) {
		tws->scan_render = gs_texrender_create(GS_RGBA, GS_ZS_NONE);
	} else {
		gs_texrender_reset(tws->scan_render);
	}
	double factor;
	float new_line_position;
	if (fabs(tws->scan_x) > fabs(tws->scan_y)) {
		new_line_position =
			fabs(tws->scan_x) *
			(tws->duration * 1000.0 / (double)tws->scan_duration);
		factor = tws->line_position / fabs(tws->scan_x);
	} else {
		new_line_position =
			fabs(tws->scan_y) *
			(tws->duration * 1000.0 / (double)tws->scan_duration);
		factor = tws->line_position / fabs(tws->scan_y);
	}
	uint32_t scan_width = ceil(new_line_position - tws->line_position);
	if (scan_width > 0) {
		if (!tws->line_render) {
			tws->line_render =
				gs_texrender_create(GS_RGBA, GS_ZS_NONE);
		} else {
			gs_texrender_reset(tws->line_render);
		}
	}

	gs_blend_state_push();
	gs_blend_function(GS_BLEND_ONE, GS_BLEND_ZERO);

	if (scan_width > 0) {
		if (gs_texrender_begin(tws->line_render,
				       (tws->cd + scan_width) * 2, tws->cd)) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
			gs_ortho(0.0f, (float)(tws->cd + scan_width) * 2, 0.0f,
				 tws->cd, -100.0f, 100.0f);

			gs_matrix_translate3f(tws->cd + scan_width, 0.0f, 0.0f);
			gs_matrix_rotaa4f(0.0f, 0.0f, -1.0f,
					  RAD(tws->rotation));
			gs_matrix_translate3f(
				-1.0 * factor * tws->scan_x + tws->start_x,
				-1.0 * factor * tws->scan_y + tws->start_y,
				0.0f);

			const uint32_t parent_flags =
				obs_source_get_output_flags(target);
			const bool custom_draw =
				(parent_flags & OBS_SOURCE_CUSTOM_DRAW) != 0;
			const bool async = (parent_flags & OBS_SOURCE_ASYNC) !=
					   0;
			if (target == parent && !custom_draw && !async)
				obs_source_default_render(target);
			else
				obs_source_video_render(target);

			gs_texrender_end(tws->line_render);
		}
	}

	if (gs_texrender_begin(tws->scan_render, tws->cx, tws->cy)) {
		if (tws->line_position <= 0.0f) {
			struct vec4 clear_color;
			vec4_zero(&clear_color);
			gs_clear(GS_CLEAR_COLOR, &clear_color, 0.0f, 0);
		}
		gs_ortho(0.0f, (float)tws->cx, 0, (float)tws->cy, -100.0f,
			 100.0f);
		gs_matrix_translate3f(factor * tws->scan_x - tws->start_x,
				      factor * tws->scan_y - tws->start_y,
				      0.0f);

		gs_matrix_rotaa4f(0.0f, 0.0f, 1.0f, RAD(tws->rotation));
		gs_matrix_translate3f(-1.0f * (scan_width + tws->cd), 0.0f,
				      0.0f);
		if (scan_width > 0) {
			gs_texture_t *tex =
				gs_texrender_get_texture(tws->line_render);
			if (tex) {
				gs_effect_t *effect =
					obs_get_base_effect(OBS_EFFECT_DEFAULT);
				gs_eparam_t *image =
					gs_effect_get_param_by_name(effect,
								    "image");
				gs_effect_set_texture(image, tex);
				while (gs_effect_loop(effect, "Draw"))
					gs_draw_sprite(tex, 0,
						       (tws->cd + scan_width) *
							       2,
						       tws->cd);
			}
		}
		gs_matrix_translate3f(-1.0f * tws->line_width, scan_width,
				      0.0f);
		gs_blend_function(GS_BLEND_SRCALPHA, GS_BLEND_INVSRCALPHA);
		gs_effect_t *solid = obs_get_base_effect(OBS_EFFECT_SOLID);
		gs_eparam_t *color =
			gs_effect_get_param_by_name(solid, "color");
		gs_technique_t *tech = gs_effect_get_technique(solid, "Solid");

		gs_effect_set_vec4(color, &tws->line_color);
		gs_technique_begin(tech);
		gs_technique_begin_pass(tech, 0);

		gs_draw_sprite(0, 0,
			       (tws->cd + tws->line_width + scan_width) * 2,
			       tws->line_width);

		gs_technique_end_pass(tech);
		gs_technique_end(tech);

		gs_texrender_end(tws->scan_render);
	}

	gs_blend_state_pop();

	tws->line_position = new_line_position;

	tws->processed_frame = true;

	draw_frame(tws);
}

static obs_properties_t *tws_properties(void *data)
{
	obs_properties_t *ppts = obs_properties_create();
	obs_property_t *p = obs_properties_add_int(
		ppts, "scan_duration", obs_module_text("ScanDuration"), 0,
		100000, 1000);
	obs_property_int_set_suffix(p, "ms");

	p = obs_properties_add_int(ppts, "line_width",
				   obs_module_text("LineWidth"), 0, 100, 1);
	obs_property_int_set_suffix(p, "px");

	p = obs_properties_add_color(ppts, "line_color",
				     obs_module_text("LineColor"));
	p = obs_properties_add_float_slider(ppts, "line_opacity",
					    obs_module_text("LineOpacity"), 0.0,
					    100.0, 1.0);
	obs_property_float_set_suffix(p, "%");
	p = obs_properties_add_float_slider(
		ppts, "rotation", obs_module_text("Rotation"), 0.0, 360.0, 1.0);
	obs_property_float_set_suffix(p, obs_module_text("Degrees"));
	p = obs_properties_add_bool(ppts, "transparent",
				    obs_module_text("Transparent"));
	return ppts;
}

void tws_defaults(obs_data_t *settings)
{
	obs_data_set_default_int(settings, "scan_duration", 10000);
	obs_data_set_default_int(settings, "line_width", 3);
	obs_data_set_default_int(settings, "line_color", 0xffffff55);
	obs_data_set_default_double(settings, "line_opacity", 100.0);
}

bool tws_enable_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey,
		       bool pressed)
{
	struct tws_info *tws = data;
	if (!pressed)
		return false;
	if (obs_source_enabled(tws->source))
		return false;

	obs_source_set_enabled(tws->source, true);
	return true;
}

bool tws_disable_hotkey(void *data, obs_hotkey_pair_id id, obs_hotkey_t *hotkey,
			bool pressed)
{
	struct tws_info *tws = data;
	if (!pressed)
		return false;
	if (!obs_source_enabled(tws->source))
		return false;
	obs_source_set_enabled(tws->source, false);
	return true;
}

static void tws_tick(void *data, float t)
{
	struct tws_info *f = data;

	if (obs_source_enabled(f->source)) {
		f->duration += (double)t;
	} else {
		f->duration = 0.0;
		f->line_position = 0.0f;
	}
	if (f->hotkey == OBS_INVALID_HOTKEY_PAIR_ID) {
		obs_source_t *parent = obs_filter_get_parent(f->source);
		if (parent) {
			f->hotkey = obs_hotkey_pair_register_source(
				parent, "tws.Enable", obs_module_text("Enable"),
				"tws.Disable", obs_module_text("Disable"),
				tws_enable_hotkey, tws_disable_hotkey, f, f);
		}
	}
	check_size(f);

	f->processed_frame = false;
}

struct obs_source_info tws_filter = {
	.id = "time_warp_scan_filter",
	.type = OBS_SOURCE_TYPE_FILTER,
	.output_flags = OBS_OUTPUT_VIDEO,
	.get_name = tws_get_name,
	.create = tws_create,
	.destroy = tws_destroy,
	.load = tws_update,
	.update = tws_update,
	.video_render = tws_video_render,
	.get_properties = tws_properties,
	.get_defaults = tws_defaults,
	.video_tick = tws_tick,
};

OBS_DECLARE_MODULE()
OBS_MODULE_AUTHOR("Exeldro");
OBS_MODULE_USE_DEFAULT_LOCALE("time-warp-scan", "en-US")
MODULE_EXPORT const char *obs_module_description(void)
{
	return obs_module_text("Description");
}

MODULE_EXPORT const char *obs_module_name(void)
{
	return obs_module_text("TimeWarpScan");
}

bool obs_module_load(void)
{
	blog(LOG_INFO, "[TimeWarpScan] loaded version %s", PROJECT_VERSION);
	obs_register_source(&tws_filter);
	return true;
}

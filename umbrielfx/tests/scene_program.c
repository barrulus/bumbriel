// Internal generic renderer backend, independent of compositor admission.
#include "render_fixture.h"
#include "render/fx_renderer/scene_program.h"
#include "render/egl.h"
#include <wlr/util/transform.h>

static struct wlr_texture *solid(struct fixture *fixture, uint8_t red, uint8_t green, uint8_t blue) {
	uint8_t pixel[] = {red, green, blue, 0};
	return wlr_texture_from_pixels(fixture->renderer, DRM_FORMAT_ABGR8888, 4, 1, 1, pixel);
}

static bool pixel(struct fixture *fixture, struct wlr_buffer *buffer, int x, int y,
		uint8_t out[4]) {
	size_t size = (size_t)buffer->width * buffer->height * 4;
	uint8_t *pixels = malloc(size);
	if (!pixels) {
		return false;
	}
	bool ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, buffer->width * 4, pixels);
	if (ok) {
		memcpy(out, pixels + ((size_t)y * buffer->width + x) * 4, 4);
	}
	free(pixels);
	return ok;
}

static bool experiment(struct fixture *fixture) {
	struct fx_scene_limits limits;
	if (!check(fx_scene_program_get_limits(fixture->renderer, &limits), "query scene stage capabilities")) {
		return false;
	}
	fprintf(stderr, "scene bundle limits: texture=%u vertex=%u fragment=%u samplers=%u\n",
		limits.texture_size, limits.vertex_vectors, limits.fragment_vectors, limits.fragment_texture_units);
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 128, 64);
	struct wlr_buffer *intermediate = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 128, 64);
	struct fx_scene_target *target = fx_scene_target_create(fixture->renderer, buffer, true);
	struct fx_scene_target *composed = fx_scene_target_create(fixture->renderer, intermediate, true);
	struct fx_scene_sources sources = {
		.vertex =
			"vec4 transition_vertex(vec2 uv) { float w=1.0+umbriel_direction*uv.x;"
			"vec2 xy=(umbriel_current_box.xy+uv*umbriel_current_box.zw)/umbriel_output_size;"
			"xy.y+=sin(uv.x*3.14159265)*umbriel_time;"
			"return vec4((xy*2.0-1.0)*w,umbriel_source_box.x*w,w); }",
		.fragment =
			"vec4 transition_fragment(vec2 uv,vec2 output_uv) {"
			"if(umbriel_navigation_position>0.5)return vec4(uv.x,uv.y,output_uv.y,0.0);"
			"return umbriel_sample_item(uv); }",
	};
	struct fx_scene_program *program = fx_scene_program_create(fixture->renderer, FX_SCENE_SET, &sources, NULL, 0);
	struct fx_scene_mesh mesh = {0};
	bool ok = check(target && composed && program && fx_scene_mesh_create(&mesh, 1, 1, 64),
		"generic scene bundle, prepared depth targets and mesh");
	if (!ok) {
		fx_scene_target_destroy(target);
		fx_scene_target_destroy(composed);
		fx_scene_program_unref(program);
		if (buffer) {
			wlr_buffer_drop(buffer);
		}
		if (intermediate) {
			wlr_buffer_drop(intermediate);
		}
		return false;
	}
	ok &= check(fx_scene_program_reads_time(program), "active authored vertex time is reflected for frame demand");
	struct wlr_texture *textures[64] = {0};
	struct fx_scene_draw draws[64] = {0};
	for (unsigned i = 0; ok && i < 64; i++) {
		textures[i] = solid(fixture, i * 3 + 16, 255 - i * 3, i + 32);
		draws[i].input.texture = textures[i];
		draws[i].mesh = &mesh;
		draws[i].item.ordinal = i;
		draws[i].item.token = i + 1;
		ok &= check(textures[i] != NULL, "independent retained face texture");
	}
	struct fx_scene_frame frame = {.output_size = {128, 64}, .scale = 1, .scene_count = 64};
	static const unsigned counts[] = {1, 2, 3, 4, 5, 8, 64};
	for (unsigned n = 0; ok && n < sizeof(counts) / sizeof(counts[0]); n++) {
		unsigned count = counts[n];
		frame.scene_count = count;
		for (unsigned i = 0; i < count; i++) {
			draws[i].item.current_box[0] = (float)i * 128 / count;
			draws[i].item.current_box[2] = 128.0f / count;
			draws[i].item.current_box[3] = 64;
		}
		ok &= check(fx_scene_program_render(program, target, NULL, &frame, NULL, draws, count),
			"generic bundle renders every admitted face");
		for (unsigned i = 0; ok && i < count; i++) {
			uint8_t rgba[4];
			ok &= pixel(fixture, buffer, (i + 0.5) * 128 / count, 32, rgba) &&
				check(rgba[0] == i * 3 + 16 && rgba[1] == 255 - i * 3 && rgba[2] == i + 32 && rgba[3] == 255,
					"numbered generic face samples own source and forces alpha one");
		}
	}
	for (unsigned i = 0; i < 2; i++) {
		draws[i].item.current_box[0] = 0;
		draws[i].item.current_box[2] = 128;
	}
	frame.scene_count = 2;
	for (unsigned reverse = 0; ok && reverse < 2; reverse++) {
		draws[0].item.source_box[0] = reverse ? 0.5f : -0.5f;
		draws[1].item.source_box[0] = reverse ? -0.5f : 0.5f;
		ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 2);
		uint8_t rgba[4];
		ok &= pixel(fixture, buffer, 64, 32, rgba) && check(rgba[0] == (reverse ? 19 : 16),
			"generic opaque depth follows geometry rather than submission order");
	}
	draws[0].item.source_box[0] = 0;
	frame.scene_count = 1;
	frame.navigation_position = 1;
	for (unsigned perspective = 0; ok && perspective < 2; perspective++) {
		frame.direction = perspective;
		ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 1);
		uint8_t rgba[4];
		double u = 64.5 / 128, v = 16.5 / 64;
		// This sample lies in the quad's first triangle (u+v<1). Its
		// perspective denominator is (1-u)+u/2 for vertex W=(1,2,1).
		double item_v = perspective ? v / (1 - u * 0.5) : v;
		ok &= pixel(fixture, buffer, 64, 16, rgba) &&
			check(fabs(rgba[0] - (perspective ? u / (2 - u) : u) * 255) < 2 &&
				fabs(rgba[1] - item_v * 255) < 2 && fabs(rgba[2] - v * 255) < 2,
				"generic wrapper preserves W and top-left raster coordinates on both axes");
	}
	frame.direction = 0;
	for (unsigned transform = 0; ok && transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		int logical_width = 128, logical_height = 64;
		wlr_output_transform_coords(transform, &logical_width, &logical_height);
		frame.output_transform = transform;
		frame.output_size[0] = draws[0].item.current_box[2] = logical_width;
		frame.output_size[1] = draws[0].item.current_box[3] = logical_height;
		ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 1);
		struct wlr_box point = {64, 16, 1, 1};
		wlr_box_transform(&point, &point, transform, 128, 64);
		double u = (point.x + 0.5) / logical_width, v = (point.y + 0.5) / logical_height;
		uint8_t transformed[4];
		ok &= pixel(fixture, buffer, 64, 16, transformed) &&
			check(fabs(transformed[0] - u * 255) < 2 && fabs(transformed[1] - v * 255) < 2 &&
				fabs(transformed[2] - v * 255) < 2, "non-square output transform keeps item and raster UV coherent");
	}
	frame.output_transform = WL_OUTPUT_TRANSFORM_NORMAL;
	frame.output_size[0] = draws[0].item.current_box[2] = 128;
	frame.output_size[1] = draws[0].item.current_box[3] = 64;
	struct fx_scene_mesh grid = {0};
	frame.direction = 0;
	frame.navigation_position = 0;
	frame.time = 0.25f;
	ok &= check(fx_scene_mesh_create(&grid, 8, 8, 1), "bounded authored grid");
	draws[0].mesh = &grid;
	ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 1);
	uint8_t grid_pixel[4];
	ok &= pixel(fixture, buffer, 64, 8, grid_pixel) &&
		check(grid_pixel[3] == 0, "grid interior vertices deform the top edge");
	draws[0].mesh = &mesh;
	ok &= fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 1);
	ok &= pixel(fixture, buffer, 64, 8, grid_pixel) &&
		check(grid_pixel[3] == 255, "quad negative control lacks interior deformation");
	frame.time = 0;
	fx_scene_mesh_finish(&grid);
	// A declared optional stage is atomic: failed final compilation must not
	// silently install a bundle containing only its geometry stages.
	sources.composite = "this is invalid GLSL";
	struct fx_scene_program *invalid = fx_scene_program_create(fixture->renderer, FX_SCENE_SET, &sources, NULL, 0);
	ok &= check(invalid == NULL, "invalid optional final stage rejects entire bundle");
	fx_scene_program_unref(invalid);
	sources.composite = "vec4 transition_composite(vec2 uv){return umbriel_sample_composed(uv).bgra;}";
	struct fx_scene_program *final = fx_scene_program_create(fixture->renderer, FX_SCENE_SET, &sources, NULL, 0);
	frame.navigation_position = 0;
	frame.direction = 0;
	ok &= check(final && fx_scene_program_render(final, target, composed, &frame, NULL, draws, 1),
		"final composite reads a separate completed geometry target");
	uint8_t rgba[4];
	ok &= pixel(fixture, buffer, 64, 32, rgba) && check(rgba[0] == 32 && rgba[2] == 16,
		"authored final stage changes output");
	frame.navigation_position = 1;
	frame.output_transform = WL_OUTPUT_TRANSFORM_90;
	frame.output_size[0] = draws[0].item.current_box[2] = 64;
	frame.output_size[1] = draws[0].item.current_box[3] = 128;
	ok &= check(fx_scene_program_render(final, target, composed, &frame, NULL, draws, 1),
		"final composite of rotated intermediate target");
	struct wlr_box final_point = {64, 16, 1, 1};
	wlr_box_transform(&final_point, &final_point, WL_OUTPUT_TRANSFORM_90, 128, 64);
	ok &= pixel(fixture, buffer, 64, 16, rgba) &&
		check(fabs(rgba[0] - (final_point.y + 0.5) / 128 * 255) < 2 &&
			fabs(rgba[2] - (final_point.x + 0.5) / 64 * 255) < 2,
			"composed sampler hides physical output rotation");
	frame.navigation_position = 0;
	frame.output_transform = WL_OUTPUT_TRANSFORM_NORMAL;
	frame.output_size[0] = draws[0].item.current_box[2] = 128;
	frame.output_size[1] = draws[0].item.current_box[3] = 64;
	ok &= check(!fx_scene_program_render(final, target, target, &frame, NULL, draws, 1),
		"feedback alias is rejected before drawing");
	struct fx_scene_sources pair_sources = {
		.fragment = "uniform float amount; vec4 transition(vec2 uv){return mix(umbriel_sample_from(uv),umbriel_sample_to(uv),amount);}",
	};
	struct fx_scene_parameter parameter = {.name = "amount", .components = 1, .value = {0.25f}};
	struct fx_scene_program *pair_program = fx_scene_program_create(fixture->renderer, FX_SCENE_PAIR,
		&pair_sources, &parameter, 1);
	struct fx_scene_input pair[2] = {{.texture = textures[0]}, {.texture = textures[63]}};
	ok &= check(pair_program && fx_scene_program_render(pair_program, target, NULL, &frame, pair, NULL, 0),
		"pair samples two independently bound textures and immutable user parameter");
	ok &= pixel(fixture, buffer, 64, 32, rgba) && check(abs(rgba[0] - 63) <= 1,
		"pair mix has both sources, not an aliased destination");
	struct fx_scene_sources window_sources = {
		.vertex = "vec4 transition_vertex(vec2 uv){return vec4(uv*2.0-1.0,0.0,1.0);}",
		.fragment = "vec4 transition_fragment(vec2 uv,vec2 out_uv){if(umbriel_item_token!=umbriel_target_token||any(notEqual(umbriel_framing_transform,vec4(0.5,0.5,4.0,-8.0))))return vec4(1.0,0.0,1.0,1.0);return vec4(umbriel_progress,umbriel_motion_progress,umbriel_native_opacity,1.0);}",
	};
	struct fx_scene_program *window_program = fx_scene_program_create(fixture->renderer, FX_SCENE_WINDOWS,
		&window_sources, NULL, 0);
	frame.progress = 0.75f;
	frame.target_token = 1;
	draws[0].item.motion_progress = 0.25f;
	draws[0].item.native_opacity = 0.5f;
	memcpy(draws[0].item.framing_transform, (float[]){0.5f, 0.5f, 4, -8}, sizeof(float) * 4);
	ok &= check(window_program && fx_scene_program_render(window_program, target, NULL, &frame, NULL, draws, 1),
		"window profile binds independent lifecycle and motion clocks");
	ok &= pixel(fixture, buffer, 64, 32, rgba) && check(abs(rgba[0] - 191) <= 1 && abs(rgba[1] - 64) <= 1 &&
		abs(rgba[2] - 128) <= 1, "native opacity is metadata and is not multiplied twice");
	ok &= check(!fx_scene_program_render(window_program, target, NULL, &frame, NULL, draws, 129),
		"draw count bound rejects before touching unavailable draw descriptors");
	fx_scene_program_unref(window_program);
	char parameter_source[4096] = {0};
	struct fx_scene_parameter full_parameters[FX_SCENE_PARAMETERS] = {0};
	for (unsigned i = 0; i < FX_SCENE_PARAMETERS; i++) {
		snprintf(full_parameters[i].name, sizeof(full_parameters[i].name), "p%u", i);
		full_parameters[i].components = 1;
		full_parameters[i].value[0] = 1.0f / FX_SCENE_PARAMETERS;
		char declaration[64];
		snprintf(declaration, sizeof(declaration), "uniform float p%u;\n", i);
		strcat(parameter_source, declaration);
	}
	strcat(parameter_source, "vec4 transition(vec2 uv){return vec4(");
	for (unsigned i = 0; i < FX_SCENE_PARAMETERS; i++) {
		strcat(parameter_source, i ? "+" : "");
		strcat(parameter_source, full_parameters[i].name);
	}
	strcat(parameter_source,
		",umbriel_audio_level()+umbriel_palette_at(0.0).r,umbriel_audio_band(1.0),1.0);}");
	pair_sources.fragment = parameter_source;
	struct fx_scene_program *full = fx_scene_program_create(fixture->renderer, FX_SCENE_PAIR,
		&pair_sources, full_parameters, FX_SCENE_PARAMETERS);
	frame.palette_count = 1;
	frame.palette[0] = 0.25f;
	frame.audio_levels[0] = 1;
	frame.audio_levels[3] = 0.5f;
	frame.audio_bands[15] = 0.125f;
	ok &= check(full && fx_scene_program_render(full, target, NULL, &frame, pair, NULL, 0),
		"full independent scene parameter table binds beside palette and shared audio");
	ok &= check(fx_scene_program_reads_audio(full) && !fx_scene_program_reads_audio(pair_program),
		"audio capability query reads retained reflection without preparing anything");
	ok &= pixel(fixture, buffer, 64, 32, rgba) && check(rgba[0] == 255 && abs(rgba[1] - 191) <= 1 &&
		abs(rgba[2] - 32) <= 1, "scene parameters, palette and audio are all present");
	fx_scene_program_unref(full);
	fx_scene_program_unref(pair_program);
	fx_scene_program_unref(final);
	fx_scene_program_unref(program);
	struct wlr_render_pass *ordinary = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
	if (ordinary) {
		wlr_render_pass_add_rect(ordinary, &(struct wlr_render_rect_options){
			.box = {.width = 128, .height = 64}, .color = {.g = 1, .a = 1},
			.blend_mode = WLR_RENDER_BLEND_MODE_NONE,
		});
		ok &= check(wlr_render_pass_submit(ordinary), "ordinary pass after generic scene rendering");
		ok &= pixel(fixture, buffer, 64, 32, rgba) && check(rgba[1] == 255 && rgba[0] == 0,
			"generic scene backend restores ordinary renderer state");
	} else {
		ok = false;
	}
	fx_scene_mesh_finish(&mesh);
	for (unsigned i = 0; i < 64; i++) {
		if (textures[i]) {
			wlr_texture_destroy(textures[i]);
		}
	}
	fx_scene_target_destroy(composed);
	fx_scene_target_destroy(target);
	if (intermediate) {
		wlr_buffer_drop(intermediate);
	}
	if (buffer) {
		wlr_buffer_drop(buffer);
	}
	return ok;
}

static bool picking_experiment(struct fixture *fixture) {
	struct wlr_buffer *buffer = create_output_buffer(fixture, DRM_FORMAT_ARGB8888, 128, 64);
	struct fx_scene_target *target = buffer ? fx_scene_target_create(fixture->renderer, buffer, true) : NULL;
	struct fx_scene_picker *picker = fx_scene_picker_create(fixture->renderer);
	struct wlr_texture *texture = solid(fixture, 64, 128, 192);
	struct fx_scene_mesh mesh = {0};
	struct fx_scene_sources sources = {
		.vertex = "vec4 transition_vertex(vec2 uv){float w=1.0+umbriel_direction*uv.x;"
			"return vec4((uv*2.0-1.0)*w,umbriel_source_box.x*w,w);}",
		.fragment = "vec4 transition_fragment(vec2 uv,vec2 output_uv){"
			"if(umbriel_progress>0.5&&gl_FragCoord.x<32.0)discard;"
			"if(umbriel_item_token==2&&umbriel_time>0.5) return vec4(1.0);"
			"if(umbriel_navigation_velocity>0.5)return umbriel_sample_item(uv)+umbriel_sample_item(uv*0.5);"
			"if(umbriel_navigation_position>0.5)return umbriel_sample_item(vec2(1.0-uv.x,uv.y));"
			"return umbriel_sample_item(uv);}",
	};
	struct fx_scene_program *program = fx_scene_program_create(fixture->renderer, FX_SCENE_SET, &sources, NULL, 0);
	bool ok = check(target && picker && texture && program && fx_scene_mesh_create(&mesh, 1, 1, 2),
		"preflight bounded picker, retained scene target, authored program and mesh");
	if (!ok) goto done;
	struct fx_scene_draw draws[2] = {
		{.item = {.kind = FX_SCENE_FACE, .token = 1, .ordinal = 0, .source_box = {0.5}},
			.input = {.texture = texture}, .mesh = &mesh},
		{.item = {.kind = FX_SCENE_FACE, .token = 2, .ordinal = 1, .source_box = {-0.5}},
			.input = {.texture = texture}, .mesh = &mesh},
	};
	struct fx_scene_frame frame = {.output_size = {128, 64}, .scale = 1, .scene_count = 2};
	struct fx_scene_pick hit = {0};
	ok &= check(fx_scene_program_supports_picking(program), "registry compiled authored picking variant");
	ok &= check(fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 2), "render overlap before pick");
	uint8_t before[4], after[4];
	ok &= pixel(fixture, buffer, 64, 32, before);
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT &&
		hit.token == 2 && hit.ordinal == 1 && fabsf(hit.uv[0] - 64.5f / 128) < 0.0001f &&
		fabsf(hit.uv[1] - 32.5f / 64) < 0.0001f, "nearest authored depth returns face identity and sampled source UV");
	ok &= pixel(fixture, buffer, 64, 32, after) && check(memcmp(before, after, 4) == 0, "picking never modifies displayed target");
	draws[1].item.source_box[0] = 0.5f;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT &&
		hit.token == 1, "equal depth preserves actual draw-order winner");
	draws[1].item.source_box[0] = -0.5f;
	frame.direction = 1;
	float screen_u = 64.5f / 128;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT &&
		fabsf(hit.uv[0] - screen_u / (2 - screen_u)) < 0.0001f, "authored homogeneous w preserves perspective-correct UV");
	frame.navigation_position = 1;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT &&
		fabsf(hit.uv[0] - (1 - screen_u / (2 - screen_u))) < 0.0001f, "fragment remapping returns actual sampled source coordinate");
	frame.navigation_position = 0;
	frame.direction = 0;
	frame.progress = 1;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 16, 32, &hit) == FX_SCENE_PICK_MISS,
		"authored fragment discard leaves no hit");
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT,
		"one-pixel viewport preserves authored gl_FragCoord");
	frame.progress = 0;
	frame.time = 1;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_UNSUPPORTED,
		"opaque unsampled front fragment blocks hidden face picking");
	frame.time = 0;
	frame.navigation_velocity = 1;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_UNSUPPORTED,
		"multiple source samples have no unambiguous window mapping");
	frame.navigation_velocity = 0;
	for (unsigned transform = 0; transform <= WL_OUTPUT_TRANSFORM_FLIPPED_270; transform++) {
		frame.output_transform = transform;
		ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 31.75f, 15.75f, &hit) == FX_SCENE_PICK_HIT &&
			hit.token == 2 && fabsf(hit.uv[0] - 0.25f) < 0.012f && fabsf(hit.uv[1] - 0.25f) < 0.012f,
			"all output rotations/reflections retain logical source coordinates");
	}
	frame.output_transform = 0;
	ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, -1, 32, &hit) == FX_SCENE_PICK_MISS &&
		fx_scene_program_pick(program, picker, target, &frame, draws, 2, NAN, 32, &hit) == FX_SCENE_PICK_UNSUPPORTED,
		"outside and malformed pointer coordinates fail safely");
	// Verify input-time picking does not leak bindings/state into the next pass.
	struct fx_renderer *renderer = fx_get_renderer(fixture->renderer);
	struct wlr_egl_context previous;
	if (wlr_egl_make_current(renderer->egl, &previous)) {
		glActiveTexture(GL_TEXTURE0);
		glBindTexture(GL_TEXTURE_2D, fx_get_texture(texture)->tex);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glViewport(3, 4, 17, 19);
		glEnable(GL_BLEND);
		glEnable(GL_SCISSOR_TEST);
		glEnable(GL_DITHER);
		glDepthMask(GL_FALSE);
		glDepthFunc(GL_GREATER);
		glActiveTexture(GL_TEXTURE1);
		ok &= check(fx_scene_program_pick(program, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_HIT,
			"picking succeeds with unrelated caller GL state");
		GLint viewport[4], depth_func, active;
		GLboolean depth_mask;
		glGetIntegerv(GL_VIEWPORT, viewport);
		glGetIntegerv(GL_DEPTH_FUNC, &depth_func);
		glGetIntegerv(GL_ACTIVE_TEXTURE, &active);
		glGetBooleanv(GL_DEPTH_WRITEMASK, &depth_mask);
		ok &= check(viewport[0] == 3 && viewport[1] == 4 && viewport[2] == 17 && viewport[3] == 19 &&
			depth_func == GL_GREATER && !depth_mask && active == GL_TEXTURE1 && glIsEnabled(GL_BLEND) &&
			glIsEnabled(GL_SCISSOR_TEST) && glIsEnabled(GL_DITHER), "picking restores caller GL state");
		glActiveTexture(GL_TEXTURE0);
		GLint min_filter, mag_filter;
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &min_filter);
		glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &mag_filter);
		ok &= check(min_filter == GL_NEAREST && mag_filter == GL_NEAREST, "picking restores retained texture sampling parameters");
		glDisable(GL_SCISSOR_TEST);
		glDepthMask(GL_TRUE);
		glDepthFunc(GL_LESS);
		glActiveTexture(GL_TEXTURE0);
		wlr_egl_restore_context(&previous);
	} else ok = false;
	sources.composite = "vec4 transition_composite(vec2 uv){return umbriel_sample_composed(uv);}";
	struct fx_scene_program *composite = fx_scene_program_create(fixture->renderer, FX_SCENE_SET, &sources, NULL, 0);
	ok &= check(composite && !fx_scene_program_supports_picking(composite) &&
		fx_scene_program_pick(composite, picker, target, &frame, draws, 2, 64, 32, &hit) == FX_SCENE_PICK_UNSUPPORTED,
		"arbitrary final composites fail closed without an inverse source mapping");
	fx_scene_program_unref(composite);
	ok &= check(fx_scene_program_render(program, target, NULL, &frame, NULL, draws, 2), "ordinary scene rendering survives picking");
done:
	fx_scene_mesh_finish(&mesh);
	fx_scene_picker_destroy(picker);
	fx_scene_target_destroy(target);
	fx_scene_program_unref(program);
	if (texture) wlr_texture_destroy(texture);
	if (buffer) wlr_buffer_drop(buffer);
	return ok;
}

int main(void) {
	struct fixture fixture;
	if (!fixture_init(&fixture)) {
		fixture_finish(&fixture);
		return 77;
	}
	bool ok = experiment(&fixture) && picking_experiment(&fixture);
	fixture_finish(&fixture);
	return ok ? 0 : 1;
}

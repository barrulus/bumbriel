#include "render/fx_renderer/scene_program.h"

#include <math.h>
#include <drm_fourcc.h>
#include <limits.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/render/allocator.h>
#include <wlr/render/drm_format_set.h>
#include <wlr/render/interface.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>
#include "render/egl.h"
#include "render/fx_renderer/audio_inputs.h"
#include "render/fx_renderer/effect.h"
#include "umbrielfx/render/fx_renderer/fx_offscreen_buffers.h"
#include "render/fx_renderer/fx_renderer.h"
#include "umbrielfx/render/pass.h"

enum location {
	OUTPUT_SIZE, SCALE, TIME, PROGRESS, LINEAR_PROGRESS, DIRECTION, SEED,
	NAV_POSITION, NAV_VELOCITY, SCENE_COUNT, ROLE, PALETTE, PALETTE_COUNT,
	AUDIO_LEVELS, AUDIO_BANDS, ITEM_KIND, ITEM_ORDINAL, ITEM_TOKEN,
	CURRENT_BOX, SOURCE_BOX, DESTINATION_BOX, CAPTURE_EXTENT, CONTENT_BOUNDS,
	AXIS, VIEWPORT, FRAMING, MOTION_PROGRESS, LINEAR_MOTION_PROGRESS, NATIVE_OPACITY, COVERAGE_BOX,
	CLIP_MATRIX, RASTER_MATRIX, TARGET_TOKEN, FRAMING_TRANSFORM,
	TARGET_SIZE, INPUT0, INPUT1, MATRIX0, MATRIX1, PICK_OFFSET, PICK_PASS, POINTER, ZOOM, LOCATION_COUNT,
};

static const char *names[LOCATION_COUNT] = {
	"umbriel_output_size", "umbriel_scale", "umbriel_time", "umbriel_progress",
	"umbriel_linear_progress", "umbriel_direction", "umbriel_random_seed",
	"umbriel_navigation_position", "umbriel_navigation_velocity", "umbriel_scene_count",
	"umbriel_role", "umbriel_palette", "umbriel_palette_count", "umbriel_audio_levels",
	"umbriel_audio_bands", "umbriel_item_kind", "umbriel_item_ordinal", "umbriel_item_token",
	"umbriel_current_box", "umbriel_source_box", "umbriel_destination_box",
	"umbriel_capture_extent", "umbriel_content_bounds", "umbriel_axis", "umbriel_viewport",
	"umbriel_framing", "umbriel_motion_progress", "umbriel_linear_motion_progress",
	"umbriel_native_opacity", "umbriel_coverage_box", "_fx_clip_matrix", "_fx_raster_matrix", "umbriel_target_token", "umbriel_framing_transform", "_fx_target_size",
	"_fx_input0", "_fx_input1", "_fx_matrix0", "_fx_matrix1",
	"_fx_pick_offset", "_fx_pick_pass", "umbriel_pointer", "umbriel_zoom",
};

struct scene_stage {
	GLuint program;
	GLint location[LOCATION_COUNT];
};

struct fx_scene_program {
	struct fx_renderer *renderer;
	struct wl_listener destroy;
	unsigned references;
	enum fx_scene_profile profile;
	struct scene_stage main, composite, pick, backdrop;
};

struct fx_scene_picker {
	struct fx_renderer *renderer;
	struct wl_listener destroy;
	GLuint framebuffer, color, depth;
};
_Static_assert(sizeof(struct fx_scene_picker) <= 128, "picker metadata reservation");
_Static_assert(sizeof(struct fx_texture) <= FX_SCENE_INPUT_METADATA_BYTES, "retained input metadata reservation");

struct fx_scene_scratch {
	unsigned references;
	struct fx_offscreen_buffers buffers;
	unsigned shadow_padding;
	bool shadow_prepared;
	struct fx_effect_light recipes[FX_SCENE_MAX_PARTICIPANTS];
	struct fx_effect_light_cache *lights[FX_SCENE_MAX_PARTICIPANTS];
	unsigned light_count;
	float light_scale;
};

struct fx_scene_target {
	struct fx_renderer *renderer;
	struct wl_listener destroy;
	struct wlr_buffer *buffer;
	struct fx_framebuffer *framebuffer;
	GLuint depth;
	bool working_space;
	struct fx_scene_scratch *shared;
};

static const char preamble[] =
	"precision highp float;\n"
	// GLES gives vertex and fragment integers different default precision.
	// Shared uniforms must have identical precision to link on strict drivers.
	"precision highp int;\n"
	"#define sin(x) sin(mod((x), 6.283185307179586))\n"
	"#define cos(x) cos(mod((x), 6.283185307179586))\n"
	"uniform vec2 umbriel_output_size;\n"
	"uniform vec2 umbriel_pointer;\n"
	"uniform float umbriel_zoom;\n"
	"uniform float umbriel_scale, umbriel_time, umbriel_progress, umbriel_linear_progress, umbriel_direction;\n"
	"uniform vec4 umbriel_random_seed;\n"
	"uniform float umbriel_navigation_position, umbriel_navigation_velocity;\n"
	"uniform int umbriel_scene_count, umbriel_role, umbriel_target_token;\n"
	"uniform vec4 umbriel_palette[4]; uniform int umbriel_palette_count;\n"
	"vec4 umbriel_palette_at(float t){ if(umbriel_palette_count<=0)return vec4(0.0);"
	"float p=fract(t)*float(umbriel_palette_count); float a=floor(p);"
	"float b=mod(a+1.0,float(umbriel_palette_count));vec4 x=umbriel_palette[0],y=x;"
	"for(int i=0;i<4;i++){if(float(i)==a)x=umbriel_palette[i];if(float(i)==b)y=umbriel_palette[i];}"
	"return mix(x,y,p-a);}\n"
	FX_AUDIO_INPUT_SOURCE
	"uniform int umbriel_item_kind, umbriel_item_ordinal, umbriel_item_token;\n"
	"uniform vec4 umbriel_current_box, umbriel_source_box, umbriel_destination_box;\n"
	"uniform vec4 umbriel_capture_extent, umbriel_content_bounds;\n"
	"uniform vec2 umbriel_axis; uniform vec4 umbriel_viewport; uniform int umbriel_framing;\n"
	"uniform float umbriel_motion_progress, umbriel_linear_motion_progress, umbriel_native_opacity;\n"
	"uniform vec4 umbriel_coverage_box, umbriel_framing_transform;\n"
	"#define umbriel_clamped_progress clamp(umbriel_progress, 0.0, 1.0)\n";

static const char fragment_preamble[] =
	"uniform vec2 _fx_target_size;\n"
	"uniform vec2 _fx_pick_offset; uniform int _fx_pick_pass;\n"
	"uniform mat3 _fx_raster_matrix;\n"
	"vec2 _fx_output_uv(){return (_fx_raster_matrix*vec3((gl_FragCoord.xy+_fx_pick_offset)/_fx_target_size,1.0)).xy;}\n"
	"uniform sampler2D _fx_input0, _fx_input1; uniform mat3 _fx_matrix0, _fx_matrix1;\n"
	"vec4 _fx_sample0(vec2 uv) {\n"
	" if (any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return vec4(0.0);\n"
	" vec2 p=(_fx_matrix0*vec3(uv,1.0)).xy;\n"
	" if (any(lessThan(p,vec2(0.0))) || any(greaterThan(p,vec2(1.0)))) return vec4(0.0);\n"
	" return texture2D(_fx_input0,p); }\n"
	"vec4 _fx_sample1(vec2 uv) {\n"
	" if (any(lessThan(uv,vec2(0.0))) || any(greaterThan(uv,vec2(1.0)))) return vec4(0.0);\n"
	" vec2 p=(_fx_matrix1*vec3(uv,1.0)).xy;\n"
	" if (any(lessThan(p,vec2(0.0))) || any(greaterThan(p,vec2(1.0)))) return vec4(0.0);\n"
	" return texture2D(_fx_input1,p); }\n";

static const char default_vertex[] =
	"vec4 transition_vertex(vec2 uv) { return vec4(uv*2.0-1.0,0.0,1.0); }\n";

static GLuint shader_create(GLenum type, const char *common, const char *body, const char *suffix,
		const char *sampling) {
	const char *varying = type == GL_VERTEX_SHADER
		? "attribute vec2 _fx_mesh_uv; varying vec2 _fx_item_uv; uniform mat3 _fx_clip_matrix;\n"
		: "varying vec2 _fx_item_uv;\n";
	const char *parts[] = {preamble, varying, sampling, common ? common : "", body, suffix};
	size_t size = 1;
	for (unsigned i = 0; i < 6; i++) {
		size += strlen(parts[i]) + 1;
	}
	char *source = malloc(size);
	if (!source) {
		return 0;
	}
	source[0] = '\0';
	for (unsigned i = 0; i < 6; i++) {
		strcat(source, parts[i]);
		strcat(source, "\n");
	}
	GLuint shader = compile_shader(type, source);
	free(source);
	return shader;
}

static bool stage_create(struct scene_stage *stage, const char *common, const char *vertex,
		const char *fragment, const char *suffix, const char *helpers,
		const struct fx_scene_parameter *parameters, unsigned parameter_count) {
	size_t sampling_size = strlen(fragment_preamble) + strlen(helpers) + 1;
	char *sampling = malloc(sampling_size);
	if (!sampling) {
		return false;
	}
	strcpy(sampling, fragment_preamble);
	strcat(sampling, helpers);
	GLuint vert = shader_create(GL_VERTEX_SHADER, common, vertex,
		"void main(){_fx_item_uv=_fx_mesh_uv;vec4 p=transition_vertex(_fx_mesh_uv);"
		"vec3 h=_fx_clip_matrix*vec3(p.xy,p.w);gl_Position=vec4(h.xy,p.z,h.z);}\n", "");
	GLuint frag = shader_create(GL_FRAGMENT_SHADER, common, fragment, suffix, sampling);
	free(sampling);
	if (!vert || !frag) {
		glDeleteShader(vert);
		glDeleteShader(frag);
		return false;
	}
	stage->program = glCreateProgram();
	glAttachShader(stage->program, vert);
	glAttachShader(stage->program, frag);
	glBindAttribLocation(stage->program, 0, "_fx_mesh_uv");
	glLinkProgram(stage->program);
	glDeleteShader(vert);
	glDeleteShader(frag);
	GLint linked = 0;
	glGetProgramiv(stage->program, GL_LINK_STATUS, &linked);
	if (!linked) {
		char log[1024];
		glGetProgramInfoLog(stage->program, sizeof(log), NULL, log);
		wlr_log(WLR_ERROR, "Experimental scene stage failed to link: %s", log);
		return false;
	}
	GLint active = 0;
	glGetProgramiv(stage->program, GL_ACTIVE_UNIFORMS, &active);
	for (GLint i = 0; i < active; i++) {
		char name[128];
		GLsizei length;
		GLint size;
		GLenum type;
		glGetActiveUniform(stage->program, i, sizeof(name), &length, &size, &type, name);
		char *bracket = strchr(name, '[');
		if (bracket) {
			*bracket = '\0';
		}
		bool known = false;
		for (unsigned j = 0; j < LOCATION_COUNT; j++) {
			known |= strcmp(name, names[j]) == 0;
		}
		for (unsigned j = 0; j < parameter_count; j++) {
			if (strcmp(name, parameters[j].name) != 0) {
				continue;
			}
			static const GLenum types[] = {GL_FLOAT, GL_FLOAT_VEC2, GL_FLOAT_VEC3, GL_FLOAT_VEC4};
			if (bracket != NULL || size != 1 || type != types[parameters[j].components - 1]) {
				return false;
			}
			known = true;
		}
		if (!known) {
			return false;
		}
	}
	for (unsigned i = 0; i < LOCATION_COUNT; i++) {
		stage->location[i] = glGetUniformLocation(stage->program, names[i]);
	}
	glUseProgram(stage->program);
	for (unsigned i = 0; i < parameter_count; i++) {
		GLint location = glGetUniformLocation(stage->program, parameters[i].name);
		switch (parameters[i].components) {
		case 1: glUniform1fv(location, 1, parameters[i].value); break;
		case 2: glUniform2fv(location, 1, parameters[i].value); break;
		case 3: glUniform3fv(location, 1, parameters[i].value); break;
		case 4: glUniform4fv(location, 1, parameters[i].value); break;
		}
	}
	return glGetError() == GL_NO_ERROR;
}

static void program_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_scene_program *program = wl_container_of(listener, program, destroy);
	wl_list_remove(&program->destroy.link);
	program->renderer = NULL;
}

struct fx_scene_program *fx_scene_program_ref(struct fx_scene_program *program) {
	if (program) {
		program->references++;
	}
	return program;
}

bool fx_scene_program_reads_audio(const struct fx_scene_program *program) {
	return program && program->renderer &&
		(program->main.location[AUDIO_LEVELS] >= 0 || program->main.location[AUDIO_BANDS] >= 0 ||
			(program->composite.program && (program->composite.location[AUDIO_LEVELS] >= 0 ||
				program->composite.location[AUDIO_BANDS] >= 0)) ||
			(program->backdrop.program && (program->backdrop.location[AUDIO_LEVELS] >= 0 ||
				program->backdrop.location[AUDIO_BANDS] >= 0)));
}

bool fx_scene_program_reads_time(const struct fx_scene_program *program) {
	return program && program->renderer && (program->main.location[TIME] >= 0 ||
		(program->composite.program && program->composite.location[TIME] >= 0) ||
		(program->backdrop.program && program->backdrop.location[TIME] >= 0));
}

bool fx_scene_program_reads_pointer(const struct fx_scene_program *program) {
	return program && program->renderer && (program->main.location[POINTER] >= 0 ||
		(program->composite.program && program->composite.location[POINTER] >= 0) ||
		(program->backdrop.program && program->backdrop.location[POINTER] >= 0));
}

bool fx_scene_program_reads_zoom(const struct fx_scene_program *program) {
	return program && program->renderer && (program->main.location[ZOOM] >= 0 ||
		(program->composite.program && program->composite.location[ZOOM] >= 0) ||
		(program->backdrop.program && program->backdrop.location[ZOOM] >= 0));
}

bool fx_scene_program_supports_picking(const struct fx_scene_program *program) {
	return program && program->renderer && program->pick.program;
}

static void picker_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_scene_picker *picker = wl_container_of(listener, picker, destroy);
	wl_list_remove(&picker->destroy.link);
	picker->renderer = NULL;
}

struct fx_scene_picker *fx_scene_picker_create(struct wlr_renderer *wlr_renderer) {
	if (!wlr_renderer || !wlr_renderer_is_fx(wlr_renderer)) return NULL;
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(renderer->egl, &previous)) return NULL;
	GLint fbo, texture, renderbuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &texture);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &renderbuffer);
	struct fx_scene_picker *picker = calloc(1, sizeof(*picker));
	if (picker) {
		glGenFramebuffers(1, &picker->framebuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, picker->framebuffer);
		glGenTextures(1, &picker->color);
		glBindTexture(GL_TEXTURE_2D, picker->color);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, picker->color, 0);
		glGenRenderbuffers(1, &picker->depth);
		glBindRenderbuffer(GL_RENDERBUFFER, picker->depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, 1, 1);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, picker->depth);
		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE || glGetError() != GL_NO_ERROR) {
			glDeleteFramebuffers(1, &picker->framebuffer);
			glDeleteTextures(1, &picker->color);
			glDeleteRenderbuffers(1, &picker->depth);
			free(picker);
			picker = NULL;
		} else {
			picker->renderer = renderer;
			picker->destroy.notify = picker_renderer_destroy;
			wl_signal_add(&wlr_renderer->events.destroy, &picker->destroy);
		}
	}
	glBindFramebuffer(GL_FRAMEBUFFER, fbo);
	glBindTexture(GL_TEXTURE_2D, texture);
	glBindRenderbuffer(GL_RENDERBUFFER, renderbuffer);
	wlr_egl_restore_context(&previous);
	return picker;
}

void fx_scene_picker_destroy(struct fx_scene_picker *picker) {
	if (!picker) return;
	if (picker->renderer) {
		struct wlr_egl_context previous;
		if (wlr_egl_make_current(picker->renderer->egl, &previous)) {
			glDeleteFramebuffers(1, &picker->framebuffer);
			glDeleteTextures(1, &picker->color);
			glDeleteRenderbuffers(1, &picker->depth);
			wlr_egl_restore_context(&previous);
		}
		wl_list_remove(&picker->destroy.link);
	}
	free(picker);
}

static void target_finish_lights(struct fx_scene_target *target) {
	for (unsigned i = 0; i < target->shared->light_count; i++) {
		fx_effect_light_cache_destroy(target->shared->lights[i]);
		target->shared->lights[i] = NULL;
	}
	target->shared->light_count = 0;
}

static void target_finish_scratch(struct fx_scene_target *target) {
	target_finish_lights(target);
	fx_offscreen_buffers_finish_local(&target->shared->buffers);
	target->shared->shadow_prepared = false;
}

static void target_release_scratch(struct fx_scene_target *target) {
	if (--target->shared->references == 0) {
		target_finish_scratch(target);
		free(target->shared);
	}
}

static void target_renderer_destroy(struct wl_listener *listener, void *data) {
	struct fx_scene_target *target = wl_container_of(listener, target, destroy);
	wl_list_remove(&target->destroy.link);
	target_finish_scratch(target);
	target->renderer = NULL;
	target->framebuffer = NULL;
	target->depth = 0;
}

struct wlr_buffer *fx_scene_buffer_create(struct wlr_renderer *renderer,
		struct wlr_allocator *allocator, int width, int height, bool floating_point) {
	if (!renderer || !wlr_renderer_is_fx(renderer) || !allocator || width <= 0 || height <= 0 ||
			!renderer->impl->get_render_formats ||
			(floating_point && !renderer->features.output_color_transform)) return NULL;
	struct fx_scene_limits limits;
	if (!fx_scene_program_get_limits(renderer, &limits) ||
			(unsigned)width > limits.texture_size || (unsigned)height > limits.texture_size) return NULL;
	const struct wlr_drm_format_set *formats = renderer->impl->get_render_formats(renderer);
	const struct wlr_drm_format *format = formats ? wlr_drm_format_set_get(formats,
		floating_point ? DRM_FORMAT_ABGR16161616F : DRM_FORMAT_ARGB8888) : NULL;
	return format ? wlr_allocator_create_buffer(allocator, width, height, format) : NULL;
}

static struct fx_scene_target *target_create(struct wlr_renderer *wlr_renderer,
		struct wlr_buffer *buffer, bool depth, int working_space) {
	if (!wlr_renderer || !wlr_renderer_is_fx(wlr_renderer) || !buffer || buffer->width <= 0 || buffer->height <= 0) {
		return NULL;
	}
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(renderer->egl, &previous)) {
		return NULL;
	}
	GLint old_fbo, old_renderbuffer;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
	glGetIntegerv(GL_RENDERBUFFER_BINDING, &old_renderbuffer);
	struct fx_scene_target *target = calloc(1, sizeof(*target));
	if (!target) {
		goto out;
	}
	target->renderer = renderer;
	target->framebuffer = fx_framebuffer_get_or_create(renderer, buffer);
	if (!target->framebuffer) {
		free(target);
		target = NULL;
		goto out;
	}
	target->working_space = working_space < 0 ? target->framebuffer->drm_format == DRM_FORMAT_ABGR16161616F : working_space;
	glBindFramebuffer(GL_FRAMEBUFFER, fx_framebuffer_get_fbo(target->framebuffer));
	if (depth) {
		GLint attached = GL_NONE;
		glGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
			GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &attached);
		if (attached != GL_NONE) {
			free(target);
			target = NULL;
			goto out;
		}
		glGenRenderbuffers(1, &target->depth);
		glBindRenderbuffer(GL_RENDERBUFFER, target->depth);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, buffer->width, buffer->height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target->depth);
	}
	bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE && glGetError() == GL_NO_ERROR;
	if (!ok) {
		if (depth) {
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glDeleteRenderbuffers(1, &target->depth);
		}
		free(target);
		target = NULL;
		goto out;
	}
	target->shared = calloc(1, sizeof(*target->shared));
	if (!target->shared) {
		if (target->depth) {
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glDeleteRenderbuffers(1, &target->depth);
		}
		free(target);
		target = NULL;
		goto out;
	}
	target->shared->references = 1;
	target->buffer = wlr_buffer_lock(buffer);
	target->destroy.notify = target_renderer_destroy;
	wl_signal_add(&wlr_renderer->events.destroy, &target->destroy);
out:
	glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
	glBindRenderbuffer(GL_RENDERBUFFER, old_renderbuffer);
	wlr_egl_restore_context(&previous);
	return target;
}

struct fx_scene_target *fx_scene_target_create(struct wlr_renderer *renderer,
		struct wlr_buffer *buffer, bool depth) {
	return target_create(renderer, buffer, depth, -1);
}

struct fx_scene_target *fx_scene_target_create_with_color(struct wlr_renderer *renderer,
		struct wlr_buffer *buffer, bool depth, bool working_space) {
	return target_create(renderer, buffer, depth, working_space);
}

void fx_scene_target_destroy(struct fx_scene_target *target) {
	if (!target) {
		return;
	}
	if (target->renderer) {
		struct wlr_egl_context previous;
		if (target->depth && wlr_egl_make_current(target->renderer->egl, &previous)) {
			GLint old_fbo;
			glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
			glBindFramebuffer(GL_FRAMEBUFFER, fx_framebuffer_get_fbo(target->framebuffer));
			glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
			glDeleteRenderbuffers(1, &target->depth);
			glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
			wlr_egl_restore_context(&previous);
		}
		wl_list_remove(&target->destroy.link);
	}
	target_release_scratch(target);
	wlr_buffer_unlock(target->buffer);
	free(target);
}

static uint32_t target_scratch_format(const struct fx_scene_target *target) {
	return target->framebuffer->drm_format == DRM_FORMAT_ABGR16161616F
		? DRM_FORMAT_ABGR16161616F : DRM_FORMAT_ABGR8888;
}

bool fx_scene_target_share_scratch(struct fx_scene_target *target, struct fx_scene_target *owner) {
	if (!target || !owner || !target->renderer || target->renderer != owner->renderer ||
			target->buffer->width != owner->buffer->width || target->buffer->height != owner->buffer->height ||
			target_scratch_format(target) != target_scratch_format(owner) || target->working_space != owner->working_space ||
			!owner->shared->shadow_prepared) return false;
	if (target->shared == owner->shared) return true;
	if (target->shared->shadow_prepared || target->shared->light_count || owner->shared->references == UINT_MAX) return false;
	owner->shared->references++;
	target_release_scratch(target);
	target->shared = owner->shared;
	return true;
}

uint64_t fx_scene_target_shadow_bytes(const struct fx_scene_target *target, unsigned padding) {
	if (!target || !target->renderer || padding > 4096) {
		return 0;
	}
	uint64_t width = (uint64_t)target->buffer->width + padding * 2;
	uint64_t height = (uint64_t)target->buffer->height + padding * 2;
	if (width > INT_MAX || height > INT_MAX || width * height > UINT64_MAX / 24) {
		return 0;
	}
	return width * height * 3 * (target_scratch_format(target) == DRM_FORMAT_ABGR16161616F ? 8 : 4);
}

bool fx_scene_target_prepare_shadow(struct fx_scene_target *target, unsigned padding) {
	if (!fx_scene_target_shadow_bytes(target, padding) || !target->renderer->allocator) {
		return false;
	}
	if (target->shared->shadow_prepared) {
		return target->shared->shadow_padding == padding;
	}
	struct fx_renderer *renderer = target->renderer;
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(renderer->egl, &previous)) {
		return false;
	}
	GLint old_fbo;
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &old_fbo);
	bool failed = !fx_effect_prepare_shadow(renderer);
	target->shared->buffers.renderer = renderer;
	target->shared->buffers.allocator = renderer->allocator;
	for (unsigned i = 0; !failed && i < 3; i++) {
		fx_framebuffer_get_or_create_custom(renderer, renderer->allocator,
			target->buffer->width + padding * 2, target->buffer->height + padding * 2,
			target_scratch_format(target), &target->shared->buffers.group.animation_buffers[i], &failed);
	}
	if (failed) {
		fx_offscreen_buffers_finish_local(&target->shared->buffers);
	} else {
		target->shared->shadow_prepared = true;
		target->shared->shadow_padding = padding;
	}
	glBindFramebuffer(GL_FRAMEBUFFER, old_fbo);
	wlr_egl_restore_context(&previous);
	return !failed;
}

static bool light_valid(const struct fx_effect_light *light, unsigned padding, float scale) {
	return light && light->enabled && isfinite(light->spread) && light->spread >= 0 &&
		isfinite(light->intensity) && light->intensity >= 0 && light->intensity <= 4 &&
		isfinite(light->threshold) && light->threshold >= 0 && light->threshold <= 1 &&
		isfinite(scale) && scale > 0 && ceilf(light->spread * scale) <= padding;
}

uint64_t fx_scene_target_light_bytes(const struct fx_scene_target *target, unsigned padding,
		const struct fx_effect_light *recipes, unsigned count, float scale) {
	uint64_t total = fx_scene_target_shadow_bytes(target, padding);
	if (!total || !recipes || !count || count > FX_SCENE_MAX_PARTICIPANTS) return 0;
	uint64_t width = target->buffer->width + padding * 2, height = target->buffer->height + padding * 2;
	for (unsigned i = 0; i < count; i++) {
		if (!light_valid(&recipes[i], padding, scale)) return 0;
		int levels = (int)ceilf(log2f(recipes[i].spread * scale / 6 + 1));
		levels = levels < 1 ? 1 : (levels > FX_LIGHT_LEVELS ? FX_LIGHT_LEVELS : levels);
		uint64_t bytes = sizeof(struct fx_effect_light_cache) + width * height * 8;
		uint64_t w = (width + 1) / 2, h = (height + 1) / 2;
		for (int level = 0; level <= levels; level++) {
			uint64_t lw = (w + (1u << level) - 1) >> level;
			uint64_t lh = (h + (1u << level) - 1) >> level;
			bytes += (lw ? lw : 1) * (lh ? lh : 1) * 8;
		}
		if (bytes > UINT64_MAX - total) return 0;
		total += bytes;
	}
	return total;
}

static bool same_light(const struct fx_effect_light *a, const struct fx_effect_light *b) {
	return a->enabled == b->enabled && a->spread == b->spread &&
		a->threshold == b->threshold && a->intensity == b->intensity;
}

bool fx_scene_target_prepare_light(struct fx_scene_target *target, unsigned padding,
		const struct fx_effect_light *recipes, unsigned count, float scale) {
	if (!fx_scene_target_light_bytes(target, padding, recipes, count, scale)) return false;
	if (target->shared->light_count) {
		if (target->shared->light_count != count || target->shared->light_scale != scale || target->shared->shadow_padding != padding) return false;
		for (unsigned i = 0; i < count; i++) if (!same_light(&target->shared->recipes[i], &recipes[i])) return false;
		return true;
	}
	if (!fx_scene_target_prepare_shadow(target, padding) || !fx_effect_prepare_light(target->renderer)) return false;
	for (unsigned i = 0; i < count; i++) {
		target->shared->lights[i] = fx_effect_light_cache_create(target->renderer);
		target->shared->light_count++;
		if (!target->shared->lights[i] || !fx_effect_light_cache_prepare_scene(target->shared->lights[i],
				target->buffer->width + padding * 2, target->buffer->height + padding * 2, recipes[i].spread * scale)) {
			target_finish_lights(target);
			return false;
		}
		target->shared->recipes[i] = recipes[i];
	}
	target->shared->light_scale = scale;
	return true;
}

static struct fx_effect_light_cache *target_light(struct fx_scene_target *target,
		const struct fx_effect_light *recipe) {
	for (unsigned i = 0; i < target->shared->light_count; i++) {
		if (same_light(&target->shared->recipes[i], recipe)) return target->shared->lights[i];
	}
	return NULL;
}

static bool input_valid(const struct fx_scene_input *input, const struct fx_scene_target *target) {
	if (!input || !input->texture || !wlr_texture_is_fx(input->texture)) {
		return false;
	}
	struct fx_texture *texture = fx_get_texture(input->texture);
	return texture->fx_renderer == target->renderer && texture->target == GL_TEXTURE_2D &&
		texture->locked_buffer != target->buffer && (!texture->buffer || texture->buffer->buffer != target->buffer);
}

bool fx_scene_target_mask(struct fx_scene_target *target, const struct fx_scene_frame *frame,
		const float box[4], const float corners[4]) {
	if (!target || !target->renderer || !frame || !box || !corners ||
			frame->output_transform > WL_OUTPUT_TRANSFORM_FLIPPED_270 || !isfinite(frame->scale) || frame->scale <= 0) return false;
	int width = target->buffer->width, height = target->buffer->height;
	wlr_output_transform_coords(frame->output_transform, &width, &height);
	if (!isfinite(frame->output_size[0]) || !isfinite(frame->output_size[1]) ||
			frame->output_size[0] <= 0 || frame->output_size[1] <= 0 ||
			lroundf(frame->output_size[0] * frame->scale) != width ||
			lroundf(frame->output_size[1] * frame->scale) != height) return false;
	for (unsigned i = 0; i < 4; i++) {
		if (!isfinite(box[i]) || fabsf(box[i] * frame->scale) > INT_MAX / 8 ||
				!isfinite(corners[i]) || corners[i] < 0 || corners[i] * frame->scale > INT_MAX / 8) return false;
	}
	if (box[2] <= 0 || box[3] <= 0) return false;
	struct wlr_box dst = {lroundf(box[0] * frame->scale), lroundf(box[1] * frame->scale),
		lroundf((box[0] + box[2]) * frame->scale) - lroundf(box[0] * frame->scale),
		lroundf((box[1] + box[3]) * frame->scale) - lroundf(box[1] * frame->scale)};
	wlr_box_transform(&dst, &dst, wlr_output_transform_invert(frame->output_transform), width, height);
	float physical[4];
	for (unsigned i = 0; i < 4; i++) {
		struct wlr_box corner = {.x = i == 1 || i == 2, .y = i >= 2};
		wlr_box_transform(&corner, &corner, wlr_output_transform_invert(frame->output_transform), 1, 1);
		unsigned index = corner.y ? (corner.x ? 2 : 3) : (corner.x ? 1 : 0);
		physical[index] = corners[i] * frame->scale;
	}
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(&target->renderer->wlr_renderer, target->buffer, NULL);
	if (!wlr_pass) return false;
	struct fx_gles_render_pass *pass = fx_get_render_pass(wlr_pass);
	pass->working_space = target->working_space;
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	fx_render_pass_add_rounded_rect(pass, &(struct fx_render_rounded_rect_options){
		.base = {.box = dst, .color = {1, 1, 1, 1}, .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED},
		.corners = {physical[0], physical[1], physical[2], physical[3]},
	});
	bool ok = glGetError() == GL_NO_ERROR;
	return wlr_render_pass_submit(wlr_pass) && ok;
}

bool fx_scene_target_blend(struct fx_scene_target *target,
		const struct fx_scene_input *first, const struct fx_scene_input *second,
		float weight, bool working_space) {
	if (!target || !target->renderer || !input_valid(first, target) || !input_valid(second, target) ||
			!isfinite(weight) || weight < 0 || weight > 1 ||
			working_space != target->working_space) return false;
	static const float identity[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	const struct fx_scene_input *inputs[2] = {first, second};
	for (unsigned i = 0; i < 2; i++) {
		if (inputs[i]->sample_matrix && memcmp(inputs[i]->sample_matrix, identity, sizeof(identity))) return false;
	}
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(&target->renderer->wlr_renderer, target->buffer, NULL);
	if (!wlr_pass) return false;
	struct fx_gles_render_pass *pass = fx_get_render_pass(wlr_pass);
	pass->working_space = working_space;
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 0);
	glClear(GL_COLOR_BUFFER_BIT);
	glBlendFunc(GL_ONE, GL_ONE);
	for (unsigned i = 0; i < 2; i++) {
		float alpha = i ? weight : 1 - weight;
		if (alpha == 0) continue;
		fx_render_pass_add_texture(pass, &(struct fx_render_texture_options){.base = {
			.texture = inputs[i]->texture,
			.dst_box = {0, 0, target->buffer->width, target->buffer->height},
			.alpha = &alpha, .blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
			.filter_mode = WLR_SCALE_FILTER_BILINEAR,
			.transfer_function = working_space ? WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR : 0,
		}});
	}
	bool ok = glGetError() == GL_NO_ERROR;
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	return wlr_render_pass_submit(wlr_pass) && ok;
}

static void bind_input(const struct scene_stage *stage, const struct fx_scene_input *input, unsigned unit) {
	static const float identity[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
	glActiveTexture(GL_TEXTURE0 + unit);
	glBindTexture(GL_TEXTURE_2D, fx_get_texture(input->texture)->tex);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glUniform1i(stage->location[unit ? INPUT1 : INPUT0], unit);
	glUniformMatrix3fv(stage->location[unit ? MATRIX1 : MATRIX0], 1, GL_FALSE,
		input->sample_matrix ? input->sample_matrix : identity);
}

static void transform_uv_matrix(enum wl_output_transform transform, float matrix[9]) {
	struct wlr_box origin = {0}, x = {.x = 1}, y = {.y = 1};
	wlr_box_transform(&origin, &origin, transform, 1, 1);
	wlr_box_transform(&x, &x, transform, 1, 1);
	wlr_box_transform(&y, &y, transform, 1, 1);
	const float values[] = {
		x.x - origin.x, x.y - origin.y, 0,
		y.x - origin.x, y.y - origin.y, 0,
		origin.x, origin.y, 1,
	};
	memcpy(matrix, values, sizeof(values));
}

static void bind_frame(const struct scene_stage *stage, const struct fx_scene_frame *frame,
		const struct fx_scene_target *target, unsigned padding) {
	const GLint *l = stage->location;
	glUseProgram(stage->program);
	float raster[9], clip[9];
	transform_uv_matrix(frame->output_transform, raster);
	transform_uv_matrix(wlr_output_transform_invert(frame->output_transform), clip);
	clip[6] = clip[0] + clip[3] + 2 * clip[6] - 1;
	clip[7] = clip[1] + clip[4] + 2 * clip[7] - 1;
	if (padding) {
		float sx = (float)target->buffer->width / (target->buffer->width + 2 * padding);
		float sy = (float)target->buffer->height / (target->buffer->height + 2 * padding);
		for (unsigned column = 0; column < 3; column++) {
			clip[column * 3] *= sx;
			clip[column * 3 + 1] *= sy;
		}
		float tx = -(float)padding / target->buffer->width;
		float ty = -(float)padding / target->buffer->height;
		raster[6] += raster[0] * tx + raster[3] * ty;
		raster[7] += raster[1] * tx + raster[4] * ty;
		raster[0] /= sx; raster[1] /= sx;
		raster[3] /= sy; raster[4] /= sy;
	}
	glUniformMatrix3fv(l[CLIP_MATRIX], 1, GL_FALSE, clip);
	glUniformMatrix3fv(l[RASTER_MATRIX], 1, GL_FALSE, raster);
	glUniform2fv(l[OUTPUT_SIZE], 1, frame->output_size);
	glUniform2fv(l[POINTER], 1, frame->pointer);
	glUniform1f(l[ZOOM], frame->zoom);
	glUniform2f(l[TARGET_SIZE], target->buffer->width + 2 * padding, target->buffer->height + 2 * padding);
	glUniform2f(l[PICK_OFFSET], 0, 0);
	glUniform1i(l[PICK_PASS], 0);
	glUniform1f(l[SCALE], frame->scale);
	glUniform1f(l[TIME], frame->time);
	glUniform1f(l[PROGRESS], frame->progress);
	glUniform1f(l[LINEAR_PROGRESS], frame->linear_progress);
	glUniform1f(l[DIRECTION], frame->direction);
	glUniform4fv(l[SEED], 1, frame->random_seed);
	glUniform1f(l[NAV_POSITION], frame->navigation_position);
	glUniform1f(l[NAV_VELOCITY], frame->navigation_velocity);
	glUniform1i(l[SCENE_COUNT], frame->scene_count);
	glUniform1i(l[ROLE], frame->role);
	glUniform1i(l[TARGET_TOKEN], frame->target_token);
	glUniform2fv(l[AXIS], 1, frame->axis);
	glUniform4fv(l[VIEWPORT], 1, frame->viewport);
	glUniform1i(l[FRAMING], frame->framing);
	glUniform4fv(l[PALETTE], 4, frame->palette);
	glUniform1i(l[PALETTE_COUNT], frame->palette_count);
	glUniform4fv(l[AUDIO_LEVELS], 1, frame->audio_levels);
	glUniform4fv(l[AUDIO_BANDS], 4, frame->audio_bands);
}

static void bind_item(const struct scene_stage *stage, const struct fx_scene_item *item) {
	const GLint *l = stage->location;
	glUniform1i(l[ITEM_KIND], item->kind);
	glUniform1i(l[ITEM_ORDINAL], item->ordinal);
	glUniform1i(l[ITEM_TOKEN], item->token);
	glUniform4fv(l[CURRENT_BOX], 1, item->current_box);
	glUniform4fv(l[SOURCE_BOX], 1, item->source_box);
	glUniform4fv(l[DESTINATION_BOX], 1, item->destination_box);
	glUniform4fv(l[CAPTURE_EXTENT], 1, item->capture_extent);
	glUniform4fv(l[CONTENT_BOUNDS], 1, item->content_bounds);
	glUniform1f(l[MOTION_PROGRESS], item->motion_progress);
	glUniform1f(l[LINEAR_MOTION_PROGRESS], item->linear_motion_progress);
	glUniform1f(l[NATIVE_OPACITY], item->native_opacity);
	glUniform4fv(l[COVERAGE_BOX], 1, item->coverage_box);
	glUniform4fv(l[FRAMING_TRANSFORM], 1, item->framing_transform);
}

static void draw_mesh(const struct fx_scene_mesh *mesh) {
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, mesh->uv);
	glDrawElements(GL_TRIANGLES, mesh->index_count, GL_UNSIGNED_SHORT, mesh->indices);
}

// Input handling can run between ordinary render passes. Preserve the exact
// state we touch, including the caller's vertex-array pointer and buffer.
struct pick_gl_state {
	GLint framebuffer, program, viewport[4], active_texture, texture;
	GLint array_buffer, element_buffer, depth_func;
	GLint attribute_enabled, attribute_size, attribute_type, attribute_normalized;
	GLint attribute_stride, attribute_buffer;
	void *attribute_pointer;
	GLboolean color_mask[4], depth_mask, enabled[6];
	GLfloat clear_color[4], clear_depth, depth_range[2];
};

static const GLenum pick_capabilities[] = {
	GL_DEPTH_TEST, GL_SCISSOR_TEST, GL_BLEND, GL_CULL_FACE, GL_DITHER, GL_POLYGON_OFFSET_FILL,
};

static void pick_save_state(struct pick_gl_state *state) {
	glGetIntegerv(GL_FRAMEBUFFER_BINDING, &state->framebuffer);
	glGetIntegerv(GL_CURRENT_PROGRAM, &state->program);
	glGetIntegerv(GL_VIEWPORT, state->viewport);
	glGetIntegerv(GL_ACTIVE_TEXTURE, &state->active_texture);
	glActiveTexture(GL_TEXTURE0);
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &state->texture);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &state->array_buffer);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &state->element_buffer);
	glGetIntegerv(GL_DEPTH_FUNC, &state->depth_func);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_ENABLED, &state->attribute_enabled);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &state->attribute_size);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &state->attribute_type);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &state->attribute_normalized);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &state->attribute_stride);
	glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_BUFFER_BINDING, &state->attribute_buffer);
	glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &state->attribute_pointer);
	glGetBooleanv(GL_COLOR_WRITEMASK, state->color_mask);
	glGetBooleanv(GL_DEPTH_WRITEMASK, &state->depth_mask);
	glGetFloatv(GL_COLOR_CLEAR_VALUE, state->clear_color);
	glGetFloatv(GL_DEPTH_CLEAR_VALUE, &state->clear_depth);
	glGetFloatv(GL_DEPTH_RANGE, state->depth_range);
	for (unsigned i = 0; i < 6; i++) state->enabled[i] = glIsEnabled(pick_capabilities[i]);
}

static void pick_restore_state(const struct pick_gl_state *state) {
	glBindFramebuffer(GL_FRAMEBUFFER, state->framebuffer);
	glUseProgram(state->program);
	glViewport(state->viewport[0], state->viewport[1], state->viewport[2], state->viewport[3]);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, state->texture);
	glActiveTexture(state->active_texture);
	glBindBuffer(GL_ARRAY_BUFFER, state->attribute_buffer);
	glVertexAttribPointer(0, state->attribute_size, state->attribute_type, state->attribute_normalized,
		state->attribute_stride, state->attribute_pointer);
	if (state->attribute_enabled) glEnableVertexAttribArray(0); else glDisableVertexAttribArray(0);
	glBindBuffer(GL_ARRAY_BUFFER, state->array_buffer);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, state->element_buffer);
	glDepthFunc(state->depth_func);
	glDepthMask(state->depth_mask);
	glColorMask(state->color_mask[0], state->color_mask[1], state->color_mask[2], state->color_mask[3]);
	glClearColor(state->clear_color[0], state->clear_color[1], state->clear_color[2], state->clear_color[3]);
	glClearDepthf(state->clear_depth);
	glDepthRangef(state->depth_range[0], state->depth_range[1]);
	for (unsigned i = 0; i < 6; i++) {
		if (state->enabled[i]) glEnable(pick_capabilities[i]); else glDisable(pick_capabilities[i]);
	}
}

enum fx_scene_pick_status fx_scene_program_pick(struct fx_scene_program *program,
		struct fx_scene_picker *picker, struct fx_scene_target *target,
		const struct fx_scene_frame *frame, const struct fx_scene_draw *draws,
		unsigned count, float x, float y, struct fx_scene_pick *result) {
	if (!fx_scene_program_supports_picking(program) || !picker || !target || !frame || !result ||
			picker->renderer != program->renderer || target->renderer != program->renderer || !target->depth ||
			!isfinite(frame->output_size[0]) || !isfinite(frame->output_size[1]) ||
			frame->output_size[0] <= 0 || frame->output_size[1] <= 0 ||
			frame->output_transform > WL_OUTPUT_TRANSFORM_FLIPPED_270 ||
			!draws || !count || count > FX_SCENE_MAX_FACES || count != (unsigned)frame->scene_count ||
			!isfinite(x) || !isfinite(y)) return FX_SCENE_PICK_UNSUPPORTED;
	if (x < 0 || y < 0 || x >= frame->output_size[0] || y >= frame->output_size[1]) return FX_SCENE_PICK_MISS;
	uint64_t vertices = 0;
	for (unsigned i = 0; i < count; i++) {
		const struct fx_scene_mesh *mesh = draws[i].mesh;
		if (!mesh || !mesh->uv || !mesh->indices || mesh->vertices < 4 || !mesh->index_count ||
				mesh->vertices > (FX_SCENE_MAX_GRID + 1) * (FX_SCENE_MAX_GRID + 1) ||
				mesh->index_count > FX_SCENE_MAX_GRID * FX_SCENE_MAX_GRID * 6 ||
				draws[i].item.kind != FX_SCENE_FACE || draws[i].item.token < 1 ||
				draws[i].item.token > (int)FX_SCENE_MAX_FACES || draws[i].item.ordinal < 0 ||
				draws[i].item.ordinal >= (int)FX_SCENE_MAX_FACES || draws[i].emission || draws[i].shadow ||
				draws[i].light || !input_valid(&draws[i].input, target)) return FX_SCENE_PICK_UNSUPPORTED;
		vertices += mesh->vertices;
	}
	if (vertices > FX_SCENE_MAX_VERTICES) return FX_SCENE_PICK_UNSUPPORTED;
	float matrix[9];
	transform_uv_matrix(wlr_output_transform_invert(frame->output_transform), matrix);
	float u = x / frame->output_size[0], v = y / frame->output_size[1];
	int px = (int)floorf((matrix[0] * u + matrix[3] * v + matrix[6]) * target->buffer->width);
	int py = (int)floorf((matrix[1] * u + matrix[4] * v + matrix[7]) * target->buffer->height);
	// A reflected exact logical edge maps to the last physical pixel.
	px = px < 0 ? 0 : px >= target->buffer->width ? target->buffer->width - 1 : px;
	py = py < 0 ? 0 : py >= target->buffer->height ? target->buffer->height - 1 : py;
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(program->renderer->egl, &previous)) return FX_SCENE_PICK_UNSUPPORTED;
	struct pick_gl_state state;
	pick_save_state(&state);
	glBindFramebuffer(GL_FRAMEBUFFER, picker->framebuffer);
	glViewport(-px, -py, target->buffer->width, target->buffer->height);
	for (unsigned i = 1; i < 6; i++) glDisable(pick_capabilities[i]);
	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);
	glDepthMask(GL_TRUE);
	glDepthRangef(0, 1);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glClearColor(0, 0, 0, 0);
	glClearDepthf(1);
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	bind_frame(&program->pick, frame, target, 0);
	glUniform2f(program->pick.location[PICK_OFFSET], px, py);
	uint8_t pixels[2][4] = {{0}};
	for (unsigned pass = 0; pass < 2; pass++) {
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
		glUniform1i(program->pick.location[PICK_PASS], pass);
		for (unsigned i = 0; i < count; i++) {
			bind_item(&program->pick, &draws[i].item);
			glActiveTexture(GL_TEXTURE0);
			glBindTexture(GL_TEXTURE_2D, fx_get_texture(draws[i].input.texture)->tex);
			static const GLenum parameters[] = {GL_TEXTURE_MIN_FILTER, GL_TEXTURE_MAG_FILTER,
				GL_TEXTURE_WRAP_S, GL_TEXTURE_WRAP_T};
			GLint previous_parameters[4];
			for (unsigned p = 0; p < 4; p++) glGetTexParameteriv(GL_TEXTURE_2D, parameters[p], &previous_parameters[p]);
			bind_input(&program->pick, &draws[i].input, 0);
			draw_mesh(draws[i].mesh);
			for (unsigned p = 0; p < 4; p++) glTexParameteri(GL_TEXTURE_2D, parameters[p], previous_parameters[p]);
		}
		glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixels[pass]);
	}
	bool ok = glGetError() == GL_NO_ERROR;
	pick_restore_state(&state);
	ok = glGetError() == GL_NO_ERROR && ok;
	wlr_egl_restore_context(&previous);
	if (!ok || pixels[0][2]) return FX_SCENE_PICK_UNSUPPORTED;
	if (!pixels[0][3]) return FX_SCENE_PICK_MISS;
	*result = (struct fx_scene_pick){.token = pixels[0][0], .ordinal = pixels[0][1],
		.uv = {(pixels[1][0] * 256u + pixels[1][1]) / 65535.0f,
			(pixels[1][2] * 256u + pixels[1][3]) / 65535.0f}};
	return FX_SCENE_PICK_HIT;
}

static bool render_stage(struct fx_scene_program *program, const struct scene_stage *stage,
		struct fx_scene_target *target, const struct fx_scene_frame *frame,
		const struct fx_scene_input pair[2], const struct fx_scene_draw *draws, unsigned count,
		bool composite) {
	struct wlr_render_pass *wlr_pass = wlr_renderer_begin_buffer_pass(&program->renderer->wlr_renderer,
		target->buffer, NULL);
	if (!wlr_pass) {
		return false;
	}
	struct fx_gles_render_pass *pass = fx_get_render_pass(wlr_pass);
	pass->working_space = target->working_space;
	if (target->shared->shadow_prepared) {
		pass->fx_offscreen_buffers = &target->shared->buffers;
		// Prepared storage survives frames containing no shadow draw.
		pass->group_used = true;
	}
	pixman_region32_union_rect(&pass->updated_region, &pass->updated_region, 0, 0,
		target->buffer->width, target->buffer->height);
	glDisable(GL_CULL_FACE);
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_BLEND);
	glDisable(GL_DEPTH_TEST);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	glDepthMask(GL_TRUE);
	glClearColor(0, 0, 0, 0);
	glClearDepthf(1);
	glDepthRangef(0, 1);
	glClear(GL_COLOR_BUFFER_BIT | (target->depth ? GL_DEPTH_BUFFER_BIT : 0));
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	glEnableVertexAttribArray(0);
	// Backdrops draw into the same target before geometry. They neither write
	// depth nor participate in picking, and need no extra full-output buffer.
	if (!composite && program->backdrop.program) {
		bind_frame(&program->backdrop, frame, target, 0);
		const struct fx_scene_item empty = {0};
		bind_item(&program->backdrop, &empty);
		float uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
		uint16_t indices[] = {0, 1, 2, 2, 1, 3};
		const struct fx_scene_mesh quad = {.uv = uv, .indices = indices, .vertices = 4, .index_count = 6};
		glDepthMask(GL_FALSE);
		draw_mesh(&quad);
		glDepthMask(GL_TRUE);
	}
	bind_frame(stage, frame, target, 0);
	if (composite || program->profile == FX_SCENE_PAIR) {
		// Final window stages need the obstacle's owner geometry, not whichever
		// companion happened to draw last. Clear it when no target is present.
		struct fx_scene_item target_item = {0};
		if (composite && program->profile == FX_SCENE_WINDOWS && frame->target_token != 0) {
			for (unsigned i = 0; i < count; i++) {
				if (draws[i].item.token == frame->target_token && draws[i].item.kind == FX_SCENE_CONTENT) {
					target_item = draws[i].item;
					break;
				}
			}
		}
		bind_item(stage, &target_item);
		static float uv[] = {0, 0, 1, 0, 0, 1, 1, 1};
		static uint16_t indices[] = {0, 1, 2, 2, 1, 3};
		const struct fx_scene_mesh quad = {.uv = uv, .indices = indices, .vertices = 4, .index_count = 6};
		bind_input(stage, &pair[0], 0);
		if (!composite) {
			bind_input(stage, &pair[1], 1);
		}
		draw_mesh(&quad);
	} else {
		if (program->profile == FX_SCENE_SET) {
			glEnable(GL_DEPTH_TEST);
			glDepthFunc(GL_LESS);
		} else {
			glEnable(GL_BLEND);
		}
		for (unsigned i = 0; i < count; i++) {
			const struct fx_scene_draw *draw = &draws[i];
			unsigned padding = draw->shadow || draw->light ? target->shared->shadow_padding : 0;
			struct wlr_box padded = {-(int)padding, -(int)padding,
				target->buffer->width + padding * 2, target->buffer->height + padding * 2};
			if ((draw->shadow || draw->light) && !fx_render_pass_begin_capture(pass, &padded)) {
				pass->incomplete = true;
				break;
			}
			if (draw->shadow && !fx_render_pass_begin_animation(pass)) {
				fx_render_pass_end_capture(pass, &padded, NULL);
				pass->incomplete = true;
				break;
			}
			// Native kernels change GL bindings and disable their attribute arrays.
			glBindBuffer(GL_ARRAY_BUFFER, 0);
			glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
			glEnableVertexAttribArray(0);
			glDisable(GL_SCISSOR_TEST);
			bind_frame(stage, frame, target, padding);
			bind_input(stage, &draw->input, 0);
			bind_item(stage, &draw->item);
			if (program->profile == FX_SCENE_WINDOWS) glEnable(GL_BLEND);
			if (draw->emission && !draw->light) {
				glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_COLOR, GL_ZERO, GL_ONE);
			} else {
				glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
			}
			draw_mesh(draw->mesh);
			if (draw->light) {
				struct fx_effect_light_cache *cache = target_light(target, draw->light);
				struct wlr_texture *texture = fx_texture_from_buffer(&program->renderer->wlr_renderer, pass->buffer->buffer);
				bool light_ok = texture && fx_render_pass_emit_scene_light(pass, cache, texture, draw->light, frame->scale);
				if (texture) wlr_texture_destroy(texture);
				pixman_region32_t empty;
				pixman_region32_init(&empty);
				fx_render_pass_end_capture(pass, &padded, &empty);
				pixman_region32_fini(&empty);
				if (!light_ok) {
					pass->incomplete = true;
					break;
				}
				fx_render_pass_add_effect_light(pass, cache, draw->light, &padded, NULL);
			}
			if (draw->shadow) {
				const struct fx_scene_shadow *shadow = draw->shadow;
				float color[4];
				memcpy(color, shadow->color, sizeof(color));
				color[3] *= 1 - shadow->native_mix;
				bool shadow_ok = fx_render_pass_end_animation_shadow(pass, shadow->softness,
					shadow->offset[0], shadow->offset[1], color, NULL);
				if (shadow_ok && shadow->native_mix > 0) {
					glBlendFunc(GL_ONE, GL_ONE);
					fx_render_pass_add_texture(pass, &(struct fx_render_texture_options){.base = {
						.texture = shadow->native_texture,
						.dst_box = {shadow->native_box[0] + padding, shadow->native_box[1] + padding,
							shadow->native_box[2], shadow->native_box[3]},
						.alpha = &shadow->native_mix,
						.filter_mode = WLR_SCALE_FILTER_NEAREST,
						.blend_mode = WLR_RENDER_BLEND_MODE_PREMULTIPLIED,
						.transfer_function = pass->working_space ? WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR : 0,
					}});
					glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
				}
				fx_render_pass_end_capture(pass, &padded, NULL);
				if (!shadow_ok) {
					pass->incomplete = true;
					break;
				}
			}
		}
	}
	bool ok = !pass->incomplete && glGetError() == GL_NO_ERROR;
	// Restore the renderer's ordinary-pass invariants before submission and
	// any output conversion. There is no renderer state cache to invalidate.
	glDisableVertexAttribArray(0);
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_CULL_FACE);
	glDepthMask(GL_TRUE);
	glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, 0);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, 0);
	glUseProgram(0);
	return wlr_render_pass_submit(wlr_pass) && ok;
}

bool fx_scene_program_render(struct fx_scene_program *program,
		struct fx_scene_target *target, struct fx_scene_target *composed,
		const struct fx_scene_frame *frame, const struct fx_scene_input pair[2],
		const struct fx_scene_draw *draws, unsigned draw_count) {
	if (!program || !program->renderer || !target || target->renderer != program->renderer || !frame ||
			!isfinite(frame->output_size[0]) || !isfinite(frame->output_size[1]) ||
			frame->output_size[0] <= 0 || frame->output_size[1] <= 0 ||
			frame->output_transform > WL_OUTPUT_TRANSFORM_FLIPPED_270 ||
			frame->scene_count < 0 || frame->scene_count > (int)FX_SCENE_MAX_FACES ||
			frame->framing < 0 || frame->framing > 1 ||
			frame->target_token < 0 || frame->target_token > (int)FX_SCENE_MAX_PARTICIPANTS ||
			frame->role < 0 || frame->role > 1 || frame->palette_count < 0 || frame->palette_count > 4) {
		return false;
	}
	bool final = program->composite.program != 0;
	if (final && (!composed || composed->renderer != program->renderer || composed->buffer == target->buffer ||
			composed->buffer->width != target->buffer->width || composed->buffer->height != target->buffer->height)) {
		return false;
	}
	struct fx_scene_target *first = final ? composed : target;
	if (program->profile == FX_SCENE_PAIR) {
		if (draw_count || !pair || !input_valid(&pair[0], first) || !input_valid(&pair[1], first)) {
			return false;
		}
	} else {
		unsigned max_draws = program->profile == FX_SCENE_SET ? FX_SCENE_MAX_FACES : FX_SCENE_MAX_DRAWS;
		if (!draws || !draw_count || draw_count > max_draws || (program->profile == FX_SCENE_SET && !first->depth)) {
			return false;
		}
		if (program->profile == FX_SCENE_SET && draw_count != (unsigned)frame->scene_count) {
			return false;
		}
		uint64_t vertices = 0;
		for (unsigned i = 0; i < draw_count; i++) {
			const struct fx_scene_mesh *mesh = draws[i].mesh;
			if (draws[i].item.kind < FX_SCENE_FACE || draws[i].item.kind > FX_SCENE_STATIC ||
					draws[i].item.token < 0 || draws[i].item.token > (int)(program->profile == FX_SCENE_SET
						? FX_SCENE_MAX_FACES : FX_SCENE_MAX_PARTICIPANTS) ||
					draws[i].item.ordinal < 0 || draws[i].item.ordinal >= (int)max_draws) {
				return false;
			}
			if (!mesh || !mesh->uv || !mesh->indices || mesh->vertices < 4 || !mesh->index_count ||
					mesh->vertices > (FX_SCENE_MAX_GRID + 1) * (FX_SCENE_MAX_GRID + 1) ||
					mesh->index_count > FX_SCENE_MAX_GRID * FX_SCENE_MAX_GRID * 6 ||
					!input_valid(&draws[i].input, first) || !input_valid(&draws[i].input, target) ||
					(program->profile == FX_SCENE_SET && draws[i].emission)) {
				return false;
			}
			if (draws[i].light && (program->profile != FX_SCENE_WINDOWS ||
					draws[i].item.kind != FX_SCENE_EMISSION || draws[i].shadow ||
					frame->scale != first->shared->light_scale || !target_light(first, draws[i].light))) {
				return false;
			}
			const struct fx_scene_shadow *shadow = draws[i].shadow;
			if (shadow) {
				if (program->profile != FX_SCENE_WINDOWS || draws[i].item.kind != FX_SCENE_SHADOW ||
						!first->shared->shadow_prepared || draws[i].emission || !isfinite(shadow->softness) ||
						shadow->softness < 0 || !isfinite(shadow->offset[0]) || !isfinite(shadow->offset[1]) ||
						ceilf(shadow->softness + fmaxf(fabsf(shadow->offset[0]), fabsf(shadow->offset[1]))) > first->shared->shadow_padding) {
					return false;
				}
				if (!isfinite(shadow->native_mix) || shadow->native_mix < 0 || shadow->native_mix > 1 ||
						(shadow->native_mix > 0 && (shadow->native_box[2] <= 0 || shadow->native_box[3] <= 0 ||
							!input_valid(&(struct fx_scene_input){.texture = shadow->native_texture}, first) ||
							!input_valid(&(struct fx_scene_input){.texture = shadow->native_texture}, target)))) {
					return false;
				}
				for (unsigned c = 0; c < 4; c++) {
					if (!isfinite(shadow->color[c]) || shadow->color[c] < 0 || shadow->color[c] > 1) {
						return false;
					}
				}
			}
			vertices += mesh->vertices;
		}
		if (vertices > FX_SCENE_MAX_VERTICES) {
			return false;
		}
	}
	if (!render_stage(program, &program->main, first, frame, pair, draws, draw_count, false)) {
		return false;
	}
	if (!final) {
		return true;
	}
	struct wlr_texture *texture = wlr_texture_from_buffer(&program->renderer->wlr_renderer, composed->buffer);
	float matrix[9];
	transform_uv_matrix(wlr_output_transform_invert(frame->output_transform), matrix);
	struct fx_scene_input input[2] = {{.texture = texture, .sample_matrix = matrix}};
	bool ok = texture && input_valid(input, target) &&
		render_stage(program, &program->composite, target, frame, input, draws, draw_count, true);
	if (texture) {
		wlr_texture_destroy(texture);
	}
	return ok;
}

void fx_scene_program_unref(struct fx_scene_program *program) {
	if (!program || --program->references) {
		return;
	}
	if (program->renderer) {
		struct wlr_egl_context previous;
		if (wlr_egl_make_current(program->renderer->egl, &previous)) {
			glDeleteProgram(program->main.program);
			glDeleteProgram(program->composite.program);
			glDeleteProgram(program->pick.program);
			glDeleteProgram(program->backdrop.program);
			wlr_egl_restore_context(&previous);
		}
		wl_list_remove(&program->destroy.link);
	}
	free(program);
}

struct fx_scene_program *fx_scene_program_create(struct wlr_renderer *wlr_renderer,
		enum fx_scene_profile profile, const struct fx_scene_sources *sources,
		const struct fx_scene_parameter *parameters, unsigned parameter_count) {
	if (!wlr_renderer || !wlr_renderer_is_fx(wlr_renderer) || !sources || !sources->fragment ||
			profile < FX_SCENE_PAIR || profile > FX_SCENE_WINDOWS || parameter_count > FX_SCENE_PARAMETERS ||
			(parameter_count && !parameters) || (profile == FX_SCENE_PAIR
				? sources->vertex || sources->composite || sources->backdrop : !sources->vertex)) {
		return NULL;
	}
	size_t source_size = strlen(sources->fragment);
	const char *extra[] = {sources->common, sources->vertex, sources->composite, sources->backdrop};
	for (unsigned i = 0; i < 4; i++) {
		if (extra[i]) {
			source_size += strlen(extra[i]);
		}
	}
	if (source_size > 512u * 1024) {
		return NULL;
	}
	for (unsigned i = 0; i < parameter_count; i++) {
		const struct fx_scene_parameter *p = &parameters[i];
		if (!memchr(p->name, 0, sizeof(p->name)) || !p->name[0] ||
				strncmp(p->name, "umbriel_", 8) == 0 || strncmp(p->name, "_fx_", 4) == 0 ||
				strncmp(p->name, "gl_", 3) == 0 || strstr(p->name, "__") != NULL || strcmp(p->name, "main") == 0 ||
				strcmp(p->name, "transition") == 0 || strcmp(p->name, "transition_vertex") == 0 ||
				strcmp(p->name, "transition_fragment") == 0 || strcmp(p->name, "transition_composite") == 0 ||
				strcmp(p->name, "transition_backdrop") == 0 ||
				p->components < 1 || p->components > 4) {
			return NULL;
		}
		for (unsigned j = 0; p->name[j]; j++) {
			unsigned char c = p->name[j];
			if (!(c == '_' || isalpha(c) || (j > 0 && isdigit(c)))) {
				return NULL;
			}
		}
		for (unsigned j = 0; j < p->components; j++) {
			if (!isfinite(p->value[j])) {
				return NULL;
			}
		}
		for (unsigned j = 0; j < i; j++) {
			if (strcmp(p->name, parameters[j].name) == 0) {
				return NULL;
			}
		}
	}
	struct fx_renderer *renderer = fx_get_renderer(wlr_renderer);
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(renderer->egl, &previous)) {
		return NULL;
	}
	GLint vertex_limit, fragment_limit, texture_units;
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vertex_limit);
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fragment_limit);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &texture_units);
	struct fx_scene_program *program = NULL;
	// Reserve every declared built-in, including palette/audio, independently
	// for each stage. Do not rely on optimizer-specific uniform elimination.
	if (vertex_limit < (int)FX_SCENE_VERTEX_VECTORS + (int)parameter_count
			|| fragment_limit < (int)FX_SCENE_FRAGMENT_VECTORS + (int)parameter_count || texture_units < 2) {
		goto out;
	}
	program = calloc(1, sizeof(*program));
	if (!program) {
		goto out;
	}
	program->renderer = renderer;
	program->references = 1;
	program->profile = profile;
	program->destroy.notify = program_renderer_destroy;
	wl_signal_add(&wlr_renderer->events.destroy, &program->destroy);
	const char *helpers = profile == FX_SCENE_PAIR
		? "vec4 umbriel_sample_from(vec2 uv){return _fx_sample0(uv);}\nvec4 umbriel_sample_to(vec2 uv){return _fx_sample1(uv);}\n"
		: "vec4 umbriel_sample_item(vec2 uv){return _fx_sample0(uv);}\n";
	const char *suffix = profile == FX_SCENE_PAIR
		? "void main(){gl_FragColor=transition(_fx_output_uv());}\n"
		: profile == FX_SCENE_SET
			? "void main(){vec4 c=transition_fragment(_fx_item_uv,_fx_output_uv());gl_FragColor=vec4(c.rgb,1.0);}\n"
			: "void main(){gl_FragColor=transition_fragment(_fx_item_uv,_fx_output_uv());}\n";
	bool ok = stage_create(&program->main, sources->common,
		profile == FX_SCENE_PAIR ? default_vertex : sources->vertex,
		sources->fragment, suffix, helpers, parameters, parameter_count);
	if (ok && sources->composite) {
		ok = stage_create(&program->composite, sources->common, default_vertex, sources->composite,
			"void main(){gl_FragColor=transition_composite(_fx_output_uv());}\n",
			"vec4 umbriel_sample_composed(vec2 uv){return _fx_sample0(uv);}\n", parameters, parameter_count);
	}
	if (ok && sources->backdrop) {
		ok = stage_create(&program->backdrop, sources->common, default_vertex, sources->backdrop,
			"void main(){gl_FragColor=transition_backdrop(_fx_output_uv());}\n",
			"", parameters, parameter_count);
	}
	if (ok && profile == FX_SCENE_SET && !sources->composite) {
		ok = stage_create(&program->pick, sources->common, sources->vertex, sources->fragment,
			"void main(){vec4 c=transition_fragment(_fx_item_uv,_fx_output_uv());"
			"bool valid=_fx_pick_samples==1.0&&all(greaterThanEqual(_fx_pick_uv,vec2(0.0)))"
			"&&all(lessThanEqual(_fx_pick_uv,vec2(1.0)));"
			"if(_fx_pick_pass==0){gl_FragColor=valid?vec4(float(umbriel_item_token)/255.0,"
			"float(umbriel_item_ordinal)/255.0,0.0,1.0):vec4(0.0,0.0,1.0,1.0);}"
			"else{vec2 q=floor(clamp(_fx_pick_uv,0.0,1.0)*65535.0+0.5);"
			"gl_FragColor=vec4(floor(q.x/256.0),mod(q.x,256.0),floor(q.y/256.0),mod(q.y,256.0))/255.0;}}\n",
			"#define gl_FragCoord vec4(gl_FragCoord.xy+_fx_pick_offset,gl_FragCoord.zw)\n"
			"float _fx_pick_samples=0.0;vec2 _fx_pick_uv=vec2(0.0);"
			"vec4 umbriel_sample_item(vec2 uv){_fx_pick_samples+=1.0;_fx_pick_uv=uv;return _fx_sample0(uv);}\n",
			parameters, parameter_count);
	}
	if (!ok) {
		fx_scene_program_unref(program);
		program = NULL;
	}
out:
	glUseProgram(0);
	wlr_egl_restore_context(&previous);
	return program;
}

bool fx_scene_program_get_limits(struct wlr_renderer *wlr_renderer, struct fx_scene_limits *limits) {
	if (!limits) {
		return false;
	}
	*limits = (struct fx_scene_limits){0};
	if (!wlr_renderer || !wlr_renderer_is_fx(wlr_renderer)) {
		return false;
	}
	struct wlr_egl_context previous;
	if (!wlr_egl_make_current(fx_get_renderer(wlr_renderer)->egl, &previous)) {
		return false;
	}
	GLint texture, vertex, fragment, units;
	glGetIntegerv(GL_MAX_TEXTURE_SIZE, &texture);
	glGetIntegerv(GL_MAX_VERTEX_UNIFORM_VECTORS, &vertex);
	glGetIntegerv(GL_MAX_FRAGMENT_UNIFORM_VECTORS, &fragment);
	glGetIntegerv(GL_MAX_TEXTURE_IMAGE_UNITS, &units);
	bool ok = glGetError() == GL_NO_ERROR && texture > 0 && vertex > 0 && fragment > 0 && units > 0;
	if (ok) {
		*limits = (struct fx_scene_limits){texture, vertex, fragment, units};
	}
	wlr_egl_restore_context(&previous);
	return ok;
}

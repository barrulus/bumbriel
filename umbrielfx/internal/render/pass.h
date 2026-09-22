#ifndef FX_RENDER_PASS_H
#define FX_RENDER_PASS_H

#include <umbrielfx/render/pass.h>
#include <stdbool.h>
#include <wlr/render/pass.h>
#include <wlr/util/box.h>
#include <wlr/render/interface.h>

void fx_render_box(const struct wlr_box *box, const pixman_region32_t *clip, GLint attrib);
void fx_set_proj_matrix(GLint loc, const float proj[9], const struct wlr_box *box);
void fx_make_tex_matrix(float matrix[9], enum wl_output_transform transform, const struct wlr_fbox *box);
void fx_set_tex_matrix(GLint loc, enum wl_output_transform transform, const struct wlr_fbox *box);
bool fx_render_target_init(GLuint *texture, GLuint *framebuffer, int width, int height, GLenum type);

struct fx_render_texture_options fx_render_texture_options_default(
		const struct wlr_render_texture_options *base);

struct fx_render_rect_options fx_render_rect_options_default(
		const struct wlr_render_rect_options *base);

struct fx_gles_render_pass *fx_begin_buffer_pass(struct fx_framebuffer *buffer,
	struct wlr_egl_context *prev_ctx, struct fx_render_timer *timer,
	struct wlr_drm_syncobj_timeline *signal_timeline, uint64_t signal_point,
	struct wlr_color_transform *color_transform,
	struct fx_offscreen_buffers *output_buffers);

#endif

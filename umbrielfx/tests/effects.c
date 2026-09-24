#include "render/fx_renderer/animation_history.h"
#include "render_fixture.h"
#include "render/fx_renderer/decoration.h"
#include "render/fx_renderer/shaders.h"
#include "render/fx_renderer/params.h"

#include <math.h>
#include <string.h>
#include <umbrielfx/render/animation.h>
#include <umbrielfx/render/decoration.h>
#include <umbrielfx/render/fx_renderer/fx_offscreen_buffers.h>
#include <umbrielfx/render/pass.h>
#include <umbrielfx/render/postprocess.h>
#include <wlr/render/egl.h>

static bool check_parameter_isolation(struct fixture* fixture) {
  const char* source = "uniform float gain; vec4 animation(vec2 p) { return umbriel_sample(p) * gain; }";
  struct fx_animation_shader* configured = fx_animation_shader_create(fixture->renderer, source, "configured");
  struct fx_animation_shader* defaults = fx_animation_shader_create(fixture->renderer, source, "defaults");
  const struct fx_shader_param gain = {.name = "gain", .type = FX_PARAM_FLOAT, .length = 1, .numbers = {0.75f}};
  bool ok = configured && defaults && configured->program == defaults->program
      && fx_animation_shader_set_params(configured, &gain, 1)
      && fx_animation_shader_set_params(defaults, NULL, 0);
  struct wlr_egl_context previous;
  if (ok && wlr_egl_make_current(fx_get_renderer(fixture->renderer)->egl, &previous)) {
    const GLint location = glGetUniformLocation(configured->program, "gain");
    float value = -1;
    glUseProgram(configured->program);
    fx_uniform_values_bind(configured->params);
    glGetUniformfv(configured->program, location, &value);
    ok &= check(value == 0.75f, "configured uniform belongs to its effect instance");
    fx_uniform_values_bind(defaults->params);
    glGetUniformfv(defaults->program, location, &value);
    ok &= check(value == 0, "omitted uniform cannot inherit another instance's value");
    wlr_egl_restore_context(&previous);
  } else {
    ok = false;
  }
  fx_animation_shader_unref(configured);
  fx_animation_shader_unref(defaults);
  return ok;
}

static bool check_params(struct fixture* fixture) {
  const char* body =
      "uniform float gain; uniform int count; uniform bool enabled; uniform vec3 color; "
      "uniform ivec2 offset; vec4 postprocess(vec3 p) { return enabled ? vec4(color * gain * float(count) "
      "+ vec3(float(offset.x + offset.y)), 1.0) : tex2D_screen(p.xy); }";
  struct fx_shader_param params[] = {
      {.name = "gain", .type = FX_PARAM_INT, .length = 1, .integers = {1}},
      {.name = "count", .type = FX_PARAM_INT, .length = 1, .integers = {2}},
      {.name = "enabled", .type = FX_PARAM_BOOL, .length = 1, .integers = {1}},
      {.name = "color", .type = FX_PARAM_FLOAT, .length = 3, .numbers = {0.2f, 0.3f, 0.4f}},
      {.name = "offset", .type = FX_PARAM_INT, .length = 2, .integers = {0, 0}},
  };
  struct fx_postprocess_source source = {.code = body, .label = "params", .params = params, .param_count = 5};
  struct fx_postprocess_chain* chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  bool ok = check(chain != NULL, "valid typed uniforms compile");
  fx_postprocess_chain_unref(chain);
  params[1].type = FX_PARAM_FLOAT;
  chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  ok &= check(chain == NULL, "integer uniforms reject floats");
  fx_postprocess_chain_unref(chain);
  params[1].type = FX_PARAM_INT;
  params[3].length = 2;
  chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  ok &= check(chain == NULL, "uniform vector dimensions must match");
  fx_postprocess_chain_unref(chain);
  params[3].length = 3;
  params[4].name = "missing";
  chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  ok &= check(chain == NULL, "unknown uniforms are rejected");
  fx_postprocess_chain_unref(chain);
  struct fx_animation_shader* animation = fx_animation_shader_create(
      fixture->renderer, "uniform float gain; vec4 animation(vec2 p) { return umbriel_sample(p) * gain; }",
      "animation params"
  );
  ok &= check(animation != NULL && fx_animation_shader_set_params(animation, params, 1), "animation params bind");
  fx_animation_shader_unref(animation);
  struct fx_decoration_shader* decoration = fx_decoration_shader_create(
      fixture->renderer, "uniform float gain; vec4 ring_color(vec2 p) { return vec4(gain); }", "decoration params"
  );
  ok &= check(decoration != NULL && fx_decoration_shader_set_params(decoration, params, 1), "decoration params bind");
  fx_decoration_shader_unref(decoration);
  return ok;
}

static bool check_pipeline(struct fixture* fixture, size_t count) {
  struct fx_animation_shader* shader = fx_animation_shader_create(
      fixture->renderer, "vec4 animation(vec2 p) { vec4 c = umbriel_sample(p); return vec4(c.rgb * 0.8, c.a); }",
      "pipeline"
  );
  if (shader == NULL)
    return false;
  struct fx_animation_shader* shared = fx_animation_shader_create(
      fixture->renderer, "vec4 animation(vec2 p) { vec4 c = umbriel_sample(p); return vec4(c.rgb * 0.8, c.a); }",
      "shared pipeline"
  );
  if (!check(shared != NULL && shared->program == shader->program, "identical source shares its linked program")) {
    fx_animation_shader_unref(shared);
    fx_animation_shader_unref(shader);
    return false;
  }
  fx_animation_shader_unref(shader);
  shader = shared;
  struct fx_animation_shader* shaders[FX_ANIMATION_MAX_PASSES];
  struct fx_animation_history histories[FX_ANIMATION_MAX_PASSES];
  for (size_t i = 0; i < count; ++i) {
    shaders[i] = shader;
    fx_animation_history_init(&histories[i]);
  }
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, TEST_WIDTH, TEST_HEIGHT);
  if (buffer == NULL) {
    fx_animation_shader_unref(shader);
    return false;
  }
  struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
  bool ok = base != NULL;
  if (base != NULL) {
    struct fx_gles_render_pass* pass = fx_get_render_pass(base);
    ok = fx_render_pass_init_offscreen_buffers(base, fixture->output) && fx_render_pass_begin_animation(pass);
    const struct wlr_box box = {0, 0, TEST_WIDTH, TEST_HEIGHT};
    wlr_render_pass_add_rect(
        base,
        &(struct wlr_render_rect_options){
            .box = box,
            .color = {1, 0, 0, 1},
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
        }
    );
    if (ok)
      fx_render_pass_end_animation_pipeline(
          pass, shaders, count,
          &(struct fx_animation_parameters){.transition_id = 1, .progress = 0.5f, .linear_progress = 0.5f}, &box, &box,
          WL_OUTPUT_TRANSFORM_NORMAL, NULL, NULL, histories, fixture->output, true
      );
    ok = wlr_render_pass_submit(base) && ok;
    uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];
    ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, TEST_WIDTH * 4, pixels) && ok;
    if (ok) {
      const int red = pixels[TEST_WIDTH * TEST_HEIGHT / 2] & 255;
      ok = check(abs(red - (int)lroundf(255 * powf(0.8f, count))) <= 3, "each event pass samples the preceding result");
    }
  }
  for (size_t i = 0; i < count; ++i)
    fx_animation_history_finish(&histories[i]);
  wlr_buffer_drop(buffer);
  fx_animation_shader_unref(shader);
  return ok;
}

static bool check_outer_pipeline(struct fixture* fixture, bool illumination) {
  struct fx_decoration_shader* shader = fx_decoration_shader_create(fixture->renderer,
      "vec4 ring_color(vec2 p) { return vec4(1.0, 0.0, 0.0, 1.0); }", "outer source");
  const struct fx_postprocess_source source = {
      .code = "vec4 postprocess(vec3 p) { return vec4(0.0, 1.0, 0.0, 1.0); }", .label = "opaque outer pass"};
  struct fx_postprocess_chain* chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  struct fx_decoration_pipeline* pipeline = NULL;
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, TEST_WIDTH, TEST_HEIGHT);
  bool ok = shader != NULL && chain != NULL && buffer != NULL;
  if (ok) {
    struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
    ok = base != NULL;
    if (base) {
      ok = fx_render_pass_init_offscreen_buffers(base, fixture->output) && ok;
      wlr_render_pass_add_rect(base, &(struct wlr_render_rect_options){
          .box = {0, 0, TEST_WIDTH, TEST_HEIGHT}, .color = {0, 0, 1, 1}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE});
      const struct fx_render_border_options ring = {
          .shader = shader, .postprocess = chain, .pipeline = &pipeline,
          .shader_scale = 1, .shader_coordinate_scale = 1,
          .logical_width = 12, .logical_height = 12, .logical_hole = {2, 2, 8, 8},
          .logical_corners = {2, 2, 2, 2}, .box = {2, 2, 12, 12},
          .clipped_region = {.area = {4, 4, 8, 8}, .corners = {2, 2, 2, 2}},
          .inner_width = 2, .inner_color = {1, 0, 0, 1}, .outer_color = {1, 0, 0, 1},
      };
      struct fx_decoration_light* light = NULL;
      if (illumination)
        ok = fx_render_pass_add_decoration_light(fx_get_render_pass(base), &light, &ring,
            &(struct fx_decoration_light_parameters){.enabled = true, .spread = 2, .intensity = 1, .threshold = 0},
            &(struct wlr_box){-10, -10, 36, 36}, NULL) && ok;
      else
        fx_render_pass_add_border(fx_get_render_pass(base), &ring);
      ok = wlr_render_pass_submit(base) && ok;
      uint32_t pixels[TEST_WIDTH * TEST_HEIGHT];
      ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, TEST_WIDTH * 4, pixels) && ok;
      if (ok && illumination) {
        unsigned red = 0, green = 0;
        for (size_t i = 0; i < TEST_WIDTH * TEST_HEIGHT; ++i) {
          red += pixels[i] & 255;
          green += (pixels[i] >> 8) & 255;
        }
        ok &= check(red == 0 && green > 100, "border light emits the final processed color");
      } else if (ok) {
        ok &= check((pixels[8 * TEST_WIDTH + 3] & 0xffffff) == 0x00ff00, "outer passes process the border raster");
        ok &= check((pixels[8 * TEST_WIDTH + 8] & 0xffffff) == 0xff0000, "opaque outer passes preserve the client hole");
        ok &= check((pixels[0] & 0xffffff) == 0xff0000, "outer passes stay within their padded raster");
      }
      fx_decoration_light_destroy(light);
    }
  }
  fx_decoration_pipeline_destroy(pipeline);
  fx_decoration_shader_unref(shader);
  fx_postprocess_chain_unref(chain);
  if (buffer)
    wlr_buffer_drop(buffer);
  return ok;
}

int main(void) {
  struct fixture fixture;
  if (!fixture_init(&fixture)) {
    fixture_finish(&fixture);
    return 77;
  }
  bool ok = check_params(&fixture);
  ok = check_parameter_isolation(&fixture) && ok;
  ok = check_outer_pipeline(&fixture, false) && ok;
  ok = check_outer_pipeline(&fixture, true) && ok;
  for (size_t count = 1; count <= FX_ANIMATION_MAX_PASSES; ++count)
    ok = check_pipeline(&fixture, count) && ok;
  fixture_finish(&fixture);
  return ok ? 0 : 1;
}

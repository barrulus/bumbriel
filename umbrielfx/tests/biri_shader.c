// Pixel tests for the editable Biri collection, through the production renderer.
#include "render/fx_renderer/decoration.h"
#include "render_fixture.h"

#include <assert.h>
#include <drm_fourcc.h>
#include <fcntl.h>
#include <math.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <umbrielfx/render/animation.h>
#include <umbrielfx/render/fx_renderer/fx_offscreen_buffers.h>
#include <umbrielfx/render/fx_renderer/fx_renderer.h>
#include <umbrielfx/render/pass.h>
#include <umbrielfx/types/wlr_scene.h>
#include <unistd.h>
#include <wayland-server-core.h>
#include <wlr/backend/headless.h>
#include <wlr/render/allocator.h>
#include <wlr/render/interface.h>
#include <wlr/render/pass.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/render/wlr_texture.h>
#include <wlr/types/wlr_buffer.h>
#include <wlr/types/wlr_output.h>
#include <xf86drm.h>

#define SIZE 128

static char* read_source(const char* directory, const char* name) {
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s", directory, name);
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    perror(path);
    return NULL;
  }
  char* text = calloc(256 * 1024 + 1, 1);
  if (text != NULL) {
    size_t length = fread(text, 1, 256 * 1024, file);
    if (ferror(file) || length == 256 * 1024) {
      free(text);
      text = NULL;
    }
  }
  fclose(file);
  return text;
}

static bool write_frame(const char* name, int frame, const uint32_t* pixels) {
  const char* directory = getenv("BIRI_SHADER_FRAMES");
  if (directory == NULL)
    return true;
  char path[4096];
  snprintf(path, sizeof(path), "%s/%s-%d.ppm", directory, name, frame);
  FILE* file = fopen(path, "wb");
  if (file == NULL)
    return false;
  fprintf(file, "P6\n%d %d\n255\n", SIZE, SIZE);
  for (int y = 0; y < SIZE; y++) {
    for (int x = 0; x < SIZE; x++) {
      uint32_t pixel = pixels[y * SIZE + x];
      unsigned alpha = pixel >> 24;
      unsigned background = ((x / 8 + y / 8) % 2) ? 32 : 64;
      for (unsigned shift = 0; shift < 24; shift += 8) {
        unsigned value = ((pixel >> shift) & 255) + background * (255 - alpha) / 255;
        fputc(value > 255 ? 255 : value, file);
      }
    }
  }
  bool ok = !ferror(file);
  return fclose(file) == 0 && ok;
}

static bool render_shader(
    struct fixture* fixture, struct fx_animation_shader* shader, float progress, float alpha,
    uint32_t pixels[SIZE * SIZE]
) {
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, SIZE, SIZE);
  if (!check(buffer != NULL, "allocate shader output")) {
    return false;
  }
  struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
  if (!check(base != NULL, "begin shader render")) {
    wlr_buffer_drop(buffer);
    return false;
  }
  struct fx_gles_render_pass* pass = fx_get_render_pass(base);
  const struct wlr_box box = {.width = SIZE, .height = SIZE};
  wlr_render_pass_add_rect(
      base,
      &(struct wlr_render_rect_options){
          .box = box,
          .color = {0},
          .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
      }
  );
  bool ok = fx_render_pass_init_offscreen_buffers(base, fixture->output) && fx_render_pass_begin_animation(pass);
  if (ok) {
    // Asymmetric coloured tiles expose displacement, rotation and lost source sampling.
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        wlr_render_pass_add_rect(
            base,
            &(struct wlr_render_rect_options){
                .box = {x * 16, y * 16, 16, 16},
                .color = {alpha * (x + 1) / 8, alpha * (y + 1) / 8, alpha * 0.25f, alpha},
                .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
            }
        );
      }
    }
    const struct fx_animation_parameters parameters = {
        .progress = progress,
        .linear_progress = progress,
        .direction = 1,
        .random_seed = {0.375f, 0.25f, 0.5f, 0.75f},
    };
    fx_render_pass_end_animation(pass, shader, &parameters, &box, &box, WL_OUTPUT_TRANSFORM_NORMAL, NULL);
  }
  ok = wlr_render_pass_submit(base) && ok;
  if (ok) {
    ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, SIZE * 4, pixels);
  }
  wlr_buffer_drop(buffer);
  return check(ok, "render/read shader pixels");
}

static bool render_decoration(
    struct fixture* fixture, struct fx_decoration_shader* shader, float time, uint32_t pixels[SIZE * SIZE]
) {
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, SIZE, SIZE);
  if (buffer == NULL)
    return false;
  struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
  if (base == NULL) {
    wlr_buffer_drop(buffer);
    return false;
  }
  const struct wlr_box box = {.width = SIZE, .height = SIZE};
  wlr_render_pass_add_rect(
      base,
      &(struct wlr_render_rect_options){
          .box = box,
          .color = {0},
          .blend_mode = WLR_RENDER_BLEND_MODE_NONE,
      }
  );
  fx_render_pass_add_border(
      fx_get_render_pass(base),
      &(struct fx_render_border_options){
          .box = box,
          .shader = shader,
          .shader_time = time,
          .shader_padding = 24,
          .shader_scale = 1,
          .logical_width = SIZE,
          .logical_height = SIZE,
          .logical_hole = {32, 32, 64, 64},
          .logical_corners = {12, 12, 12, 12},
          .clipped_region = {.area = {32, 32, 64, 64}, .corners = {12, 12, 12, 12}},
          .inner_width = 6,
          .inner_color = {.r = 1, .g = 1, .b = 1, .a = 1},
      }
  );
  bool ok = wlr_render_pass_submit(base) && read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, SIZE * 4, pixels);
  wlr_buffer_drop(buffer);
  return ok;
}

static size_t different_pixels(const uint32_t* a, const uint32_t* b) {
  size_t different = 0;
  for (size_t i = 0; i < SIZE * SIZE; i++) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
      int delta = (int)((a[i] >> shift) & 255) - (int)((b[i] >> shift) & 255);
      if (abs(delta) > 3) {
        different++;
        break;
      }
    }
  }
  return different;
}

static bool test_decoration_visibility(struct fixture* fixture) {
  struct wlr_scene* scene = wlr_scene_create();
  if (!check(scene != NULL, "create decoration scene"))
    return false;
  struct wlr_scene_output* output = wlr_scene_output_create(scene, fixture->output);
  if (output == NULL) {
    wlr_scene_node_destroy(&scene->tree.node);
    return false;
  }
  float color[4] = {1, 1, 1, 1};
  struct wlr_scene_border* border = wlr_scene_border_create(&scene->tree, color, color);
  if (border == NULL) {
    wlr_scene_node_destroy(&scene->tree.node);
    return false;
  }
  wlr_scene_border_set_geometry(
      border, 16, 16, 4, 0, (struct clipped_region){.area = {4, 4, 8, 8}}, (struct fx_corner_radii){0},
      (struct fx_corner_radii){0}
  );
  struct fx_decoration_shader* animated = fx_decoration_shader_create(
      fixture->renderer, "vec4 ring_color(vec2 p) { return vec4(0.5 + 0.5*sin(umbriel_time)); }", "visible-clock"
  );
  struct fx_decoration_shader* still = fx_decoration_shader_create(
      fixture->renderer,
      "// umbriel_time in a comment is not a capability\nvec4 ring_color(vec2 p) { return vec4(1); }", "static-clock"
  );
  bool ok = animated != NULL && still != NULL;
  const struct fx_decoration_parameters parameters = {.speed = 1, .animated = true};
  if (ok) {
    wlr_scene_border_set_shader(border, animated, &parameters);
    ok = check(wlr_scene_output_tick_decoration_shaders(output, 1), "visible ring must request frames") && ok;
    wlr_scene_node_set_enabled(&border->node, false);
    ok = check(!wlr_scene_output_tick_decoration_shaders(output, 2), "hidden ring must idle") && ok;
    wlr_scene_node_set_enabled(&border->node, true);
    wlr_scene_node_set_position(&border->node, 100, 100);
    ok = check(!wlr_scene_output_tick_decoration_shaders(output, 3), "off-output ring must idle") && ok;
    wlr_scene_node_set_position(&border->node, 0, 0);
    struct wlr_scene_rect* cover = wlr_scene_rect_create(&scene->tree, 16, 16, color);
    if (cover != NULL) {
      ok = check(!wlr_scene_output_tick_decoration_shaders(output, 4), "occluded ring must idle") && ok;
      wlr_scene_node_destroy(&cover->node);
    } else
      ok = false;
    wlr_scene_border_set_shader(border, still, &parameters);
    ok = check(!wlr_scene_output_tick_decoration_shaders(output, 5), "comment-only time must idle") && ok;
    wlr_scene_border_set_shader(border, animated, &parameters);
    struct wlr_scene_border* snapshot = wlr_scene_border_create(&scene->tree, color, color);
    if (snapshot != NULL) {
      wlr_scene_border_set_geometry(
          snapshot, 16, 16, 4, 0, (struct clipped_region){.area = {4, 4, 8, 8}}, (struct fx_corner_radii){0},
          (struct fx_corner_radii){0}
      );
      wlr_scene_border_copy_shader(snapshot, border);
      wlr_scene_node_set_enabled(&border->node, false);
      ok = check(!wlr_scene_output_tick_decoration_shaders(output, 6), "frozen close snapshot must idle") && ok;
    } else
      ok = false;
    wlr_scene_border_set_shader(border, NULL, NULL);
  }
  fx_decoration_shader_unref(animated);
  fx_decoration_shader_unref(still);
  wlr_scene_node_destroy(&scene->tree.node);
  return ok;
}

static bool render_light(
    struct fixture* fixture, struct fx_decoration_shader* shader, struct fx_decoration_light** cache, float time,
    float alpha, uint32_t* pixels
) {
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, SIZE, SIZE);
  if (buffer == NULL)
    return false;
  struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
  if (base == NULL) {
    wlr_buffer_drop(buffer);
    return false;
  }
  wlr_render_pass_add_rect(
      base,
      &(struct wlr_render_rect_options){
          .box = {0, 0, SIZE, SIZE}, .color = {0.2, 0.2, 0.2, 1}, .blend_mode = WLR_RENDER_BLEND_MODE_NONE
      }
  );
  const struct fx_render_border_options ring = {
      .shader = shader,
      .shader_time = time,
      .shader_scale = 1,
      .shader_padding = 6,
      .logical_width = 64,
      .logical_height = 64,
      .logical_hole = {12, 12, 40, 40},
      .inner_width = 6,
      .inner_color = {alpha, alpha, alpha, alpha},
  };
  bool ok = fx_render_pass_add_decoration_light(
      fx_get_render_pass(base), cache, &ring,
      &(struct fx_decoration_light_parameters){.enabled = true, .spread = 8, .intensity = 4, .threshold = 0.5},
      &(struct wlr_box){8, 8, 112, 112}, NULL
  );
  ok = wlr_render_pass_submit(base) && ok;
  if (ok)
    ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, SIZE * 4, pixels);
  wlr_buffer_drop(buffer);
  return ok;
}

static bool test_illumination(struct fixture* fixture) {
  struct fx_decoration_shader* shader = fx_decoration_shader_create(
      fixture->renderer,
      "vec4 ring_color(vec2 p) { float d = ring_distance(p);"
      "if (d < 0.0 || d > ring_width) return vec4(0);"
      "return p.x < 20.0 + 15.0 * sin(umbriel_time) ? vec4(1,0,0,1) : vec4(0,0,0.4,1); }",
      "illumination-bright-red-dim-blue"
  );
  if (shader == NULL)
    return false;
  uint32_t first[SIZE * SIZE], second[SIZE * SIZE], dim[SIZE * SIZE], cached[SIZE * SIZE];
  struct fx_decoration_light* cache = NULL;
  bool ok = render_light(fixture, shader, &cache, 0, 1, first)
      && render_light(fixture, shader, &cache, 0, 1, cached)
      && check(different_pixels(first, cached) == 0, "static illumination must remain stable")
      && render_light(fixture, shader, &cache, 1.5, 1, second)
      && check(different_pixels(first, second) > 20, "illumination must follow moving bright details")
      && render_light(fixture, shader, &cache, 0, 0.4, dim);
  size_t inward = 0, outward = 0;
  for (int y = 0; ok && y < SIZE; ++y)
    for (int x = 0; ok && x < SIZE; ++x) {
      const uint32_t pixel = first[y * SIZE + x];
      ok = check(((pixel >> 16) & 255) == 51, "dim blue detail must not emit light")
          && check(dim[y * SIZE + x] == 0xff333333, "emission threshold must use post-opacity colour")
          && check((pixel >> 24) == 255, "screen blending must preserve destination alpha");
      if ((pixel & 255) > 60) {
        if (x > 44 && x < 84 && y > 44 && y < 84)
          inward++;
        if (x < 38 || x > 90 || y < 38 || y > 90)
          outward++;
      }
    }
  ok = check(inward > 10 && outward > 10, "actual ring light must spill both inward and outward") && ok;
  if (ok)
    ok = write_frame("illumination", 0, first) && write_frame("illumination", 1, second);
  fx_decoration_light_destroy(cache);
  fx_decoration_shader_unref(shader);
  return ok;
}

int main(int argc, char** argv) {
  if (argc < 2 || argc > 3) {
    fprintf(stderr, "usage: %s shader-directory [reference-directory]\n", argv[0]);
    return 1;
  }
  struct fixture fixture;
  if (!fixture_init(&fixture)) {
    fixture_finish(&fixture);
    fprintf(stderr, "No usable DRM renderer for shader pixel tests\n");
    return 77;
  }
  const char* names[] = {"whirlpool", "melt", "ripple", "lightning"};
  struct fx_animation_shader* identity = fx_animation_shader_create(
      fixture.renderer, "vec4 animation(vec2 uv) { return umbriel_sample(uv); }", "identity"
  );
  bool ok = check(identity != NULL, "compile identity");
  ok = test_illumination(&fixture) && ok;
  uint32_t baseline[SIZE * SIZE], translucent[SIZE * SIZE], pixels[SIZE * SIZE];
  if (ok) {
    ok = render_shader(&fixture, identity, 0, 1, baseline) && render_shader(&fixture, identity, 0, 0.5f, translucent);
  }
  for (size_t i = 0; ok && i < sizeof(names) / sizeof(names[0]); i++) {
    for (int close = 0; ok && close < 2; close++) {
      char name[128];
      snprintf(name, sizeof(name), "%s-%s.glsl", names[i], close ? "close" : "open");
      char* source = read_source(argv[1], name);
      struct fx_animation_shader* shader =
          source != NULL ? fx_animation_shader_create(fixture.renderer, source, name) : NULL;
      free(source);
      ok = check(shader != NULL, name);
      struct fx_animation_shader* reference = NULL;
      if (argc == 3) {
        char* reference_source = read_source(argv[2], name);
        reference =
            reference_source != NULL ? fx_animation_shader_create(fixture.renderer, reference_source, name) : NULL;
        free(reference_source);
        ok = check(reference != NULL, "compile source reference") && ok;
      }
      for (int frame = 0; ok && frame <= 4; frame++) {
        const float progress = frame / 4.0f;
        ok = render_shader(&fixture, shader, progress, 1, pixels);
        if (!ok) {
          break;
        }
        if (frame == 2) {
          ok = check(different_pixels(pixels, baseline) > SIZE, "intermediate frame must visibly modify the source");
        } else if ((frame == 0 && close) || (frame == 4 && !close)) {
          ok = check(different_pixels(pixels, baseline) == 0, "visible endpoint must equal source");
        } else if (frame == 0 || frame == 4) {
          for (size_t pixel = 0; ok && pixel < SIZE * SIZE; pixel++) {
            ok = check(pixels[pixel] == 0, "hidden endpoint must be transparent black");
          }
        }
        ok = write_frame(name, frame, pixels) && ok;
        printf("%s progress=%.2f modified=%zu\n", name, progress, different_pixels(pixels, baseline));
        if (ok && reference != NULL) {
          uint32_t original[SIZE * SIZE];
          char reference_name[160];
          snprintf(reference_name, sizeof(reference_name), "source-%s", name);
          ok =
              render_shader(&fixture, reference, progress, 1, original) && write_frame(reference_name, frame, original);
          if (ok && frame > 0 && frame < 4 && ((!close && i < 2) || (close && i == 2))) {
            ok = check(
                different_pixels(original, pixels) == 0,
                "unmodified lifecycle interior must match original GLSL pixel-for-pixel"
            );
          }
        }
      }
      if (ok) {
        ok = render_shader(&fixture, shader, close ? 0 : 1, 0.5f, pixels)
            && check(different_pixels(pixels, translucent) == 0, "translucent endpoint must not multiply alpha twice");
      }
      if (ok) {
        ok = render_shader(&fixture, shader, 0.5f, 0.5f, pixels);
        for (size_t pixel = 0; ok && pixel < SIZE * SIZE; pixel++) {
          unsigned alpha = pixels[pixel] >> 24;
          for (unsigned shift = 0; ok && shift < 24; shift += 8) {
            ok = check(((pixels[pixel] >> shift) & 255) <= alpha + 1, "intermediate colour must remain premultiplied");
          }
        }
      }
      fx_animation_shader_unref(reference);
      fx_animation_shader_unref(shader);
    }
  }
  const char* rings[] = {"pulse", "rainbow-ripple", "lightning", "fuse"};
  for (size_t i = 0; ok && i < sizeof(rings) / sizeof(rings[0]); i++) {
    char name[128];
    snprintf(name, sizeof(name), "rings/%s.glsl", rings[i]);
    char* source = read_source(argv[1], name);
    struct fx_decoration_shader* shader =
        source != NULL ? fx_decoration_shader_create(fixture.renderer, source, name) : NULL;
    ok = check(shader != NULL, name);
    if (ok)
      ok = render_decoration(&fixture, shader, 0.1f, baseline) && render_decoration(&fixture, shader, 1.3f, pixels);
    if (ok) {
      ok = check(different_pixels(baseline, pixels) > 20, "ring must animate away from its initial frame");
      size_t colored = 0;
      for (int y = 0; ok && y < SIZE; y++) {
        for (int x = 0; ok && x < SIZE; x++) {
          uint32_t pixel = pixels[y * SIZE + x];
          if (x > 44 && x < 84 && y > 44 && y < 84) {
            ok = check(pixel == 0, "procedural ring must leave the client hole transparent");
          }
          if ((pixel >> 24) > 20)
            colored++;
        }
      }
      ok = check(colored > 50, "ring must render visible pixels") && ok;
      printf("%s colored=%zu animated=%zu\n", name, colored, different_pixels(baseline, pixels));
    }
    if (ok)
      ok = write_frame(rings[i], 0, baseline) && write_frame(rings[i], 1, pixels);
    if (ok && i >= 2) {
      const char* constant = i == 2 ? "const int LIGHTNING_COUNT = 1;" : "const int EMBER_COUNT = 1;";
      const char* found = strstr(source, constant);
      ok = check(found != NULL, "editable head count constant exists");
      const int counts[] = {1, 0, 2, 3, 4, 9};
      for (size_t c = 0; ok && c < sizeof(counts) / sizeof(counts[0]); c++) {
        size_t length = strlen(source) + 64;
        char* variant = malloc(length);
        if (variant == NULL) {
          ok = false;
          break;
        }
        snprintf(
            variant, length, "%.*sconst int %s = %d;%s", (int)(found - source), source,
            i == 2 ? "LIGHTNING_COUNT" : "EMBER_COUNT", counts[c], found + strlen(constant)
        );
        struct fx_decoration_shader* program = fx_decoration_shader_create(fixture.renderer, variant, name);
        free(variant);
        ok =
            check(program != NULL, "compile editable head count") && render_decoration(&fixture, program, 1.3f, pixels);
        if (ok && (counts[c] == 0 || counts[c] == 9)) {
          ok = check(different_pixels(pixels, baseline) == 0, "out-of-range count must clamp to nearest limit");
        } else if (ok && counts[c] > 1) {
          ok = check(different_pixels(pixels, baseline) > 20, "adding a head must change the ring pixels");
        }
        memcpy(baseline, pixels, sizeof(baseline));
        fx_decoration_shader_unref(program);
      }
    }
    free(source);
    fx_decoration_shader_unref(shader);
  }
  if (ok)
    ok = test_decoration_visibility(&fixture);
  fx_animation_shader_unref(identity);
  struct fx_decoration_shader* retained = fx_decoration_shader_create(
      fixture.renderer, "vec4 ring_color(vec2 p) { return vec4(1); }", "retained-after-renderer-destroy"
  );
  ok = check(retained != NULL, "create retained program") && ok;
  struct fx_decoration_light* retained_light = NULL;
  if (retained != NULL)
    ok = render_light(&fixture, retained, &retained_light, 0, 1, pixels) && ok;
  fixture_finish(&fixture);
  // Nodes and compilation caches may release a program after its renderer.
  fx_decoration_shader_unref(retained);
  fx_decoration_light_destroy(retained_light);
  return ok ? 0 : 1;
}

#include "render/fx_renderer/postprocess.h"

#include "render_fixture.h"

#include <string.h>
#include <umbrielfx/render/pass.h>
#include <umbrielfx/types/wlr_scene.h>
#include <wlr/render/color.h>

#define SIZE 128
static bool render_effect(
    struct fixture* fixture, struct fx_postprocess_chain* chain, struct fx_postprocess_state** state, float time,
    float cursor, float background, bool advance, bool fail, uint32_t pixels[SIZE * SIZE]
) {
  struct wlr_buffer* buffer = create_output_buffer(fixture, DRM_FORMAT_ABGR8888, SIZE, SIZE);
  if (buffer == NULL)
    return false;
  struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(fixture->renderer, buffer, NULL);
  if (base == NULL) {
    wlr_buffer_drop(buffer);
    return false;
  }
  // Asymmetric tiles expose coordinate and displacement errors.
  for (int y = 0; y < SIZE; y += 8)
    for (int x = 0; x < SIZE; x += 8) {
      wlr_render_pass_add_rect(
          base,
          &(struct wlr_render_rect_options){
              .box = {x, y, 8, 8},
              .color = {background * x / SIZE, background * y / SIZE, background * 0.7f, 1},
              .blend_mode = WLR_RENDER_BLEND_MODE_NONE
          }
      );
    }
  struct fx_gles_render_pass* pass = fx_get_render_pass(base);
  bool ok = chain == NULL
      || fx_render_pass_postprocess(
                pass, state, chain,
                &(struct fx_postprocess_parameters){
                    .time = time,
                    .scale = 1,
                    .cursor = {cursor, cursor},
                    .output_size = {SIZE, SIZE},
                    .region = {0, 0, 1, 1},
                    .box = {0, 0, SIZE, SIZE}
                },
                NULL, advance
      );
  if (fail)
    fx_postprocess_commit(&pass->postprocess_updates, false);
  ok = wlr_render_pass_submit(base) && ok;
  if (ok)
    ok = read_buffer(fixture, buffer, DRM_FORMAT_ABGR8888, SIZE * 4, pixels);
  wlr_buffer_drop(buffer);
  return ok;
}
static struct fx_postprocess_chain* load_chain(struct fixture* fixture, const char* directory, const char* name) {
  char path[256];
  const bool multi = strcmp(name, "cursor/comet") == 0 || strcmp(name, "cursor/comet-glow") == 0;
  char* code[2] = {0};
  struct fx_postprocess_source sources[2] = {0};
  for (int i = 0; i < (multi ? 2 : 1); ++i) {
    snprintf(path, sizeof(path), multi ? "%s-%d.glsl" : "%s.glsl", name, i);
    code[i] = read_text_file(directory, path);
    sources[i] =
        (struct fx_postprocess_source){.code = code[i], .label = name, .buffer = strcmp(name, "cursor/trail") == 0};
  }
  struct fx_postprocess_chain* chain = fx_postprocess_chain_create(fixture->renderer, sources, multi ? 2 : 1);
  free(code[0]);
  free(code[1]);
  return chain;
}

static bool test_feedback(struct fixture* fixture, const char* directory, const char* name) {
  struct fx_postprocess_chain* chain = load_chain(fixture, directory, name);
  if (chain == NULL)
    return false;
  struct fx_postprocess_state *state = NULL, *reference = NULL;
  uint32_t baseline[SIZE * SIZE], pixels[SIZE * SIZE], expected[SIZE * SIZE];
  bool ok = render_effect(fixture, NULL, &reference, 0, -1000, 1, true, false, baseline)
      && render_effect(fixture, chain, &state, 0, -1000, 1, true, false, pixels)
      && check(count_differences(pixels, baseline, SIZE * SIZE, 0) == 0,
               "empty initial history must not contain the desktop")
      && render_effect(fixture, chain, &state, 0, 64, 0, true, false, pixels)
      && render_effect(fixture, chain, &reference, 0, 64, 0, true, false, expected)
      && check(count_differences(pixels, expected, SIZE * SIZE, 0) == 0,
               "independent histories must start identically");
  for (int i = 0; ok && i < 4; ++i)
    ok = render_effect(fixture, chain, &state, 0, -1000, 1, false, false, pixels);
  ok = render_effect(fixture, chain, &state, 0, -1000, 1, true, true, pixels) && ok;
  ok = render_effect(fixture, chain, &state, 0, -1000, 0, true, false, pixels)
      && render_effect(fixture, chain, &reference, 0, -1000, 0, true, false, expected)
      && check(
           count_differences(pixels, expected, SIZE * SIZE, 0) == 0,
           "capture and failed submit must not advance or overwrite feedback"
      )
      && ok;
  for (int frame = 0; ok && frame < 140; ++frame)
    ok = render_effect(fixture, chain, &state, frame * 0.01f, -1000, frame % 2, true, false, pixels);
  ok = render_effect(fixture, chain, &state, 2, -1000, 1, true, false, pixels)
      && check(
           count_differences(pixels, baseline, SIZE * SIZE, 0) == 0,
           "feedback must decay to black without scroll/video smear"
      )
      && ok;
  fx_postprocess_state_destroy(state);
  fx_postprocess_state_destroy(reference);
  fx_postprocess_chain_unref(chain);
  return ok;
}

// Uses RGBX targets; two retained consumers must survive buffer reuse.
static bool test_capture(struct fixture* fixture, bool linear) {
  struct wlr_buffer* target = create_output_buffer(fixture, DRM_FORMAT_XBGR8888, SIZE, SIZE);
  if (target == NULL)
    return false;
  struct wlr_color_transform* transform =
      linear ? wlr_color_transform_init_linear_to_inverse_eotf(WLR_COLOR_TRANSFER_FUNCTION_SRGB) : NULL;
  const struct fx_postprocess_source source = {
      .code = "vec4 postprocess(vec3 p) { vec4 c=tex2D_screen(p.xy); return vec4(c.a-c.rgb,c.a); }",
      .label = "capture invert"
  };
  struct fx_postprocess_chain* chain = fx_postprocess_chain_create(fixture->renderer, &source, 1);
  struct fx_postprocess_state* state = NULL;
  struct wlr_texture* consumers[2] = {0};
  bool ok = chain != NULL;
  uint32_t pixels[SIZE * SIZE];
  for (int frame = 0; ok && frame < 3; ++frame) {
    struct wlr_render_pass* base = wlr_renderer_begin_buffer_pass(
        fixture->renderer, target, &(struct wlr_buffer_pass_options){.color_transform = transform}
    );
    if (base == NULL) {
      ok = false;
      break;
    }
    struct fx_gles_render_pass* pass = fx_get_render_pass(base);
    wlr_render_pass_add_rect(
        base,
        &(struct wlr_render_rect_options){
            .box = {0, 0, SIZE, SIZE},
            .color = {frame ? 0.75f : 0.25f, 0.5f, 0.75f, 1},
            .blend_mode = WLR_RENDER_BLEND_MODE_NONE
        }
    );
    ok = fx_render_pass_save_effect_capture(pass) && ok;
    ok = fx_render_pass_postprocess(
             pass, &state, chain,
             &(struct fx_postprocess_parameters){
                 .scale = 1, .output_size = {SIZE, SIZE}, .region = {0, 0, 1, 1}, .box = {0, 0, SIZE, SIZE}
             },
             NULL, true
         )
        && ok;
    struct fx_framebuffer* framebuffer = pass->output_buffer;
    ok = wlr_render_pass_submit(base) && ok;
    ok = read_buffer(fixture, target, DRM_FORMAT_ABGR8888, SIZE * 4, pixels) && ok;
    int red = pixels[SIZE * 64 + 64] & 255;
    ok = check(abs(red - (frame ? 191 : 64)) <= 2, "capture sees unfiltered RGBX source") && ok;
    framebuffer->effect_capture_valid = false;
    ok = read_buffer(fixture, target, DRM_FORMAT_ABGR8888, SIZE * 4, pixels) && ok;
    red = pixels[SIZE * 64 + 64] & 255;
    ok = check(abs(red - (frame ? 64 : 191)) <= 2, "display retains inverted RGBX source") && ok;
    framebuffer->effect_capture_valid = true;
    if (frame == 0)
      for (int i = 0; i < 2; ++i)
        consumers[i] = wlr_texture_from_buffer(fixture->renderer, target);
  }
  for (int i = 0; i < 2; ++i)
    if (consumers[i] != NULL) {
      ok = wlr_texture_read_pixels(
               consumers[i],
               &(struct wlr_texture_read_pixels_options){
                   .data = pixels, .format = DRM_FORMAT_ABGR8888, .stride = SIZE * 4, .src_box = {0, 0, SIZE, SIZE}
               }
           )
          && ok;
      ok = check(abs((int)(pixels[SIZE * 64 + 64] & 255) - 64) <= 2, "retained capture consumer is immutable") && ok;
      wlr_texture_destroy(consumers[i]);
    } else
      ok = false;
  fx_postprocess_state_destroy(state);
  fx_postprocess_chain_unref(chain);
  wlr_color_transform_unref(transform);
  wlr_buffer_drop(target);
  return ok;
}

static bool test_scheduling(struct fixture* fixture) {
  struct wlr_scene* scene = wlr_scene_create();
  struct wlr_scene_output* output = wlr_scene_output_create(scene, fixture->output);
  struct wlr_output* second = wlr_headless_add_output(fixture->backend, 16, 16);
  struct wlr_scene_output* other = wlr_scene_output_create(scene, second);
  wlr_scene_output_set_position(other, 100, 0);
  struct wlr_scene_rect* marker = wlr_scene_rect_create(&scene->tree, 16, 16, (float[4]){0});
  const struct fx_postprocess_source clock = {
      .label = "clock", .code = "vec4 postprocess(vec3 p) { return vec4(sin(umbriel_time)); }"
  };
  const struct fx_postprocess_source comment = {
      .label = "comment", .code = "// umbriel_time\nvec4 postprocess(vec3 p) { return tex2D_screen(p.xy); }"
  };
  struct fx_postprocess_chain* animated = fx_postprocess_chain_create(fixture->renderer, &clock, 1);
  struct fx_postprocess_chain* still = fx_postprocess_chain_create(fixture->renderer, &comment, 1);
  bool ok = scene != NULL && output != NULL && other != NULL && marker != NULL && animated != NULL && still != NULL;
  if (ok) {
    wlr_scene_rect_set_postprocess(marker, animated);
    ok = check(wlr_scene_output_tick_postprocess(output, 1, false), "visible animated window requests frames") && ok;
    ok = check(!wlr_scene_output_tick_postprocess(other, 1, false), "other output remains idle") && ok;
    wlr_scene_node_set_enabled(&marker->node, false);
    ok = check(!wlr_scene_output_tick_postprocess(output, 2, false), "hidden window idles") && ok;
    wlr_scene_node_set_enabled(&marker->node, true);
    struct wlr_scene_rect* cover = wlr_scene_rect_create(&scene->tree, 16, 16, (float[4]){1, 1, 1, 1});
    ok = check(!wlr_scene_output_tick_postprocess(output, 3, false), "occluded window idles") && ok;
    wlr_scene_node_destroy(&cover->node);
    wlr_scene_rect_set_postprocess(marker, still);
    ok = check(!wlr_scene_output_tick_postprocess(output, 4, false), "comment-only clock does not animate") && ok;
    wlr_scene_output_set_postprocess(
        output, NULL, 0, (struct fx_scene_postprocess){.chain = animated}, true, false, FX_POSTPROCESS_ON_DAMAGE
    );
    ok = check(!wlr_scene_output_tick_postprocess(output, 5, false), "on-damage policy idles") && ok;
    wlr_scene_output_set_postprocess(
        output, NULL, 0, (struct fx_scene_postprocess){.chain = still}, true, false, FX_POSTPROCESS_CONTINUOUS
    );
    ok = check(wlr_scene_output_tick_postprocess(output, 6, false), "continuous policy forces frames") && ok;
    ok = check(!wlr_scene_output_tick_postprocess(output, 7, true), "session lock suspends effects") && ok;
    ok = check(wlr_scene_output_tick_postprocess(output, 8, false), "unlock resumes effects") && ok;
    wlr_scene_output_set_postprocess(
        output, NULL, 0, (struct fx_scene_postprocess){0}, false, false, FX_POSTPROCESS_AUTO
    );
    ok = check(!wlr_scene_output_tick_postprocess(output, 9, false), "disabled effects restore idle") && ok;
  }
  fx_postprocess_chain_unref(animated);
  fx_postprocess_chain_unref(still);
  wlr_scene_node_destroy(&scene->tree.node);
  wlr_output_destroy(second);
  return ok;
}

int main(int argc, char** argv) {
  if (argc != 2 && argc != 3)
    return 1;
  struct fixture fixture;
  if (!fixture_init(&fixture)) {
    fixture_finish(&fixture);
    return 77;
  }
  const char* names[] = {
      "window/adaptive-text-v3",
      "window/adaptive-text-v4",
      "window/autumn-leaves",
      "window/crt",
      "window/cvd-deutan-alphabet",
      "window/cvd-deutan-combo",
      "window/cvd-deutan-drift",
      "window/cvd-deutan-oriented",
      "window/cvd-deutan",
      "window/cvd-protan",
      "window/cvd-tritan",
      "window/film-grain",
      "window/fire-tendrils",
      "window/fire",
      "window/fisheye-rgb",
      "window/mercury-sheen",
      "window/parchment-dark",
      "window/parchment",
      "window/pixel-mosaic",
      "window/prairie-wind",
      "window/rainbow-radial",
      "window/rainbow-smoke",
      "window/rainbow-waves",
      "window/rainfall",
      "window/rgb-border",
      "window/rgb-shimmer",
      "window/ripple-drops",
      "window/rolling-clouds",
      "window/rorschach",
      "window/rorschach2",
      "window/snowfall",
      "screen/crt",
      "screen/grayscale",
      "screen/vignette",
      "screen/warmtint",
      "cursor/adaptive",
      "cursor/blueglow",
      "cursor/rainbow-tunnel",
      "cursor/rainbow-tunnel-bare",
      "cursor/ripple",
      "cursor/shockwave",
      "cursor/spotlight",
      "cursor/comet",
      "cursor/comet-glow",
      "cursor/trail"
  };
  const bool animated[] = {false, false, true,  false, true,  true,  true,  true,  true,  true,  true,  true,
                           true,  true,  false, true,  false, false, false, true,  true,  true,  true,  true,
                           true,  true,  true,  true,  true,  true,  true,  false, false, false, false, false,
                           false, true,  true,  true,  true,  false, true,  true,  true};
  uint32_t baseline[SIZE * SIZE], first[SIZE * SIZE], second[SIZE * SIZE];
  struct fx_postprocess_state* empty = NULL;
  bool ok = render_effect(&fixture, NULL, &empty, 0, 64, 1, true, false, baseline);
  for (size_t i = 0; ok && i < sizeof(names) / sizeof(names[0]); ++i) {
    struct fx_postprocess_chain* chain = load_chain(&fixture, argv[1], names[i]);
    struct fx_postprocess_state* state = NULL;
    ok = check(chain != NULL, names[i])
        && check(fx_postprocess_chain_animated(chain) == animated[i], "compiled animation capability");
    const float cursor = strcmp(names[i], "cursor/spotlight") == 0 ? -100 : 64;
    if (ok)
      ok = render_effect(&fixture, chain, &state, 0.1, cursor, 1, true, false, first)
          && render_effect(
               &fixture, chain, &state, 1.3, strcmp(names[i], "cursor/trail") == 0 ? 90 : cursor, 1, true, false, second
          )
          && check(
               count_differences(first, baseline, SIZE * SIZE, 0) + count_differences(second, baseline, SIZE * SIZE, 0)
                   > 20,
               "preset must visibly change the composed scene"
          );
    if (ok && animated[i])
      ok = check(
          count_differences(first, second, SIZE * SIZE, 0) > 10, "animated preset must change after its first frame"
      );
    if (ok && !animated[i])
      ok = check(count_differences(first, second, SIZE * SIZE, 0) == 0, "static preset must remain unchanged");
    if (ok)
      ok = write_frame(names[i], 0, first, SIZE, SIZE) && write_frame(names[i], 1, second, SIZE, SIZE);
    if (ok && argc == 3) {
      struct fx_postprocess_chain* reference = load_chain(&fixture, argv[2], names[i]);
      struct fx_postprocess_state* history = NULL;
      uint32_t expected[SIZE * SIZE];
      ok = reference != NULL && render_effect(&fixture, reference, &history, 0.1, cursor, 1, true, false, expected);
      if (ok) {
        char name[256];
        snprintf(name, sizeof(name), "reference-%s", names[i]);
        ok = write_frame(name, 0, expected, SIZE, SIZE);
        size_t changed = count_differences(expected, first, SIZE * SIZE, 0);
        printf("source comparison %s: %zu differing pixels\n", names[i], changed);
        // Reversed smoothstep edges may differ by one quantisation step.
        for (int pixel = 0; ok && pixel < SIZE * SIZE; ++pixel)
          for (int c = 0; c < 4; ++c)
            if (abs((int)((expected[pixel] >> (c * 8)) & 255) - (int)((first[pixel] >> (c * 8)) & 255)) > 1)
              ok = false;
        ok = check(ok, "source/reference pixels exceed one quantization step");
      }
      fx_postprocess_state_destroy(history);
      fx_postprocess_chain_unref(reference);
    }
    printf("%s %s\n", names[i], ok ? "OK" : "FAIL");
    fx_postprocess_state_destroy(state);
    fx_postprocess_chain_unref(chain);
  }
  const char* feedback[] = {"cursor/comet", "cursor/comet-glow", "cursor/trail"};
  for (size_t i = 0; ok && i < 3; ++i)
    ok = test_feedback(&fixture, argv[1], feedback[i]);
  ok = test_capture(&fixture, false) && ok;
  ok = test_capture(&fixture, true) && ok;
  ok = test_scheduling(&fixture) && ok;
  struct fx_postprocess_chain* retained = load_chain(&fixture, argv[1], "cursor/trail");
  struct fx_postprocess_state* retained_state = NULL;
  ok = retained != NULL && render_effect(&fixture, retained, &retained_state, 0, 64, 1, true, false, first) && ok;
  fixture_finish(&fixture);
  fx_postprocess_state_destroy(retained_state);
  fx_postprocess_chain_unref(retained);
  return ok ? 0 : 1;
}

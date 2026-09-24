#include "render/fx_renderer/postprocess.h"

#include "postprocess_composite_frag_src.h"
#include "postprocess_frag_src.h"
#include "render/egl.h"
#include "render/fx_renderer/fx_renderer.h"
#include "render/pass.h"
#include "util/matrix.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wlr/util/log.h>
#include <wlr/util/transform.h>

struct effect_program {
  GLuint program;
  GLint proj, tex_proj, pos, size, output_size, cursor, region, time, scale;
  GLint screen, source, previous, screen_previous, buffer, source_matrix, first, linear;
  GLint palette, palette_count;
};
struct effect_pass {
  struct effect_program color, buffer;
  bool feedback;
};
struct fx_postprocess_chain {
  unsigned refs;
  struct fx_renderer* renderer;
  struct wl_listener destroy;
  size_t count;
  bool animated, pointer, screen_previous;
  struct effect_pass passes[FX_POSTPROCESS_MAX_PASSES];
  GLuint composite;
  GLint proj, tex_proj, pos, tex, linear, size, radius, mask;
};
struct effect_target {
  GLuint texture, fbo;
};
struct effect_storage {
  struct effect_target result[2], buffer[2];
};
struct fx_postprocess_state {
  unsigned refs;
  struct fx_postprocess_chain* chain;
  struct wlr_box box;
  enum wl_output_transform transform;
  bool linear, valid, failed;
  unsigned current;
  struct effect_target source[2], black;
  struct effect_storage passes[FX_POSTPROCESS_MAX_PASSES];
};
struct effect_update {
  struct wl_list link;
  struct fx_postprocess_state* state;
  unsigned current;
};

static void chain_renderer_destroy(struct wl_listener* listener, void* data) {
  struct fx_postprocess_chain* chain = wl_container_of(listener, chain, destroy);
  wl_list_remove(&chain->destroy.link);
  chain->renderer = NULL;
}

struct fx_postprocess_chain* fx_postprocess_chain_ref(struct fx_postprocess_chain* chain) {
  if (chain != NULL)
    chain->refs++;
  return chain;
}
void fx_postprocess_chain_unref(struct fx_postprocess_chain* chain) {
  if (chain == NULL || --chain->refs != 0)
    return;
  if (chain->renderer != NULL) {
    struct wlr_egl_context previous;
    if (wlr_egl_make_current(chain->renderer->egl, &previous)) {
      for (size_t i = 0; i < chain->count; ++i) {
        glDeleteProgram(chain->passes[i].color.program);
        glDeleteProgram(chain->passes[i].buffer.program);
      }
      glDeleteProgram(chain->composite);
      wlr_egl_restore_context(&previous);
    }
    wl_list_remove(&chain->destroy.link);
  }
  free(chain);
}
bool fx_postprocess_chain_animated(const struct fx_postprocess_chain* chain) {
  return chain != NULL && chain->renderer != NULL && chain->animated;
}
bool fx_postprocess_chain_reads_pointer(const struct fx_postprocess_chain* chain) {
  return chain != NULL && chain->renderer != NULL && chain->pointer;
}

static bool compile_effect(struct effect_program* p, const struct fx_postprocess_source* source, bool buffer) {
  const char* entry = buffer ? "postprocess_buffer" : "postprocess";
  size_t capacity = sizeof(postprocess_frag_src) + strlen(source->code) + 256;
  char* code = malloc(capacity);
  if (code == NULL)
    return false;
  snprintf(
      code, capacity,
      "%s\n#line 1\n%s\nvoid main() { gl_FragColor = %s(vec3(umbriel_region.xy + v_texcoord * umbriel_region.zw, "
      "1.0)); }\n",
      postprocess_frag_src, source->code, entry
  );
  wlr_log(WLR_DEBUG, "Compiling postprocess %s: %s", entry, source->label);
  p->program = link_program(code);
  free(code);
  if (p->program == 0) {
    wlr_log(WLR_ERROR, "Postprocess '%s' failed; disabling its complete chain", source->label);
    return false;
  }
#define UNIFORM(member, name) p->member = glGetUniformLocation(p->program, name)
  UNIFORM(proj, "proj");
  UNIFORM(tex_proj, "tex_proj");
  UNIFORM(size, "umbriel_size");
  UNIFORM(output_size, "umbriel_output_size");
  UNIFORM(cursor, "umbriel_cursor");
  UNIFORM(region, "umbriel_region");
  UNIFORM(time, "umbriel_time");
  UNIFORM(scale, "umbriel_scale");
  UNIFORM(screen, "effect_screen");
  UNIFORM(source, "effect_source");
  UNIFORM(previous, "effect_previous");
  UNIFORM(screen_previous, "effect_screen_previous");
  UNIFORM(buffer, "effect_buffer");
  UNIFORM(source_matrix, "effect_source_matrix");
  UNIFORM(first, "effect_first");
  UNIFORM(linear, "effect_linear");
  UNIFORM(palette, "umbriel_palette");
  UNIFORM(palette_count, "umbriel_palette_count");
#undef UNIFORM
  p->pos = glGetAttribLocation(p->program, "pos");
  return true;
}

struct fx_postprocess_chain*
fx_postprocess_chain_create(struct wlr_renderer* renderer, const struct fx_postprocess_source* sources, size_t count) {
  if (!wlr_renderer_is_fx(renderer) || count == 0 || count > FX_POSTPROCESS_MAX_PASSES)
    return NULL;
  struct fx_renderer* fx = fx_get_renderer(renderer);
  struct wlr_egl_context previous;
  if (!wlr_egl_make_current(fx->egl, &previous))
    return NULL;
  struct fx_postprocess_chain* chain = calloc(1, sizeof(*chain));
  if (chain == NULL) {
    wlr_egl_restore_context(&previous);
    return NULL;
  }
  chain->refs = 1;
  chain->renderer = fx;
  chain->count = count;
  chain->destroy.notify = chain_renderer_destroy;
  wl_signal_add(&renderer->events.destroy, &chain->destroy);
  for (size_t i = 0; i < count; ++i) {
    struct effect_pass* pass = &chain->passes[i];
    if (sources[i].code == NULL
        || !compile_effect(&pass->color, &sources[i], false)
        || (sources[i].buffer && !compile_effect(&pass->buffer, &sources[i], true)))
      goto fail;
    const struct effect_program* programs[] = {&pass->color, &pass->buffer};
    pass->feedback = sources[i].buffer;
    for (size_t j = 0; j < 2; ++j) {
      const struct effect_program* p = programs[j];
      if (p->program == 0)
        continue;
      pass->feedback |= p->previous >= 0 || (!sources[i].buffer && p->buffer >= 0);
      chain->animated |= p->time >= 0 || pass->feedback;
      chain->pointer |= p->cursor >= 0;
      chain->screen_previous |= p->screen_previous >= 0;
    }
  }
  chain->animated |= chain->screen_previous;
  chain->composite = link_program(postprocess_composite_frag_src);
  if (chain->composite == 0)
    goto fail;
  chain->proj = glGetUniformLocation(chain->composite, "proj");
  chain->tex_proj = glGetUniformLocation(chain->composite, "tex_proj");
  chain->pos = glGetAttribLocation(chain->composite, "pos");
  chain->tex = glGetUniformLocation(chain->composite, "tex");
  chain->linear = glGetUniformLocation(chain->composite, "linear");
  chain->size = glGetUniformLocation(chain->composite, "size");
  chain->radius = glGetUniformLocation(chain->composite, "radius");
  chain->mask = glGetUniformLocation(chain->composite, "mask");
  wlr_egl_restore_context(&previous);
  return chain;
fail:
  fx_postprocess_chain_unref(chain);
  wlr_egl_restore_context(&previous);
  return NULL;
}

static void target_finish(struct effect_target* target) {
  glDeleteFramebuffers(1, &target->fbo);
  glDeleteTextures(1, &target->texture);
  *target = (struct effect_target){0};
}
static bool target_init(struct effect_target* target, int width, int height, bool linear) {
  GLint maximum;
  glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maximum);
  if (width <= 0 || height <= 0 || width > maximum || height > maximum)
    return false;
  if (!fx_render_target_init(
          &target->texture, &target->fbo, width, height, linear ? GL_HALF_FLOAT_OES : GL_UNSIGNED_BYTE
      ))
    return false;
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  return true;
}
static void state_clear(struct fx_postprocess_state* state) {
  for (size_t i = 0; i < state->chain->count; ++i)
    for (size_t j = 0; j < 2; ++j) {
      target_finish(&state->passes[i].result[j]);
      target_finish(&state->passes[i].buffer[j]);
    }
  target_finish(&state->source[0]);
  target_finish(&state->source[1]);
  target_finish(&state->black);
  state->valid = false;
  state->current = 0;
}
void fx_postprocess_state_destroy(struct fx_postprocess_state* state) {
  if (state == NULL || --state->refs != 0)
    return;
  if (state->chain->renderer != NULL) {
    struct wlr_egl_context previous;
    if (wlr_egl_make_current(state->chain->renderer->egl, &previous)) {
      state_clear(state);
      wlr_egl_restore_context(&previous);
    }
  }
  fx_postprocess_chain_unref(state->chain);
  free(state);
}

void fx_postprocess_commit(struct wl_list* updates, bool success) {
  struct effect_update *update, *tmp;
  wl_list_for_each_safe(update, tmp, updates, link) {
    if (success) {
      update->state->current = update->current;
      update->state->valid = true;
    }
    fx_postprocess_state_destroy(update->state);
    wl_list_remove(&update->link);
    free(update);
  }
}

static bool state_prepare(
    struct fx_postprocess_state* state, const struct fx_postprocess_parameters* parameters, bool linear, int width,
    int height
) {
  if (wlr_box_equal(&state->box, &parameters->box)
      && state->transform == parameters->transform
      && state->linear == linear
      && (state->failed || state->source[0].texture != 0))
    return !state->failed;
  state_clear(state);
  state->box = parameters->box;
  state->transform = parameters->transform;
  state->linear = linear;
  state->failed = true;
  if (!target_init(&state->black, 1, 1, false))
    return false;
  for (int j = 0; j < 2; ++j) {
    if (!target_init(&state->source[j], state->box.width, state->box.height, linear))
      return false;
    for (size_t i = 0; i < state->chain->count; ++i) {
      if (!target_init(&state->passes[i].result[j], width, height, linear))
        return false;
      if (state->chain->passes[i].buffer.program != 0
          && !target_init(&state->passes[i].buffer[j], width, height, linear))
        return false;
    }
  }
  state->failed = false;
  return true;
}

static struct fx_postprocess_state* state_create(struct fx_postprocess_chain* chain) {
  struct fx_postprocess_state* state = calloc(1, sizeof(*state));
  if (state != NULL) {
    state->refs = 1;
    state->chain = fx_postprocess_chain_ref(chain);
  }
  return state;
}

bool fx_render_pass_postprocess(
    struct fx_gles_render_pass* pass, struct fx_postprocess_state** owner, struct fx_postprocess_chain* chain,
    const struct fx_postprocess_parameters* parameters, const pixman_region32_t* clip, bool advance
) {
  if (chain == NULL || chain->renderer != pass->buffer->renderer || wlr_box_empty(&parameters->box))
    return false;
  struct wlr_box intersection;
  if (!wlr_box_intersection(
          &intersection, &parameters->box,
          &(struct wlr_box){.width = pass->buffer->buffer->width, .height = pass->buffer->buffer->height}
      ))
    return false;
  if (*owner != NULL && (*owner)->chain != chain) {
    fx_postprocess_state_destroy(*owner);
    *owner = NULL;
  }
  if (*owner == NULL)
    *owner = state_create(chain);
  if (*owner == NULL)
    return false;
  // Captures render into scratch state so a pending display update survives.
  struct fx_postprocess_state* state = advance ? *owner : state_create(chain);
  if (state == NULL)
    return false;
  int width = parameters->box.width, height = parameters->box.height;
  wlr_output_transform_coords(parameters->transform, &width, &height);
  GLint viewport[4];
  GLfloat clear[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  glGetFloatv(GL_COLOR_CLEAR_VALUE, clear);
  GLboolean scissor = glIsEnabled(GL_SCISSOR_TEST), stencil = glIsEnabled(GL_STENCIL_TEST);
  glDisable(GL_SCISSOR_TEST);
  glDisable(GL_STENCIL_TEST);
  glDisable(GL_BLEND);
  glActiveTexture(GL_TEXTURE0);
  bool ok = state_prepare(state, parameters, pass->has_color_transform, width, height);
  if (!ok)
    goto restore;
  struct fx_postprocess_state* previous = *owner;
  bool valid = previous->valid
      && wlr_box_equal(&previous->box, &parameters->box)
      && previous->transform == parameters->transform
      && previous->linear == pass->has_color_transform;
  unsigned next = 1 - state->current;
  GLuint original = state->source[next].texture;
  glBindFramebuffer(GL_FRAMEBUFFER, state->source[next].fbo);
  glClearColor(0, 0, 0, 0);
  glClear(GL_COLOR_BUFFER_BIT);
  // Sampled rather than copied: RGBX targets cannot CopyTexSubImage into RGBA.
  struct wlr_texture* source = fx_texture_from_buffer(&chain->renderer->wlr_renderer, pass->buffer->buffer);
  if (source == NULL) {
    ok = false;
    goto restore;
  }
  struct fx_texture* texture = fx_get_texture(source);
  glBindFramebuffer(GL_FRAMEBUFFER, state->source[next].fbo);
  glViewport(0, 0, parameters->box.width, parameters->box.height);
  glUseProgram(chain->composite);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(texture->target, texture->tex);
  glTexParameteri(texture->target, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(texture->target, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glUniform1i(chain->tex, 0);
  glUniform1i(chain->linear, false);
  glUniform1i(chain->mask, false);
  float copy_projection[9];
  matrix_projection(copy_projection, parameters->box.width, parameters->box.height, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  pixman_region32_t source_clip;
  pixman_region32_init_rect(
      &source_clip, intersection.x - parameters->box.x, intersection.y - parameters->box.y, intersection.width,
      intersection.height
  );
  const struct wlr_fbox source_box = {
      (double)parameters->box.x / source->width, (double)parameters->box.y / source->height,
      (double)parameters->box.width / source->width, (double)parameters->box.height / source->height
  };
  const struct wlr_box copy_box = {.width = parameters->box.width, .height = parameters->box.height};
  fx_set_proj_matrix(chain->proj, copy_projection, &copy_box);
  fx_set_tex_matrix(chain->tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &source_box);
  fx_render_box(&copy_box, &source_clip, chain->pos);
  pixman_region32_fini(&source_clip);
  wlr_texture_destroy(source);
  GLuint input = original;
  float source_matrix[9], projection[9];
  const struct wlr_fbox unit = {.width = 1, .height = 1};
  fx_make_tex_matrix(source_matrix, wlr_output_transform_invert(parameters->transform), &unit);
  matrix_projection(projection, width, height, WL_OUTPUT_TRANSFORM_FLIPPED_180);
  const struct wlr_box local = {.width = width, .height = height};
  glViewport(0, 0, width, height);
  for (size_t i = 0; i < chain->count; ++i) {
    struct effect_storage* storage = &state->passes[i];
    GLuint old_result = valid ? previous->passes[i].result[previous->current].texture : state->black.texture;
    GLuint accumulator = chain->passes[i].buffer.program != 0
        ? (valid ? previous->passes[i].buffer[previous->current].texture : state->black.texture)
        : old_result;
    for (int step = 0; step < 2; ++step) {
      const struct effect_program* p = step == 0 ? &chain->passes[i].buffer : &chain->passes[i].color;
      if (p->program == 0)
        continue;
      struct effect_target* target = step == 0 ? &storage->buffer[next] : &storage->result[next];
      glBindFramebuffer(GL_FRAMEBUFFER, target->fbo);
      glUseProgram(p->program);
      const GLuint textures[] = {
          input, original, old_result, valid ? previous->source[previous->current].texture : original, accumulator
      };
      const GLint locations[] = {p->screen, p->source, p->previous, p->screen_previous, p->buffer};
      for (unsigned t = 0; t < 5; ++t) {
        glActiveTexture(GL_TEXTURE0 + t);
        glBindTexture(GL_TEXTURE_2D, textures[t]);
        glUniform1i(locations[t], t);
      }
      glUniform2f(p->size, width, height);
      glUniform2fv(p->output_size, 1, parameters->output_size);
      glUniform2fv(p->cursor, 1, parameters->cursor);
      glUniform4fv(p->region, 1, parameters->region);
      glUniform1f(p->time, parameters->time);
      glUniform1f(p->scale, parameters->scale);
      glUniform1i(p->first, i == 0);
      const int palette_count = parameters->palette != NULL ? parameters->palette_count : 0;
      glUniform1i(p->palette_count, palette_count);
      if (palette_count > 0)
        glUniform4fv(p->palette, palette_count, parameters->palette);
      glUniform1i(p->linear, pass->has_color_transform);
      glUniformMatrix3fv(p->source_matrix, 1, GL_FALSE, source_matrix);
      fx_set_proj_matrix(p->proj, projection, &local);
      fx_set_tex_matrix(p->tex_proj, WL_OUTPUT_TRANSFORM_NORMAL, &unit);
      fx_render_box(&local, NULL, p->pos);
      if (step == 0)
        accumulator = target->texture;
      else
        input = target->texture;
    }
  }
  fx_framebuffer_bind(pass->buffer);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
  if (stencil)
    glEnable(GL_STENCIL_TEST);
  glUseProgram(chain->composite);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, input);
  glUniform1i(chain->tex, 0);
  glUniform1i(chain->linear, pass->has_color_transform);
  glUniform1i(chain->mask, true);
  glUniform2f(chain->size, width, height);
  glUniform4f(
      chain->radius, parameters->corners.top_left, parameters->corners.top_right, parameters->corners.bottom_right,
      parameters->corners.bottom_left
  );
  fx_set_proj_matrix(chain->proj, pass->projection_matrix, &parameters->box);
  fx_set_tex_matrix(chain->tex_proj, parameters->transform, &unit);
  fx_render_box(&parameters->box, clip, chain->pos);
  if (!pass->suppress_updated) {
    pixman_region32_t updated;
    pixman_region32_init_rect(&updated, intersection.x, intersection.y, intersection.width, intersection.height);
    if (clip != NULL)
      pixman_region32_intersect(&updated, &updated, clip);
    pixman_region32_union(&pass->updated_region, &pass->updated_region, &updated);
    pixman_region32_fini(&updated);
  }
  if (advance) {
    struct effect_update* update = calloc(1, sizeof(*update));
    if (update == NULL) {
      ok = false;
      goto restore;
    }
    update->state = state;
    update->current = next;
    state->refs++;
    wl_list_insert(pass->postprocess_updates.prev, &update->link);
  }
restore:
  fx_framebuffer_bind(pass->buffer);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
  glClearColor(clear[0], clear[1], clear[2], clear[3]);
  if (scissor)
    glEnable(GL_SCISSOR_TEST);
  if (stencil)
    glEnable(GL_STENCIL_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
  for (unsigned t = 0; t < 5; ++t) {
    glActiveTexture(GL_TEXTURE0 + t);
    glBindTexture(GL_TEXTURE_2D, 0);
  }
  glActiveTexture(GL_TEXTURE0);
  if (!advance)
    fx_postprocess_state_destroy(state);
  return ok;
}

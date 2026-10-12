#include "umbrielfx/types/linux_drm_syncobj.h"

#include <wlr/types/wlr_linux_drm_syncobj_v1.h>

struct wlr_drm_syncobj_timeline*
umbrielfx_linux_drm_syncobj_release_point(struct wlr_surface* surface, uint64_t* point) {
  struct wlr_linux_drm_syncobj_surface_v1_state* state = wlr_linux_drm_syncobj_v1_get_surface_state(surface);
  if (state == NULL || state->release_timeline == NULL) {
    return NULL;
  }
  *point = state->release_point;
  return state->release_timeline;
}

#include "scene/workspace_sources.h"

#include "input/cursor.h"
#include "output/output.h"
#include "scene/effect_registry.h"
#include "scene/workspace_inventory.h"
#include "server/server.h"
#include "view/view.h"
#include "wlr.h"
#include "workspace/workspace.h"

extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_program.h"
#include "../../umbrielfx/internal/render/fx_renderer/scene_resources.h"
#include "../../umbrielfx/internal/types/scene_source.h"
#include "../../umbrielfx/internal/types/wlr_scene.h"
}

#include <algorithm>
#include <cmath>
#include <limits>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_set>

namespace umbriel {
  fx_scene_resource_pool& presentationAggregatePool() {
    static fx_scene_resource_pool pool{.limit = FX_SCENE_TOTAL_BUDGET, .used = 0};
    return pool;
  }

  bool Output::registerWorkspaceSources(WorkspaceSources* sources) {
    if (m_activeWorkspaceSources != nullptr && m_activeWorkspaceSources != sources) {
      return false;
    }
    m_activeWorkspaceSources = sources;
    return true;
  }
  void Output::unregisterWorkspaceSources(WorkspaceSources* sources) {
    if (m_activeWorkspaceSources == sources) {
      m_activeWorkspaceSources = nullptr;
    }
  }
  void Output::notePresentationSourceContent() {
    if (m_activeWorkspaceSources) {
      m_activeWorkspaceSources->contentChanged();
    }
  }
  void Output::notePresentationViewMapped(View& view) {
    if (m_activeWorkspaceSources) {
      m_activeWorkspaceSources->viewMapped(view);
    }
  }
  void Output::notePresentationViewUnmapping(View& view) {
    if (m_activeWorkspaceSources) {
      m_activeWorkspaceSources->viewWillUnmap(view);
    }
  }
  struct WorkspaceSources::State {
    struct History {
      std::string identity;
      bool landing = false;
      fx_scene_reservation reservation{};
      fx_scene_source_session* session = nullptr;
      bool pending = false;
      void finish(bool submitted) {
        if (pending) {
          fx_scene_source_session_finish_frame_for_test(session, submitted);
          pending = false;
        }
      }
      ~History() {
        finish(false);
        fx_scene_source_session_destroy_for_test(session);
        fx_scene_release(&reservation);
      }
    };
    struct Sources {
      struct Hit {
        wlr_fbox box;
        std::string window;
      };
      struct FaceHits {
        std::string identity;
        std::vector<Hit> hits;
        int width = 0, height = 0;
      };
      fx_scene_reservation reservation{};
      std::vector<FaceHits> hits;
      std::vector<fx_scene_source_pair_for_test> pairs;
      uint64_t retained = 0;
      fx_scene_source_pair_for_test mixed{};
      std::array<fx_scene_target*, 2> mixedTargets{};
      ~Sources() {
        for (auto* target : mixedTargets) {
          fx_scene_target_destroy(target);
        }
        fx_scene_source_pair_finish_for_test(&mixed);
        for (auto& pair : pairs) {
          fx_scene_source_pair_finish_for_test(&pair);
        }
        fx_scene_release(&reservation);
      }
    };
    struct SurfaceWatch {
      State* owner;
      wlr_surface* surface;
      wl_listener commit{};
      wl_listener destroy{};
      SurfaceWatch(State& state, wlr_surface* watched) : owner(&state), surface(watched) {
        commit.notify = [](wl_listener* listener, void*) {
          SurfaceWatch* watch;
          watch = wl_container_of(listener, watch, commit);
          ++watch->owner->revision;
          wlr_output_schedule_frame(watch->owner->output.wlr());
        };
        destroy.notify = [](wl_listener* listener, void*) {
          SurfaceWatch* watch;
          watch = wl_container_of(listener, watch, destroy);
          wl_list_remove(&watch->commit.link);
          wl_list_init(&watch->commit.link);
          wl_list_remove(&watch->destroy.link);
          wl_list_init(&watch->destroy.link);
          watch->surface = nullptr;
          ++watch->owner->revision;
          wlr_output_schedule_frame(watch->owner->output.wlr());
        };
        wl_signal_add(&surface->events.commit, &commit);
        wl_signal_add(&surface->events.destroy, &destroy);
      }
      ~SurfaceWatch() {
        wl_list_remove(&commit.link);
        wl_list_remove(&destroy.link);
      }
    };
    struct Descriptor {
      std::vector<fx_scene_source_root_override> roots;
      std::vector<wlr_scene_tree*> clips;
      fx_scene_source_view view{};
      fx_scene_source_view_plan plan{};
      wlr_box contentBounds{};
      History* history = nullptr;
      size_t hitCount = 0;
    };
    WorkspaceSources& owner;
    std::function<void(PresentationFallback)> invalidated;
    bool preview = false;
    Server& server;
    Output& output;
    fx_scene_resource_pool pool{.limit = FX_SCENE_OUTPUT_BUDGET, .used = 0};
    std::unique_ptr<WorkspaceInventoryHold> inventory;
    std::vector<View*> owners;
    std::vector<std::unique_ptr<SurfaceWatch>> watches;
    std::unique_ptr<Sources> current;
    std::unique_ptr<Sources> candidate;
    std::unique_ptr<Sources> frozen;
    // Face and native landing are distinct occurrences even for one identity.
    // Their histories outlive image refreshes, but never their native inventory.
    std::vector<std::unique_ptr<History>> histories;
    WorkspaceSourceFace frozenMetadata;
    wlr_scene_tree* tree = nullptr;
    wlr_scene_buffer* picture = nullptr;
    wlr_box box{};
    std::string selected;
    std::vector<std::string> visible;
    std::vector<std::string> captureIdentities;
    bool pairOnly = false;
    std::vector<std::string> queuedVisible;
    std::vector<WorkspaceSourceFace> metadata;
    std::string landingIdentity;
    float framingProgress = 0;
    float landingMix = 0;
    std::optional<WorkspaceSourceFace> landingMetadata;
    uint64_t revision = 1;
    uint64_t capturedRevision = 0;
    uint64_t committedRevision = 0;
    uint64_t audioRevision = 0;
    uint64_t captures = 0;
    uint64_t callbacks = 0;
    bool pending = false;
    wl_event_source* preparationTimeout = nullptr;
    bool restorePending = false;
    bool callbacksPending = false;
    bool renderLocked = false;
    bool inputOwned = false;
    PresentationFallback fallback = PresentationFallback::None;

    void registerAudio() {
      std::vector<View*> contributing;
      for (View* view : owners) {
        if (view->pinned()
            || (view->workspace() != nullptr && std::ranges::contains(visible, view->workspace()->id()))) {
          contributing.push_back(view);
        }
      }
      server.effects().setSourceOccurrences(this, &output, contributing, true);
    }
    void watchSurfaces() {
      // Re-enumeration discovers new subsurfaces/popups after a parent commit.
      std::unordered_set<wlr_surface*> seen;
      struct Iteration {
        State& state;
        std::unordered_set<wlr_surface*>& seen;
      } iteration{*this, seen};
      for (View* view : owners) {
        if (pairOnly
            && frozen
            && !view->pinned()
            && (view->workspace() == nullptr || !std::ranges::contains(visible, view->workspace()->id()))) {
          continue;
        }
        wlr_xdg_surface_for_each_surface(
            view->toplevel()->base,
            [](wlr_surface* surface, int, int, void* data) {
              auto& iteration = *static_cast<Iteration*>(data);
              if (!iteration.seen.insert(surface).second) {
                return;
              }
              if (std::ranges::none_of(iteration.state.watches, [surface](const auto& watch) {
                    return watch->surface == surface;
                  })) {
                iteration.state.watches.push_back(std::make_unique<SurfaceWatch>(iteration.state, surface));
              }
            },
            &iteration
        );
      }
      for (uint32_t layer = 0; layer < 3; ++layer) {
        wlr_scene_node_for_each_buffer(
            &server.shellLayerTree(layer)->node,
            [](wlr_scene_buffer* buffer, int, int, void* data) {
              auto& iteration = *static_cast<Iteration*>(data);
              auto* sceneSurface = wlr_scene_surface_try_from_buffer(buffer);
              if (sceneSurface == nullptr || !iteration.seen.insert(sceneSurface->surface).second) {
                return;
              }
              if (std::ranges::none_of(iteration.state.watches, [&](const auto& watch) {
                    return watch->surface == sceneSurface->surface;
                  })) {
                iteration.state.watches.push_back(
                    std::make_unique<SurfaceWatch>(iteration.state, sceneSurface->surface)
                );
              }
            },
            &iteration
        );
      }
      std::erase_if(watches, [&](const auto& watch) { return !seen.contains(watch->surface); });
    }
    Descriptor describe(size_t index, float scale) {
      Descriptor result;
      auto& group = *output.workspaceGroup();
      auto* workspace = group.workspaceAt(index);
      auto* root = &server.scene()->tree;
      wlr_scene_node* first;
      first = wl_container_of(root->children.next, first, link);
      result.view = {
          .first = first,
          .last = &server.pinnedTree()->node,
          .roots = nullptr,
          .root_count = 0,
          .bypass_clips = nullptr,
          .bypass_clip_count = 0,
          .extent = box,
          .scale = scale,
          .viewport = box,
          .transparent = false,
          .nodes = nullptr,
          .node_count = 0,
          .emission_owner = nullptr,
          .session = nullptr
      };
      const auto add = [&](wlr_scene_node* node, fx_scene_source_visibility visibility,
                           fx_scene_source_framing framing = FX_SCENE_SOURCE_CONTENT) {
        result.roots.push_back(
            {.root = node, .visibility = visibility, .offset_x = 0, .offset_y = 0, .framing = framing}
        );
      };
      // Shared desktop bands stay in face coordinates, including layer-shell
      // exclusive content; source framing never configures or moves clients.
      add(first, FX_SCENE_SOURCE_INHERIT, FX_SCENE_SOURCE_VIEWPORT);
      for (uint32_t layer = 0; layer < 3; ++layer) {
        auto* node = &server.shellLayerTree(layer)->node;
        if (node != first) {
          add(node, FX_SCENE_SOURCE_INHERIT, FX_SCENE_SOURCE_VIEWPORT);
        }
      }
      add(&server.pinnedTree()->node, FX_SCENE_SOURCE_INHERIT, FX_SCENE_SOURCE_VIEWPORT);
      for (const auto& other : server.outputs()) {
        if (other.get() != &output) {
          add(&other->viewRoot()->node, FX_SCENE_SOURCE_HIDDEN);
          add(&other->fullscreenRoot()->node, FX_SCENE_SOURCE_HIDDEN);
          add(&other->pinnedRoot()->node, FX_SCENE_SOURCE_HIDDEN);
        }
      }
      add(&output.viewRoot()->node, FX_SCENE_SOURCE_VISIBLE);
      add(&output.fullscreenRoot()->node, FX_SCENE_SOURCE_VISIBLE);
      result.clips = {output.viewRoot(), output.fullscreenRoot()};
      for (size_t i = 0; i < group.workspaceCount(); ++i) {
        auto* item = group.workspaceAt(i);
        auto* itemTree = item->tileShadowLayer()->node.parent;
        const auto visibility = item == workspace ? FX_SCENE_SOURCE_VISIBLE : FX_SCENE_SOURCE_HIDDEN;
        add(&itemTree->node, visibility);
        result.roots.back().offset_x = -itemTree->node.x;
        result.roots.back().offset_y = -itemTree->node.y;
        add(&item->fullscreenTree()->node, visibility);
        result.roots.back().offset_x = -item->fullscreenTree()->node.x;
        result.roots.back().offset_y = -item->fullscreenTree()->node.y;
        if (item == workspace) {
          result.clips.push_back(itemTree);
          result.clips.push_back(item->fullscreenTree());
        }
      }
      for (View* view : owners) {
        if (!view->pinned()) {
          add(&view->sceneTree()->node,
              view->workspace() == workspace ? FX_SCENE_SOURCE_VISIBLE : FX_SCENE_SOURCE_HIDDEN);
        }
      }
      return result;
    }
    bool refresh() {
      watchSurfaces();
      std::vector<Descriptor> descriptors;
      uint64_t retained = 0;
      uint64_t peak = 0;
      uint64_t newHistoryBytes = 0;
      uint64_t hitBytes = 0;
      // All faces scale together. The complete inventory is preserved at every
      // attempt, and the old displayed inventory remains charged throughout.
      for (uint32_t divisor : {1U, 2U, 4U}) {
        if (pairOnly && divisor != 1) {
          break;
        }
        descriptors.clear();
        const size_t faceCount = captureIdentities.size();
        const size_t sourceCount = faceCount + (landingIdentity.empty() ? 0 : 1);
        descriptors.reserve(sourceCount);
        retained = peak = newHistoryBytes = hitBytes = 0;
        bool valid = true;
        for (size_t i = 0; i < sourceCount; ++i) {
          if (i == 0 && frozen) {
            descriptors.emplace_back();
            continue;
          }
          const auto& identity = i < faceCount ? captureIdentities[i] : landingIdentity;
          const auto sourceIndex = static_cast<size_t>(
              std::ranges::find(inventory->identities(), identity) - inventory->identities().begin()
          );
          const auto sourceScale =
              i < faceCount ? output.wlr()->scale / static_cast<float>(divisor) : output.wlr()->scale;
          auto& descriptor = descriptors.emplace_back(describe(sourceIndex, sourceScale));
          descriptor.view.roots = descriptor.roots.data();
          descriptor.view.root_count = descriptor.roots.size();
          descriptor.view.bypass_clips = descriptor.clips.data();
          descriptor.view.bypass_clip_count = descriptor.clips.size();
          // Query complete visual content with output/scroll clips bypassed,
          // retaining window clips. This does not configure or move clients.
          if (!fx_scene_source_view_bounds_for_test(
                  output.sceneOutput(), &descriptor.view, FX_SCENE_SOURCE_CONTENT, &descriptor.contentBounds
              )) {
            valid = false;
            break;
          }
          const float progress = i < faceCount ? framingProgress : 0.0F;
          if (progress > 0) {
            const auto& bounds = descriptor.contentBounds;
            const fx_scene_box viewport{
                static_cast<double>(box.x), static_cast<double>(box.y), static_cast<double>(box.width),
                static_cast<double>(box.height)
            };
            const fx_scene_box content{
                static_cast<double>(bounds.x), static_cast<double>(bounds.y), static_cast<double>(bounds.width),
                static_cast<double>(bounds.height)
            };
            fx_scene_box framed{};
            if (!fx_scene_frame_extent(
                    &viewport, &content, bounds.width > 0 && bounds.height > 0 ? 1 : 0, true, &framed
                )) {
              valid = false;
              break;
            }
            const double width = std::lerp(viewport.width, framed.width, progress);
            const double height = std::lerp(viewport.height, framed.height, progress);
            descriptor.view.extent = {
                .x = static_cast<int>(std::floor(std::lerp(viewport.x, framed.x, progress))),
                .y = static_cast<int>(std::floor(std::lerp(viewport.y, framed.y, progress))),
                .width = static_cast<int>(std::ceil(width)),
                .height = static_cast<int>(std::ceil(height))
            };
            descriptor.view.scale =
                sourceScale * static_cast<float>(box.width) / static_cast<float>(descriptor.view.extent.width);
          } else {
            // Native viewport framing keeps its ordinary clipping, including
            // the full-resolution landing source throughout the transition.
            descriptor.view.bypass_clips = nullptr;
            descriptor.view.bypass_clip_count = 0;
          }
          if (!fx_scene_source_view_plan_for_test(output.sceneOutput(), &descriptor.view, &descriptor.plan)
              || descriptor.plan.retained_bytes > std::numeric_limits<uint64_t>::max() - retained) {
            valid = false;
            break;
          }
          retained += descriptor.plan.retained_bytes;
          if (!pairOnly && i < faceCount) {
            if (!fx_scene_source_view_hits(
                    output.sceneOutput(), &descriptor.view, nullptr, nullptr, &descriptor.hitCount
                )) {
              valid = false;
              break;
            }
            // Stable map identities are bounded below; reserve both vector
            // entries and worst-case string storage before taking a snapshot.
            hitBytes += sizeof(Sources::FaceHits) + 256 + descriptor.hitCount * (sizeof(Sources::Hit) + 256);
          }
          peak = std::max(peak, descriptor.plan.capture_bytes);
          auto existing = std::ranges::find_if(histories, [&](const auto& history) {
            return history->identity == identity && history->landing == (i >= faceCount);
          });
          if (existing != histories.end()
              && (descriptor.plan.history_bytes == 0
                  || !fx_scene_source_view_session_matches(
                      output.sceneOutput(), &descriptor.view, (*existing)->session
                  ))) {
            histories.erase(existing);
            existing = histories.end();
          }
          if (existing != histories.end()) {
            descriptor.history = existing->get();
            descriptor.view.session = descriptor.history->session;
          } else {
            if (descriptor.plan.history_bytes > std::numeric_limits<uint64_t>::max() - newHistoryBytes) {
              valid = false;
              break;
            }
            newHistoryBytes += descriptor.plan.history_bytes;
          }
        }
        if (!valid) {
          continue;
        }
        // Native landing and its independent mixed pair are both included
        // before acquisition, even while their blend weight is zero.
        if (!landingIdentity.empty()) {
          const uint64_t mixBytes = descriptors.back().plan.retained_bytes + 4096;
          if (retained > std::numeric_limits<uint64_t>::max() - mixBytes) {
            return false;
          }
          retained += mixBytes;
        }
        const uint64_t landing =
            preview ? static_cast<uint64_t>(output.wlr()->width) * output.wlr()->height * 16 + 4096 : 0;
        if (peak > std::numeric_limits<uint64_t>::max() - landing
            || newHistoryBytes > std::numeric_limits<uint64_t>::max() - peak - landing
            || retained > std::numeric_limits<uint64_t>::max() - peak - landing - newHistoryBytes) {
          return false;
        }
        auto next = std::make_unique<Sources>();
        const uint64_t imageBytes = retained + peak + landing + hitBytes;
        // A workspace set must also admit its next live refresh while this
        // candidate remains displayed. Otherwise a large first frame can fit
        // alone, then strand even the smallest successor outside the budget.
        // The current images are already charged, so only add the shortfall.
        const uint64_t refreshRetained = pairOnly ? 0 : retained + landing + hitBytes;
        const uint64_t currentBytes = current ? current->reservation.bytes : 0;
        const uint64_t refreshHeadroom = refreshRetained > currentBytes ? refreshRetained - currentBytes : 0;
        if (refreshHeadroom > std::numeric_limits<uint64_t>::max() - imageBytes - newHistoryBytes
            || !fx_scene_reserve(
                &next->reservation, &pool, &presentationAggregatePool(), imageBytes + newHistoryBytes + refreshHeadroom
            )) {
          continue;
        }
        // Full image + history admission precedes every allocation. Split the
        // reservation in this single-threaded pool so histories can survive
        // releasing transient capture images and the old displayed revision.
        fx_scene_release(&next->reservation);
        if (!fx_scene_reserve(&next->reservation, &pool, &presentationAggregatePool(), imageBytes)) {
          return false;
        }
        for (size_t i = 0; i < descriptors.size(); ++i) {
          auto& descriptor = descriptors[i];
          if (descriptor.plan.history_bytes == 0 || descriptor.history != nullptr) {
            continue;
          }
          auto history = std::make_unique<History>();
          history->identity = i < faceCount ? captureIdentities[i] : landingIdentity;
          history->landing = i >= faceCount;
          if (!fx_scene_reserve(
                  &history->reservation, &pool, &presentationAggregatePool(), descriptor.plan.history_bytes
              )) {
            return false;
          }
          history->session = fx_scene_source_view_session_create(
              output.sceneOutput(), &descriptor.view, descriptor.plan.history_bytes
          );
          if (history->session == nullptr) {
            return false;
          }
          descriptor.history = history.get();
          descriptor.view.session = history->session;
          histories.push_back(std::move(history));
        }
        next->retained = retained;
        next->pairs.resize(descriptors.size());
        if (!pairOnly) {
          next->hits.reserve(faceCount);
          for (size_t i = 0; i < faceCount; ++i) {
            auto& snapshot = next->hits.emplace_back();
            snapshot.identity = captureIdentities[i];
            snapshot.width = box.width;
            snapshot.height = box.height;
            snapshot.hits.reserve(descriptors[i].hitCount);
            struct Capture {
              State& state;
              Sources::FaceHits& snapshot;
            } capture{*this, snapshot};
            size_t count = 0;
            if (!fx_scene_source_view_hits(
                    output.sceneOutput(), &descriptors[i].view,
                    [](wlr_scene_node* node, const wlr_fbox* bounds, void* data) {
                      auto& capture = *static_cast<Capture*>(data);
                      std::string identity;
                      for (auto* ancestor = node; ancestor;
                           ancestor = ancestor->parent ? &ancestor->parent->node : nullptr) {
                        auto owner = std::ranges::find_if(capture.state.owners, [ancestor](View* view) {
                          return &view->sceneTree()->node == ancestor;
                        });
                        if (owner != capture.state.owners.end()) {
                          if (const auto* id = (*owner)->extForeignIdentifier()) {
                            identity = id;
                            if (identity.size() >= 256)
                              return false;
                          }
                          break;
                        }
                      }
                      capture.snapshot.hits.push_back({*bounds, std::move(identity)});
                      return true;
                    },
                    &capture, &count
                )
                || count != descriptors[i].hitCount)
              return false;
          }
        }
        server.effects().bindSourceAudio(this);
        for (size_t i = 0; i < descriptors.size(); ++i) {
          if (i == 0 && frozen) {
            continue;
          }
          if (auto* history = descriptors[i].history) {
            if (!fx_scene_source_session_begin_frame_for_test(history->session)) {
              return false;
            }
            history->pending = true;
          }
          if (!fx_scene_source_view_pair_capture_for_test(
                  output.sceneOutput(), &descriptors[i].view, descriptors[i].plan.total_bytes, &next->pairs[i]
              )) {
            return false;
          }
          ++captures;
        }
        if (!landingIdentity.empty()) {
          const auto& nativePlan = descriptors.back().plan;
          auto& native = next->pairs.back();
          const auto foundLanding = std::ranges::find(captureIdentities, landingIdentity);
          const auto landingIndex = static_cast<size_t>(foundLanding - captureIdentities.begin());
          auto& face = next->pairs[landingIndex];
          next->mixed.working_space = native.working_space;
          next->mixed.floating_point = native.floating_point;
          next->mixed.display = fx_scene_buffer_create(
              server.renderer(), server.allocator(), nativePlan.width, nativePlan.height, native.floating_point
          );
          next->mixed.unfiltered = fx_scene_buffer_create(
              server.renderer(), server.allocator(), nativePlan.width, nativePlan.height, native.floating_point
          );
          const std::array<wlr_buffer*, 2> targets{next->mixed.display, next->mixed.unfiltered};
          const std::array<wlr_buffer*, 2> first{face.display, face.unfiltered};
          const std::array<wlr_buffer*, 2> second{native.display, native.unfiltered};
          using Texture = std::unique_ptr<wlr_texture, decltype(&wlr_texture_destroy)>;
          for (size_t role = 0; role < targets.size(); ++role) {
            if (targets[role] == nullptr) {
              return false;
            }
            auto*& target = next->mixedTargets[role];
            target = fx_scene_target_create_with_color(server.renderer(), targets[role], false, native.working_space);
            Texture faceTexture(wlr_texture_from_buffer(server.renderer(), first[role]), wlr_texture_destroy);
            Texture nativeTexture(wlr_texture_from_buffer(server.renderer(), second[role]), wlr_texture_destroy);
            const fx_scene_input faceInput{.texture = faceTexture.get(), .sample_matrix = nullptr};
            const fx_scene_input nativeInput{.texture = nativeTexture.get(), .sample_matrix = nullptr};
            if (target == nullptr
                || !faceTexture
                || !nativeTexture
                || !fx_scene_target_blend(target, &faceInput, &nativeInput, landingMix, native.working_space)) {
              return false;
            }
          }
        }
        // Capture scratch has been released; retain only the owned images and
        // landing allowance while this candidate awaits output submission.
        fx_scene_release(&next->reservation);
        const bool reserved =
            fx_scene_reserve(&next->reservation, &pool, &presentationAggregatePool(), retained + landing + hitBytes);
        if (!reserved) {
          return false; // Shrinking cannot fail in this single-threaded reservation pool.
        }
        candidate = std::move(next);
        capturedRevision = revision;
        const auto found = std::ranges::find(captureIdentities, selected);
        const auto index = static_cast<size_t>(found - captureIdentities.begin());
        metadata.clear();
        landingMetadata.reset();
        metadata.reserve(candidate->pairs.size());
        for (size_t faceIndex = 0; faceIndex < candidate->pairs.size(); ++faceIndex) {
          const auto& facePair = candidate->pairs[faceIndex];
          auto& face = faceIndex < captureIdentities.size() ? metadata.emplace_back() : landingMetadata.emplace();
          if (faceIndex == 0 && frozen) {
            face = frozenMetadata;
            continue;
          }
          face.identity = faceIndex < captureIdentities.size() ? captureIdentities[faceIndex] : landingIdentity;
          face.display = facePair.display;
          face.unfiltered = facePair.unfiltered;
          face.width = descriptors[faceIndex].plan.width;
          face.height = descriptors[faceIndex].plan.height;
          face.workingSpace = facePair.working_space;
          face.floatingPoint = facePair.floating_point;
          face.viewport = box;
          face.extent = descriptors[faceIndex].view.extent;
          face.contentBounds = descriptors[faceIndex].contentBounds;
          fx_scene_source_view_framing_for_test(
              &descriptors[faceIndex].view, FX_SCENE_SOURCE_CONTENT, face.contentFraming.data()
          );
          fx_scene_source_view_framing_for_test(
              &descriptors[faceIndex].view, FX_SCENE_SOURCE_VIEWPORT, face.viewportFraming.data()
          );
        }
        if (landingMetadata) {
          auto& mixedFace = *std::ranges::find(metadata, landingIdentity, &WorkspaceSourceFace::identity);
          mixedFace.display = candidate->mixed.display;
          mixedFace.unfiltered = candidate->mixed.unfiltered;
          mixedFace.width = landingMetadata->width;
          mixedFace.height = landingMetadata->height;
        }
        const auto& pair = metadata[index];
        if (!preview) {
          pending = false;
          return true;
        }
        if (tree == nullptr) {
          tree = wlr_scene_tree_create(&server.scene()->tree);
          if (tree == nullptr) {
            return false;
          }
          wlr_scene_node_set_enabled(&tree->node, false);
          wlr_scene_node_place_above(&tree->node, &server.pinnedTree()->node);
          wlr_scene_node_set_position(&tree->node, box.x, box.y);
          picture = wlr_scene_buffer_create(tree, pair.display);
          if (picture == nullptr) {
            return false;
          }
          wlr_scene_buffer_set_dest_size(picture, box.width, box.height);
        } else {
          wlr_scene_buffer_set_buffer(picture, pair.display);
        }
        if (pair.workingSpace) {
          wlr_scene_buffer_set_transfer_function(picture, WLR_COLOR_TRANSFER_FUNCTION_EXT_LINEAR);
          wlr_scene_buffer_set_primaries(picture, WLR_COLOR_NAMED_PRIMARIES_SRGB);
        }
        if (!fx_scene_output_replace_range_for_test(
                output.sceneOutput(), descriptors[0].view.first, descriptors[0].view.last
            )
            || !fx_scene_output_bind_replacement_roles_for_test(output.sceneOutput(), picture, pair.unfiltered)) {
          return false;
        }
        wlr_scene_node_set_enabled(&tree->node, true);
        pending = false;
        return true;
      }
      fallback = PresentationFallback::ResourceBudget;
      return false;
    }
  };

  WorkspaceSources::WorkspaceSources(
      Server& server, Output& output, std::function<void(PresentationFallback)> invalidated, bool previewForTest
  )
      : m_state(std::make_unique<State>(*this, std::move(invalidated), previewForTest, server, output)) {}
  WorkspaceSources::~WorkspaceSources() {
    cancel(PresentationFallback::OutputRemoved);
    ordinaryCommitSucceeded();
  }
  bool WorkspaceSources::open(std::string_view identity) {
    auto& state = *m_state;
    if (state.inventory || state.restorePending) {
      return false;
    }
    state.inventory = state.output.workspaceGroup()->holdPresentationInventory([this] {
      cancel(PresentationFallback::TopologyChanged);
      if (m_state->invalidated) {
        m_state->invalidated(PresentationFallback::TopologyChanged);
      }
    });
    if (!state.inventory) {
      return false;
    }
    if (std::ranges::find(state.inventory->identities(), identity) == state.inventory->identities().end()) {
      state.inventory.reset();
      return false;
    }
#ifdef UMBRIEL_TEST_IPC
    if (state.preview && state.output.presentationProbeActive()) {
      state.inventory.reset();
      return false;
    }
#endif
    // Reserve dismissal ownership during preparation too. No displaced image is
    // published until normal layout/configure readiness and all sources succeed.
#ifdef UMBRIEL_TEST_IPC
    if (state.preview) {
      state.inputOwned = state.output.beginSceneInput([this] { cancel(PresentationFallback::InputDismissal); });
      if (!state.inputOwned) {
        state.inventory.reset();
        return false;
      }
    }
#endif
    if (!state.output.registerWorkspaceSources(this)) {
      if (state.inputOwned) {
        state.output.endSceneInput(false);
        state.inputOwned = false;
      }
      state.inventory.reset();
      return false;
    }
    state.selected = identity;
    state.captureIdentities = state.inventory->identities();
    state.pairOnly = false;
    state.visible = {std::string(identity)};
    state.box = state.output.layoutBox();
    state.fallback = PresentationFallback::None;
    state.pending = true;
    state.preparationTimeout = wl_event_loop_add_timer(
        wl_display_get_event_loop(state.server.display()),
        [](void* data) {
          auto& state = *static_cast<State*>(data);
          auto invalidated = state.invalidated;
          state.owner.cancel(PresentationFallback::SourceUnavailable);
          if (invalidated) {
            invalidated(PresentationFallback::SourceUnavailable);
          }
          return 0;
        },
        &state
    );
    if (state.preparationTimeout == nullptr || wl_event_source_timer_update(state.preparationTimeout, 2000) != 0) {
      cancel(PresentationFallback::SourceUnavailable);
      return false;
    }
    ++state.revision;
    for (const auto& view : state.server.views()) {
      if (view->mapped() && view->animatesOn(&state.output) && (view->workspace() != nullptr || view->pinned())) {
        state.owners.push_back(view.get());
        view->addPresentationSourceOccurrence();
      }
    }
    state.watchSurfaces();
    state.registerAudio();
    wlr_output_lock_attach_render(state.output.wlr(), true);
    state.renderLocked = true;
    wlr_output_schedule_frame(state.output.wlr());
    return true;
  }
  bool WorkspaceSources::begin(std::string_view identity) { return open(identity); }
  bool WorkspaceSources::beginPair(std::string_view from, std::string_view to) {
    if (from == to || !open(from)) {
      return false;
    }
    auto& state = *m_state;
    if (!std::ranges::contains(state.inventory->identities(), to)) {
      cancel(PresentationFallback::SourceUnavailable);
      return false;
    }
    state.captureIdentities = {std::string(from), std::string(to)};
    state.pairOnly = true;
    state.visible = state.captureIdentities;
    state.registerAudio();
    return true;
  }
  bool WorkspaceSources::commitSelection(std::string_view identity) {
    return m_state->inventory && m_state->inventory->commitSelection(identity);
  }
  bool WorkspaceSources::activateSelection(std::string_view identity) {
    return m_state->inventory && m_state->inventory->activateSelection(identity);
  }
  bool WorkspaceSources::freezeOutgoing() {
    auto& state = *m_state;
    if (!state.pairOnly || state.frozen || !state.inventory) {
      return false;
    }
    const auto& identity = state.captureIdentities.front();
    const size_t index = static_cast<size_t>(
        std::ranges::find(state.inventory->identities(), identity) - state.inventory->identities().begin()
    );
    auto descriptor = state.describe(index, state.output.wlr()->scale);
    descriptor.view.roots = descriptor.roots.data();
    descriptor.view.root_count = descriptor.roots.size();
    if (!fx_scene_source_view_plan_for_test(state.output.sceneOutput(), &descriptor.view, &descriptor.plan)) {
      return false;
    }
    // The outgoing endpoint is already the authoritative native workspace.
    // Replay its committed feedback provenance; a fresh virtual history would
    // visibly restart it at progress zero before the workspace even moves.
    const bool replayHistory = descriptor.plan.history_bytes != 0;
    const uint64_t captureBytes = replayHistory
        ? fx_scene_source_frozen_pair_bytes(state.output.sceneOutput(), descriptor.view.first, descriptor.view.last)
        : descriptor.plan.total_bytes;
    if (captureBytes == 0) {
      state.fallback = PresentationFallback::ResourceBudget;
      return false;
    }
    auto frozen = std::make_unique<State::Sources>();
    if (!fx_scene_reserve(&frozen->reservation, &state.pool, &presentationAggregatePool(), captureBytes)) {
      state.fallback = PresentationFallback::ResourceBudget;
      return false;
    }
    frozen->pairs.resize(1);
    state.server.effects().bindSourceAudio(&state);
    const bool captured = replayHistory
        ? fx_scene_source_pair_capture_for_test(
              state.output.sceneOutput(), descriptor.view.first, descriptor.view.last, captureBytes,
              &frozen->pairs.front()
          )
        : fx_scene_source_view_pair_capture_for_test(
              state.output.sceneOutput(), &descriptor.view, captureBytes, &frozen->pairs.front()
          );
    if (!captured) {
      return false;
    }
    const auto& pair = frozen->pairs.front();
    const uint64_t retainedBytes = replayHistory ? static_cast<uint64_t>(pair.display->width)
                * pair.display->height
                * (pair.floating_point ? 8 : 4)
                * (pair.display == pair.unfiltered ? 1 : 2)
            + 4096
                                                 : descriptor.plan.retained_bytes;
    fx_scene_release(&frozen->reservation);
    if (!fx_scene_reserve(&frozen->reservation, &state.pool, &presentationAggregatePool(), retainedBytes)) {
      return false;
    }
    frozen->retained = retainedBytes;
    state.frozenMetadata = {
        .identity = identity,
        .display = pair.display,
        .unfiltered = pair.unfiltered,
        .width = descriptor.plan.width,
        .height = descriptor.plan.height,
        .workingSpace = pair.working_space,
        .floatingPoint = pair.floating_point,
        .viewport = state.box,
        .contentBounds = state.box,
        .extent = state.box,
        .contentFraming = {1, 1, 0, 0},
        .viewportFraming = {1, 1, 0, 0}
    };
    state.frozen = std::move(frozen);
    ++state.captures;
    const std::array<std::string, 1> destination{state.captureIdentities.back()};
    return setVisibleWorkspaces(destination);
  }
  bool WorkspaceSources::setVisibleWorkspaces(std::span<const std::string> identities) {
    auto& state = *m_state;
    if (!state.inventory || identities.empty() || identities.size() > state.inventory->identities().size()) {
      return false;
    }
    std::unordered_set<std::string> unique;
    for (const auto& identity : identities) {
      if (!unique.insert(identity).second || !std::ranges::contains(state.captureIdentities, identity)) {
        return false;
      }
    }
    std::vector<std::string> next(identities.begin(), identities.end());
    if (state.candidate) {
      state.queuedVisible = std::move(next);
      return true;
    }
    if (state.visible != next) {
      state.visible = std::move(next);
      state.selected = state.visible.front();
      ++state.revision;
      state.registerAudio();
      wlr_output_schedule_frame(state.output.wlr());
    }
    return true;
  }
  bool WorkspaceSources::select(std::string_view identity) {
    const std::array<std::string, 1> selected{std::string(identity)};
    return setVisibleWorkspaces(selected);
  }
  WorkspaceSourceResult WorkspaceSources::prepareFrame(bool animate) {
    tick(animate);
    if (!m_state->inventory) {
      return WorkspaceSourceResult::Failed;
    }
    return m_state->pending ? WorkspaceSourceResult::Preparing : WorkspaceSourceResult::Ready;
  }
  std::vector<WorkspaceSourceFace> WorkspaceSources::faces() const { return m_state->metadata; }
  std::optional<WorkspaceSources::Pick> WorkspaceSources::pick(size_t ordinal, float u, float v) const {
    const auto& state = *m_state;
    if (!state.current
        || ordinal >= state.current->hits.size()
        || !std::isfinite(u)
        || !std::isfinite(v)
        || u < 0
        || v < 0
        || u > 1
        || v > 1)
      return std::nullopt;
    const auto& face = state.current->hits[ordinal];
    const double x = u * face.width, y = v * face.height;
    for (auto hit = face.hits.rbegin(); hit != face.hits.rend(); ++hit) {
      const auto& box = hit->box;
      if (x >= box.x && y >= box.y && x < box.x + box.width && y < box.y + box.height)
        return Pick{face.identity, hit->window};
    }
    return Pick{face.identity, {}};
  }
  bool WorkspaceSources::requestLanding(std::string_view identity) {
    auto& state = *m_state;
    if (!state.inventory || !std::ranges::contains(state.captureIdentities, identity)) {
      return false;
    }
    if (state.landingIdentity == identity) {
      return true;
    }
    if (state.candidate) {
      return false;
    }
    if (state.landingIdentity != identity) {
      state.landingIdentity = identity;
      state.landingMetadata.reset();
      contentChanged();
    }
    return true;
  }
  bool WorkspaceSources::setFramingProgress(float progress) {
    if (!std::isfinite(progress) || progress < 0 || progress > 1 || !m_state->inventory) {
      return false;
    }
    if (m_state->framingProgress != progress) {
      m_state->framingProgress = progress;
      contentChanged();
    }
    return true;
  }
  bool WorkspaceSources::setLandingMix(float weight) {
    if (!std::isfinite(weight) || weight < 0 || weight > 1 || m_state->landingIdentity.empty()) {
      return false;
    }
    if (m_state->landingMix != weight) {
      m_state->landingMix = weight;
      contentChanged();
    }
    return true;
  }
  std::optional<WorkspaceSourceFace> WorkspaceSources::landing() const { return m_state->landingMetadata; }
  void WorkspaceSources::frameSubmitted(bool success) {
    if (success) {
      frameCommitted();
    }
  }
  fx_scene_resource_pool& WorkspaceSources::resourcePool() { return m_state->pool; }
  uint64_t WorkspaceSources::reservedBytes() const { return m_state->pool.used; }
  uint64_t WorkspaceSources::revision() const { return m_state->capturedRevision; }
  PresentationFallback WorkspaceSources::lastFallback() const { return m_state->fallback; }
  void WorkspaceSources::cancel(PresentationFallback reason) {
    auto& state = *m_state;
    if (!state.inventory && !state.tree && !state.inputOwned) {
      return;
    }
    state.fallback = reason;
    if (state.preparationTimeout != nullptr) {
      wl_event_source_remove(state.preparationTimeout);
      state.preparationTimeout = nullptr;
    }
    state.pending = false;
    state.queuedVisible.clear();
    state.visible.clear();
    state.captureIdentities.clear();
    state.pairOnly = false;
    state.metadata.clear();
    state.landingIdentity.clear();
    state.framingProgress = 0;
    state.landingMix = 0;
    state.landingMetadata.reset();
    state.server.effects().clearSourceOccurrences(&state);
    if (state.tree != nullptr) {
      state.restorePending = true;
      wlr_scene_node_set_enabled(&state.tree->node, false);
      fx_scene_output_replace_range_for_test(state.output.sceneOutput(), nullptr, nullptr);
      wlr_scene_node_destroy(&state.tree->node);
      state.tree = nullptr;
      state.picture = nullptr;
    }
    if (state.inputOwned) {
      state.output.endSceneInput(state.restorePending);
      state.inputOwned = false;
    }
    state.watches.clear();
    for (View* view : state.owners) {
      view->removePresentationSourceOccurrence();
    }
    state.owners.clear();
    state.candidate.reset();
    state.current.reset();
    state.frozen.reset();
    state.histories.clear();
    state.frozenMetadata = {};
    state.inventory.reset();
    state.callbacksPending = false;
    if (state.renderLocked) {
      wlr_output_lock_attach_render(state.output.wlr(), false);
      state.renderLocked = false;
    }
    state.output.unregisterWorkspaceSources(this);
    wlr_output_schedule_frame(state.output.wlr());
  }
  void WorkspaceSources::viewMapped(View& view) {
    auto& state = *m_state;
    if (!state.inventory
        || !view.mapped()
        || !view.animatesOn(&state.output)
        || (view.workspace() == nullptr && !view.pinned())
        || std::ranges::contains(state.owners, &view)) {
      return;
    }
    state.owners.push_back(&view);
    view.addPresentationSourceOccurrence();
    state.watchSurfaces();
    state.registerAudio();
    contentChanged();
  }
  void WorkspaceSources::viewWillUnmap(View& view) {
    auto& state = *m_state;
    if (!std::ranges::contains(state.owners, &view)) {
      return;
    }
    std::erase(state.owners, &view);
    view.removePresentationSourceOccurrence();
    // Drop every listener before the native unmap can destroy a surface; the
    // retained paired images own their buffers independently of this View.
    state.watchSurfaces();
    state.registerAudio();
    contentChanged();
  }
  void WorkspaceSources::contentChanged() {
    if (m_state->inventory) {
      ++m_state->revision;
      wlr_output_schedule_frame(m_state->output.wlr());
    }
  }
  void WorkspaceSources::tick(bool animate) {
    auto& state = *m_state;
    if (!state.inventory) {
      return;
    }
    const auto box = state.output.layoutBox();
    if (state.server.sessionLocked()
        || box.x != state.box.x
        || box.y != state.box.y
        || box.width != state.box.width
        || box.height != state.box.height) {
      cancel(state.server.sessionLocked() ? PresentationFallback::Locked : PresentationFallback::TopologyChanged);
      return;
    }
    if (state.candidate) {
      return; // Retry precisely the captured revision until successful output submission.
    }
    for (const auto* view : state.owners) {
      if (state.pairOnly
          && !view->pinned()
          && (view->workspace() == nullptr || !std::ranges::contains(state.visible, view->workspace()->id()))) {
        continue;
      }
      const auto* surface = view->toplevel()->base;
      if (surface->configure_idle != nullptr || surface->current.configure_serial != surface->scheduled_serial) {
        return;
      }
    }
    if (state.preparationTimeout != nullptr) {
      wl_event_source_remove(state.preparationTimeout);
      state.preparationTimeout = nullptr;
    }
    const auto audioRevision = state.server.effects().audioInputRevision(&state.output);
    if (animate || audioRevision != state.audioRevision) {
      state.audioRevision = audioRevision;
      ++state.revision;
    }
    if (state.pending || state.revision != state.committedRevision) {
      if (!state.refresh()) {
        cancel(
            state.fallback == PresentationFallback::ResourceBudget ? state.fallback
                                                                   : PresentationFallback::SourceUnavailable
        );
      }
    }
  }
  void WorkspaceSources::frameCommitted() {
    auto& state = *m_state;
    if (state.candidate) {
      for (auto& history : state.histories) {
        history->finish(true);
      }
      state.current = std::move(state.candidate);
      state.committedRevision = state.capturedRevision;
      state.callbacksPending = true;
      if (state.revision != state.committedRevision) {
        wlr_output_schedule_frame(state.output.wlr());
      }
    }
  }
  void WorkspaceSources::sendFrameDone(const timespec& when) {
    auto& state = *m_state;
    if (!state.callbacksPending || !state.inventory) {
      return;
    }
    state.callbacksPending = false;
    std::unordered_set<wlr_surface*> sent;
    struct Delivery {
      State& state;
      const timespec& when;
      std::unordered_set<wlr_surface*>& sent;
    } delivery{state, when, sent};
    for (View* view : state.owners) {
      if (!view->pinned() && !std::ranges::contains(state.visible, view->workspace()->id())) {
        continue; // Only contributors to the displayed face are paced.
      }
      wlr_xdg_surface_for_each_surface(
          view->toplevel()->base,
          [](wlr_surface* surface, int, int, void* data) {
            auto& delivery = *static_cast<Delivery*>(data);
            if (delivery.sent.insert(surface).second && !wl_list_empty(&surface->current.frame_callback_list)) {
              wlr_surface_send_frame_done(surface, &delivery.when);
              ++delivery.state.callbacks;
            }
          },
          &delivery
      );
    }
    // Shared captured layer bands have native membership, but their ordinary
    // callbacks are suppressed by replacement just like workspace windows.
    wlr_scene_frame_done_event event{.output = state.output.sceneOutput(), .when = when};
    for (uint32_t layer = 0; layer < 3; ++layer) {
      wlr_scene_node_for_each_buffer(
          &state.server.shellLayerTree(layer)->node,
          [](wlr_scene_buffer* buffer, int, int, void* data) {
            wlr_scene_buffer_send_frame_done(buffer, static_cast<wlr_scene_frame_done_event*>(data));
          },
          &event
      );
    }
    if (!state.queuedVisible.empty()) {
      state.visible = std::move(state.queuedVisible);
      state.queuedVisible.clear();
      state.selected = state.visible.front();
      ++state.revision;
      state.registerAudio();
      wlr_output_schedule_frame(state.output.wlr());
    }
  }

  void WorkspaceSources::ordinaryCommitSucceeded() {
    if (m_state->restorePending && !active()) {
      m_state->restorePending = false;
      m_state->output.sceneRestoreCommitted();
    }
  }
  bool WorkspaceSources::active() const { return m_state->inventory && (m_state->candidate || m_state->current); }
  bool WorkspaceSources::preparing() const { return m_state->pending; }
  bool WorkspaceSources::renderLocked() const { return m_state->renderLocked; }
  nlohmann::json WorkspaceSources::status() const {
    const auto& state = *m_state;
    nlohmann::json faces = nlohmann::json::array();
    const auto boxJSON = [](const wlr_box& box) {
      return nlohmann::json::array({box.x, box.y, box.width, box.height});
    };
    for (const auto& face : state.metadata) {
      faces.push_back(
          {{"identity", face.identity},
           {"viewport", boxJSON(face.viewport)},
           {"content_bounds", boxJSON(face.contentBounds)},
           {"extent", boxJSON(face.extent)},
           {"content_framing", face.contentFraming},
           {"width", face.width},
           {"height", face.height}}
      );
    }
    return {
        {"faces", std::move(faces)},
        {"framing_progress", state.framingProgress},
        {"landing_mix", state.landingMix},
        {"active", active()},
        {"preparing", preparing()},
        {"selected", state.selected},
        {"ids", state.inventory ? state.inventory->identities() : std::vector<std::string>{}},
        {"capture_ids", state.captureIdentities},
        {"pair_only", state.pairOnly},
        {"revision", state.revision},
        {"captured_revision", state.capturedRevision},
        {"committed_revision", state.committedRevision},
        {"reserved_bytes", state.pool.used},
        {"retained_bytes", state.current ? state.current->retained : 0},
        {"face_width", state.metadata.empty() ? 0 : state.metadata.front().width},
        {"face_height", state.metadata.empty() ? 0 : state.metadata.front().height},
        {"landing_width", state.landingMetadata ? state.landingMetadata->width : 0},
        {"landing_height", state.landingMetadata ? state.landingMetadata->height : 0},
        {"captures", state.captures},
        {"callbacks", state.callbacks},
        {"restore_pending", state.restorePending},
        {"fallback", static_cast<int>(state.fallback)}
    };
  }
#ifdef UMBRIEL_TEST_IPC
  nlohmann::json Output::workspaceSourceProbe(std::string_view argument) {
    if (!m_workspaceSourceProbe) {
      m_workspaceSourceProbe =
          std::make_unique<WorkspaceSources>(*m_server, *this, std::function<void(PresentationFallback)>{}, true);
    }
    std::istringstream input{std::string(argument)};
    std::string action, identity;
    input >> action >> identity;
    if ((action == "open" && !m_workspaceSourceProbe->open(identity))
        || (action == "select" && !m_workspaceSourceProbe->select(identity))) {
      return {{"err", "workspace source admission rejected"}, {"state", m_workspaceSourceProbe->status()}};
    }
    if (action == "move-view") {
      std::string destination;
      input >> destination;
      View* moving = nullptr;
      Workspace* target = nullptr;
      for (const auto& view : m_server->views()) {
        if (view->mapped() && view->extForeignIdentifier() != nullptr && identity == view->extForeignIdentifier()) {
          moving = view.get();
        }
      }
      for (size_t i = 0; i < m_workspaceGroup->workspaceCount(); ++i) {
        auto* workspace = m_workspaceGroup->workspaceAt(i);
        if (workspace->id() == destination) {
          target = workspace;
        }
      }
      if (moving == nullptr || target == nullptr) {
        return {{"err", "invalid source move owner or workspace"}};
      }
      moving->setWorkspace(target, true, LayoutAttachOrigin::MovedView);
    } else if (action == "landing") {
      if (!m_workspaceSourceProbe->requestLanding(identity)) {
        return {{"err", "native landing request rejected"}};
      }
    } else if (action == "framing" || action == "mix") {
      float value = 0;
      std::istringstream number(identity);
      if (!(number >> value)
          || (action == "framing" ? !m_workspaceSourceProbe->setFramingProgress(value)
                                  : !m_workspaceSourceProbe->setLandingMix(value))) {
        return {{"err", "invalid source framing or landing blend"}};
      }
    } else if (action == "commit") {
      if (!m_workspaceSourceProbe->commitSelection(identity)) {
        return {{"err", "native destination commit rejected"}};
      }
      m_workspaceSourceProbe->cancel(PresentationFallback::None);
    } else if (action == "cancel") {
      m_workspaceSourceProbe->cancel(PresentationFallback::BindingRemoved);
    } else if (action != "status" && action != "open" && action != "select") {
      return {{"err", "expected open ID, select ID, landing ID, move-view VIEW WORKSPACE, cancel or status"}};
    }
    return {{"ok", m_workspaceSourceProbe->status()}};
  }

#endif
} // namespace umbriel

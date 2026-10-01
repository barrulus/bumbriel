#include "check.h"
#include "scene/composition.h"
#include "wlr.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

extern "C" {
#include "../../umbrielfx/internal/render/fx_renderer/scene_program.h"

#include <umbrielfx/render/effect.h>
}

// Exercise ownership and final-submit arbitration independently of the GPU
// pixel contracts in UmbrielFX's scene-pair/set tests.
struct fx_scene_target {
  bool prepared = false;
  wlr_buffer* buffer = nullptr;
};
struct fx_scene_picker {};

namespace {
  unsigned buffers = 0;
  unsigned targets = 0;
  unsigned textures = 0;
  unsigned renders = 0;
  unsigned allocations = 0;
  unsigned failAllocation = 0;
  unsigned shadowPrepares = 0, lightPrepares = 0, shares = 0, failShare = 0;
  int failRole = -1;
  std::array<fx_scene_frame, 2> rendered{};
  unsigned pickers = 0, picks = 0;
  bool failPicker = false;
  fx_scene_pick_status pickStatus = FX_SCENE_PICK_HIT;
  fx_scene_frame pickedFrame{};
  fx_scene_item pickedItem{};
  std::array<float, 9> pickedMatrix{};
  wlr_buffer* pickedSource = nullptr;
  wlr_buffer* pickedTarget = nullptr;
  std::unordered_map<wlr_texture*, wlr_buffer*> textureSources;

  struct Fixture {
    wlr_renderer renderer{};
    wlr_allocator allocator{};
    fx_scene_resource_pool output{FX_SCENE_OUTPUT_BUDGET, 0};
    fx_scene_resource_pool aggregate{FX_SCENE_TOTAL_BUDGET, 0};
    std::shared_ptr<umbriel::scene_experiment::ProgramBundle> bundle =
        std::make_shared<umbriel::scene_experiment::ProgramBundle>();
    Fixture() {
      CHECK_EQ(buffers, 0U);
      CHECK_EQ(targets, 0U);
      CHECK_EQ(textures, 0U);
      CHECK_EQ(pickers, 0U);
      CHECK(textureSources.empty());
      allocations = renders = 0;
      shadowPrepares = lightPrepares = shares = failShare = 0;
      failAllocation = 0;
      failRole = -1;
      picks = 0;
      failPicker = false;
      pickStatus = FX_SCENE_PICK_HIT;
      pickedSource = pickedTarget = nullptr;
      wl_signal_init(&renderer.events.destroy);
      bundle->program = std::shared_ptr<fx_scene_program>(reinterpret_cast<fx_scene_program*>(this), [](auto*) {});
    }
    ~Fixture() {
      CHECK_EQ(buffers, 0U);
      CHECK_EQ(targets, 0U);
      CHECK_EQ(textures, 0U);
      CHECK_EQ(pickers, 0U);
      CHECK(textureSources.empty());
      CHECK_EQ(output.used, 0U);
      CHECK_EQ(aggregate.used, 0U);
    }
    std::unique_ptr<umbriel::SceneComposition> create() {
      return umbriel::SceneComposition::create(&renderer, &allocator, output, aggregate, bundle, 64, 48, false);
    }
  };
} // namespace

extern "C" {
bool __wrap_fx_scene_program_get_limits(wlr_renderer*, fx_scene_limits* limits) {
  *limits = {8192, 256, 256, 8};
  return true;
}
wlr_buffer* __wrap_fx_scene_buffer_create(wlr_renderer*, wlr_allocator*, int width, int height, bool) {
  if (++allocations == failAllocation) {
    return nullptr;
  }
  auto* buffer = new wlr_buffer{};
  buffer->width = width;
  buffer->height = height;
  ++buffers;
  return buffer;
}
void __wrap_wlr_buffer_drop(wlr_buffer* buffer) {
  if (buffer != nullptr) {
    --buffers;
    delete buffer;
  }
}
fx_scene_target* __wrap_fx_scene_target_create_with_color(wlr_renderer*, wlr_buffer* buffer, bool, bool) {
  ++targets;
  return new fx_scene_target{.buffer = buffer};
}
uint64_t __wrap_fx_scene_target_shadow_bytes(const fx_scene_target*, unsigned) { return 300; }
uint64_t __wrap_fx_scene_target_light_bytes(const fx_scene_target*, unsigned, const fx_effect_light*, unsigned, float) {
  return 500;
}
bool __wrap_fx_scene_target_prepare_shadow(fx_scene_target* target, unsigned) {
  ++shadowPrepares;
  target->prepared = true;
  return true;
}
bool __wrap_fx_scene_target_prepare_light(fx_scene_target* target, unsigned, const fx_effect_light*, unsigned, float) {
  ++lightPrepares;
  target->prepared = true;
  return true;
}
bool __wrap_fx_scene_target_share_scratch(fx_scene_target* target, fx_scene_target* owner) {
  CHECK(owner->prepared);
  CHECK(!target->prepared);
  if (++shares == failShare)
    return false;
  target->prepared = true;
  return true;
}
void __wrap_fx_scene_target_destroy(fx_scene_target* target) {
  if (target != nullptr) {
    --targets;
    delete target;
  }
}
wlr_texture* __wrap_wlr_texture_from_buffer(wlr_renderer*, wlr_buffer* buffer) {
  ++textures;
  auto* texture = new wlr_texture{};
  textureSources.emplace(texture, buffer);
  return texture;
}
void __wrap_wlr_texture_destroy(wlr_texture* texture) {
  CHECK_EQ(textureSources.erase(texture), 1U);
  --textures;
  delete texture;
}
bool __wrap_fx_scene_program_render(
    fx_scene_program*, fx_scene_target*, fx_scene_target*, const fx_scene_frame* frame, const fx_scene_input*,
    const fx_scene_draw*, unsigned
) {
  ++renders;
  rendered[static_cast<unsigned>(frame->role)] = *frame;
  return frame->role != failRole;
}
bool __wrap_fx_scene_program_supports_picking(const fx_scene_program* program) {
  const auto& fixture = *reinterpret_cast<const Fixture*>(program);
  return fixture.bundle->definition.sources.scope == umbriel::scene_experiment::Scope::WorkspaceSet
      && !fixture.bundle->definition.sources.stages[static_cast<size_t>(umbriel::scene_experiment::Stage::Composite)];
}
fx_scene_picker* __wrap_fx_scene_picker_create(wlr_renderer*) {
  if (failPicker)
    return nullptr;
  ++pickers;
  return new fx_scene_picker{};
}
void __wrap_fx_scene_picker_destroy(fx_scene_picker* picker) {
  if (!picker)
    return;
  --pickers;
  delete picker;
}
fx_scene_pick_status __wrap_fx_scene_program_pick(
    fx_scene_program*, fx_scene_picker*, fx_scene_target* target, const fx_scene_frame* frame,
    const fx_scene_draw* draws, unsigned count, float, float, fx_scene_pick* result
) {
  ++picks;
  CHECK_EQ(count, 1U);
  pickedFrame = *frame;
  pickedItem = draws[0].item;
  CHECK(textureSources.contains(draws[0].input.texture));
  pickedSource = textureSources.at(draws[0].input.texture);
  pickedTarget = target->buffer;
  pickedMatrix = {};
  if (draws[0].input.sample_matrix)
    std::copy_n(draws[0].input.sample_matrix, 9, pickedMatrix.begin());
  *result = {.ordinal = 0, .token = draws[0].item.token, .uv = {0.25F, 0.75F}};
  return pickStatus;
}
}

UMBRIEL_TEST(sceneCompositionRetriesCompletedPairWithoutRelatching) {
  Fixture fixture;
  auto composition = fixture.create();
  CHECK(composition != nullptr);
  CHECK_EQ(buffers, 4U);
  CHECK_EQ(targets, 4U);
  CHECK_EQ(fixture.output.used, composition->reservedBytes());
  CHECK_EQ(fixture.aggregate.used, composition->reservedBytes());
  wlr_buffer sourceA{}, sourceB{};
  const std::array<umbriel::SceneComposition::Source, 2> sources{{{&sourceA, &sourceA}, {&sourceB, &sourceB}}};
  fx_scene_frame frame{};
  frame.progress = 0.25F;
  frame.audio_levels[3] = 0.8F;
  CHECK(composition->render(frame, sources));
  CHECK_EQ(renders, 2U);
  CHECK_EQ(rendered[0].audio_levels[3], rendered[1].audio_levels[3]);
  CHECK_EQ(textures, 0U);
  const auto pending = composition->candidate();
  CHECK(pending.display != nullptr);
  CHECK(pending.display != pending.unfiltered);
  CHECK(composition->committed().display == nullptr);
  composition->submitted(false);
  frame.progress = 0.75F;
  frame.audio_levels[3] = 0;
  CHECK(composition->render(frame, sources));
  CHECK_EQ(renders, 2U);
  CHECK_EQ(rendered[0].progress, 0.25F);
  CHECK(composition->candidate().display == pending.display);
  composition->submitted(true);
  CHECK(!composition->pending());
  CHECK(composition->committed().display == pending.display);
  CHECK(composition->render(frame, sources));
  CHECK_EQ(renders, 4U);
  CHECK(composition->candidate().display != pending.display);
  CHECK(composition->committed().display == pending.display);
}

UMBRIEL_TEST(sceneCompositionRoleFailureNeverPublishesPartialPair) {
  Fixture fixture;
  auto composition = fixture.create();
  CHECK(composition != nullptr);
  wlr_buffer source{};
  const std::array<umbriel::SceneComposition::Source, 2> sources{{{&source, &source}, {&source, &source}}};
  fx_scene_frame frame{};
  CHECK(composition->render(frame, sources));
  composition->submitted(true);
  const auto committed = composition->committed();
  failRole = 1;
  CHECK(!composition->render(frame, sources));
  CHECK(!composition->pending());
  CHECK(composition->candidate().display == nullptr);
  CHECK(composition->committed().display == committed.display);
  CHECK_EQ(textures, 0U);
  failRole = -1;
  CHECK(composition->render(frame, sources));
  wl_signal_emit_mutable(&fixture.renderer.events.destroy, &fixture.renderer);
  CHECK(!composition->render(frame, sources));
  CHECK(composition->candidate().display == nullptr);
  CHECK(composition->committed().display == nullptr);
}

UMBRIEL_TEST(sceneCompositionReservationAndAllocationFailAtomically) {
  Fixture fixture;
  fixture.output.limit = 1;
  CHECK(fixture.create() == nullptr);
  CHECK_EQ(allocations, 0U);
  CHECK_EQ(fixture.aggregate.used, 0U);
  fixture.output.limit = FX_SCENE_OUTPUT_BUDGET;
  for (unsigned index = 1; index <= 4; ++index) {
    allocations = 0;
    failAllocation = index;
    CHECK(fixture.create() == nullptr);
    CHECK_EQ(buffers, 0U);
    CHECK_EQ(targets, 0U);
    CHECK_EQ(fixture.output.used, 0U);
    CHECK_EQ(fixture.aggregate.used, 0U);
  }
}

UMBRIEL_TEST(sceneCompositionSharesOneCompanionReservationAcrossRolesAndVersions) {
  for (const bool light : {false, true}) {
    Fixture fixture;
    auto composition = fixture.create();
    const auto base = composition->reservedBytes();
    const std::array<fx_effect_light, 1> recipes{};
    const auto selected = light ? std::span<const fx_effect_light>(recipes) : std::span<const fx_effect_light>();
    CHECK(composition->prepareCompanions(32, light ? 48 : 0, selected, 1));
    CHECK_EQ(composition->reservedBytes(), base + (light ? 500 : 300));
    CHECK_EQ(fixture.output.used, composition->reservedBytes());
    CHECK_EQ(fixture.aggregate.used, composition->reservedBytes());
    CHECK_EQ(shadowPrepares, light ? 0U : 1U);
    CHECK_EQ(lightPrepares, light ? 1U : 0U);
    CHECK_EQ(shares, 3U);
  }
}

UMBRIEL_TEST(sceneCompositionCompanionFailureKeepsReservationUntilTargetsDie) {
  Fixture fixture;
  auto composition = fixture.create();
  const auto base = composition->reservedBytes();
  fixture.output.limit = base + 299;
  CHECK(!composition->prepareCompanions(32, 0, {}, 1));
  CHECK_EQ(shadowPrepares, 0U);
  CHECK_EQ(fixture.output.used, base);
  fixture.output.limit = FX_SCENE_OUTPUT_BUDGET;
  failShare = 2;
  CHECK(!composition->prepareCompanions(32, 0, {}, 1));
  CHECK_EQ(fixture.output.used, base + 300);
  CHECK_EQ(fixture.aggregate.used, base + 300);
  composition.reset();
  CHECK_EQ(fixture.output.used, 0U);
  CHECK_EQ(fixture.aggregate.used, 0U);
}

UMBRIEL_TEST(sceneCompositionCompositeScratchIsSharedWhileCompletedOutputsStaySeparate) {
  Fixture fixture;
  fixture.bundle->definition.sources.stages[static_cast<size_t>(umbriel::scene_experiment::Stage::Composite)] =
      umbriel::ShaderSource{};
  auto composition = fixture.create();
  CHECK(composition != nullptr);
  CHECK_EQ(buffers, 5U);
  CHECK(composition->prepareCompanions(32, 0, {}, 1));
  CHECK_EQ(shadowPrepares, 1U);
  CHECK_EQ(shares, 0U);
  wlr_buffer source{};
  const std::array<umbriel::SceneComposition::Source, 2> sources{{{&source, &source}, {&source, &source}}};
  fx_scene_frame frame{};
  CHECK(composition->render(frame, sources));
  const auto first = composition->candidate();
  CHECK(first.display != first.unfiltered);
  composition->submitted(true);
  CHECK(composition->render(frame, sources));
  CHECK(composition->candidate().display != first.display);
  CHECK(composition->candidate().unfiltered != first.unfiltered);
  CHECK_EQ(buffers, 5U);
}

UMBRIEL_TEST(sceneCompositionPickingRetainsOnlySubmittedInputsAcrossRetry) {
  Fixture fixture;
  fixture.bundle->definition.sources.scope = umbriel::scene_experiment::Scope::WorkspaceSet;
  auto composition = fixture.create();
  CHECK(composition != nullptr);
  CHECK_EQ(pickers, 1U);
  wlr_buffer sourceA{}, sourceB{};
  std::array<float, 9> matrix{1, 0, 0, 0, 1, 0, 0.125F, 0.25F, 1};
  std::array<umbriel::SceneComposition::Source, 1> sources{{{&sourceA, &sourceA, matrix.data()}}};
  std::array<fx_scene_draw, 1> draws{};
  draws[0].item.token = 7;
  fx_scene_frame frame{};
  frame.scene_count = 1;
  frame.time = 12;
  frame.progress = 0.25F;
  frame.audio_levels[3] = 0.8F;
  CHECK(!composition->pick(32, 24));
  CHECK_EQ(picks, 0U);
  CHECK(composition->render(frame, sources, draws));
  CHECK_EQ(textures, 1U);
  CHECK(!composition->pick(32, 24));
  CHECK_EQ(picks, 0U);
  const auto first = composition->candidate();
  composition->submitted(true);
  // Caller storage is transient and can change immediately after rendering.
  matrix[6] = 0.75F;
  draws[0].item.token = 9;
  frame.time = 22;
  frame.audio_levels[3] = 0.1F;
  auto hit = composition->pick(32, 24);
  CHECK(hit.has_value());
  CHECK_EQ(hit->ordinal, 0U);
  CHECK_EQ(hit->u, 0.25F);
  CHECK_EQ(hit->v, 0.75F);
  CHECK_EQ(pickedFrame.role, 0);
  CHECK_EQ(pickedFrame.time, 12.0F);
  CHECK_EQ(pickedFrame.audio_levels[3], 0.8F);
  CHECK_EQ(pickedItem.token, 7);
  CHECK_EQ(pickedMatrix[6], 0.125F);
  CHECK(pickedSource == &sourceA);
  CHECK(pickedTarget == first.display);
  sources[0].display = sources[0].unfiltered = &sourceB;
  CHECK(composition->render(frame, sources, draws));
  CHECK_EQ(textures, 2U); // Committed and pending display imports remain alive.
  const auto second = composition->candidate();
  composition->submitted(false);
  CHECK(composition->pick(32, 24).has_value());
  CHECK_EQ(pickedFrame.time, 12.0F);
  CHECK(pickedSource == &sourceA);
  CHECK(pickedTarget == first.display);
  const auto previousRenders = renders;
  frame.time = 99;
  matrix[6] = 0.5F;
  CHECK(composition->render(frame, sources, draws));
  CHECK_EQ(renders, previousRenders);
  composition->submitted(true);
  CHECK_EQ(textures, 1U); // Previous committed input released only on success.
  CHECK(composition->pick(32, 24).has_value());
  CHECK_EQ(pickedFrame.time, 22.0F);
  CHECK_EQ(pickedItem.token, 9);
  CHECK_EQ(pickedMatrix[6], 0.75F);
  CHECK(pickedSource == &sourceB);
  CHECK(pickedTarget == second.display);
  CHECK_EQ(fixture.output.used, composition->reservedBytes());
}

UMBRIEL_TEST(sceneCompositionPickingRoleFailureAndRendererLossReleaseSnapshots) {
  Fixture fixture;
  fixture.bundle->definition.sources.scope = umbriel::scene_experiment::Scope::WorkspaceSet;
  auto composition = fixture.create();
  wlr_buffer source{};
  const std::array<umbriel::SceneComposition::Source, 1> sources{{{&source, &source}}};
  std::array<fx_scene_draw, 1> draws{};
  fx_scene_frame frame{};
  frame.scene_count = 1;
  frame.time = 1;
  CHECK(composition->render(frame, sources, draws));
  composition->submitted(true);
  failRole = 1;
  frame.time = 2;
  CHECK(!composition->render(frame, sources, draws));
  CHECK_EQ(textures, 1U);
  CHECK(composition->pick(32, 24).has_value());
  CHECK_EQ(pickedFrame.time, 1.0F);
  pickStatus = FX_SCENE_PICK_UNSUPPORTED;
  CHECK(!composition->pick(32, 24));
  pickStatus = FX_SCENE_PICK_MISS;
  CHECK(!composition->pick(32, 24));
  const auto previousPicks = picks;
  CHECK(!composition->pick(NAN, 24));
  CHECK_EQ(picks, previousPicks);
  failRole = -1;
  CHECK(composition->render(frame, sources, draws));
  CHECK_EQ(textures, 2U);
  wl_signal_emit_mutable(&fixture.renderer.events.destroy, &fixture.renderer);
  CHECK_EQ(textures, 0U);
  CHECK(!composition->pick(32, 24));
  CHECK_EQ(picks, previousPicks);
  CHECK(!composition->render(frame, sources, draws));
}

UMBRIEL_TEST(sceneCompositionPickingAllocationFailureReleasesCompleteReservation) {
  Fixture fixture;
  fixture.bundle->definition.sources.scope = umbriel::scene_experiment::Scope::WorkspaceSet;
  failPicker = true;
  CHECK(fixture.create() == nullptr);
  CHECK_EQ(pickers, 0U);
  CHECK_EQ(allocations, 0U);
  CHECK_EQ(fixture.output.used, 0U);
  CHECK_EQ(fixture.aggregate.used, 0U);
  failPicker = false;
  fixture.bundle->definition.sources.stages[static_cast<size_t>(umbriel::scene_experiment::Stage::Composite)] =
      umbriel::ShaderSource{};
  auto composition = fixture.create();
  CHECK(composition != nullptr);
  CHECK_EQ(pickers, 0U);
  CHECK(!composition->pick(32, 24));
  CHECK_EQ(picks, 0U);
}

int main() { return RUN_TESTS(); }

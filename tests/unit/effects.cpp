#include "config/effects.h"

#include "check.h"
#include "config/effect_state.h"
#include "config/section.h"

#include <algorithm>
#include <fstream>
#include <unistd.h>

using namespace umbriel;

namespace {
  struct Fixture {
    std::vector<ConfigDiagnostic> diagnostics;
    std::vector<std::filesystem::path> watches;
    EffectLibrary read(std::string_view text) {
      diagnostics.clear();
      watches.clear();
      const auto table = toml::parse(text, std::string("/tmp/effects.toml"));
      return readEffectLibrary(table.get("effects"), diagnostics, watches);
    }
    bool error(std::string_view message = {}) const {
      return std::ranges::any_of(diagnostics, [&](const auto& diagnostic) {
        return diagnostic.severity == ConfigDiagnostic::Severity::Error && diagnostic.message.contains(message);
      });
    }
  };
  size_t index(EffectScope scope) { return static_cast<size_t>(scope); }
} // namespace

UMBRIEL_TEST(definitionsAreStrictAndNamesHaveNoBuiltinMeaning) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.off.content]
enabled = false
[effects."name.with.dots".screen]
passes = [{ builtin = "grayscale" }]
)");
  CHECK(!fixture.error());
  CHECK_EQ(library.size(), 2U);
  CHECK(!library.at("off").scopes[index(EffectScope::Content)]->enabled);
  for (const auto text : {
           "[effects.empty]",
           "[effects.bad.content]",
           "[effects.'bad name'.content]\nenabled=false",
           "[effects.bad.border]\npadding=20",
           "[effects.bad.content]\nenabled=false\npalette=true",
           "[effects.bad.content]\npasses=[]",
           "[effects.bad.content]\npasses=[{builtin='unknown'}]",
           "[effects.bad.content]\npasses=[{builtin='invert',shader='missing'}]",
           "[effects.bad.open]\npasses=[{builtin='invert'}]",
           "[effects.bad.border.outer]\npasses=[{builtin='invert'}]",
           "[effects.bad.screen]\npasses=[{builtin='invert',buffer=false}]",
           "[effects.bad.screen]\npasses=[{builtin='invert',params={amount=2}}]",
           "[effects.bad.screen]\npasses=[{builtin='temperature',params={kelvin=6500.0}}]",
           "[effects.bad.screen]\npasses=[{builtin='saturation',params={amount=inf}}]",
           "[effects.bad.screen]\nspeed=nan\npasses=[{builtin='invert'}]",
           "[effects.bad.screen]\nfocused_only=false\npasses=[{builtin='invert'}]",
           "[effects.bad.move]\nduration_ms=500\nenabled=false",
           "[effects.bad.open]\nanimated=false\npasses=[{builtin='invert'}]",
           "[effects.bad]\nchoose=['good']\n[effects.bad.content]\nenabled=false",
           "[effects.bad]\nchoose=[]",
           "[effects.bad]\nchoose=['bad']",
           "[effects.bad]\nchoose=['missing']",
           "[effects.bad]\nselection='random'",
       }) {
    fixture.read(text);
    CHECK(fixture.error());
  }
}

UMBRIEL_TEST(choicesHaveCompleteUniformScopeSetsAndApplicableFamilies) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.a.content]
enabled=false
[effects.b.content]
passes=[{builtin='invert'}]
[effects.favourites]
choose=['a','b']
selection='round_robin'
)");
  CHECK(!fixture.error());
  const EffectSelector selector{{"favourites"}, {}};
  validateEffectSelector(library, selector, EffectOwner::Layer, fixture.diagnostics);
  CHECK(!fixture.error());
  validateEffectSelector(library, selector, EffectOwner::Region, fixture.diagnostics);
  CHECK(fixture.error("applicable"));
  for (const auto text : {
           "[effects.a.content]\nenabled=false\n[effects.b.screen]\nenabled=false\n[effects.c]\nchoose=['a','b']",
           "[effects.a.content]\nenabled=false\n[effects.a.screen]\nenabled=false\n[effects.c]\nchoose=['a']",
           "[effects.a.content]\nenabled=false\n[effects.c]\nchoose=['a','a']",
           "[effects.a.content]\nenabled=false\n[effects.b]\nchoose=['a']\n[effects.c]\nchoose=['b']",
       }) {
    fixture.read(text);
    CHECK(fixture.error());
  }
}

UMBRIEL_TEST(resolutionReplacesLeavesAndRecordsWinningAndOverriddenOrigins) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.pair.border.inner]
passes=[{builtin='invert'}]
[effects.pair.border.outer]
enabled=false
[effects.external.border.inner]
enabled=false
[effects.external.screen]
passes=[{builtin='grayscale'}]
[effects.quiet.content]
enabled=false
)");
  CHECK(!fixture.error());
  std::vector<EffectSelector> selectors{
      {{"pair", "quiet"}, {"appearance.toml", 3, 1}}, {{"external"}, {"rules.toml", 9, 1}}
  };
  const auto resolved = resolveEffects(library, selectors, kWindowEffects);
  const auto& inner = resolved[index(EffectScope::BorderInner)];
  CHECK(inner.pipeline.has_value());
  CHECK(!inner.pipeline->enabled);
  CHECK_EQ(inner.source.effect, std::string("external"));
  CHECK_EQ(inner.source.assignment.file, std::string("rules.toml"));
  CHECK_EQ(inner.source.definition.file, std::string("/tmp/effects.toml"));
  CHECK_EQ(inner.overridden.size(), 1U);
  CHECK_EQ(inner.overridden.front().effect, std::string("pair"));
  CHECK(resolved[index(EffectScope::BorderOuter)].pipeline.has_value());
  CHECK(!resolved[index(EffectScope::Screen)].pipeline.has_value());
  selectors.push_back({{}, {"empty.toml", 1, 1}});
  selectors.push_back({{"quiet"}, {"window.toml", 1, 1}});
  const auto cleared = resolveEffects(library, selectors, kWindowEffects);
  CHECK(!cleared[index(EffectScope::BorderInner)].pipeline);
  CHECK(cleared[index(EffectScope::Content)].pipeline.has_value());
  CHECK_EQ(cleared[index(EffectScope::BorderInner)].overridden.size(), 2U);
}

UMBRIEL_TEST(resolutionDoesNotAllocateAndKeepsNamedVariants) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.a.content]
enabled=false
[effects.b.content]
passes=[{builtin='invert'}]
[effects.pick]
choose=['a','b']
)");
  const std::array selectors{EffectSelector{{"pick"}, {"config.toml", 1, 1}}};
  CHECK(!resolveEffects(library, selectors, kWindowEffects)[0].pipeline);
  const auto resolved = resolveEffects(library, selectors, kWindowEffects, {{"pick", "b"}});
  CHECK(resolved[0].pipeline.has_value());
  CHECK_EQ(resolved[0].source.effect, std::string("b"));
  CHECK_EQ(resolved[0].source.choice, std::string("pick"));
}

UMBRIEL_TEST(shaderSourcesAndParametersRetainTheirTypesAndWatchFailures) {
  Fixture fixture;
  const auto path = std::filesystem::path("/tmp") / ("umbriel-effect-" + std::to_string(getpid()));
  {
    std::ofstream stream(path);
    stream << "uniform vec3 color; vec4 postprocess(vec3 p) { return vec4(color,1.0); }";
  }
  auto library = fixture.read(
      "[effects.test.content]\npasses=[{shader='"
      + path.filename().string()
      + "', params={color=[1,0.5,0], flag=true, count=2, amount=0.5}}]"
  );
  CHECK(!fixture.error());
  CHECK(fixture.watches == std::vector{path});
  const auto& pass = library.at("test").scopes[0]->passes[0];
  CHECK(std::holds_alternative<bool>(pass.params.at("flag")));
  CHECK(std::holds_alternative<int32_t>(pass.params.at("count")));
  CHECK(std::holds_alternative<float>(pass.params.at("amount")));
  CHECK_EQ(std::get<std::vector<EffectNumber>>(pass.params.at("color")).size(), 3U);
  for (const auto value : {"[1]", "[1,2,3,4,5]", "[true,false]", "'text'", "{nested=1}", "inf", "2147483648"}) {
    fixture.read(
        "[effects.test.content]\npasses=[{shader='" + path.filename().string() + "',params={bad=" + value + "}}]"
    );
    CHECK(fixture.error("parameter"));
  }
  fixture.read("[effects.test.content]\npasses=[{shader='" + path.filename().string() + "',params={umbriel_time=1}}]");
  CHECK(fixture.error("reserved"));
  std::filesystem::remove(path);
  fixture.read("[effects.test.content]\npasses=[{shader='" + path.filename().string() + "'}]");
  CHECK(fixture.error("cannot read"));
  CHECK(fixture.watches == std::vector{path});
}

UMBRIEL_TEST(regionSelectorsRequireIdentityGeometryAndExplicitEffects) {
  const auto table = toml::parse(
      R"(
[[effect_region]]
name='reading'
width=100
height=200
effects=[]
[[effect_region]]
name='reading'
width=0
)",
      std::string("/tmp/regions.toml")
  );
  std::vector<ConfigDiagnostic> diagnostics;
  const auto regions = readEffectRegions(table.get("effect_region"), diagnostics);
  CHECK_EQ(regions.size(), 2U);
  CHECK_EQ(regions[0].x, 0);
  CHECK(regions[0].effects.names.empty());
  CHECK_EQ(diagnostics.size(), 3U);
  CHECK(std::ranges::all_of(diagnostics, [](const auto& diagnostic) {
    return diagnostic.severity == ConfigDiagnostic::Severity::Error && diagnostic.file == "/tmp/regions.toml";
  }));
}

UMBRIEL_TEST(runtimeMasksRestoreConfigAndChoicesKeepDormantLeases) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.a.content]
passes=[{builtin='invert'}]
[effects.a.border.inner]
enabled=false
[effects.b.content]
passes=[{builtin='grayscale'}]
[effects.b.border.inner]
enabled=false
[effects.pick]
choose=['a','b']
selection='round_robin'
)");
  ShaderPoolAllocator allocator;
  EffectState first(EffectOwner::Window), second(EffectOwner::Window);
  const std::array input{EffectInput{{{"pick"}, {}}, kWindowEffects}};
  const auto content = effectBit(EffectScope::Content);
  first.resolve(library, input, allocator, kWindowEffects);
  second.resolve(library, input, allocator, kWindowEffects);
  CHECK_EQ(first.resolved()[0].source.effect, std::string("a"));
  CHECK_EQ(second.resolved()[0].source.effect, std::string("b"));
  first.off(content);
  first.resolve(library, input, allocator, kWindowEffects);
  CHECK_EQ(first.leases().size(), 1U);
  CHECK_EQ(first.resolved()[0].source.effect, std::string("a"));
  first.on(content);
  first.cycle(library, allocator, "pick", content);
  first.resolve(library, input, allocator, kWindowEffects);
  CHECK_EQ(first.resolved()[0].source.effect, std::string("b"));
  first.set({{"a"}, {}}, content);
  first.resolve(library, input, allocator, kWindowEffects);
  CHECK_EQ(first.resolved()[0].source.effect, std::string("a"));
  first.defaults(content);
  first.resolve(library, input, allocator, kWindowEffects);
  CHECK_EQ(first.resolved()[0].source.effect, std::string("b"));
  first.toggle(kWindowEffects);
  CHECK_EQ(first.disabled(), kWindowEffects);
  first.toggle(kWindowEffects);
  CHECK_EQ(first.disabled(), 0);
  const auto captured = first.resolved();
  first.release();
  CHECK(first.leases().empty());
  CHECK_EQ(captured[0].source.effect, std::string("b"));
}

UMBRIEL_TEST(overriddenChoicesDoNotReserveAndOwnerFamiliesAllocateIndependently) {
  Fixture fixture;
  const auto library = fixture.read(R"(
[effects.a.content]
enabled=false
[effects.b.content]
enabled=false
[effects.pick]
choose=['a','b']
)");
  ShaderPoolAllocator allocator;
  EffectState window(EffectOwner::Window), layer(EffectOwner::Layer);
  std::vector<EffectInput> inputs{{{{"pick"}, {}}, kAllEffects}, {{{"a"}, {}}, kAllEffects}};
  window.resolve(library, inputs, allocator, kWindowEffects);
  CHECK(window.leases().empty());
  inputs.pop_back();
  window.resolve(library, inputs, allocator, kWindowEffects);
  layer.resolve(library, inputs, allocator, kLayerEffects);
  CHECK_EQ(window.resolved()[0].source.effect, std::string("a"));
  CHECK_EQ(layer.resolved()[0].source.effect, std::string("a"));
}

int main() { return RUN_TESTS(); }

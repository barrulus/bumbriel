#include "config/scene_effects.h"

#include "check.h"
#include "config/section.h"

#include <cstdlib>
#include <fstream>
#include <limits>
#include <unistd.h>

namespace {
  using namespace umbriel::scene_experiment;

  class Fixture {
  public:
    Fixture() {
      char pattern[] = "/tmp/umbriel-scene-sources-XXXXXX";
      const char* created = mkdtemp(pattern);
      CHECK(created != nullptr);
      if (created == nullptr) {
        std::abort();
      }
      directory = created;
      std::filesystem::create_directory(directory / "included");
    }
    ~Fixture() { std::filesystem::remove_all(directory); }

    void write(std::string_view name, const std::string& content) const {
      std::ofstream stream(directory / "included" / name);
      stream << content;
      CHECK(stream.good());
    }

    SourceReadResult read(std::string_view text, Scope scope) {
      diagnostics.clear();
      const auto table = toml::parse(text, (directory / "included/preset.toml").string());
      umbriel::Section section(table, "experiment", diagnostics);
      return readSources(section, scope, diagnostics);
    }

    std::filesystem::path directory;
    std::vector<umbriel::ConfigDiagnostic> diagnostics;
  };
} // namespace

UMBRIEL_TEST(sceneScopeRejectsEveryWrongTrigger) {
  for (Scope scope : {Scope::WorkspacePair, Scope::WorkspaceSet, Scope::WindowScene}) {
    for (Binding binding :
         {Binding::WorkspaceSwitch, Binding::WindowOpen, Binding::WindowClose, Binding::Presentation, Binding::Other}) {
      const bool expected = (scope == Scope::WorkspacePair && binding == Binding::WorkspaceSwitch)
          || (scope == Scope::WorkspaceSet && binding == Binding::Presentation)
          || (scope == Scope::WindowScene && (binding == Binding::WindowOpen || binding == Binding::WindowClose));
      CHECK(acceptsBinding(scope, binding) == expected);
    }
  }
}

UMBRIEL_TEST(sceneSourceReadIsAtomicAndKeepsAllRepairWatches) {
  Fixture fixture;
  fixture.write("fragment.glsl", "fragment version one");
  fixture.write("common.glsl", "common version one");
  constexpr std::string_view declaration = R"(
common_shader = 'common.glsl'
vertex_shader = 'vertex.glsl'
shader = 'fragment.glsl'
composite_shader = 'composite.glsl'
backdrop_shader = 'backdrop.glsl'
)";
  const auto broken = fixture.read(declaration, Scope::WindowScene);
  CHECK(!broken.sources);
  CHECK_EQ(broken.watchPaths.size(), 5U);
  for (const auto& path : broken.watchPaths) {
    CHECK(path.parent_path() == fixture.directory / "included");
  }
  fixture.write("vertex.glsl", "vertex version one");
  fixture.write("composite.glsl", "composite version one");
  fixture.write("backdrop.glsl", "backdrop version one");
  const auto repaired = fixture.read(declaration, Scope::WindowScene);
  CHECK(repaired.sources.has_value());
  CHECK(fixture.diagnostics.empty());
  CHECK(repaired.watchPaths == broken.watchPaths);
  fixture.write("vertex.glsl", "vertex version two");
  const auto edited = fixture.read(declaration, Scope::WindowScene);
  CHECK(edited.sources.has_value());
  CHECK(edited.sources != repaired.sources);
  CHECK(repaired.sources->stages[static_cast<std::size_t>(Stage::Vertex)]->code == "vertex version one");
  fixture.write("composite.glsl", "   ");
  CHECK(!fixture.read(declaration, Scope::WindowScene).sources);
}

UMBRIEL_TEST(sceneSourcesEnforceRequiredStagesAndAggregateBudget) {
  Fixture fixture;
  fixture.write("source.glsl", "source");
  CHECK(fixture.read("shader = 'source.glsl'", Scope::WorkspacePair).sources.has_value());
  CHECK(!fixture.read("shader = 'source.glsl'", Scope::WorkspaceSet).sources);
  CHECK(!fixture.read("vertex_shader = 'source.glsl'", Scope::WorkspaceSet).sources);
  CHECK(!fixture.read("shader = 'source.glsl'\nvertex_shader = 'source.glsl'", Scope::WorkspacePair).sources);
  CHECK(!fixture.read("shader = 'source.glsl'\ncomposite_shader = 'source.glsl'", Scope::WorkspacePair).sources);
  CHECK(!fixture.read("shader = 'source.glsl'\nbackdrop_shader = 'source.glsl'", Scope::WorkspacePair).sources);
  fixture.write("source.glsl", std::string(umbriel::kShaderSourceLimit, 'x'));
  const auto boundary = fixture.read("shader = 'source.glsl'\nvertex_shader = 'source.glsl'", Scope::WorkspaceSet);
  CHECK(boundary.sources.has_value());
  CHECK_EQ(boundary.watchPaths.size(), 1U);
  CHECK(!fixture
             .read(
                 "shader = 'source.glsl'\nvertex_shader = 'source.glsl'\ncommon_shader = 'source.glsl'",
                 Scope::WorkspaceSet
             )
             .sources);
}

UMBRIEL_TEST(sceneParametersRejectReservedDuplicateAndInvalidValues) {
  std::array parameters{Parameter{.name = "strength", .components = 1, .values = {0.5F, 0, 0, 0}}};
  CHECK(!validateParameters(parameters));
  for (std::string_view invalid :
       {"", "1name", "a.b", "umbriel_time", "gl_Position", "_fx_wrapper", "name__reserved", "float", "uniform", "main",
        "transition", "transition_vertex", "transition_fragment", "transition_composite"}) {
    parameters[0].name = invalid;
    CHECK(validateParameters(parameters).has_value());
  }
  parameters[0].name = "strength";
  parameters[0].values[0] = std::numeric_limits<float>::quiet_NaN();
  CHECK(validateParameters(parameters).has_value());
  parameters[0].values[0] = 0;
  parameters[0].values[1] = 1;
  CHECK(validateParameters(parameters).has_value());
  parameters[0].values[1] = 0;
  parameters[0].components = 5;
  CHECK(validateParameters(parameters).has_value());
  parameters[0].components = 1;
  const std::array duplicates{parameters[0], parameters[0]};
  CHECK(validateParameters(duplicates).has_value());
  const std::vector<Parameter> excess(kParameterLimit + 1);
  CHECK(validateParameters(excess).has_value());
  parameters[0].name = std::string(kParameterNameLimit, 'a');
  CHECK(!validateParameters(parameters));
  parameters[0].name += 'a';
  CHECK(validateParameters(parameters).has_value());
}

UMBRIEL_TEST(sceneParameterTablesAreAtomicAndTyped) {
  std::vector<umbriel::ConfigDiagnostic> diagnostics;
  const auto read = [&](std::string_view text) {
    diagnostics.clear();
    const auto table = toml::parse(text);
    umbriel::Section section(table, "experiment", diagnostics);
    return readParameters(section);
  };
  const auto empty = read("");
  CHECK(empty && empty->empty());
  const auto values = read("[parameters]\nscalar = 2\nvector = [1.5, -2, 0, 4]\nsingle = [0.25]");
  CHECK(values && values->size() == 3);
  CHECK(diagnostics.empty());
  if (values && values->size() == 3) {
    CHECK((*values)[0] == (Parameter{.name = "scalar", .components = 1, .values = {2, 0, 0, 0}}));
    CHECK((*values)[1] == (Parameter{.name = "single", .components = 1, .values = {0.25F, 0, 0, 0}}));
    CHECK((*values)[2] == (Parameter{.name = "vector", .components = 4, .values = {1.5F, -2, 0, 4}}));
  }
  for (std::string_view invalid :
       {"true", "'1'", "[]", "[1,2,3,4,5]", "[1,true]", "[[1]]", "{nested=1}", "inf", "nan", "1e100"}) {
    CHECK(!read("[parameters]\nvalid = 1\nbroken = " + std::string(invalid)));
    CHECK(!diagnostics.empty());
  }
  CHECK(!read("parameters = 1"));
  CHECK(!read("[parameters]\ntransition = 1"));
  std::string excess = "[parameters]\n";
  for (size_t i = 0; i <= kParameterLimit; ++i) {
    excess += "p" + std::to_string(i) + " = 0\n";
  }
  CHECK(!read(excess));
}

UMBRIEL_TEST(sceneStageAdmissionCountsAllInputsAndRejectsOverflow) {
  StageUsage usage{
      .builtinVectors = 10,
      .parameterVectors = 2,
      .paletteVectors = 17,
      .audioVectors = 5,
      .samplers = 2,
      .cpuEntries = 7
  };
  StageLimits limits{.uniformVectors = 36, .textureUnits = 2, .cpuEntries = 7};
  CHECK(fitsStage(usage, limits));
  --limits.uniformVectors;
  CHECK(!fitsStage(usage, limits));
  ++limits.uniformVectors;
  --limits.textureUnits;
  CHECK(!fitsStage(usage, limits));
  ++limits.textureUnits;
  --limits.cpuEntries;
  CHECK(!fitsStage(usage, limits));
  ++limits.cpuEntries;
  usage.builtinVectors = std::numeric_limits<unsigned>::max();
  CHECK(!fitsStage(usage, limits));
}

int main() { return RUN_TESTS(); }

#include "config/shader_pool.h"

#include "check.h"

using namespace umbriel;

UMBRIEL_TEST(leasesRemainStableAndReleasedSlotsAreReused) {
  ShaderPoolAllocator allocator;
  std::string name = "rings";
  const std::string policy = "unused_first";
  std::vector<std::string> candidates{"fuse", "pulse", "rainbow"};
  std::shared_ptr<ShaderPoolLease> first, second, third, fourth;
  allocator.select(name, candidates, policy, first);
  allocator.select(name, candidates, policy, first);
  allocator.select(name, candidates, policy, second);
  allocator.select(name, candidates, policy, third);
  CHECK_EQ(first->preset, std::string("fuse"));
  CHECK_EQ(second->preset, std::string("pulse"));
  CHECK_EQ(third->preset, std::string("rainbow"));
  second.reset();
  allocator.select(name, candidates, policy, fourth);
  CHECK_EQ(fourth->preset, std::string("pulse"));
  // Full pool: least-used, breaking ties in rotation order.
  allocator.select(name, candidates, policy, second);
  CHECK_EQ(second->preset, std::string("rainbow"));
  allocator.select(name, candidates, policy, first, true);
  CHECK_EQ(first->preset, std::string("pulse"));
}

UMBRIEL_TEST(reloadKeepsNamesAndReplacesRemovedEntries) {
  ShaderPoolAllocator allocator;
  std::string name = "rings";
  const std::string policy = "unused_first";
  std::vector<std::string> candidates{"a", "b", "c"};
  std::shared_ptr<ShaderPoolLease> first, second;
  allocator.select(name, candidates, policy, first);
  allocator.select(name, candidates, policy, second);
  candidates = {"c", "b", "a"};
  allocator.select(name, candidates, policy, first);
  CHECK_EQ(first->preset, std::string("a"));
  candidates = {"c", "b"};
  allocator.select(name, candidates, policy, first);
  CHECK_EQ(first->preset, std::string("c"));
  allocator.select(name, candidates, policy, second);
  CHECK_EQ(second->preset, std::string("b"));
}

UMBRIEL_TEST(roundRobinWrapsAndPoolsAreIndependent) {
  ShaderPoolAllocator allocator;
  std::string name = "one";
  const std::string policy = "round_robin";
  std::vector<std::string> candidates{"a", "b"};
  std::shared_ptr<ShaderPoolLease> first, second, third;
  allocator.select(name, candidates, policy, first);
  allocator.select(name, candidates, policy, second);
  allocator.select(name, candidates, policy, third);
  CHECK_EQ(third->preset, std::string("a"));
  name = "two";
  allocator.select(name, candidates, policy, third);
  CHECK_EQ(third->preset, std::string("a"));
  candidates = {"only"};
  allocator.select(name, candidates, policy, third, true);
  allocator.select(name, candidates, policy, third, true);
  CHECK_EQ(third->preset, std::string("only"));
  candidates.clear();
  allocator.select(name, candidates, policy, third);
  CHECK(!third);
}

UMBRIEL_TEST(cyclingStartsAfterAnExplicitPreset) {
  ShaderPoolAllocator allocator;
  std::string name = "rings";
  const std::string policy = "unused_first";
  std::vector<std::string> candidates{"a", "b", "c"};
  std::shared_ptr<ShaderPoolLease> lease;
  allocator.select(name, candidates, policy, lease);
  allocator.select(name, candidates, policy, lease, true, "c");
  CHECK_EQ(lease->preset, std::string("a"));
}

UMBRIEL_TEST(effectRandomCyclingExcludesCurrentAndRetainsLeasesOnReload) {
  ShaderPoolAllocator allocator;
  const std::vector<std::string> candidates{"a", "b", "c"};
  std::shared_ptr<ShaderPoolLease> lease;
  allocator.select("choice/window", candidates, "random", lease);
  for (int i = 0; i < 100; ++i) {
    const auto previous = lease->preset;
    allocator.select("choice/window", candidates, "random", lease);
    CHECK_EQ(lease->preset, previous);
    allocator.select("choice/window", candidates, "random", lease, true);
    CHECK(lease->preset != previous);
  }
}

int main() { return RUN_TESTS(); }

#include "config/shader_pool.h"

#include "check.h"

using namespace umbriel;

UMBRIEL_TEST(leasesRemainStableAndReleasedSlotsAreReused) {
  ShaderPoolAllocator allocator;
  Config::Shaders::Pool pool{.name = "rings", .scope = "border", .presets = {"fuse", "pulse", "rainbow"}};
  std::shared_ptr<ShaderPoolLease> first, second, third, fourth;
  allocator.select(pool, first);
  allocator.select(pool, first);
  allocator.select(pool, second);
  allocator.select(pool, third);
  CHECK_EQ(first->preset, std::string("fuse"));
  CHECK_EQ(second->preset, std::string("pulse"));
  CHECK_EQ(third->preset, std::string("rainbow"));
  second.reset();
  allocator.select(pool, fourth);
  CHECK_EQ(fourth->preset, std::string("pulse"));
  // Full pool: least-used, breaking ties in rotation order.
  allocator.select(pool, second);
  CHECK_EQ(second->preset, std::string("rainbow"));
  allocator.select(pool, first, true);
  CHECK_EQ(first->preset, std::string("pulse"));
}

UMBRIEL_TEST(reloadKeepsNamesAndReplacesRemovedEntries) {
  ShaderPoolAllocator allocator;
  Config::Shaders::Pool pool{.name = "rings", .scope = "border", .presets = {"a", "b", "c"}};
  std::shared_ptr<ShaderPoolLease> first, second;
  allocator.select(pool, first);
  allocator.select(pool, second);
  pool.presets = {"c", "b", "a"};
  allocator.select(pool, first);
  CHECK_EQ(first->preset, std::string("a"));
  pool.presets = {"c", "b"};
  allocator.select(pool, first);
  CHECK_EQ(first->preset, std::string("c"));
  allocator.select(pool, second);
  CHECK_EQ(second->preset, std::string("b"));
}

UMBRIEL_TEST(roundRobinWrapsAndPoolsAreIndependent) {
  ShaderPoolAllocator allocator;
  Config::Shaders::Pool pool{.name = "one", .scope = "border", .allocation = "round-robin", .presets = {"a", "b"}};
  std::shared_ptr<ShaderPoolLease> first, second, third;
  allocator.select(pool, first);
  allocator.select(pool, second);
  allocator.select(pool, third);
  CHECK_EQ(third->preset, std::string("a"));
  pool.name = "two";
  allocator.select(pool, third);
  CHECK_EQ(third->preset, std::string("a"));
  pool.presets = {"only"};
  allocator.select(pool, third, true);
  allocator.select(pool, third, true);
  CHECK_EQ(third->preset, std::string("only"));
  pool.presets.clear();
  allocator.select(pool, third);
  CHECK(!third);
}

UMBRIEL_TEST(cyclingStartsAfterAnExplicitPreset) {
  ShaderPoolAllocator allocator;
  Config::Shaders::Pool pool{.name = "rings", .scope = "border", .presets = {"a", "b", "c"}};
  std::shared_ptr<ShaderPoolLease> lease;
  allocator.select(pool, lease);
  allocator.select(pool, lease, true, "c");
  CHECK_EQ(lease->preset, std::string("a"));
}

int main() { return RUN_TESTS(); }

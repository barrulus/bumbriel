#pragma once

#include <algorithm>
#include <limits>
#include <memory>
#include <random>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace umbriel {
  // Views own leases. Unmapping/destroying a view releases its slot automatically.
  // Names, rather than indices or config pointers, survive edits and reordering.
  struct ShaderPoolLease {
    std::string pool;
    std::string preset;
  };

  class ShaderPoolAllocator {
  public:
    void select(
        std::string_view name, std::span<const std::string> candidates, std::string_view policy,
        std::shared_ptr<ShaderPoolLease>& lease, bool cycle = false, std::string_view currentPreset = {}
    ) {
      if (candidates.empty()) {
        lease.reset();
        return;
      }
      const auto current = !currentPreset.empty() ? std::ranges::find(candidates, currentPreset)
          : lease && lease->pool == name          ? std::ranges::find(candidates, lease->preset)
                                                  : candidates.end();
      if (!cycle && current != candidates.end() && lease && lease->pool == name && lease->preset == *current)
        return;
      std::erase_if(m_leases, [](const auto& entry) { return entry.expired(); });
      size_t start = 0;
      const auto previous = std::ranges::find(candidates, m_previous[std::string(name)]);
      if (cycle && current != candidates.end())
        start = (static_cast<size_t>(current - candidates.begin()) + 1) % candidates.size();
      else if (previous != candidates.end())
        start = (static_cast<size_t>(previous - candidates.begin()) + 1) % candidates.size();
      size_t selected = start;
      size_t least = std::numeric_limits<size_t>::max();
      for (size_t offset = 0; offset < candidates.size(); ++offset) {
        const size_t index = (start + offset) % candidates.size();
        // A cycle must change the effect when there is more than one choice.
        if (cycle && candidates.size() > 1 && current != candidates.end() && candidates[index] == *current)
          continue;
        size_t count = 0;
        for (const auto& entry : m_leases)
          if (const auto other = entry.lock();
              other && other != lease && other->pool == name && other->preset == candidates[index])
            ++count;
        if (count < least) {
          least = count;
          selected = index;
        }
        if (policy == "round_robin" || count == 0)
          break;
      }
      if (policy == "random") {
        std::vector<size_t> eligible;
        for (size_t i = 0; i < candidates.size(); ++i)
          if (!cycle || candidates.size() == 1 || current == candidates.end() || candidates[i] != *current)
            eligible.push_back(i);
        selected = eligible[std::uniform_int_distribution<size_t>(0, eligible.size() - 1)(m_random)];
      }
      if (!lease) {
        lease = std::make_shared<ShaderPoolLease>();
        m_leases.push_back(lease);
      }
      lease->pool = name;
      lease->preset = candidates[selected];
      m_previous[std::string(name)] = lease->preset;
    }

  private:
    std::mt19937 m_random{std::random_device{}()};
    std::vector<std::weak_ptr<ShaderPoolLease>> m_leases;
    std::unordered_map<std::string, std::string> m_previous;
  };
} // namespace umbriel

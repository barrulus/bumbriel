#pragma once

#include "config/config.h"

#include <algorithm>
#include <limits>
#include <memory>
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
        const Config::Shaders::Pool& pool, std::shared_ptr<ShaderPoolLease>& lease, bool cycle = false,
        std::string_view currentPreset = {}
    ) {
      if (pool.presets.empty()) {
        lease.reset();
        return;
      }
      const auto current = !currentPreset.empty() ? std::ranges::find(pool.presets, currentPreset)
          : lease && lease->pool == pool.name     ? std::ranges::find(pool.presets, lease->preset)
                                                  : pool.presets.end();
      if (!cycle && current != pool.presets.end())
        return;
      std::erase_if(m_leases, [](const auto& entry) { return entry.expired(); });
      size_t start = 0;
      const auto previous = std::ranges::find(pool.presets, m_previous[pool.name]);
      if (cycle && current != pool.presets.end())
        start = (static_cast<size_t>(current - pool.presets.begin()) + 1) % pool.presets.size();
      else if (previous != pool.presets.end())
        start = (static_cast<size_t>(previous - pool.presets.begin()) + 1) % pool.presets.size();
      size_t selected = start;
      size_t least = std::numeric_limits<size_t>::max();
      for (size_t offset = 0; offset < pool.presets.size(); ++offset) {
        const size_t index = (start + offset) % pool.presets.size();
        // A cycle must change the effect when there is more than one choice.
        if (cycle && pool.presets.size() > 1 && current != pool.presets.end() && pool.presets[index] == *current)
          continue;
        size_t count = 0;
        for (const auto& entry : m_leases)
          if (const auto other = entry.lock();
              other && other != lease && other->pool == pool.name && other->preset == pool.presets[index])
            ++count;
        if (count < least) {
          least = count;
          selected = index;
        }
        if (pool.allocation == "round-robin" || count == 0)
          break;
      }
      if (!lease) {
        lease = std::make_shared<ShaderPoolLease>();
        m_leases.push_back(lease);
      }
      lease->pool = pool.name;
      lease->preset = pool.presets[selected];
      m_previous[pool.name] = lease->preset;
    }

  private:
    std::vector<std::weak_ptr<ShaderPoolLease>> m_leases;
    std::unordered_map<std::string, std::string> m_previous;
  };
} // namespace umbriel

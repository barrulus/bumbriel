#include "config/effect_state.h"

#include <format>
#include <set>

namespace umbriel {
  ShaderPoolAllocator& effectAllocator() {
    static ShaderPoolAllocator allocator;
    return allocator;
  }

  EffectState& globalEffectState() {
    static EffectState state{EffectOwner::Global};
    return state;
  }

  std::map<std::string, EffectState, std::less<>>& regionEffectOverrides() {
    static std::map<std::string, EffectState, std::less<>> states;
    return states;
  }

  void EffectState::appendOverrides(std::vector<EffectInput>& inputs) const {
    for (size_t i = 0; i < m_runtime.size(); ++i) {
      const auto bit = static_cast<EffectMask>(EffectMask{1} << i);
      if (m_runtime[i])
        inputs.push_back({*m_runtime[i], bit, false, m_cycles[i]});
      if (m_disabled & bit)
        inputs.push_back({{{}, {"runtime", 0, 0}}, bit, true});
    }
  }

  void EffectState::set(const EffectSelector& selector, EffectMask mask) {
    for (size_t i = 0; i < m_runtime.size(); ++i)
      if (mask & (EffectMask{1} << i)) {
        m_runtime[i] = selector;
        m_cycles[i] = 0;
      }
    on(mask);
  }

  void EffectState::off(EffectMask mask) { m_disabled |= mask; }
  void EffectState::on(EffectMask mask) { m_disabled &= ~mask; }
  void EffectState::toggle(EffectMask mask, const ResolvedEffects* effective) {
    const auto& resolved = effective ? *effective : m_resolved;
    for (size_t i = 0; i < resolved.size(); ++i)
      if ((mask & (EffectMask{1} << i))
          && !(m_disabled & (EffectMask{1} << i))
          && resolved[i].pipeline
          && resolved[i].pipeline->enabled) {
        off(mask);
        return;
      }
    on(mask);
  }

  void EffectState::defaults(EffectMask mask) {
    for (size_t i = 0; i < m_runtime.size(); ++i)
      if (mask & (EffectMask{1} << i))
        m_runtime[i].reset();
    on(mask);
  }

  std::string EffectState::allocationKey(std::string_view choice) const {
    return std::format("{}:{}", static_cast<unsigned>(m_owner), choice);
  }

  void EffectState::cycle(
      const EffectLibrary& library, ShaderPoolAllocator& allocator, std::string_view choice, EffectMask mask
  ) {
    const auto found = library.find(choice);
    if (found == library.end() || !found->second.choose)
      return;
    (void)allocator;
    static uint64_t sequence = 0;
    set({{std::string(choice)}, {"runtime", 0, 0}}, mask);
    ++sequence;
    for (size_t i = 0; i < m_cycles.size(); ++i)
      if (mask & (EffectMask{1} << i))
        m_cycles[i] = sequence;
  }

  void EffectState::prune(const EffectLibrary& library, std::vector<ConfigDiagnostic>* diagnostics) {
    bool reported = false;
    for (size_t i = 0; i < m_runtime.size(); ++i) {
      auto& selector = m_runtime[i];
      if (!selector || !std::ranges::any_of(selector->names, [&](const auto& name) { return !library.contains(name); }))
        continue;
      if (diagnostics && !reported) {
        diagnostics->push_back(
            {ConfigDiagnostic::Severity::Warning,
             "deleted runtime effect reference cleared; restoring configured selection", selector->origin.file,
             selector->origin.line, selector->origin.column}
        );
        reported = true;
      }
      selector.reset();
      m_cycles[i] = 0;
      on(static_cast<EffectMask>(EffectMask{1} << i));
    }
  }

  void EffectState::resolve(
      const EffectLibrary& library, std::span<const EffectInput> inputs, ShaderPoolAllocator& allocator,
      EffectMask mask, std::vector<ConfigDiagnostic>* diagnostics
  ) {
    mask &= effectMask(m_owner);
    prune(library, diagnostics);
    EffectVariants variants;
    for (const auto& [name, definition] : library)
      if (definition.choose && !definition.choose->empty())
        variants[name] = definition.choose->front();
    std::array<uint64_t, kEffectScopeCount> cycles{};
    const auto resolve = [&] {
      ResolvedEffects resolved;
      const auto apply = [&](const EffectInput& input) {
        const auto selectedMask = input.mask & mask;
        if (input.disabled) {
          for (size_t i = 0; i < resolved.size(); ++i)
            if (selectedMask & (EffectMask{1} << i)) {
              if (!resolved[i].pipeline)
                resolved[i].pipeline.emplace();
              resolved[i].pipeline->enabled = false;
              resolved[i].runtimeDisabled = true;
            }
          return;
        }
        applyEffects(resolved, library, input.selector, selectedMask, variants);
        EffectMask supplied = input.selector.names.empty() ? selectedMask : 0;
        for (const auto& name : input.selector.names) {
          const auto found = library.find(name);
          if (found != library.end()) {
            const auto& definition = found->second;
            supplied |= definition.choose ? library.at(definition.choose->front()).mask() : definition.mask();
          }
        }
        for (size_t i = 0; i < cycles.size(); ++i)
          if (supplied & selectedMask & (EffectMask{1} << i))
            cycles[i] = input.cycle;
      };
      for (const auto& input : inputs)
        apply(input);
      for (size_t i = 0; i < m_runtime.size(); ++i)
        if (m_runtime[i] && (mask & (EffectMask{1} << i)))
          apply({*m_runtime[i], static_cast<EffectMask>(EffectMask{1} << i), false, m_cycles[i]});
      return resolved;
    };
    std::set<std::string> contributing;
    const auto provisional = resolve();
    for (const auto& leaf : provisional)
      if (!leaf.source.choice.empty())
        contributing.insert(leaf.source.choice);
    std::erase_if(m_leases, [&](const auto& entry) { return !contributing.contains(entry.first); });
    for (const auto& name : contributing) {
      if (m_owner == EffectOwner::Global)
        continue;
      const auto& definition = library.at(name);
      auto& lease = m_leases[name];
      uint64_t requestedCycle = 0;
      std::string current;
      for (size_t i = 0; i < provisional.size(); ++i)
        if (provisional[i].source.choice == name) {
          requestedCycle = std::max(requestedCycle, cycles[i]);
          if (std::ranges::contains(*definition.choose, m_resolved[i].source.effect))
            current = m_resolved[i].source.effect;
        }
      const bool cycle = requestedCycle != 0 && requestedCycle != m_seenCycles[name];
      allocator.select(allocationKey(name), *definition.choose, definition.selection, lease, cycle, current);
      m_seenCycles[name] = requestedCycle;
      if (lease)
        variants[name] = lease->preset;
    }
    m_resolved = resolve();
  }

  void EffectState::release() {
    m_leases.clear();
    m_seenCycles.clear();
    m_resolved = {};
  }
} // namespace umbriel

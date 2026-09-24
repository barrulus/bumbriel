#pragma once

#include "config/effects.h"
#include "config/shader_pool.h"

namespace umbriel {
  struct EffectInput {
    EffectSelector selector;
    EffectMask mask = kAllEffects;
    bool disabled = false;
    uint64_t cycle = 0;
    bool operator==(const EffectInput&) const = default;
  };

  ShaderPoolAllocator& effectAllocator();

  class EffectState;
  EffectState& globalEffectState();
  std::map<std::string, EffectState, std::less<>>& regionEffectOverrides();

  class EffectState {
  public:
    explicit EffectState(EffectOwner owner) : m_owner(owner) {}
    void set(const EffectSelector& selector, EffectMask mask);
    void off(EffectMask mask);
    void on(EffectMask mask);
    void toggle(EffectMask mask, const ResolvedEffects* effective = nullptr);
    void defaults(EffectMask mask);
    void cycle(const EffectLibrary& library, ShaderPoolAllocator& allocator, std::string_view choice, EffectMask mask);
    void resolve(
        const EffectLibrary& library, std::span<const EffectInput> inputs, ShaderPoolAllocator& allocator,
        EffectMask mask, std::vector<ConfigDiagnostic>* diagnostics = nullptr
    );
    void release();
    void prune(const EffectLibrary& library, std::vector<ConfigDiagnostic>* diagnostics = nullptr);
    void appendOverrides(std::vector<EffectInput>& inputs) const;
    [[nodiscard]] const ResolvedEffects& resolved() const { return m_resolved; }
    [[nodiscard]] EffectMask disabled() const { return m_disabled; }
    [[nodiscard]] const auto& leases() const { return m_leases; }

  private:
    [[nodiscard]] std::string allocationKey(std::string_view choice) const;
    EffectOwner m_owner;
    EffectMask m_disabled = 0;
    std::array<std::optional<EffectSelector>, kEffectScopeCount> m_runtime;
    std::array<uint64_t, kEffectScopeCount> m_cycles{};
    std::map<std::string, uint64_t, std::less<>> m_seenCycles;
    std::map<std::string, std::shared_ptr<ShaderPoolLease>, std::less<>> m_leases;
    ResolvedEffects m_resolved;
  };
} // namespace umbriel

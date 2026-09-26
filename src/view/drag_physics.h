#pragma once

#include <array>
#include <cstdint>

namespace umbriel {

  // A dragged window deforms as an elastic sheet pinned at the grab point and
  // relaxes back to rest after release. A 4x4 grid of point masses (kPoints,
  // row-major) carries the deformation; the shader samples it with the same
  // bicubic Bernstein weights used to pin the grab point here.
  class DragPhysics {
  public:
    static constexpr int kPoints = 16;
    using Sheet = std::array<std::array<float, 2>, kPoints>;
    // `grabX`/`grabY` are the grab point as fractions of the window (0-1). `transitionId` comes from the
    // shared transition-id source (never 0), since this class mints no ids of its own.
    void begin(float width, float height, float grabX, float grabY, uint64_t transitionId);
    // Pointer delta in logical pixels since the previous call.
    void move(float dx, float dy);
    void release();
    // Advances by `seconds` at a fixed 240 Hz step. A pause over 250 ms settles immediately. Returns active().
    bool tick(double seconds);
    [[nodiscard]] bool active() const { return m_active; }
    [[nodiscard]] bool grabbed() const { return m_grabbed; }
    [[nodiscard]] uint64_t transitionId() const { return m_transitionId; }
    // Displacements divided by the window size, as the shader expects.
    [[nodiscard]] Sheet normalizedDisplacement() const;
    // Largest displacement in logical pixels, for the drawn rectangle's expand.
    [[nodiscard]] float maxDisplacement() const;

  private:
    void constrain();
    Sheet m_displacement{};
    Sheet m_velocity{};
    std::array<float, kPoints> m_weights{};
    std::array<float, kPoints> m_drag{};
    float m_width = 1.0F;
    float m_height = 1.0F;
    double m_remainder = 0.0;
    uint64_t m_transitionId = 0;
    bool m_grabbed = false;
    bool m_active = false;
  };

} // namespace umbriel

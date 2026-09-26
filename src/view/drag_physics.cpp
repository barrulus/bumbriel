#include "view/drag_physics.h"

#include <algorithm>
#include <cmath>

namespace umbriel {
  namespace {
    // Spring sheet tuning. Stiffness pulls each mass home, coupling ties it
    // to its neighbours, damping bleeds energy, pointer response scales how
    // far a motion loads the sheet. Behaviour, not these numbers, is tested.
    constexpr float kStiffness = 36.0F;
    constexpr float kCoupling = 100.0F;
    constexpr float kDamping = 6.5F;
    constexpr float kPointerResponse = 2.0F;
    constexpr double kStep = 1.0 / 240.0;
    constexpr double kSettleAfterPause = 0.25;
    constexpr float kMaxDisplacementPx = 200.0F;
    constexpr float kMaxVelocity = 4000.0F;
    constexpr float kSettledDisplacement = 0.02F;
    constexpr float kSettledVelocity = 0.2F;

    uint64_t nextTransition() {
      static uint64_t serial = 0;
      return ++serial;
    }
  } // namespace

  void DragPhysics::begin(float width, float height, float grabX, float grabY) {
    *this = DragPhysics{};
    m_width = std::max(width, 1.0F);
    m_height = std::max(height, 1.0F);
    m_transitionId = nextTransition();
    m_grabbed = true;
    // One bicubic Bernstein surface couples the whole window; the shader
    // interpolates with the same weights, so the pin lands exactly on the pointer.
    const float u = std::clamp(grabX, 0.0F, 1.0F);
    const float v = std::clamp(grabY, 0.0F, 1.0F);
    const float ux = 1 - u, vy = 1 - v;
    const float horizontal[4] = {ux * ux * ux, 3 * u * ux * ux, 3 * u * u * ux, u * u * u};
    const float vertical[4] = {vy * vy * vy, 3 * v * vy * vy, 3 * v * v * vy, v * v * v};
    for (int i = 0; i < kPoints; ++i) {
      m_weights[i] = horizontal[i % 4] * vertical[i / 4];
    }
    // Spread pointer response around the grab instead of a sharp bump in the
    // nearest masses; normalise so the grab point itself does not move.
    const float x = u * 3, y = v * 3;
    float anchor = 0;
    for (int i = 0; i < kPoints; ++i) {
      const float dx = (static_cast<float>(i % 4) - x) / 3, dy = (static_cast<float>(i / 4) - y) / 3;
      m_drag[i] = std::exp(-(dx * dx + dy * dy) / 0.35F);
      anchor += m_weights[i] * m_drag[i];
    }
    for (int i = 0; i < kPoints; ++i) {
      m_drag[i] = 1 - m_drag[i] / anchor;
    }
  }

  void DragPhysics::constrain() {
    if (m_grabbed) {
      float sum = 0, position[2] = {0, 0}, velocity[2] = {0, 0};
      for (int i = 0; i < kPoints; ++i) {
        sum += m_weights[i] * m_weights[i];
        for (int axis = 0; axis < 2; ++axis) {
          position[axis] += m_weights[i] * m_displacement[i][axis];
          velocity[axis] += m_weights[i] * m_velocity[i][axis];
        }
      }
      // Pin the interpolated grab point exactly, even between grid vertices.
      for (int i = 0; i < kPoints; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
          m_displacement[i][axis] -= m_weights[i] * position[axis] / sum;
          m_velocity[i][axis] -= m_weights[i] * velocity[axis] / sum;
        }
      }
    }
    // Bound adjacent slopes so the shader's inverse lookup stays a contraction
    // (no folding), then bound absolute excursion and speed. One global scale
    // preserves the pin, unlike clamping each mass.
    float ratio = 1;
    for (int axis = 0; axis < 2; ++axis) {
      float horizontal = 0, vertical = 0;
      const float extent = axis == 0 ? m_width : m_height;
      for (int y = 0; y < 4; ++y) {
        for (int x = 0; x < 4; ++x) {
          const int i = y * 4 + x;
          if (x < 3)
            horizontal = std::max(horizontal, std::abs(m_displacement[i + 1][axis] - m_displacement[i][axis]));
          if (y < 3)
            vertical = std::max(vertical, std::abs(m_displacement[i + 4][axis] - m_displacement[i][axis]));
        }
      }
      ratio = std::max(ratio, 3 * (horizontal + vertical) / (extent * 0.7F));
    }
    for (int i = 0; i < kPoints; ++i) {
      ratio = std::max(ratio, std::abs(m_displacement[i][0]) / std::min(kMaxDisplacementPx, m_width / 5));
      ratio = std::max(ratio, std::abs(m_displacement[i][1]) / std::min(kMaxDisplacementPx, m_height / 5));
      for (int axis = 0; axis < 2; ++axis) {
        ratio = std::max(ratio, std::abs(m_velocity[i][axis]) / kMaxVelocity);
      }
    }
    if (ratio > 1) {
      for (auto& point : m_displacement) {
        point[0] /= ratio;
        point[1] /= ratio;
      }
      for (auto& point : m_velocity) {
        point[0] /= ratio;
        point[1] /= ratio;
      }
    }
  }

  void DragPhysics::move(float dx, float dy) {
    if (!m_grabbed || !std::isfinite(dx) || !std::isfinite(dy) || (dx == 0 && dy == 0)) {
      return;
    }
    for (int i = 0; i < kPoints; ++i) {
      m_displacement[i][0] -= kPointerResponse * dx * m_drag[i];
      m_displacement[i][1] -= kPointerResponse * dy * m_drag[i];
    }
    constrain();
    m_active = true;
  }

  void DragPhysics::release() { m_grabbed = false; }

  bool DragPhysics::tick(double seconds) {
    if (!m_active || !std::isfinite(seconds) || seconds <= 0) {
      return m_active;
    }
    if (seconds > kSettleAfterPause) {
      m_displacement = {};
      m_velocity = {};
      m_remainder = 0;
      m_active = false;
      return false;
    }
    m_remainder += seconds;
    while (m_remainder + 1e-12 >= kStep) {
      m_remainder -= kStep;
      Sheet acceleration{};
      for (int i = 0; i < kPoints; ++i) {
        const int neighbours[4] = {
            i % 4 > 0 ? i - 1 : -1, i % 4 < 3 ? i + 1 : -1, i >= 4 ? i - 4 : -1, i < 12 ? i + 4 : -1
        };
        for (int axis = 0; axis < 2; ++axis) {
          acceleration[i][axis] = -kStiffness * m_displacement[i][axis] - kDamping * m_velocity[i][axis];
          for (const int n : neighbours) {
            if (n >= 0) {
              acceleration[i][axis] += kCoupling * (m_displacement[n][axis] - m_displacement[i][axis]);
            }
          }
        }
      }
      for (int i = 0; i < kPoints; ++i) {
        for (int axis = 0; axis < 2; ++axis) {
          m_velocity[i][axis] += acceleration[i][axis] * static_cast<float>(kStep);
          m_displacement[i][axis] += m_velocity[i][axis] * static_cast<float>(kStep);
        }
      }
      constrain();
    }
    m_active = false;
    for (int i = 0; i < kPoints; ++i) {
      for (int axis = 0; axis < 2; ++axis) {
        m_active |= std::abs(m_displacement[i][axis]) > kSettledDisplacement
            || std::abs(m_velocity[i][axis]) > kSettledVelocity;
      }
    }
    if (!m_active) {
      m_displacement = {};
      m_velocity = {};
      m_remainder = 0;
    }
    return m_active;
  }

  DragPhysics::Sheet DragPhysics::normalizedDisplacement() const {
    Sheet sheet{};
    for (int i = 0; i < kPoints; ++i) {
      sheet[i][0] = m_displacement[i][0] / m_width;
      sheet[i][1] = m_displacement[i][1] / m_height;
    }
    return sheet;
  }

  float DragPhysics::maxDisplacement() const {
    float largest = 0;
    for (const auto& point : m_displacement) {
      largest = std::max({largest, std::abs(point[0]), std::abs(point[1])});
    }
    return largest;
  }

} // namespace umbriel

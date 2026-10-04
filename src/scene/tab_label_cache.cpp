#include "scene/tab_label_cache.h"

#include "scene/color.h"
#include "scene/text_buffer.h"

extern "C" {
#include <wlr/types/wlr_buffer.h>
}

#include <algorithm>
#include <format>
#include <utility>

namespace umbriel {

  TabLabelCache::TabLabelCache(size_t capacity) : m_capacity(std::max<size_t>(1, capacity)) {}

  TabLabelCache::~TabLabelCache() {
    for (Entry& entry : m_entries) {
      wlr_buffer_drop(entry.label.buffer);
    }
  }

  const TabLabel* TabLabelCache::label(const TabLabelRequest& request) {
    if (request.text.empty() || request.maxWidth <= 0) {
      return nullptr;
    }
    const std::string markup =
        std::format("<span foreground='{}'>{}</span>", rgbaHex(request.color), escapeMarkup(request.text));
    std::string key = std::format("{}\x1f{}\x1f{}\x1f{}", markup, request.font, request.maxWidth, request.scale);
    if (const auto found = m_index.find(key); found != m_index.end()) {
      m_entries.splice(m_entries.begin(), m_entries, found->second);
      return &found->second->label;
    }

    const TextBufferResult rendered = renderTextBuffer({
        .markup = markup,
        .font = std::string(request.font),
        .maxWidth = request.maxWidth,
        .padding = 0,
        .scale = request.scale,
        .ellipsize = true,
    });
    if (rendered.buffer == nullptr) {
      return nullptr;
    }
    if (m_entries.size() >= m_capacity) {
      Entry& oldest = m_entries.back();
      wlr_buffer_drop(oldest.label.buffer);
      m_index.erase(oldest.key);
      m_entries.pop_back();
    }
    m_entries.push_front({
        .key = key,
        .label = {.buffer = rendered.buffer, .width = rendered.logicalWidth, .height = rendered.logicalHeight},
    });
    m_index.emplace(std::move(key), m_entries.begin());
    return &m_entries.front().label;
  }

  std::shared_ptr<TabLabelCache> TabLabelCache::shared() {
    static std::weak_ptr<TabLabelCache> instance;
    std::shared_ptr<TabLabelCache> cache = instance.lock();
    if (cache == nullptr) {
      cache = std::make_shared<TabLabelCache>();
      instance = cache;
    }
    return cache;
  }

} // namespace umbriel

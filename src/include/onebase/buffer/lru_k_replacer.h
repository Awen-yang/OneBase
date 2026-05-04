#pragma once
#include <list>
#include <mutex>
#include <unordered_map>
#include "onebase/common/config.h"
#include "onebase/common/types.h"

namespace onebase {

class LRUKReplacer {
 public:
  explicit LRUKReplacer(size_t num_frames, size_t k);
  ~LRUKReplacer() = default;

  auto Evict(frame_id_t *frame_id) -> bool;
  void RecordAccess(frame_id_t frame_id);
  void SetEvictable(frame_id_t frame_id, bool set_evictable);
  void Remove(frame_id_t frame_id);
  auto Size() const -> size_t;

 private:
  size_t max_frames_;
  size_t k_;
  size_t curr_size_{0};
  size_t current_timestamp_{0};
  mutable std::mutex latch_;

  struct FrameEntry {
    std::list<size_t> history_;
    bool is_evictable_{false};
  };
  std::unordered_map<frame_id_t, FrameEntry> entries_;
};

}  // namespace onebase

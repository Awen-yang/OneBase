#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/common/exception.h"

namespace onebase {

LRUKReplacer::LRUKReplacer(size_t num_frames, size_t k)
    : max_frames_(num_frames), k_(k) {}

auto LRUKReplacer::Evict(frame_id_t *frame_id) -> bool {
  std::scoped_lock lock(latch_);
  if (curr_size_ == 0) {
    return false;
  }

  // Frames with fewer than k accesses have +inf backward k-distance and are
  // preferred. Among them, evict the one with the earliest first access.
  // Otherwise evict the frame with the smallest k-th-most-recent access.
  bool found_inf = false;
  size_t best_score = 0;
  frame_id_t victim = INVALID_FRAME_ID;

  for (const auto &[fid, entry] : entries_) {
    if (!entry.is_evictable_) {
      continue;
    }
    if (entry.history_.size() < k_) {
      size_t earliest = entry.history_.front();
      if (!found_inf || earliest < best_score) {
        found_inf = true;
        best_score = earliest;
        victim = fid;
      }
    } else if (!found_inf) {
      size_t kth = entry.history_.front();
      if (victim == INVALID_FRAME_ID || kth < best_score) {
        best_score = kth;
        victim = fid;
      }
    }
  }

  if (victim == INVALID_FRAME_ID) {
    return false;
  }

  *frame_id = victim;
  entries_.erase(victim);
  curr_size_--;
  return true;
}

void LRUKReplacer::RecordAccess(frame_id_t frame_id) {
  std::scoped_lock lock(latch_);
  auto &entry = entries_[frame_id];
  entry.history_.push_back(current_timestamp_);
  if (entry.history_.size() > k_) {
    entry.history_.pop_front();
  }
  current_timestamp_++;
}

void LRUKReplacer::SetEvictable(frame_id_t frame_id, bool set_evictable) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }
  if (it->second.is_evictable_ != set_evictable) {
    curr_size_ += set_evictable ? 1 : -1;
    it->second.is_evictable_ = set_evictable;
  }
}

void LRUKReplacer::Remove(frame_id_t frame_id) {
  std::scoped_lock lock(latch_);
  auto it = entries_.find(frame_id);
  if (it == entries_.end()) {
    return;
  }
  if (!it->second.is_evictable_) {
    throw OneBaseException("Cannot remove non-evictable frame");
  }
  entries_.erase(it);
  curr_size_--;
}

auto LRUKReplacer::Size() const -> size_t {
  std::scoped_lock lock(latch_);
  return curr_size_;
}

}  // namespace onebase

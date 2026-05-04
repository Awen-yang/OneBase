#pragma once
#include <list>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "onebase/buffer/lru_k_replacer.h"
#include "onebase/storage/disk/disk_manager.h"
#include "onebase/storage/page/page.h"

namespace onebase {

class BufferPoolManager {
 public:
  BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k = LRUK_REPLACER_K);
  ~BufferPoolManager();

  auto GetPoolSize() const -> size_t { return pool_size_; }

  auto NewPage(page_id_t *page_id) -> Page *;
  auto FetchPage(page_id_t page_id) -> Page *;
  auto UnpinPage(page_id_t page_id, bool is_dirty) -> bool;
  auto DeletePage(page_id_t page_id) -> bool;
  auto FlushPage(page_id_t page_id) -> bool;
  void FlushAllPages();

 private:
  // Pick a free frame, evicting and flushing a victim if needed.
  // Returns INVALID_FRAME_ID iff every frame is pinned. Caller holds latch_.
  auto AcquireFrame() -> frame_id_t;

  size_t pool_size_;
  DiskManager *disk_manager_;
  std::unique_ptr<LRUKReplacer> replacer_;
  Page *pages_;
  std::unordered_map<page_id_t, frame_id_t> page_table_;
  std::list<frame_id_t> free_list_;
  std::mutex latch_;
};

}  // namespace onebase

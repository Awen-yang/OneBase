#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/logger.h"

namespace onebase {

BufferPoolManager::BufferPoolManager(size_t pool_size, DiskManager *disk_manager, size_t replacer_k)
    : pool_size_(pool_size), disk_manager_(disk_manager) {
  pages_ = new Page[pool_size_];
  replacer_ = std::make_unique<LRUKReplacer>(pool_size, replacer_k);
  for (size_t i = 0; i < pool_size_; ++i) {
    free_list_.emplace_back(static_cast<frame_id_t>(i));
  }
}

BufferPoolManager::~BufferPoolManager() { delete[] pages_; }

auto BufferPoolManager::AcquireFrame() -> frame_id_t {
  if (!free_list_.empty()) {
    auto frame = free_list_.front();
    free_list_.pop_front();
    return frame;
  }
  frame_id_t frame;
  if (!replacer_->Evict(&frame)) {
    return INVALID_FRAME_ID;
  }
  auto &victim = pages_[frame];
  if (victim.is_dirty_) {
    disk_manager_->WritePage(victim.page_id_, victim.data_);
  }
  page_table_.erase(victim.page_id_);
  return frame;
}

auto BufferPoolManager::NewPage(page_id_t *page_id) -> Page * {
  std::scoped_lock lock(latch_);

  auto frame = AcquireFrame();
  if (frame == INVALID_FRAME_ID) {
    return nullptr;
  }

  auto new_pid = disk_manager_->AllocatePage();
  auto &page = pages_[frame];
  page.ResetMemory();
  page.page_id_ = new_pid;
  page.pin_count_ = 1;
  page.is_dirty_ = false;

  page_table_[new_pid] = frame;
  replacer_->RecordAccess(frame);
  replacer_->SetEvictable(frame, false);

  *page_id = new_pid;
  return &page;
}

auto BufferPoolManager::FetchPage(page_id_t page_id) -> Page * {
  std::scoped_lock lock(latch_);

  if (auto it = page_table_.find(page_id); it != page_table_.end()) {
    auto frame = it->second;
    pages_[frame].pin_count_++;
    replacer_->RecordAccess(frame);
    replacer_->SetEvictable(frame, false);
    return &pages_[frame];
  }

  auto frame = AcquireFrame();
  if (frame == INVALID_FRAME_ID) {
    return nullptr;
  }

  auto &page = pages_[frame];
  page.ResetMemory();
  page.page_id_ = page_id;
  page.pin_count_ = 1;
  page.is_dirty_ = false;
  disk_manager_->ReadPage(page_id, page.data_);

  page_table_[page_id] = frame;
  replacer_->RecordAccess(frame);
  replacer_->SetEvictable(frame, false);
  return &page;
}

auto BufferPoolManager::UnpinPage(page_id_t page_id, bool is_dirty) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }
  auto &page = pages_[it->second];
  if (page.pin_count_ <= 0) {
    return false;
  }
  page.pin_count_--;
  if (is_dirty) {
    page.is_dirty_ = true;
  }
  if (page.pin_count_ == 0) {
    replacer_->SetEvictable(it->second, true);
  }
  return true;
}

auto BufferPoolManager::DeletePage(page_id_t page_id) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return true;
  }
  auto frame = it->second;
  auto &page = pages_[frame];
  if (page.pin_count_ > 0) {
    return false;
  }
  replacer_->Remove(frame);
  page_table_.erase(it);
  free_list_.push_back(frame);
  page.ResetMemory();
  page.page_id_ = INVALID_PAGE_ID;
  page.pin_count_ = 0;
  page.is_dirty_ = false;
  disk_manager_->DeallocatePage(page_id);
  return true;
}

auto BufferPoolManager::FlushPage(page_id_t page_id) -> bool {
  std::scoped_lock lock(latch_);
  auto it = page_table_.find(page_id);
  if (it == page_table_.end()) {
    return false;
  }
  auto frame = it->second;
  disk_manager_->WritePage(page_id, pages_[frame].data_);
  pages_[frame].is_dirty_ = false;
  return true;
}

void BufferPoolManager::FlushAllPages() {
  std::scoped_lock lock(latch_);
  for (auto &[pid, frame] : page_table_) {
    disk_manager_->WritePage(pid, pages_[frame].data_);
    pages_[frame].is_dirty_ = false;
  }
}

}  // namespace onebase

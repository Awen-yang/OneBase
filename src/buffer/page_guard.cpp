#include "onebase/buffer/page_guard.h"
#include "onebase/common/exception.h"

namespace onebase {

// --- BasicPageGuard ---

BasicPageGuard::BasicPageGuard(BufferPoolManager *bpm, Page *page)
    : bpm_(bpm), page_(page) {}

BasicPageGuard::BasicPageGuard(BasicPageGuard &&that) noexcept
    : bpm_(that.bpm_), page_(that.page_), is_dirty_(that.is_dirty_) {
  that.bpm_ = nullptr;
  that.page_ = nullptr;
}

auto BasicPageGuard::operator=(BasicPageGuard &&that) -> BasicPageGuard & {
  if (this == &that) {
    return *this;
  }
  Drop();
  bpm_ = that.bpm_;
  page_ = that.page_;
  is_dirty_ = that.is_dirty_;
  that.bpm_ = nullptr;
  that.page_ = nullptr;
  return *this;
}

BasicPageGuard::~BasicPageGuard() { Drop(); }

auto BasicPageGuard::GetPageId() const -> page_id_t {
  return page_ ? page_->GetPageId() : INVALID_PAGE_ID;
}

auto BasicPageGuard::GetData() const -> const char * { return page_->GetData(); }
auto BasicPageGuard::GetDataMut() -> char * {
  is_dirty_ = true;
  return page_->GetData();
}

auto BasicPageGuard::IsDirty() const -> bool { return is_dirty_; }

void BasicPageGuard::Drop() {
  if (page_ == nullptr) { return; }
  if (bpm_ != nullptr) {
    bpm_->UnpinPage(page_->GetPageId(), is_dirty_);
  }
  bpm_ = nullptr;
  page_ = nullptr;
  is_dirty_ = false;
}

// --- ReadPageGuard ---

ReadPageGuard::ReadPageGuard(BufferPoolManager *bpm, Page *page)
    : bpm_(bpm), page_(page) {
  if (page_) { page_->RLatch(); }
}

ReadPageGuard::ReadPageGuard(ReadPageGuard &&that) noexcept
    : bpm_(that.bpm_), page_(that.page_) {
  that.bpm_ = nullptr;
  that.page_ = nullptr;
}

auto ReadPageGuard::operator=(ReadPageGuard &&that) -> ReadPageGuard & {
  if (this == &that) {
    return *this;
  }
  Drop();
  bpm_ = that.bpm_;
  page_ = that.page_;
  that.bpm_ = nullptr;
  that.page_ = nullptr;
  return *this;
}

ReadPageGuard::~ReadPageGuard() { Drop(); }

auto ReadPageGuard::GetPageId() const -> page_id_t {
  return page_ ? page_->GetPageId() : INVALID_PAGE_ID;
}

auto ReadPageGuard::GetData() const -> const char * { return page_->GetData(); }

void ReadPageGuard::Drop() {
  if (page_ == nullptr) { return; }
  page_->RUnlatch();
  if (bpm_ != nullptr) {
    bpm_->UnpinPage(page_->GetPageId(), false);
  }
  bpm_ = nullptr;
  page_ = nullptr;
}

// --- WritePageGuard ---

WritePageGuard::WritePageGuard(BufferPoolManager *bpm, Page *page)
    : bpm_(bpm), page_(page) {
  if (page_) { page_->WLatch(); }
}

WritePageGuard::WritePageGuard(WritePageGuard &&that) noexcept
    : bpm_(that.bpm_), page_(that.page_) {
  that.bpm_ = nullptr;
  that.page_ = nullptr;
}

auto WritePageGuard::operator=(WritePageGuard &&that) -> WritePageGuard & {
  if (this == &that) {
    return *this;
  }
  Drop();
  bpm_ = that.bpm_;
  page_ = that.page_;
  that.bpm_ = nullptr;
  that.page_ = nullptr;
  return *this;
}

WritePageGuard::~WritePageGuard() { Drop(); }

auto WritePageGuard::GetPageId() const -> page_id_t {
  return page_ ? page_->GetPageId() : INVALID_PAGE_ID;
}

auto WritePageGuard::GetData() const -> const char * { return page_->GetData(); }
auto WritePageGuard::GetDataMut() -> char * { return page_->GetData(); }

void WritePageGuard::Drop() {
  if (page_ == nullptr) { return; }
  page_->WUnlatch();
  if (bpm_ != nullptr) {
    bpm_->UnpinPage(page_->GetPageId(), true);
  }
  bpm_ = nullptr;
  page_ = nullptr;
}

}  // namespace onebase

#include "onebase/storage/index/b_plus_tree_iterator.h"
#include <functional>
#include "onebase/buffer/buffer_pool_manager.h"
#include "onebase/common/exception.h"
#include "onebase/common/rid.h"
#include "onebase/storage/page/b_plus_tree_leaf_page.h"

namespace onebase {

template class BPlusTreeIterator<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_ITERATOR_TYPE::BPlusTreeIterator(page_id_t page_id, int index, BufferPoolManager *bpm)
    : page_id_(page_id), index_(index), bpm_(bpm) {}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::IsEnd() const -> bool {
  return page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator*() -> const std::pair<KeyType, ValueType> & {
  using LeafT = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  auto *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<LeafT *>(page->GetData());
  current_ = std::make_pair(leaf->KeyAt(index_), leaf->ValueAt(index_));
  bpm_->UnpinPage(page_id_, false);
  return current_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator++() -> BPlusTreeIterator & {
  using LeafT = BPlusTreeLeafPage<KeyType, ValueType, KeyComparator>;
  auto *page = bpm_->FetchPage(page_id_);
  auto *leaf = reinterpret_cast<LeafT *>(page->GetData());
  index_++;
  if (index_ >= leaf->GetSize()) {
    auto next = leaf->GetNextPageId();
    bpm_->UnpinPage(page_id_, false);
    page_id_ = next;
    index_ = 0;
  } else {
    bpm_->UnpinPage(page_id_, false);
  }
  return *this;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator==(const BPlusTreeIterator &other) const -> bool {
  return page_id_ == other.page_id_ && index_ == other.index_;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_ITERATOR_TYPE::operator!=(const BPlusTreeIterator &other) const -> bool {
  return !(*this == other);
}

}  // namespace onebase

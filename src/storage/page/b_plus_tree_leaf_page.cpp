#include "onebase/storage/page/b_plus_tree_leaf_page.h"
#include <algorithm>
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeLeafPage<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::LEAF_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
  next_page_id_ = INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::KeyIndex(const KeyType &key, const KeyComparator &comparator) const -> int {
  // lower_bound: first i with array_[i].first >= key
  int lo = 0;
  int hi = GetSize();
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (comparator(array_[mid].first, key)) {
      // array_[mid] < key
      lo = mid + 1;
    } else {
      hi = mid;
    }
  }
  return lo;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Lookup(const KeyType &key, ValueType *value,
                                         const KeyComparator &comparator) const -> bool {
  int idx = KeyIndex(key, comparator);
  if (idx >= GetSize()) {
    return false;
  }
  // KeyIndex gives the first not-less-than position; check equality.
  if (!comparator(array_[idx].first, key) && !comparator(key, array_[idx].first)) {
    *value = array_[idx].second;
    return true;
  }
  return false;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::Insert(const KeyType &key, const ValueType &value,
                                         const KeyComparator &comparator) -> int {
  int idx = KeyIndex(key, comparator);
  // Reject duplicates
  if (idx < GetSize() &&
      !comparator(array_[idx].first, key) && !comparator(key, array_[idx].first)) {
    return GetSize();
  }
  // Shift entries to the right to make room
  for (int i = GetSize(); i > idx; --i) {
    array_[i] = array_[i - 1];
  }
  array_[idx].first = key;
  array_[idx].second = value;
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_LEAF_PAGE_TYPE::RemoveAndDeleteRecord(const KeyType &key,
                                                        const KeyComparator &comparator) -> int {
  int idx = KeyIndex(key, comparator);
  if (idx >= GetSize() ||
      comparator(array_[idx].first, key) || comparator(key, array_[idx].first)) {
    return GetSize();  // not found
  }
  for (int i = idx; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveHalfTo(BPlusTreeLeafPage *recipient) {
  int total = GetSize();
  int start = total / 2;  // move the second half
  int moved = total - start;
  for (int i = 0; i < moved; ++i) {
    recipient->array_[i] = array_[start + i];
  }
  recipient->IncreaseSize(moved);
  IncreaseSize(-moved);
  // maintain leaf chain
  recipient->SetNextPageId(GetNextPageId());
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveAllTo(BPlusTreeLeafPage *recipient) {
  int dst = recipient->GetSize();
  for (int i = 0; i < GetSize(); ++i) {
    recipient->array_[dst + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  recipient->SetNextPageId(GetNextPageId());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeLeafPage *recipient) {
  auto first = array_[0];
  for (int i = 0; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
  recipient->array_[recipient->GetSize()] = first;
  recipient->IncreaseSize(1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_LEAF_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeLeafPage *recipient) {
  auto last = array_[GetSize() - 1];
  IncreaseSize(-1);
  for (int i = recipient->GetSize(); i > 0; --i) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[0] = last;
  recipient->IncreaseSize(1);
}

}  // namespace onebase

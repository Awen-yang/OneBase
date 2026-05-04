#include "onebase/storage/page/b_plus_tree_internal_page.h"
#include <functional>
#include "onebase/common/exception.h"

namespace onebase {

template class BPlusTreeInternalPage<int, page_id_t, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Init(int max_size) {
  SetPageType(IndexPageType::INTERNAL_PAGE);
  SetMaxSize(max_size);
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::KeyAt(int index) const -> KeyType {
  return array_[index].first;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetKeyAt(int index, const KeyType &key) {
  array_[index].first = key;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueAt(int index) const -> ValueType {
  return array_[index].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::SetValueAt(int index, const ValueType &value) {
  array_[index].second = value;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::ValueIndex(const ValueType &value) const -> int {
  for (int i = 0; i < GetSize(); ++i) {
    if (array_[i].second == value) {
      return i;
    }
  }
  return -1;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::Lookup(const KeyType &key, const KeyComparator &comparator) const -> ValueType {
  // array_[0].first is unused. Among indices [1, GetSize()), find the first
  // one whose key > `key`; return array_[i-1].second.
  if (GetSize() == 1) {
    return array_[0].second;
  }
  int lo = 1;
  int hi = GetSize();
  while (lo < hi) {
    int mid = (lo + hi) / 2;
    if (comparator(key, array_[mid].first)) {
      hi = mid;
    } else {
      lo = mid + 1;
    }
  }
  return array_[lo - 1].second;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::PopulateNewRoot(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) {
  array_[0].second = old_value;
  array_[1].first = key;
  array_[1].second = new_value;
  SetSize(2);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::InsertNodeAfter(const ValueType &old_value, const KeyType &key,
                                                      const ValueType &new_value) -> int {
  int idx = ValueIndex(old_value);
  for (int i = GetSize(); i > idx + 1; --i) {
    array_[i] = array_[i - 1];
  }
  array_[idx + 1].first = key;
  array_[idx + 1].second = new_value;
  IncreaseSize(1);
  return GetSize();
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::Remove(int index) {
  for (int i = index; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto B_PLUS_TREE_INTERNAL_PAGE_TYPE::RemoveAndReturnOnlyChild() -> ValueType {
  ValueType only = array_[0].second;
  SetSize(0);
  return only;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveAllTo(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // Append our entries to recipient. Our first child has no embedded key, so
  // the parent's separator (middle_key) becomes the key for it.
  int dst = recipient->GetSize();
  recipient->array_[dst].first = middle_key;
  recipient->array_[dst].second = array_[0].second;
  for (int i = 1; i < GetSize(); ++i) {
    recipient->array_[dst + i] = array_[i];
  }
  recipient->IncreaseSize(GetSize());
  SetSize(0);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveHalfTo(BPlusTreeInternalPage *recipient,
                                                 [[maybe_unused]] const KeyType &middle_key) {
  // Move the second half of entries verbatim. The caller takes
  // array_[start].first as the key to push up to the parent.
  int total = GetSize();
  int start = total / 2;
  int moved = total - start;
  for (int i = 0; i < moved; ++i) {
    recipient->array_[i] = array_[start + i];
  }
  recipient->IncreaseSize(moved);
  IncreaseSize(-moved);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveFirstToEndOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // Append our first child to recipient using middle_key as its separator;
  // shift our remaining entries left by one.
  int dst = recipient->GetSize();
  recipient->array_[dst].first = middle_key;
  recipient->array_[dst].second = array_[0].second;
  recipient->IncreaseSize(1);
  for (int i = 0; i < GetSize() - 1; ++i) {
    array_[i] = array_[i + 1];
  }
  IncreaseSize(-1);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void B_PLUS_TREE_INTERNAL_PAGE_TYPE::MoveLastToFrontOf(BPlusTreeInternalPage *recipient, const KeyType &middle_key) {
  // Donate our last entry to the front of recipient. The parent's old
  // separator (middle_key) sinks into recipient's array_[1].first; recipient's
  // original first child stays put after the shift; our donated child becomes
  // recipient's new array_[0].second. The caller updates the parent's
  // separator to our donated key.
  auto last = array_[GetSize() - 1];
  IncreaseSize(-1);
  for (int i = recipient->GetSize(); i > 0; --i) {
    recipient->array_[i] = recipient->array_[i - 1];
  }
  recipient->array_[1].first = middle_key;
  recipient->array_[0].second = last.second;
  recipient->IncreaseSize(1);
}

}  // namespace onebase

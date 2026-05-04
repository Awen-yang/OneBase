#include "onebase/storage/index/b_plus_tree.h"
#include <functional>
#include <vector>
#include "onebase/common/exception.h"
#include "onebase/storage/index/b_plus_tree_iterator.h"

namespace onebase {

template class BPlusTree<int, RID, std::less<int>>;

template <typename KeyType, typename ValueType, typename KeyComparator>
BPLUSTREE_TYPE::BPlusTree(std::string name, BufferPoolManager *bpm, const KeyComparator &comparator,
                           int leaf_max_size, int internal_max_size)
    : Index(std::move(name)), bpm_(bpm), comparator_(comparator),
      leaf_max_size_(leaf_max_size), internal_max_size_(internal_max_size) {
  if (leaf_max_size_ == 0) {
    leaf_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage) - sizeof(page_id_t)) /
        (sizeof(KeyType) + sizeof(ValueType)));
  }
  if (internal_max_size_ == 0) {
    internal_max_size_ = static_cast<int>(
        (ONEBASE_PAGE_SIZE - sizeof(BPlusTreePage)) /
        (sizeof(KeyType) + sizeof(page_id_t)));
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::IsEmpty() const -> bool {
  return root_page_id_ == INVALID_PAGE_ID;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::FindLeaf(const KeyType &key, std::vector<page_id_t> *path) -> page_id_t {
  page_id_t pid = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(pid);
    auto *bp = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (bp->IsLeafPage()) {
      bpm_->UnpinPage(pid, false);
      return pid;
    }
    if (path != nullptr) {
      path->push_back(pid);
    }
    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    auto child = internal->Lookup(key, comparator_);
    bpm_->UnpinPage(pid, false);
    pid = child;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Reparent(page_id_t child_pid, page_id_t parent_pid) {
  auto *page = bpm_->FetchPage(child_pid);
  reinterpret_cast<BPlusTreePage *>(page->GetData())->SetParentPageId(parent_pid);
  bpm_->UnpinPage(child_pid, true);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Insert(const KeyType &key, const ValueType &value) -> bool {
  // Empty tree: create the first leaf and make it root.
  if (root_page_id_ == INVALID_PAGE_ID) {
    page_id_t pid;
    auto *page = bpm_->NewPage(&pid);
    auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
    leaf->Init(leaf_max_size_);
    leaf->SetParentPageId(INVALID_PAGE_ID);
    leaf->Insert(key, value, comparator_);
    bpm_->UnpinPage(pid, true);
    root_page_id_ = pid;
    return true;
  }

  std::vector<page_id_t> path;
  auto leaf_pid = FindLeaf(key, &path);

  auto *leaf_page = bpm_->FetchPage(leaf_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

  ValueType existing;
  if (leaf->Lookup(key, &existing, comparator_)) {
    bpm_->UnpinPage(leaf_pid, false);
    return false;  // duplicate
  }

  leaf->Insert(key, value, comparator_);

  if (leaf->GetSize() <= leaf_max_size_) {
    bpm_->UnpinPage(leaf_pid, true);
    return true;
  }

  // Split the leaf and prepare the (key, child) entry to push to the parent.
  page_id_t new_leaf_pid;
  auto *new_leaf_page = bpm_->NewPage(&new_leaf_pid);
  auto *new_leaf = reinterpret_cast<LeafPage *>(new_leaf_page->GetData());
  new_leaf->Init(leaf_max_size_);
  auto old_next = leaf->GetNextPageId();
  leaf->MoveHalfTo(new_leaf);
  new_leaf->SetNextPageId(old_next);
  leaf->SetNextPageId(new_leaf_pid);
  new_leaf->SetParentPageId(leaf->GetParentPageId());

  KeyType push_up_key = new_leaf->KeyAt(0);
  page_id_t push_up_child = new_leaf_pid;
  page_id_t left_child = leaf_pid;

  bpm_->UnpinPage(leaf_pid, true);
  bpm_->UnpinPage(new_leaf_pid, true);

  // Walk up the path, splitting internal pages as needed.
  while (true) {
    if (path.empty()) {
      // Grow the tree by one level.
      page_id_t new_root_pid;
      auto *new_root_page = bpm_->NewPage(&new_root_pid);
      auto *new_root = reinterpret_cast<InternalPage *>(new_root_page->GetData());
      new_root->Init(internal_max_size_);
      new_root->SetParentPageId(INVALID_PAGE_ID);
      new_root->PopulateNewRoot(left_child, push_up_key, push_up_child);
      Reparent(left_child, new_root_pid);
      Reparent(push_up_child, new_root_pid);
      bpm_->UnpinPage(new_root_pid, true);
      root_page_id_ = new_root_pid;
      return true;
    }

    page_id_t parent_pid = path.back();
    path.pop_back();
    auto *parent_page = bpm_->FetchPage(parent_pid);
    auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
    parent->InsertNodeAfter(left_child, push_up_key, push_up_child);
    Reparent(push_up_child, parent_pid);

    if (parent->GetSize() <= internal_max_size_) {
      bpm_->UnpinPage(parent_pid, true);
      return true;
    }

    page_id_t new_internal_pid;
    auto *new_internal_page = bpm_->NewPage(&new_internal_pid);
    auto *new_internal = reinterpret_cast<InternalPage *>(new_internal_page->GetData());
    new_internal->Init(internal_max_size_);
    new_internal->SetParentPageId(parent->GetParentPageId());

    int split_at = parent->GetSize() / 2;
    KeyType new_push_up_key = parent->KeyAt(split_at);
    int moved = parent->GetSize() - split_at;
    for (int i = 0; i < moved; ++i) {
      new_internal->SetKeyAt(i, parent->KeyAt(split_at + i));
      new_internal->SetValueAt(i, parent->ValueAt(split_at + i));
    }
    new_internal->IncreaseSize(moved);
    parent->IncreaseSize(-moved);
    for (int i = 0; i < new_internal->GetSize(); ++i) {
      Reparent(new_internal->ValueAt(i), new_internal_pid);
    }

    push_up_key = new_push_up_key;
    push_up_child = new_internal_pid;
    left_child = parent_pid;

    bpm_->UnpinPage(parent_pid, true);
    bpm_->UnpinPage(new_internal_pid, true);
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
void BPLUSTREE_TYPE::Remove(const KeyType &key) {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return;
  }

  std::vector<page_id_t> path;
  auto leaf_pid = FindLeaf(key, &path);

  auto *leaf_page = bpm_->FetchPage(leaf_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(leaf_page->GetData());

  int before = leaf->GetSize();
  leaf->RemoveAndDeleteRecord(key, comparator_);
  if (leaf->GetSize() == before) {
    bpm_->UnpinPage(leaf_pid, false);
    return;  // key not present
  }

  // The root leaf can shrink to empty; that just empties the tree.
  if (leaf_pid == root_page_id_) {
    if (leaf->GetSize() == 0) {
      bpm_->UnpinPage(leaf_pid, true);
      bpm_->DeletePage(leaf_pid);
      root_page_id_ = INVALID_PAGE_ID;
    } else {
      bpm_->UnpinPage(leaf_pid, true);
    }
    return;
  }

  if (leaf->GetSize() >= leaf->GetMinSize()) {
    bpm_->UnpinPage(leaf_pid, true);
    return;
  }

  // Underflow: borrow from or merge with a sibling.
  page_id_t parent_pid = path.back();
  path.pop_back();
  bpm_->UnpinPage(leaf_pid, true);

  auto *parent_page = bpm_->FetchPage(parent_pid);
  auto *parent = reinterpret_cast<InternalPage *>(parent_page->GetData());
  int my_idx = parent->ValueIndex(leaf_pid);

  bool sib_is_left = my_idx > 0;
  page_id_t sib_pid = sib_is_left ? parent->ValueAt(my_idx - 1)
                                  : parent->ValueAt(my_idx + 1);

  auto *me_page = bpm_->FetchPage(leaf_pid);
  auto *me = reinterpret_cast<LeafPage *>(me_page->GetData());
  auto *sib_page = bpm_->FetchPage(sib_pid);
  auto *sib = reinterpret_cast<LeafPage *>(sib_page->GetData());

  if (sib->GetSize() + me->GetSize() > leaf_max_size_) {
    // Borrow one entry.
    if (sib_is_left) {
      sib->MoveLastToFrontOf(me);
      parent->SetKeyAt(my_idx, me->KeyAt(0));
    } else {
      sib->MoveFirstToEndOf(me);
      parent->SetKeyAt(my_idx + 1, sib->KeyAt(0));
    }
    bpm_->UnpinPage(leaf_pid, true);
    bpm_->UnpinPage(sib_pid, true);
    bpm_->UnpinPage(parent_pid, true);
    return;
  }

  // Merge: always preserve the left sibling and drop the right.
  page_id_t to_delete;
  int sep_idx;
  if (sib_is_left) {
    me->MoveAllTo(sib);
    sib->SetNextPageId(me->GetNextPageId());
    bpm_->UnpinPage(sib_pid, true);
    bpm_->UnpinPage(leaf_pid, true);
    to_delete = leaf_pid;
    sep_idx = my_idx;
  } else {
    sib->MoveAllTo(me);
    me->SetNextPageId(sib->GetNextPageId());
    bpm_->UnpinPage(leaf_pid, true);
    bpm_->UnpinPage(sib_pid, true);
    to_delete = sib_pid;
    sep_idx = my_idx + 1;
  }
  bpm_->DeletePage(to_delete);
  parent->Remove(sep_idx);

  // Propagate underflow up through internal nodes.
  while (true) {
    if (parent_pid == root_page_id_) {
      if (parent->GetSize() == 1) {
        // Root has one child left: that child becomes the new root.
        page_id_t only = parent->ValueAt(0);
        Reparent(only, INVALID_PAGE_ID);
        bpm_->UnpinPage(parent_pid, true);
        bpm_->DeletePage(parent_pid);
        root_page_id_ = only;
      } else {
        bpm_->UnpinPage(parent_pid, true);
      }
      return;
    }

    if (parent->GetSize() >= parent->GetMinSize()) {
      bpm_->UnpinPage(parent_pid, true);
      return;
    }

    page_id_t grand_pid = path.back();
    path.pop_back();
    bpm_->UnpinPage(parent_pid, true);

    auto *grand_page = bpm_->FetchPage(grand_pid);
    auto *grand = reinterpret_cast<InternalPage *>(grand_page->GetData());
    int gp_idx = grand->ValueIndex(parent_pid);

    bool sib2_is_left = gp_idx > 0;
    page_id_t sib2_pid = sib2_is_left ? grand->ValueAt(gp_idx - 1)
                                      : grand->ValueAt(gp_idx + 1);

    auto *p2_page = bpm_->FetchPage(parent_pid);
    auto *p2 = reinterpret_cast<InternalPage *>(p2_page->GetData());
    auto *s2_page = bpm_->FetchPage(sib2_pid);
    auto *s2 = reinterpret_cast<InternalPage *>(s2_page->GetData());

    if (s2->GetSize() + p2->GetSize() > internal_max_size_) {
      // Internal redistribute.
      if (sib2_is_left) {
        KeyType middle = grand->KeyAt(gp_idx);
        auto donated_child = s2->ValueAt(s2->GetSize() - 1);
        auto donated_key = s2->KeyAt(s2->GetSize() - 1);
        s2->IncreaseSize(-1);
        for (int i = p2->GetSize(); i > 0; --i) {
          p2->SetKeyAt(i, p2->KeyAt(i - 1));
          p2->SetValueAt(i, p2->ValueAt(i - 1));
        }
        p2->SetKeyAt(1, middle);
        p2->SetValueAt(0, donated_child);
        p2->IncreaseSize(1);
        grand->SetKeyAt(gp_idx, donated_key);
        Reparent(donated_child, parent_pid);
      } else {
        KeyType middle = grand->KeyAt(gp_idx + 1);
        auto donated_child = s2->ValueAt(0);
        auto donated_key = s2->KeyAt(1);
        p2->SetKeyAt(p2->GetSize(), middle);
        p2->SetValueAt(p2->GetSize(), donated_child);
        p2->IncreaseSize(1);
        for (int i = 0; i < s2->GetSize() - 1; ++i) {
          s2->SetKeyAt(i, s2->KeyAt(i + 1));
          s2->SetValueAt(i, s2->ValueAt(i + 1));
        }
        s2->IncreaseSize(-1);
        grand->SetKeyAt(gp_idx + 1, donated_key);
        Reparent(donated_child, parent_pid);
      }
      bpm_->UnpinPage(parent_pid, true);
      bpm_->UnpinPage(sib2_pid, true);
      bpm_->UnpinPage(grand_pid, true);
      return;
    }

    // Internal merge: preserve the left, drop the right.
    page_id_t to_delete2;
    int sep2_idx;
    if (sib2_is_left) {
      KeyType middle = grand->KeyAt(gp_idx);
      int dst = s2->GetSize();
      s2->SetKeyAt(dst, middle);
      s2->SetValueAt(dst, p2->ValueAt(0));
      for (int i = 1; i < p2->GetSize(); ++i) {
        s2->SetKeyAt(dst + i, p2->KeyAt(i));
        s2->SetValueAt(dst + i, p2->ValueAt(i));
      }
      s2->IncreaseSize(p2->GetSize());
      for (int i = dst; i < s2->GetSize(); ++i) {
        Reparent(s2->ValueAt(i), sib2_pid);
      }
      p2->SetSize(0);
      bpm_->UnpinPage(sib2_pid, true);
      bpm_->UnpinPage(parent_pid, true);
      to_delete2 = parent_pid;
      sep2_idx = gp_idx;
    } else {
      KeyType middle = grand->KeyAt(gp_idx + 1);
      int dst = p2->GetSize();
      p2->SetKeyAt(dst, middle);
      p2->SetValueAt(dst, s2->ValueAt(0));
      for (int i = 1; i < s2->GetSize(); ++i) {
        p2->SetKeyAt(dst + i, s2->KeyAt(i));
        p2->SetValueAt(dst + i, s2->ValueAt(i));
      }
      p2->IncreaseSize(s2->GetSize());
      for (int i = dst; i < p2->GetSize(); ++i) {
        Reparent(p2->ValueAt(i), parent_pid);
      }
      s2->SetSize(0);
      bpm_->UnpinPage(parent_pid, true);
      bpm_->UnpinPage(sib2_pid, true);
      to_delete2 = sib2_pid;
      sep2_idx = gp_idx + 1;
    }
    bpm_->DeletePage(to_delete2);
    grand->Remove(sep2_idx);

    parent_pid = grand_pid;
    parent = grand;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::GetValue(const KeyType &key, std::vector<ValueType> *result) -> bool {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return false;
  }
  auto leaf_pid = FindLeaf(key, nullptr);
  auto *page = bpm_->FetchPage(leaf_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
  ValueType v;
  bool found = leaf->Lookup(key, &v, comparator_);
  if (found) {
    result->push_back(v);
  }
  bpm_->UnpinPage(leaf_pid, false);
  return found;
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin() -> Iterator {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return End();
  }
  page_id_t pid = root_page_id_;
  while (true) {
    auto *page = bpm_->FetchPage(pid);
    auto *bp = reinterpret_cast<BPlusTreePage *>(page->GetData());
    if (bp->IsLeafPage()) {
      bpm_->UnpinPage(pid, false);
      return Iterator(pid, 0, bpm_);
    }
    auto *internal = reinterpret_cast<InternalPage *>(page->GetData());
    page_id_t child = internal->ValueAt(0);
    bpm_->UnpinPage(pid, false);
    pid = child;
  }
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::Begin(const KeyType &key) -> Iterator {
  if (root_page_id_ == INVALID_PAGE_ID) {
    return End();
  }
  auto leaf_pid = FindLeaf(key, nullptr);
  auto *page = bpm_->FetchPage(leaf_pid);
  auto *leaf = reinterpret_cast<LeafPage *>(page->GetData());
  int idx = leaf->KeyIndex(key, comparator_);
  if (idx >= leaf->GetSize()) {
    auto next = leaf->GetNextPageId();
    bpm_->UnpinPage(leaf_pid, false);
    if (next == INVALID_PAGE_ID) {
      return End();
    }
    return Iterator(next, 0, bpm_);
  }
  bpm_->UnpinPage(leaf_pid, false);
  return Iterator(leaf_pid, idx, bpm_);
}

template <typename KeyType, typename ValueType, typename KeyComparator>
auto BPLUSTREE_TYPE::End() -> Iterator {
  return Iterator(INVALID_PAGE_ID, 0);
}

}  // namespace onebase

#include "onebase/concurrency/lock_manager.h"

namespace onebase {

auto LockManager::OnlySharedHeld(const LockRequestQueue &q, txn_id_t exclude) -> bool {
  for (const auto &req : q.request_queue_) {
    if (req.txn_id_ == exclude) {
      continue;
    }
    if (req.granted_ && req.lock_mode_ == LockMode::EXCLUSIVE) {
      return false;
    }
  }
  return true;
}

auto LockManager::AnyOtherGranted(const LockRequestQueue &q, txn_id_t exclude) -> bool {
  for (const auto &req : q.request_queue_) {
    if (req.txn_id_ == exclude) {
      continue;
    }
    if (req.granted_) {
      return true;
    }
  }
  return false;
}

auto LockManager::LockShared(Transaction *txn, const RID &rid) -> bool {
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (txn->GetState() != TransactionState::GROWING) {
    return false;
  }
  if (txn->IsSharedLocked(rid) || txn->IsExclusiveLocked(rid)) {
    return true;
  }

  std::unique_lock<std::mutex> lk(latch_);
  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::SHARED);
  auto my_it = std::prev(queue.request_queue_.end());

  queue.cv_.wait(lk, [&]() {
    return txn->GetState() == TransactionState::ABORTED ||
           OnlySharedHeld(queue, txn->GetTransactionId());
  });

  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(my_it);
    return false;
  }

  my_it->granted_ = true;
  txn->GetSharedLockSet()->insert(rid);
  return true;
}

auto LockManager::LockExclusive(Transaction *txn, const RID &rid) -> bool {
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (txn->GetState() != TransactionState::GROWING) {
    return false;
  }
  if (txn->IsExclusiveLocked(rid)) {
    return true;
  }

  std::unique_lock<std::mutex> lk(latch_);
  auto &queue = lock_table_[rid];
  queue.request_queue_.emplace_back(txn->GetTransactionId(), LockMode::EXCLUSIVE);
  auto my_it = std::prev(queue.request_queue_.end());

  queue.cv_.wait(lk, [&]() {
    return txn->GetState() == TransactionState::ABORTED ||
           !AnyOtherGranted(queue, txn->GetTransactionId());
  });

  if (txn->GetState() == TransactionState::ABORTED) {
    queue.request_queue_.erase(my_it);
    return false;
  }

  my_it->granted_ = true;
  txn->GetExclusiveLockSet()->insert(rid);
  return true;
}

auto LockManager::LockUpgrade(Transaction *txn, const RID &rid) -> bool {
  if (txn->GetState() == TransactionState::SHRINKING) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  if (!txn->IsSharedLocked(rid)) {
    return false;
  }

  std::unique_lock<std::mutex> lk(latch_);
  auto &queue = lock_table_[rid];
  if (queue.upgrading_) {
    txn->SetState(TransactionState::ABORTED);
    return false;
  }
  queue.upgrading_ = true;

  auto my_it = std::find_if(queue.request_queue_.begin(), queue.request_queue_.end(),
                            [&](const LockRequest &r) { return r.txn_id_ == txn->GetTransactionId(); });
  if (my_it == queue.request_queue_.end()) {
    queue.upgrading_ = false;
    return false;
  }

  queue.cv_.wait(lk, [&]() { return !AnyOtherGranted(queue, txn->GetTransactionId()); });

  my_it->lock_mode_ = LockMode::EXCLUSIVE;
  my_it->granted_ = true;
  queue.upgrading_ = false;
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->insert(rid);
  return true;
}

auto LockManager::Unlock(Transaction *txn, const RID &rid) -> bool {
  std::unique_lock<std::mutex> lk(latch_);
  auto qit = lock_table_.find(rid);
  if (qit == lock_table_.end()) {
    return false;
  }
  auto &queue = qit->second;
  auto it = std::find_if(queue.request_queue_.begin(), queue.request_queue_.end(),
                         [&](const LockRequest &r) { return r.txn_id_ == txn->GetTransactionId(); });
  if (it != queue.request_queue_.end()) {
    queue.request_queue_.erase(it);
  }
  txn->GetSharedLockSet()->erase(rid);
  txn->GetExclusiveLockSet()->erase(rid);
  if (txn->GetState() == TransactionState::GROWING) {
    txn->SetState(TransactionState::SHRINKING);
  }
  queue.cv_.notify_all();
  return true;
}

}  // namespace onebase

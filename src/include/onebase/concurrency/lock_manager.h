#pragma once
#include <algorithm>
#include <condition_variable>
#include <list>
#include <mutex>
#include <unordered_map>
#include "onebase/common/rid.h"
#include "onebase/concurrency/transaction.h"

namespace onebase {

enum class LockMode { SHARED, EXCLUSIVE };

class LockManager {
 public:
  LockManager() = default;
  ~LockManager() = default;

  auto LockShared(Transaction *txn, const RID &rid) -> bool;
  auto LockExclusive(Transaction *txn, const RID &rid) -> bool;
  auto LockUpgrade(Transaction *txn, const RID &rid) -> bool;
  auto Unlock(Transaction *txn, const RID &rid) -> bool;

 private:
  struct LockRequest {
    txn_id_t txn_id_;
    LockMode lock_mode_;
    bool granted_{false};

    LockRequest(txn_id_t txn_id, LockMode lock_mode)
        : txn_id_(txn_id), lock_mode_(lock_mode) {}
  };

  struct LockRequestQueue {
    std::list<LockRequest> request_queue_;
    std::condition_variable cv_;
    bool upgrading_{false};
  };

  static auto OnlySharedHeld(const LockRequestQueue &q, txn_id_t exclude) -> bool;
  static auto AnyOtherGranted(const LockRequestQueue &q, txn_id_t exclude) -> bool;

  std::mutex latch_;
  std::unordered_map<RID, LockRequestQueue> lock_table_;
};

}  // namespace onebase

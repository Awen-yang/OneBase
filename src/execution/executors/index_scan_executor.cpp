#include "onebase/execution/executors/index_scan_executor.h"

namespace onebase {

IndexScanExecutor::IndexScanExecutor(ExecutorContext *exec_ctx, const IndexScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void IndexScanExecutor::Init() {
  auto *catalog = GetExecutorContext()->GetCatalog();
  index_info_ = catalog->GetIndex(plan_->GetIndexOid());
  table_info_ = catalog->GetTable(plan_->GetTableOid());
  matching_rids_.clear();
  cursor_ = 0;

  // Evaluate the lookup key with no input tuple — it should be a constant.
  auto key_val = plan_->GetLookupKey()->Evaluate(nullptr, nullptr);
  auto *rids = index_info_->LookupInteger(key_val.GetAsInteger());
  if (rids != nullptr) {
    matching_rids_ = *rids;
  }
}

auto IndexScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (cursor_ < matching_rids_.size()) {
    auto r = matching_rids_[cursor_++];
    auto raw = table_info_->table_->GetTuple(r);
    std::vector<Value> vals;
    vals.reserve(table_info_->schema_.GetColumnCount());
    for (uint32_t i = 0; i < table_info_->schema_.GetColumnCount(); ++i) {
      vals.push_back(raw.GetValue(&table_info_->schema_, i));
    }
    Tuple shaped(std::move(vals));
    shaped.SetRID(r);

    const auto &pred = plan_->GetPredicate();
    if (pred != nullptr) {
      auto v = pred->Evaluate(&shaped, &table_info_->schema_);
      if (!v.GetAsBoolean()) {
        continue;
      }
    }
    *tuple = shaped;
    *rid = r;
    return true;
  }
  return false;
}

}  // namespace onebase

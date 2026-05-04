#include "onebase/execution/executors/seq_scan_executor.h"

namespace onebase {

SeqScanExecutor::SeqScanExecutor(ExecutorContext *exec_ctx, const SeqScanPlanNode *plan)
    : AbstractExecutor(exec_ctx), plan_(plan) {}

void SeqScanExecutor::Init() {
  table_info_ = GetExecutorContext()->GetCatalog()->GetTable(plan_->GetTableOid());
  iter_ = table_info_->table_->Begin();
  end_ = table_info_->table_->End();
}

auto SeqScanExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  while (iter_ != end_) {
    auto raw = *iter_;
    auto r = iter_.GetRID();
    ++iter_;

    // Tuples loaded from disk only have the byte buffer; rebuild values_ so
    // ColumnValueExpression::Evaluate (which uses GetValue without a schema)
    // works on the predicate path.
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

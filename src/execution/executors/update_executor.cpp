#include "onebase/execution/executors/update_executor.h"

namespace onebase {

UpdateExecutor::UpdateExecutor(ExecutorContext *exec_ctx, const UpdatePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void UpdateExecutor::Init() {
  child_executor_->Init();
  has_updated_ = false;
}

auto UpdateExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_updated_) {
    return false;
  }
  has_updated_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);
  const auto &exprs = plan_->GetUpdateExpressions();

  int32_t updated_count = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    std::vector<Value> new_vals;
    new_vals.reserve(exprs.size());
    for (const auto &e : exprs) {
      new_vals.push_back(e->Evaluate(&child_tuple, &table_info->schema_));
    }
    Tuple new_tuple(new_vals);

    // For index maintenance, capture old key values first
    std::vector<std::pair<IndexInfo *, int32_t>> old_index_keys;
    for (auto *idx : indexes) {
      if (!idx->SupportsPointLookup()) {
        continue;
      }
      auto k = child_tuple.GetValue(&table_info->schema_, idx->GetLookupAttr()).GetAsInteger();
      old_index_keys.emplace_back(idx, k);
    }

    bool ok = table_info->table_->UpdateTuple(child_rid, new_tuple);
    if (!ok) {
      continue;
    }
    updated_count++;

    for (auto &[idx, old_k] : old_index_keys) {
      idx->RemoveEntry(old_k, child_rid);
      auto new_k = new_tuple.GetValue(&table_info->schema_, idx->GetLookupAttr()).GetAsInteger();
      idx->InsertEntry(new_k, child_rid);
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, updated_count)});
  *rid = RID{};
  return true;
}

}  // namespace onebase

#include "onebase/execution/executors/insert_executor.h"

namespace onebase {

InsertExecutor::InsertExecutor(ExecutorContext *exec_ctx, const InsertPlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void InsertExecutor::Init() {
  child_executor_->Init();
  has_inserted_ = false;
}

auto InsertExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_inserted_) {
    return false;
  }
  has_inserted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t inserted_count = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    auto inserted_rid = table_info->table_->InsertTuple(child_tuple);
    if (!inserted_rid.has_value()) {
      continue;
    }
    inserted_count++;
    // Maintain in-memory point-lookup indexes (integer keys only).
    for (auto *idx : indexes) {
      if (!idx->SupportsPointLookup()) {
        continue;
      }
      auto key_val = child_tuple.GetValue(&table_info->schema_, idx->GetLookupAttr());
      idx->InsertEntry(key_val.GetAsInteger(), inserted_rid.value());
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, inserted_count)});
  *rid = RID{};
  return true;
}

}  // namespace onebase

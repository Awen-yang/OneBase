#include "onebase/execution/executors/delete_executor.h"

namespace onebase {

DeleteExecutor::DeleteExecutor(ExecutorContext *exec_ctx, const DeletePlanNode *plan,
                               std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void DeleteExecutor::Init() {
  child_executor_->Init();
  has_deleted_ = false;
}

auto DeleteExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (has_deleted_) {
    return false;
  }
  has_deleted_ = true;

  auto *catalog = GetExecutorContext()->GetCatalog();
  auto *table_info = catalog->GetTable(plan_->GetTableOid());
  auto indexes = catalog->GetTableIndexes(table_info->name_);

  int32_t deleted_count = 0;
  Tuple child_tuple;
  RID child_rid;
  while (child_executor_->Next(&child_tuple, &child_rid)) {
    table_info->table_->DeleteTuple(child_rid);
    deleted_count++;
    for (auto *idx : indexes) {
      if (!idx->SupportsPointLookup()) {
        continue;
      }
      auto key_val = child_tuple.GetValue(&table_info->schema_, idx->GetLookupAttr());
      idx->RemoveEntry(key_val.GetAsInteger(), child_rid);
    }
  }

  *tuple = Tuple({Value(TypeId::INTEGER, deleted_count)});
  *rid = RID{};
  return true;
}

}  // namespace onebase

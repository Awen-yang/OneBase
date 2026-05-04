#include "onebase/execution/executors/sort_executor.h"
#include <algorithm>

namespace onebase {

SortExecutor::SortExecutor(ExecutorContext *exec_ctx, const SortPlanNode *plan,
                            std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void SortExecutor::Init() {
  child_executor_->Init();
  sorted_tuples_.clear();
  cursor_ = 0;

  Tuple t;
  RID r;
  while (child_executor_->Next(&t, &r)) {
    sorted_tuples_.push_back(t);
  }

  const auto &child_schema = child_executor_->GetOutputSchema();
  const auto &order_bys = plan_->GetOrderBys();

  std::sort(sorted_tuples_.begin(), sorted_tuples_.end(),
            [&](const Tuple &a, const Tuple &b) {
              for (const auto &[asc, expr] : order_bys) {
                auto va = expr->Evaluate(&a, &child_schema);
                auto vb = expr->Evaluate(&b, &child_schema);
                if (va.CompareEquals(vb).GetAsBoolean()) {
                  continue;  // tie, look at next key
                }
                bool a_less = va.CompareLessThan(vb).GetAsBoolean();
                return asc ? a_less : !a_less;
              }
              return false;
            });
}

auto SortExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= sorted_tuples_.size()) {
    return false;
  }
  *tuple = sorted_tuples_[cursor_++];
  *rid = RID{};
  return true;
}

}  // namespace onebase

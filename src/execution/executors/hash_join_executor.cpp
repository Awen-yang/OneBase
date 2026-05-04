#include "onebase/execution/executors/hash_join_executor.h"

namespace onebase {

HashJoinExecutor::HashJoinExecutor(ExecutorContext *exec_ctx, const HashJoinPlanNode *plan,
                                    std::unique_ptr<AbstractExecutor> left_executor,
                                    std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void HashJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  hash_table_.clear();
  result_tuples_.clear();
  cursor_ = 0;

  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();
  const auto &out_schema = plan_->GetOutputSchema();

  // Build phase: bucket left rows by their join key.
  Tuple lt;
  RID lr;
  while (left_executor_->Next(&lt, &lr)) {
    auto k = plan_->GetLeftKeyExpression()->Evaluate(&lt, &left_schema);
    hash_table_[k.ToString()].push_back(lt);
  }

  // Probe phase
  Tuple rt;
  RID rr;
  while (right_executor_->Next(&rt, &rr)) {
    auto k = plan_->GetRightKeyExpression()->Evaluate(&rt, &right_schema);
    auto it = hash_table_.find(k.ToString());
    if (it == hash_table_.end()) {
      continue;
    }
    for (const auto &left_tuple : it->second) {
      std::vector<Value> vals;
      vals.reserve(out_schema.GetColumnCount());
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
        vals.push_back(left_tuple.GetValue(&left_schema, i));
      }
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        vals.push_back(rt.GetValue(&right_schema, i));
      }
      result_tuples_.emplace_back(vals);
    }
  }
}

auto HashJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = RID{};
  return true;
}

}  // namespace onebase

#include "onebase/execution/executors/nested_loop_join_executor.h"

namespace onebase {

NestedLoopJoinExecutor::NestedLoopJoinExecutor(ExecutorContext *exec_ctx,
                                                const NestedLoopJoinPlanNode *plan,
                                                std::unique_ptr<AbstractExecutor> left_executor,
                                                std::unique_ptr<AbstractExecutor> right_executor)
    : AbstractExecutor(exec_ctx), plan_(plan),
      left_executor_(std::move(left_executor)), right_executor_(std::move(right_executor)) {}

void NestedLoopJoinExecutor::Init() {
  left_executor_->Init();
  right_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  // Materialize the right side once so we can rewind through it for each
  // left row. (Not optimal but simple and works for the test sizes.)
  std::vector<Tuple> right_tuples;
  Tuple r;
  RID r_rid;
  while (right_executor_->Next(&r, &r_rid)) {
    right_tuples.push_back(r);
  }

  const auto &left_schema = left_executor_->GetOutputSchema();
  const auto &right_schema = right_executor_->GetOutputSchema();
  const auto &out_schema = plan_->GetOutputSchema();
  const auto &pred = plan_->GetPredicate();

  Tuple l;
  RID l_rid;
  while (left_executor_->Next(&l, &l_rid)) {
    for (const auto &rt : right_tuples) {
      bool keep = true;
      if (pred != nullptr) {
        auto v = pred->EvaluateJoin(&l, &left_schema, &rt, &right_schema);
        keep = v.GetAsBoolean();
      }
      if (!keep) {
        continue;
      }
      // Concatenate left + right values
      std::vector<Value> vals;
      vals.reserve(out_schema.GetColumnCount());
      for (uint32_t i = 0; i < left_schema.GetColumnCount(); ++i) {
        vals.push_back(l.GetValue(&left_schema, i));
      }
      for (uint32_t i = 0; i < right_schema.GetColumnCount(); ++i) {
        vals.push_back(rt.GetValue(&right_schema, i));
      }
      result_tuples_.emplace_back(vals);
    }
  }
}

auto NestedLoopJoinExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = RID{};
  return true;
}

}  // namespace onebase

#include "onebase/execution/executors/projection_executor.h"

namespace onebase {

ProjectionExecutor::ProjectionExecutor(ExecutorContext *exec_ctx, const ProjectionPlanNode *plan,
                                        std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

void ProjectionExecutor::Init() {
  child_executor_->Init();
}

auto ProjectionExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  Tuple in;
  RID in_rid;
  if (!child_executor_->Next(&in, &in_rid)) {
    return false;
  }
  std::vector<Value> out;
  out.reserve(plan_->GetExpressions().size());
  for (const auto &expr : plan_->GetExpressions()) {
    out.push_back(expr->Evaluate(&in, &child_executor_->GetOutputSchema()));
  }
  *tuple = Tuple(out);
  *rid = in_rid;
  return true;
}

}  // namespace onebase

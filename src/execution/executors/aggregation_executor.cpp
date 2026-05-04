#include "onebase/execution/executors/aggregation_executor.h"
#include <map>

namespace onebase {

AggregationExecutor::AggregationExecutor(ExecutorContext *exec_ctx, const AggregationPlanNode *plan,
                                          std::unique_ptr<AbstractExecutor> child_executor)
    : AbstractExecutor(exec_ctx), plan_(plan), child_executor_(std::move(child_executor)) {}

namespace {

// One running aggregator per group; each entry mirrors plan_->GetAggregates().
struct AggState {
  Value cur;
  bool has_value{false};
};

// Initialize the starting Value for a given aggregate type.
auto MakeInitial(AggregationType type) -> AggState {
  AggState s;
  switch (type) {
    case AggregationType::CountStarAggregate:
    case AggregationType::CountAggregate:
      s.cur = Value(TypeId::INTEGER, 0);
      s.has_value = true;
      break;
    case AggregationType::SumAggregate:
      s.cur = Value(TypeId::INTEGER, 0);
      s.has_value = true;
      break;
    case AggregationType::MinAggregate:
    case AggregationType::MaxAggregate:
      s.has_value = false;  // wait for first input value
      break;
  }
  return s;
}

}  // namespace

void AggregationExecutor::Init() {
  child_executor_->Init();
  result_tuples_.clear();
  cursor_ = 0;

  const auto &child_schema = child_executor_->GetOutputSchema();
  const auto &group_bys = plan_->GetGroupBys();
  const auto &aggs = plan_->GetAggregates();
  const auto &agg_types = plan_->GetAggregateTypes();

  // Map from a stringified group key to its running aggregate state.
  // We use std::map (ordered) so output is deterministic when grouping.
  std::map<std::string, std::pair<std::vector<Value>, std::vector<AggState>>> table;

  Tuple t;
  RID r;
  bool saw_any = false;
  while (child_executor_->Next(&t, &r)) {
    saw_any = true;
    std::vector<Value> group_keys;
    std::string key_str;
    for (const auto &g : group_bys) {
      auto v = g->Evaluate(&t, &child_schema);
      group_keys.push_back(v);
      key_str += v.ToString();
      key_str += "|";
    }

    auto it = table.find(key_str);
    if (it == table.end()) {
      std::vector<AggState> states;
      states.reserve(aggs.size());
      for (auto type : agg_types) {
        states.push_back(MakeInitial(type));
      }
      auto inserted = table.emplace(key_str, std::make_pair(group_keys, std::move(states)));
      it = inserted.first;
    }
    auto &states = it->second.second;
    for (size_t i = 0; i < aggs.size(); ++i) {
      auto type = agg_types[i];
      Value input;
      if (type != AggregationType::CountStarAggregate && aggs[i] != nullptr) {
        input = aggs[i]->Evaluate(&t, &child_schema);
      }
      auto &st = states[i];
      switch (type) {
        case AggregationType::CountStarAggregate:
        case AggregationType::CountAggregate:
          st.cur = st.cur.Add(Value(TypeId::INTEGER, 1));
          break;
        case AggregationType::SumAggregate:
          st.cur = st.cur.Add(input);
          break;
        case AggregationType::MinAggregate:
          if (!st.has_value || input.CompareLessThan(st.cur).GetAsBoolean()) {
            st.cur = input;
            st.has_value = true;
          }
          break;
        case AggregationType::MaxAggregate:
          if (!st.has_value || input.CompareGreaterThan(st.cur).GetAsBoolean()) {
            st.cur = input;
            st.has_value = true;
          }
          break;
      }
    }
  }

  // Special case: ungrouped aggregate over an empty table still produces one row.
  if (!saw_any && group_bys.empty()) {
    std::vector<AggState> states;
    states.reserve(aggs.size());
    for (auto type : agg_types) {
      states.push_back(MakeInitial(type));
    }
    table[""] = std::make_pair(std::vector<Value>{}, std::move(states));
  }

  for (auto &[k, kv] : table) {
    auto &group_keys = kv.first;
    auto &states = kv.second;
    std::vector<Value> row;
    row.reserve(group_keys.size() + states.size());
    for (auto &g : group_keys) {
      row.push_back(g);
    }
    for (auto &s : states) {
      row.push_back(s.has_value ? s.cur : Value(TypeId::INTEGER, 0));
    }
    result_tuples_.emplace_back(row);
  }
}

auto AggregationExecutor::Next(Tuple *tuple, RID *rid) -> bool {
  if (cursor_ >= result_tuples_.size()) {
    return false;
  }
  *tuple = result_tuples_[cursor_++];
  *rid = RID{};
  return true;
}

}  // namespace onebase

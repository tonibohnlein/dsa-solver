#include <cstdint>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <istream>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "dsa/structured_problem.h"

namespace dsa {
namespace {

using Json = nlohmann::json;

void CheckFields(const Json& object, std::initializer_list<std::string_view> allowed,
                 std::string_view path) {
  if (!object.is_object()) throw std::runtime_error(std::string(path) + " must be an object");
  for (auto item = object.begin(); item != object.end(); ++item) {
    bool known = false;
    for (std::string_view field : allowed) {
      if (item.key() == field) {
        known = true;
        break;
      }
    }
    if (!known) {
      throw std::runtime_error(std::string(path) + " has unknown field '" + item.key() + "'");
    }
  }
}

template <typename Integer>
Integer ReadUnsigned(const Json& value, const std::string& path) {
  static_assert(std::is_integral_v<Integer> && std::is_unsigned_v<Integer>);
  std::uint64_t parsed = 0;
  if (value.is_number_unsigned()) {
    parsed = value.get<std::uint64_t>();
  } else if (value.is_number_integer()) {
    const std::int64_t signed_value = value.get<std::int64_t>();
    if (signed_value < 0) throw std::runtime_error(path + " must be non-negative");
    parsed = static_cast<std::uint64_t>(signed_value);
  } else {
    throw std::runtime_error(path + " must be an unsigned integer");
  }
  if (parsed > std::numeric_limits<Integer>::max()) {
    throw std::runtime_error(path + " exceeds its integer range");
  }
  return static_cast<Integer>(parsed);
}

std::int64_t ReadSigned(const Json& value, const std::string& path) {
  if (value.is_number_integer()) return value.get<std::int64_t>();
  if (value.is_number_unsigned()) {
    const std::uint64_t parsed = value.get<std::uint64_t>();
    if (parsed > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error(path + " exceeds int64 range");
    }
    return static_cast<std::int64_t>(parsed);
  }
  throw std::runtime_error(path + " must be an integer");
}

const Json& RequireField(const Json& object, const char* field, std::string_view path) {
  if (!object.is_object()) throw std::runtime_error(std::string(path) + " must be an object");
  const auto found = object.find(field);
  if (found == object.end()) {
    throw std::runtime_error(std::string(path) + " is missing '" + field + "'");
  }
  return *found;
}

const Json& RequireArray(const Json& object, const char* field, std::string_view path) {
  const Json& value = RequireField(object, field, path);
  if (!value.is_array()) {
    throw std::runtime_error(std::string(path) + "." + field + " must be an array");
  }
  return value;
}

std::string ReadString(const Json& value, const std::string& path) {
  if (!value.is_string()) throw std::runtime_error(path + " must be a string");
  return value.get<std::string>();
}

bool ReadBool(const Json& value, const std::string& path) {
  if (!value.is_boolean()) throw std::runtime_error(path + " must be a boolean");
  return value.get<bool>();
}

BenchmarkProfile ReadProfile(const Json& value, const std::string& path) {
  const std::string name = ReadString(value, path);
  if (name == "standard_dsa") return BenchmarkProfile::kStandardDsa;
  if (name == "pypto_structured") return BenchmarkProfile::kPyptoStructured;
  if (name == "pypto_core_relaxation") return BenchmarkProfile::kPyptoCoreRelaxation;
  throw std::runtime_error(path + " has unknown profile '" + name + "'");
}

ObjectiveAggregation ReadAggregation(const Json& value, const std::string& path) {
  const std::string name = ReadString(value, path);
  if (name == "lexicographic") return ObjectiveAggregation::kLexicographic;
  throw std::runtime_error(path + " has unknown aggregation '" + name + "'");
}

ObjectiveMetric ReadObjectiveMetric(const Json& value, const std::string& path) {
  const std::string name = ReadString(value, path);
  if (name == "capacity_overflow") return ObjectiveMetric::kCapacityOverflow;
  if (name == "total_peak") return ObjectiveMetric::kTotalPeak;
  if (name == "max_peak") return ObjectiveMetric::kMaxPeak;
  if (name == "reuse_cost") return ObjectiveMetric::kReuseCost;
  if (name == "bank_cost") return ObjectiveMetric::kBankCost;
  throw std::runtime_error(path + " has unknown objective metric '" + name + "'");
}

ReusePenaltyReason ReadPenaltyReason(const Json& value, const std::string& path) {
  const std::string name = ReadString(value, path);
  if (name == "generic") return ReusePenaltyReason::kGeneric;
  if (name == "cross_pipe") return ReusePenaltyReason::kCrossPipe;
  if (name == "cross_core") return ReusePenaltyReason::kCrossCore;
  if (name == "event_budget") return ReusePenaltyReason::kEventBudget;
  throw std::runtime_error(path + " has unknown reuse-penalty reason '" + name + "'");
}

const char* PenaltyReasonName(ReusePenaltyReason reason) {
  switch (reason) {
    case ReusePenaltyReason::kGeneric:
      return "generic";
    case ReusePenaltyReason::kCrossPipe:
      return "cross_pipe";
    case ReusePenaltyReason::kCrossCore:
      return "cross_core";
    case ReusePenaltyReason::kEventBudget:
      return "event_budget";
  }
  return "unknown";
}

template <typename PairType>
std::vector<PairType> ReadPairs(const Json& array, const std::string& path) {
  if (!array.is_array()) throw std::runtime_error(path + " must be an array");
  std::vector<PairType> result;
  result.reserve(array.size());
  for (std::size_t i = 0; i < array.size(); ++i) {
    const Json& item = array[i];
    const std::string item_path = path + "[" + std::to_string(i) + "]";
    CheckFields(item, {"first", "second"}, item_path);
    PairType pair;
    pair.first =
        ReadUnsigned<BufferId>(RequireField(item, "first", item_path), item_path + ".first");
    pair.second =
        ReadUnsigned<BufferId>(RequireField(item, "second", item_path), item_path + ".second");
    result.push_back(pair);
  }
  return result;
}

Json WritePairs(const std::vector<Colocation>& pairs) {
  Json result = Json::array();
  for (const Colocation& pair : pairs) {
    result.push_back({{"first", pair.first}, {"second", pair.second}});
  }
  return result;
}

Json WritePairs(const std::vector<Separation>& pairs) {
  Json result = Json::array();
  for (const Separation& pair : pairs) {
    result.push_back({{"first", pair.first}, {"second", pair.second}});
  }
  return result;
}

Json WritePairs(const std::vector<TemporalExclusion>& pairs) {
  Json result = Json::array();
  for (const TemporalExclusion& pair : pairs) {
    result.push_back({{"first", pair.first}, {"second", pair.second}});
  }
  return result;
}

StructuredProblemDocument ParseDocument(const Json& root) {
  CheckFields(root,
              {"schema_version", "profile", "instance", "relaxed_from", "relaxed_features",
               "metadata", "problem"},
              "root");
  StructuredProblemDocument document;
  document.schema_version = ReadUnsigned<std::uint32_t>(
      RequireField(root, "schema_version", "root"), "root.schema_version");
  if (document.schema_version != kStructuredProblemSchemaVersion) {
    throw std::runtime_error("unsupported structured problem schema version " +
                             std::to_string(document.schema_version));
  }
  document.profile = ReadProfile(RequireField(root, "profile", "root"), "root.profile");
  document.instance = ReadString(RequireField(root, "instance", "root"), "root.instance");
  const bool has_relaxed_features = root.find("relaxed_features") != root.end();
  if (const auto found = root.find("relaxed_from"); found != root.end()) {
    document.relaxed_from = ReadString(*found, "root.relaxed_from");
  }
  if (const auto found = root.find("relaxed_features"); found != root.end()) {
    if (!found->is_array()) throw std::runtime_error("root.relaxed_features must be an array");
    for (std::size_t i = 0; i < found->size(); ++i) {
      document.relaxed_features.push_back(
          ReadString((*found)[i], "root.relaxed_features[" + std::to_string(i) + "]"));
    }
  }
  if (const auto found = root.find("metadata"); found != root.end()) {
    if (!found->is_object()) throw std::runtime_error("root.metadata must be an object");
    for (auto item = found->begin(); item != found->end(); ++item) {
      document.metadata.emplace(item.key(),
                                ReadString(item.value(), "root.metadata." + item.key()));
    }
  }

  if (document.profile != BenchmarkProfile::kPyptoCoreRelaxation && has_relaxed_features) {
    throw std::runtime_error("root.relaxed_features is only valid for a core relaxation");
  }

  const Json& problem = RequireField(root, "problem", "root");
  CheckFields(problem, {"pools", "buffers", "constraints", "cost_model", "objective"},
              "root.problem");
  const Json& pools = RequireArray(problem, "pools", "root.problem");
  document.problem.pools.clear();
  for (std::size_t i = 0; i < pools.size(); ++i) {
    const Json& value = pools[i];
    const std::string path = "root.problem.pools[" + std::to_string(i) + "]";
    CheckFields(value, {"id", "name", "capacity", "reserved_ranges", "bank_geometry"}, path);
    Pool pool;
    pool.id = ReadUnsigned<PoolId>(RequireField(value, "id", path), path + ".id");
    pool.name = ReadString(RequireField(value, "name", path), path + ".name");
    const Json& capacity = RequireField(value, "capacity", path);
    if (!capacity.is_null()) {
      pool.capacity = ReadUnsigned<std::uint64_t>(capacity, path + ".capacity");
    }
    const Json& reserved_ranges = RequireArray(value, "reserved_ranges", path);
    for (std::size_t j = 0; j < reserved_ranges.size(); ++j) {
      const Json& range_value = reserved_ranges[j];
      const std::string range_path = path + ".reserved_ranges[" + std::to_string(j) + "]";
      CheckFields(range_value, {"begin", "end"}, range_path);
      pool.reserved_ranges.push_back(
          {ReadUnsigned<std::uint64_t>(RequireField(range_value, "begin", range_path),
                                       range_path + ".begin"),
           ReadUnsigned<std::uint64_t>(RequireField(range_value, "end", range_path),
                                       range_path + ".end")});
    }
    if (const auto found = value.find("bank_geometry"); found != value.end()) {
      if (found->is_null()) throw std::runtime_error(path + ".bank_geometry cannot be null");
      CheckFields(*found, {"bank_size", "num_banks"}, path + ".bank_geometry");
      pool.bank_geometry = BankGeometry{
          ReadUnsigned<std::uint64_t>(RequireField(*found, "bank_size", path + ".bank_geometry"),
                                      path + ".bank_geometry.bank_size"),
          ReadUnsigned<std::uint32_t>(RequireField(*found, "num_banks", path + ".bank_geometry"),
                                      path + ".bank_geometry.num_banks")};
    }
    document.problem.pools.push_back(std::move(pool));
  }

  const Json& buffers = RequireArray(problem, "buffers", "root.problem");
  for (std::size_t i = 0; i < buffers.size(); ++i) {
    const Json& value = buffers[i];
    const std::string path = "root.problem.buffers[" + std::to_string(i) + "]";
    CheckFields(value, {"id", "name", "size", "alignment", "live_intervals", "allowed_pools"},
                path);
    Buffer buffer;
    buffer.id = ReadUnsigned<BufferId>(RequireField(value, "id", path), path + ".id");
    buffer.name = ReadString(RequireField(value, "name", path), path + ".name");
    buffer.size = ReadUnsigned<std::uint64_t>(RequireField(value, "size", path), path + ".size");
    buffer.alignment =
        ReadUnsigned<std::uint64_t>(RequireField(value, "alignment", path), path + ".alignment");
    const Json& intervals = RequireArray(value, "live_intervals", path);
    for (std::size_t j = 0; j < intervals.size(); ++j) {
      const Json& interval = intervals[j];
      const std::string interval_path = path + ".live_intervals[" + std::to_string(j) + "]";
      CheckFields(interval, {"lower", "upper"}, interval_path);
      buffer.live_intervals.push_back(
          {ReadSigned(RequireField(interval, "lower", interval_path), interval_path + ".lower"),
           ReadSigned(RequireField(interval, "upper", interval_path), interval_path + ".upper")});
    }
    const Json& allowed_pools = RequireArray(value, "allowed_pools", path);
    buffer.allowed_pools.clear();
    for (std::size_t j = 0; j < allowed_pools.size(); ++j) {
      buffer.allowed_pools.push_back(ReadUnsigned<PoolId>(
          allowed_pools[j], path + ".allowed_pools[" + std::to_string(j) + "]"));
    }
    document.problem.buffers.push_back(std::move(buffer));
  }

  const Json& constraints = RequireField(problem, "constraints", "root.problem");
  CheckFields(constraints,
              {"colocations", "separations", "temporal_exclusions", "pinned_allocations"},
              "root.problem.constraints");
  document.problem.colocations =
      ReadPairs<Colocation>(RequireArray(constraints, "colocations", "root.problem.constraints"),
                            "root.problem.constraints.colocations");
  document.problem.separations =
      ReadPairs<Separation>(RequireArray(constraints, "separations", "root.problem.constraints"),
                            "root.problem.constraints.separations");
  document.problem.temporal_exclusions = ReadPairs<TemporalExclusion>(
      RequireArray(constraints, "temporal_exclusions", "root.problem.constraints"),
      "root.problem.constraints.temporal_exclusions");
  const Json& pins = RequireArray(constraints, "pinned_allocations", "root.problem.constraints");
  for (std::size_t i = 0; i < pins.size(); ++i) {
    const Json& value = pins[i];
    const std::string path =
        "root.problem.constraints.pinned_allocations[" + std::to_string(i) + "]";
    CheckFields(value, {"buffer", "pool", "offset", "exclusive_for_all_time"}, path);
    document.problem.pinned_allocations.push_back(
        {ReadUnsigned<BufferId>(RequireField(value, "buffer", path), path + ".buffer"),
         ReadUnsigned<PoolId>(RequireField(value, "pool", path), path + ".pool"),
         ReadUnsigned<std::uint64_t>(RequireField(value, "offset", path), path + ".offset"),
         ReadBool(RequireField(value, "exclusive_for_all_time", path),
                  path + ".exclusive_for_all_time")});
  }

  if (const auto found = problem.find("cost_model"); found != problem.end()) {
    if (found->is_null()) throw std::runtime_error("root.problem.cost_model cannot be null");
    CheckFields(*found, {"reuse_penalties"}, "root.problem.cost_model");
    CostModel cost_model;
    const Json& penalties = RequireArray(*found, "reuse_penalties", "root.problem.cost_model");
    for (std::size_t i = 0; i < penalties.size(); ++i) {
      const Json& value = penalties[i];
      const std::string path = "root.problem.cost_model.reuse_penalties[" + std::to_string(i) + "]";
      CheckFields(value, {"first", "second", "cost", "reason"}, path);
      cost_model.reuse_penalties.push_back(
          {ReadUnsigned<BufferId>(RequireField(value, "first", path), path + ".first"),
           ReadUnsigned<BufferId>(RequireField(value, "second", path), path + ".second"),
           ReadUnsigned<std::uint64_t>(RequireField(value, "cost", path), path + ".cost"),
           ReadPenaltyReason(RequireField(value, "reason", path), path + ".reason")});
    }
    document.problem.cost_model = std::move(cost_model);
  }

  const Json& objective = RequireField(problem, "objective", "root.problem");
  CheckFields(objective, {"aggregation", "terms"}, "root.problem.objective");
  document.problem.objective.aggregation =
      ReadAggregation(RequireField(objective, "aggregation", "root.problem.objective"),
                      "root.problem.objective.aggregation");
  document.problem.objective.terms.clear();
  const Json& terms = RequireArray(objective, "terms", "root.problem.objective");
  for (std::size_t i = 0; i < terms.size(); ++i) {
    document.problem.objective.terms.push_back(
        ReadObjectiveMetric(terms[i], "root.problem.objective.terms[" + std::to_string(i) + "]"));
  }

  const std::vector<std::string> errors = ValidateStructuredProblemDocument(document);
  if (!errors.empty()) {
    throw std::runtime_error("invalid structured problem: " + errors.front());
  }
  return document;
}

Json SerializeDocument(const StructuredProblemDocument& document) {
  const std::vector<std::string> errors = ValidateStructuredProblemDocument(document);
  if (!errors.empty()) {
    throw std::invalid_argument("cannot serialize invalid structured problem: " + errors.front());
  }

  Json root{{"schema_version", document.schema_version},
            {"profile", ToString(document.profile)},
            {"instance", document.instance},
            {"metadata", document.metadata}};
  if (document.relaxed_from) root["relaxed_from"] = *document.relaxed_from;
  if (!document.relaxed_features.empty()) root["relaxed_features"] = document.relaxed_features;

  Json problem;
  problem["pools"] = Json::array();
  for (const Pool& pool : document.problem.pools) {
    Json value{{"id", pool.id},
               {"name", pool.name},
               {"capacity", pool.capacity ? Json(*pool.capacity) : Json(nullptr)},
               {"reserved_ranges", Json::array()}};
    for (const AddressRange& range : pool.reserved_ranges) {
      value["reserved_ranges"].push_back({{"begin", range.begin}, {"end", range.end}});
    }
    if (pool.bank_geometry) {
      const BankGeometry geometry = pool.bank_geometry.value_or(BankGeometry{});
      value["bank_geometry"] = {{"bank_size", geometry.bank_size},
                                {"num_banks", geometry.num_banks}};
    }
    problem["pools"].push_back(std::move(value));
  }

  problem["buffers"] = Json::array();
  for (const Buffer& buffer : document.problem.buffers) {
    Json value{{"id", buffer.id},
               {"name", buffer.name},
               {"size", buffer.size},
               {"alignment", buffer.alignment},
               {"live_intervals", Json::array()},
               {"allowed_pools", buffer.allowed_pools}};
    for (const Interval& interval : buffer.live_intervals) {
      value["live_intervals"].push_back({{"lower", interval.lower}, {"upper", interval.upper}});
    }
    problem["buffers"].push_back(std::move(value));
  }

  Json constraints{{"colocations", WritePairs(document.problem.colocations)},
                   {"separations", WritePairs(document.problem.separations)},
                   {"temporal_exclusions", WritePairs(document.problem.temporal_exclusions)},
                   {"pinned_allocations", Json::array()}};
  for (const PinnedAllocation& pinned : document.problem.pinned_allocations) {
    constraints["pinned_allocations"].push_back(
        {{"buffer", pinned.buffer},
         {"pool", pinned.pool},
         {"offset", pinned.offset},
         {"exclusive_for_all_time", pinned.exclusive_for_all_time}});
  }
  problem["constraints"] = std::move(constraints);

  if (document.problem.cost_model) {
    Json penalties = Json::array();
    for (const ReusePenalty& penalty : document.problem.cost_model->reuse_penalties) {
      penalties.push_back({{"first", penalty.first},
                           {"second", penalty.second},
                           {"cost", penalty.cost},
                           {"reason", PenaltyReasonName(penalty.reason)}});
    }
    problem["cost_model"] = {{"reuse_penalties", std::move(penalties)}};
  }

  Json terms = Json::array();
  for (ObjectiveMetric metric : document.problem.objective.terms) {
    terms.push_back(ToString(metric));
  }
  problem["objective"] = {{"aggregation", ToString(document.problem.objective.aggregation)},
                          {"terms", std::move(terms)}};
  root["problem"] = std::move(problem);
  return root;
}

}  // namespace

StructuredProblemDocument ReadStructuredProblemJson(std::istream& input) {
  try {
    return ParseDocument(Json::parse(input));
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::string("invalid structured problem JSON: ") + error.what());
  }
}

StructuredProblemDocument ReadStructuredProblemJsonFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open structured problem input: " + path.string());
  return ReadStructuredProblemJson(input);
}

void WriteStructuredProblemJson(std::ostream& output, const StructuredProblemDocument& document) {
  output << std::setw(2) << SerializeDocument(document) << '\n';
  if (!output) throw std::runtime_error("failed to write structured problem JSON");
}

void WriteStructuredProblemJsonFile(const std::filesystem::path& path,
                                    const StructuredProblemDocument& document) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open structured problem output: " + path.string());
  WriteStructuredProblemJson(output, document);
}

}  // namespace dsa

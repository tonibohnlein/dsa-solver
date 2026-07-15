// Copyright 2026 DSA-Solver Contributors
// SPDX-License-Identifier: Apache-2.0

#include "dsa/model/architecture.h"

#include <algorithm>
#include <fstream>
#include <iomanip>
#include <limits>
#include <nlohmann/json.hpp>
#include <numeric>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>

namespace dsa {
namespace {

using Json = nlohmann::json;

const Json& RequireField(const Json& object, const char* name, std::string_view path) {
  const auto found = object.find(name);
  if (found == object.end()) {
    throw std::runtime_error(std::string(path) + " is missing field '" + name + "'");
  }
  return *found;
}

void CheckFields(const Json& object, std::initializer_list<const char*> allowed,
                 const std::string& path) {
  if (!object.is_object()) throw std::runtime_error(path + " must be an object");
  std::set<std::string> names;
  for (const char* name : allowed) names.insert(name);
  for (auto item = object.begin(); item != object.end(); ++item) {
    if (names.count(item.key()) == 0) {
      throw std::runtime_error(path + " contains unknown field '" + item.key() + "'");
    }
  }
}

std::string ReadString(const Json& value, const std::string& path) {
  if (!value.is_string()) throw std::runtime_error(path + " must be a string");
  return value.get<std::string>();
}

std::uint64_t ReadUnsigned(const Json& value, const std::string& path) {
  if (value.is_number_unsigned()) return value.get<std::uint64_t>();
  if (!value.is_number_integer()) {
    throw std::runtime_error(path + " must be an unsigned integer");
  }
  const std::int64_t result = value.get<std::int64_t>();
  if (result < 0) throw std::runtime_error(path + " must be an unsigned integer");
  return static_cast<std::uint64_t>(result);
}

std::uint32_t ReadUint32(const Json& value, const std::string& path) {
  const std::uint64_t result = ReadUnsigned(value, path);
  if (result > std::numeric_limits<std::uint32_t>::max()) {
    throw std::runtime_error(path + " exceeds uint32 range");
  }
  return static_cast<std::uint32_t>(result);
}

std::string Fnv1a64(std::string_view text) {
  std::uint64_t value = 14695981039346656037ULL;
  for (const char character : text) {
    const auto byte = static_cast<unsigned char>(character);
    value ^= byte;
    value *= 1099511628211ULL;
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << value;
  return output.str();
}

std::uint64_t LeastCommonMultiple(std::uint64_t first, std::uint64_t second) {
  const std::uint64_t divisor = std::gcd(first, second);
  const std::uint64_t quotient = first / divisor;
  if (quotient > std::numeric_limits<std::uint64_t>::max() / second) {
    throw std::overflow_error("combined program/architecture alignment overflows uint64");
  }
  return quotient * second;
}

std::vector<AddressRange> MergeRanges(std::vector<AddressRange> ranges) {
  std::sort(ranges.begin(), ranges.end(),
            [](const AddressRange& first, const AddressRange& second) {
              return std::tie(first.begin, first.end) < std::tie(second.begin, second.end);
            });
  std::vector<AddressRange> result;
  for (const AddressRange& range : ranges) {
    if (result.empty() || result.back().end < range.begin) {
      result.push_back(range);
    } else {
      result.back().end = std::max(result.back().end, range.end);
    }
  }
  return result;
}

Json SerializeArchitecture(const ArchitectureSpec& architecture) {
  const std::vector<std::string> errors = ValidateArchitectureSpec(architecture);
  if (!errors.empty()) {
    throw std::invalid_argument("cannot serialize invalid architecture specification: " +
                                errors.front());
  }
  Json root{{"schema_version", architecture.schema_version},
            {"architecture_id", architecture.architecture_id},
            {"supported_lowering_abis", architecture.supported_lowering_abis},
            {"metadata", architecture.metadata},
            {"memory_spaces", Json::array()}};
  for (const ArchitectureMemorySpace& space : architecture.memory_spaces) {
    Json value{{"logical_space", space.logical_space},
               {"usable_capacity", space.usable_capacity},
               {"physical_capacity",
                space.physical_capacity ? Json(*space.physical_capacity) : Json(nullptr)},
               {"minimum_alignment", space.minimum_alignment},
               {"reserved_ranges", Json::array()}};
    for (const AddressRange& range : space.reserved_ranges) {
      value["reserved_ranges"].push_back({{"begin", range.begin}, {"end", range.end}});
    }
    if (space.bank_geometry) {
      value["bank_geometry"] = {{"bank_size", space.bank_geometry->bank_size},
                                {"num_banks", space.bank_geometry->num_banks}};
    }
    root["memory_spaces"].push_back(std::move(value));
  }
  return root;
}

ArchitectureSpec ParseArchitecture(const Json& root) {
  CheckFields(
      root,
      {"schema_version", "architecture_id", "supported_lowering_abis", "metadata", "memory_spaces"},
      "root");
  ArchitectureSpec architecture;
  architecture.schema_version =
      ReadUint32(RequireField(root, "schema_version", "root"), "root.schema_version");
  architecture.architecture_id =
      ReadString(RequireField(root, "architecture_id", "root"), "root.architecture_id");

  const Json& abis = RequireField(root, "supported_lowering_abis", "root");
  if (!abis.is_array()) throw std::runtime_error("root.supported_lowering_abis must be an array");
  for (std::size_t i = 0; i < abis.size(); ++i) {
    architecture.supported_lowering_abis.push_back(
        ReadString(abis[i], "root.supported_lowering_abis[" + std::to_string(i) + "]"));
  }

  const Json& metadata = RequireField(root, "metadata", "root");
  if (!metadata.is_object()) throw std::runtime_error("root.metadata must be an object");
  for (auto item = metadata.begin(); item != metadata.end(); ++item) {
    architecture.metadata.emplace(item.key(),
                                  ReadString(item.value(), "root.metadata." + item.key()));
  }

  const Json& spaces = RequireField(root, "memory_spaces", "root");
  if (!spaces.is_array()) throw std::runtime_error("root.memory_spaces must be an array");
  for (std::size_t i = 0; i < spaces.size(); ++i) {
    const Json& value = spaces[i];
    const std::string path = "root.memory_spaces[" + std::to_string(i) + "]";
    CheckFields(value,
                {"logical_space", "usable_capacity", "physical_capacity", "minimum_alignment",
                 "reserved_ranges", "bank_geometry"},
                path);
    ArchitectureMemorySpace space;
    space.logical_space =
        ReadString(RequireField(value, "logical_space", path), path + ".logical_space");
    space.usable_capacity =
        ReadUnsigned(RequireField(value, "usable_capacity", path), path + ".usable_capacity");
    const Json& physical = RequireField(value, "physical_capacity", path);
    if (!physical.is_null()) {
      space.physical_capacity = ReadUnsigned(physical, path + ".physical_capacity");
    }
    space.minimum_alignment =
        ReadUnsigned(RequireField(value, "minimum_alignment", path), path + ".minimum_alignment");
    const Json& ranges = RequireField(value, "reserved_ranges", path);
    if (!ranges.is_array()) throw std::runtime_error(path + ".reserved_ranges must be an array");
    for (std::size_t j = 0; j < ranges.size(); ++j) {
      const Json& range = ranges[j];
      const std::string range_path = path + ".reserved_ranges[" + std::to_string(j) + "]";
      CheckFields(range, {"begin", "end"}, range_path);
      space.reserved_ranges.push_back(
          {ReadUnsigned(RequireField(range, "begin", range_path), range_path + ".begin"),
           ReadUnsigned(RequireField(range, "end", range_path), range_path + ".end")});
    }
    if (const auto bank = value.find("bank_geometry"); bank != value.end()) {
      if (bank->is_null()) throw std::runtime_error(path + ".bank_geometry cannot be null");
      CheckFields(*bank, {"bank_size", "num_banks"}, path + ".bank_geometry");
      space.bank_geometry =
          BankGeometry{ReadUnsigned(RequireField(*bank, "bank_size", path + ".bank_geometry"),
                                    path + ".bank_geometry.bank_size"),
                       ReadUint32(RequireField(*bank, "num_banks", path + ".bank_geometry"),
                                  path + ".bank_geometry.num_banks")};
    }
    architecture.memory_spaces.push_back(std::move(space));
  }
  const std::vector<std::string> errors = ValidateArchitectureSpec(architecture);
  if (!errors.empty())
    throw std::runtime_error("invalid architecture specification: " + errors.front());
  return architecture;
}

StructuredProblemDocument CanonicalProgram(const StructuredProblemDocument& program) {
  StructuredProblemDocument canonical = program;
  canonical.instance = "program";
  canonical.metadata.clear();
  for (const char* key : {"lifetime_ordering", "solver_input", "lowering_abi"}) {
    const auto found = program.metadata.find(key);
    if (found != program.metadata.end()) canonical.metadata.emplace(found->first, found->second);
  }
  for (Pool& pool : canonical.problem.pools) {
    pool.capacity.reset();
    pool.bank_geometry.reset();
  }

  std::sort(canonical.problem.pools.begin(), canonical.problem.pools.end(),
            [](const Pool& first, const Pool& second) { return first.name < second.name; });
  std::map<PoolId, PoolId> remapped_pool_ids;
  for (std::size_t index = 0; index < canonical.problem.pools.size(); ++index) {
    Pool& pool = canonical.problem.pools[index];
    const PoolId canonical_id = static_cast<PoolId>(index);
    remapped_pool_ids.emplace(pool.id, canonical_id);
    pool.id = canonical_id;
  }
  const auto remap_pool = [&](PoolId pool) {
    const auto found = remapped_pool_ids.find(pool);
    if (found == remapped_pool_ids.end()) {
      throw std::logic_error("validated program references an unknown pool");
    }
    return found->second;
  };
  for (Buffer& buffer : canonical.problem.buffers) {
    for (PoolId& pool : buffer.allowed_pools) pool = remap_pool(pool);
    std::sort(buffer.allowed_pools.begin(), buffer.allowed_pools.end());
  }
  for (PinnedAllocation& pin : canonical.problem.pinned_allocations) {
    pin.pool = remap_pool(pin.pool);
  }
  if (canonical.problem.pypto_structure) {
    for (PyptoPipelineGroup& group : canonical.problem.pypto_structure->pipeline_groups) {
      group.pool = remap_pool(group.pool);
    }
  }
  return canonical;
}

ArchitectureSpec CanonicalArchitecture(const ArchitectureSpec& architecture) {
  ArchitectureSpec canonical = architecture;
  std::sort(canonical.supported_lowering_abis.begin(), canonical.supported_lowering_abis.end());
  std::sort(canonical.memory_spaces.begin(), canonical.memory_spaces.end(),
            [](const ArchitectureMemorySpace& first, const ArchitectureMemorySpace& second) {
              return first.logical_space < second.logical_space;
            });
  for (ArchitectureMemorySpace& space : canonical.memory_spaces) {
    space.reserved_ranges = MergeRanges(std::move(space.reserved_ranges));
  }
  return canonical;
}

}  // namespace

const ArchitectureMemorySpace* ArchitectureSpec::FindMemorySpace(
    const std::string& logical_space) const noexcept {
  const auto found = std::find_if(
      memory_spaces.begin(), memory_spaces.end(),
      [&](const ArchitectureMemorySpace& space) { return space.logical_space == logical_space; });
  return found == memory_spaces.end() ? nullptr : &*found;
}

std::vector<std::string> ValidateArchitectureSpec(const ArchitectureSpec& architecture) {
  std::vector<std::string> errors;
  if (architecture.schema_version != kArchitectureSpecSchemaVersion) {
    errors.push_back("unsupported architecture schema version " +
                     std::to_string(architecture.schema_version));
  }
  if (architecture.architecture_id.empty()) errors.push_back("architecture_id is empty");
  if (architecture.supported_lowering_abis.empty()) {
    errors.push_back("architecture has no supported lowering ABI");
  }
  std::set<std::string> abis;
  for (const std::string& abi : architecture.supported_lowering_abis) {
    if (abi.empty()) errors.push_back("architecture has an empty lowering ABI");
    if (!abis.insert(abi).second)
      errors.push_back("architecture repeats lowering ABI '" + abi + "'");
  }
  if (architecture.memory_spaces.empty()) errors.push_back("architecture has no memory spaces");
  std::set<std::string> spaces;
  for (const ArchitectureMemorySpace& space : architecture.memory_spaces) {
    if (space.logical_space.empty()) errors.push_back("architecture has an unnamed memory space");
    if (!spaces.insert(space.logical_space).second) {
      errors.push_back("architecture repeats memory space '" + space.logical_space + "'");
    }
    if (space.usable_capacity == 0) {
      errors.push_back("memory space '" + space.logical_space + "' has zero usable capacity");
    }
    if (space.physical_capacity && *space.physical_capacity < space.usable_capacity) {
      errors.push_back("memory space '" + space.logical_space +
                       "' has physical capacity below usable capacity");
    }
    if (space.minimum_alignment == 0) {
      errors.push_back("memory space '" + space.logical_space + "' has zero alignment");
    }
    for (const AddressRange& range : space.reserved_ranges) {
      if (range.begin >= range.end || range.end > space.usable_capacity) {
        errors.push_back("memory space '" + space.logical_space +
                         "' has an invalid reserved range");
      }
    }
    if (space.bank_geometry &&
        (space.bank_geometry->bank_size == 0 || space.bank_geometry->num_banks == 0)) {
      errors.push_back("memory space '" + space.logical_space + "' has invalid bank geometry");
    }
  }
  return errors;
}

ArchitectureSpec ReadArchitectureSpecJson(std::istream& input) {
  try {
    return ParseArchitecture(Json::parse(input));
  } catch (const nlohmann::json::exception& error) {
    throw std::runtime_error(std::string("invalid architecture JSON: ") + error.what());
  }
}

ArchitectureSpec ReadArchitectureSpecJsonFile(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open architecture input: " + path.string());
  return ReadArchitectureSpecJson(input);
}

void WriteArchitectureSpecJson(std::ostream& output, const ArchitectureSpec& architecture) {
  output << std::setw(2) << SerializeArchitecture(architecture) << '\n';
  if (!output) throw std::runtime_error("failed to write architecture JSON");
}

void WriteArchitectureSpecJsonFile(const std::filesystem::path& path,
                                   const ArchitectureSpec& architecture) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open architecture output: " + path.string());
  WriteArchitectureSpecJson(output, architecture);
}

std::vector<std::string> ValidateUnboundProgram(const StructuredProblemDocument& program) {
  std::vector<std::string> errors = ValidateStructuredProblemDocument(program);
  if (!IsPyptoProfile(program.profile)) {
    errors.push_back("architecture binding requires a PyPTO program profile");
  }
  const auto abi = program.metadata.find("lowering_abi");
  if (abi == program.metadata.end() || abi->second.empty()) {
    errors.push_back("unbound program requires non-empty metadata.lowering_abi");
  }
  for (const char* key : {"target", "architecture_id", "architecture_fingerprint_fnv1a64",
                          "program_fingerprint_fnv1a64"}) {
    if (program.metadata.count(key) != 0) {
      errors.push_back("unbound program cannot contain metadata." + std::string(key));
    }
  }
  std::set<std::string> pool_names;
  for (const Pool& pool : program.problem.pools) {
    if (pool.name.empty()) errors.push_back("unbound program has an unnamed pool");
    if (!pool_names.insert(pool.name).second) {
      errors.push_back("unbound program repeats pool name '" + pool.name + "'");
    }
    if (pool.capacity) {
      errors.push_back("unbound program pool '" + pool.name + "' already has a capacity");
    }
    if (pool.bank_geometry) {
      errors.push_back("unbound program pool '" + pool.name +
                       "' already has architecture bank geometry");
    }
  }
  return errors;
}

std::string FingerprintUnboundProgram(const StructuredProblemDocument& program) {
  const std::vector<std::string> errors = ValidateUnboundProgram(program);
  if (!errors.empty()) {
    throw std::invalid_argument("cannot fingerprint invalid unbound program: " + errors.front());
  }
  std::ostringstream output;
  WriteStructuredProblemJson(output, CanonicalProgram(program));
  return Fnv1a64(output.str());
}

std::string FingerprintArchitectureSpec(const ArchitectureSpec& architecture) {
  std::ostringstream output;
  WriteArchitectureSpecJson(output, CanonicalArchitecture(architecture));
  return Fnv1a64(output.str());
}

StructuredProblemDocument BindArchitecture(const StructuredProblemDocument& program,
                                           const ArchitectureSpec& architecture) {
  const std::vector<std::string> program_errors = ValidateUnboundProgram(program);
  if (!program_errors.empty()) {
    throw std::invalid_argument("cannot bind invalid unbound program: " + program_errors.front());
  }
  const std::vector<std::string> architecture_errors = ValidateArchitectureSpec(architecture);
  if (!architecture_errors.empty()) {
    throw std::invalid_argument("cannot bind invalid architecture: " + architecture_errors.front());
  }
  const std::string& lowering_abi = program.metadata.at("lowering_abi");
  if (std::find(architecture.supported_lowering_abis.begin(),
                architecture.supported_lowering_abis.end(),
                lowering_abi) == architecture.supported_lowering_abis.end()) {
    throw std::invalid_argument("architecture '" + architecture.architecture_id +
                                "' does not support lowering ABI '" + lowering_abi + "'");
  }

  StructuredProblemDocument bound = program;
  for (Pool& pool : bound.problem.pools) {
    const ArchitectureMemorySpace* space = architecture.FindMemorySpace(pool.name);
    if (space == nullptr) {
      throw std::invalid_argument("architecture '" + architecture.architecture_id +
                                  "' does not define logical space '" + pool.name + "'");
    }
    pool.capacity = space->usable_capacity;
    pool.reserved_ranges.insert(pool.reserved_ranges.end(), space->reserved_ranges.begin(),
                                space->reserved_ranges.end());
    pool.reserved_ranges = MergeRanges(std::move(pool.reserved_ranges));
    pool.bank_geometry = space->bank_geometry;
    for (Buffer& buffer : bound.problem.buffers) {
      if (std::find(buffer.allowed_pools.begin(), buffer.allowed_pools.end(), pool.id) !=
          buffer.allowed_pools.end()) {
        buffer.alignment = LeastCommonMultiple(buffer.alignment, space->minimum_alignment);
      }
    }
  }

  const std::string program_fingerprint = FingerprintUnboundProgram(program);
  const std::string architecture_fingerprint = FingerprintArchitectureSpec(architecture);
  bound.instance += "@" + architecture.architecture_id;
  bound.metadata["architecture_id"] = architecture.architecture_id;
  bound.metadata["architecture_fingerprint_fnv1a64"] = architecture_fingerprint;
  bound.metadata["program_fingerprint_fnv1a64"] = program_fingerprint;
  bound.metadata["target"] = architecture.architecture_id;

  const std::vector<std::string> bound_errors = ValidateStructuredProblemDocument(bound);
  if (!bound_errors.empty()) {
    throw std::invalid_argument("architecture binding produced an invalid problem: " +
                                bound_errors.front());
  }
  return bound;
}

}  // namespace dsa

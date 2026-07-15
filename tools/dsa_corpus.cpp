// Copyright 2026 dsa-solver contributors
// SPDX-License-Identifier: Apache-2.0

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

#include "dsa/model/structured_problem.h"
#include "dsa/model/validator.h"

namespace {

namespace fs = std::filesystem;

struct CoverageTarget {
  std::string case_id;
  fs::path source_path;
  std::string family;
  std::uint64_t devices = 1;
  std::uint64_t minimum_documents = 1;
  bool capture = true;
  std::string exclusion_reason;
};

struct Options {
  std::vector<fs::path> inputs;
  fs::path output;
  std::optional<fs::path> coverage_targets;
  std::string source_repo;
  std::string source_commit;
  std::string producer_repo;
  std::string producer_commit;
  std::string corpus_namespace;
};

struct InputDocument {
  fs::path path;
  fs::path relative_path;
  std::string case_id;
};

struct ManifestRecord {
  std::string instance;
  std::string case_id;
  std::string source_path;
  std::string family;
  std::uint64_t devices = 1;
  std::string export_file;
  std::string instance_path;
  std::string source_fingerprint;
  std::string problem_fingerprint;
  bool representative = true;
  std::size_t pools = 0;
  std::size_t buffers = 0;
  std::size_t alias_classes = 0;
  std::size_t pipeline_groups = 0;
  std::size_t live_intervals = 0;
  std::size_t temporal_conflicts = 0;
  std::size_t reuse_candidates = 0;
  bool selected = false;
  std::string selection_reason;
};

struct RepresentativeDocument {
  std::string canonical_problem;
  std::string instance;
  fs::path relative_output;
  bool selected = false;
};

struct ProblemStats {
  std::size_t live_intervals = 0;
  std::size_t temporal_conflicts = 0;
  std::size_t reuse_candidates = 0;
  std::size_t nontrivial_alias_classes = 0;
  std::size_t pipeline_groups = 0;
  std::size_t reserved_ranges = 0;
  std::size_t reuse_penalties = 0;
};

struct Selection {
  bool selected = false;
  std::string reason;
};

[[noreturn]] void UsageError(const std::string& message) {
  throw std::invalid_argument(message + "\nRun dsa-corpus --help for usage information.");
}

void PrintHelp() {
  std::cout
      << "Usage: dsa-corpus --input PATH... --output DIR --source-repo TEXT "
         "--source-commit SHA --namespace TEXT [options]\n\n"
      << "Import raw PyPTO *.dsa.json exporter artifacts into a normalized, report-safe corpus.\n\n"
      << "Required:\n"
      << "  --input PATH                    Raw export file or root (repeatable)\n"
      << "  --output DIR                    New, empty normalized corpus directory\n"
      << "  --source-repo TEXT              Source repository name or URL\n"
      << "  --source-commit SHA             Exact source revision\n"
      << "  --namespace TEXT                Stable benchmark identity namespace\n\n"
      << "Optional producer provenance:\n"
      << "  --producer-repo TEXT            Compiler/exporter repository\n"
      << "  --producer-commit SHA           Exact compiler/exporter revision\n\n"
      << "Coverage:\n"
      << "  --coverage-targets FILE         TSV inventory of captured and excluded cases\n"
      << "  --help                          Show this help\n\n"
      << "Input directories must place each case below a top-level case_id directory. "
         "When a coverage TSV is supplied, every input case must be listed, every capture target's "
         "minimum document count must be met, and excluded targets must produce no documents.\n";
}

std::uint64_t ParseUnsigned(std::string_view text, const std::string& context) {
  if (text.empty() || text.front() == '-') {
    throw std::runtime_error(context + " must be a non-negative integer");
  }
  std::uint64_t value = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size()) {
    throw std::runtime_error(context + " must be a non-negative integer, got '" +
                             std::string(text) + "'");
  }
  return value;
}

Options ParseOptions(int argc, char** argv) {
  Options options;
  auto next = [&](int* index, const std::string& option) -> std::string_view {
    if (*index + 1 >= argc) UsageError(option + " requires a value");
    ++*index;
    return argv[*index];
  };
  for (int index = 1; index < argc; ++index) {
    const std::string option = argv[index];
    if (option == "--help") {
      PrintHelp();
      std::exit(0);
    } else if (option == "--input") {
      options.inputs.emplace_back(next(&index, option));
    } else if (option == "--output") {
      options.output = fs::path(next(&index, option));
    } else if (option == "--coverage-targets") {
      options.coverage_targets = fs::path(next(&index, option));
    } else if (option == "--source-repo") {
      options.source_repo = next(&index, option);
    } else if (option == "--source-commit") {
      options.source_commit = next(&index, option);
    } else if (option == "--producer-repo") {
      options.producer_repo = next(&index, option);
    } else if (option == "--producer-commit") {
      options.producer_commit = next(&index, option);
    } else if (option == "--namespace") {
      options.corpus_namespace = next(&index, option);
    } else {
      UsageError("unknown option '" + option + "'");
    }
  }
  if (options.inputs.empty()) UsageError("at least one --input is required");
  if (options.output.empty()) UsageError("--output is required");
  if (options.source_repo.empty()) UsageError("--source-repo is required");
  if (options.source_commit.empty()) UsageError("--source-commit is required");
  if (options.producer_repo.empty() != options.producer_commit.empty()) {
    UsageError("--producer-repo and --producer-commit must be supplied together");
  }
  if (options.corpus_namespace.empty()) UsageError("--namespace is required");
  return options;
}

std::vector<std::string> SplitTsv(const std::string& line) {
  std::vector<std::string> fields;
  std::size_t begin = 0;
  while (true) {
    const std::size_t end = line.find('\t', begin);
    fields.push_back(line.substr(begin, end == std::string::npos ? end : end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return fields;
}

void ValidateTsvField(const std::string& value, const std::string& context) {
  if (value.empty()) throw std::runtime_error(context + " must not be empty");
  if (value.find_first_of("\t\r\n") != std::string::npos) {
    throw std::runtime_error(context + " contains a tab or newline");
  }
}

void ValidateRelativeSourcePath(const fs::path& path, const std::string& context) {
  if (path.empty() || path.is_absolute()) {
    throw std::runtime_error(context + " must be a non-empty relative path");
  }
  for (const fs::path& component : path) {
    if (component == "..") throw std::runtime_error(context + " must not contain '..'");
  }
}

std::map<std::string, CoverageTarget> ReadCoverageTargets(const fs::path& path) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open coverage target TSV: " + path.string());
  std::string line;
  if (!std::getline(input, line)) throw std::runtime_error("coverage target TSV is empty");
  if (!line.empty() && line.back() == '\r') line.pop_back();
  const bool extended =
      line == "case_id\tsource_path\tfamily\tdevices\tminimum_documents\teligibility\treason";
  if (!extended && line != "case_id\tsource_path\tfamily\tdevices\tminimum_documents") {
    throw std::runtime_error("coverage target TSV has an unexpected header: " + line);
  }

  std::map<std::string, CoverageTarget> targets;
  std::size_t line_number = 1;
  while (std::getline(input, line)) {
    ++line_number;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line.empty()) continue;
    const std::vector<std::string> fields = SplitTsv(line);
    const std::size_t expected_fields = extended ? 7 : 5;
    if (fields.size() != expected_fields) {
      throw std::runtime_error("coverage target line " + std::to_string(line_number) +
                               " must contain exactly " + std::to_string(expected_fields) +
                               " tab-separated fields");
    }
    CoverageTarget target;
    target.case_id = fields[0];
    target.source_path = fs::path(fields[1]);
    target.family = fields[2];
    target.devices = ParseUnsigned(fields[3], "coverage target devices");
    target.minimum_documents = ParseUnsigned(fields[4], "coverage target minimum_documents");
    if (extended) {
      if (fields[5] == "capture") {
        target.capture = true;
      } else if (fields[5] == "exclude") {
        target.capture = false;
      } else {
        throw std::runtime_error("coverage target eligibility must be 'capture' or 'exclude'");
      }
      target.exclusion_reason = fields[6] == "-" ? std::string{} : fields[6];
    }
    ValidateTsvField(target.case_id, "coverage target case_id");
    ValidateRelativeSourcePath(target.source_path, "coverage target source_path");
    ValidateTsvField(target.family, "coverage target family");
    if (target.devices == 0) throw std::runtime_error("coverage target devices must be positive");
    if (target.capture && target.minimum_documents == 0) {
      throw std::runtime_error("capture target minimum_documents must be positive");
    }
    if (!target.capture && target.minimum_documents != 0) {
      throw std::runtime_error("excluded target minimum_documents must be zero");
    }
    if (!target.capture && target.exclusion_reason.empty()) {
      throw std::runtime_error("excluded target must provide a reason");
    }
    if (!targets.emplace(target.case_id, target).second) {
      throw std::runtime_error("duplicate coverage target case_id '" + target.case_id + "'");
    }
  }
  if (targets.empty()) throw std::runtime_error("coverage target TSV contains no targets");
  return targets;
}

bool IsDsaJson(const fs::path& path) {
  constexpr std::string_view suffix = ".dsa.json";
  const std::string name = path.filename().string();
  return name.size() >= suffix.size() &&
         name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

std::string StripDsaJsonSuffix(const fs::path& path) {
  std::string name = path.filename().string();
  constexpr std::string_view suffix = ".dsa.json";
  if (name.size() >= suffix.size()) name.resize(name.size() - suffix.size());
  return name;
}

std::string FirstComponent(const fs::path& relative_path) {
  const auto begin = relative_path.begin();
  if (begin == relative_path.end()) return {};
  return begin->string();
}

std::vector<InputDocument> DiscoverDocuments(const std::vector<fs::path>& roots) {
  std::vector<InputDocument> documents;
  for (const fs::path& root : roots) {
    if (!fs::exists(root)) throw std::runtime_error("input path does not exist: " + root.string());
    const std::size_t before = documents.size();
    if (fs::is_regular_file(root)) {
      if (!IsDsaJson(root)) {
        throw std::runtime_error("input file is not a *.dsa.json export: " + root.string());
      }
      documents.push_back({root.lexically_normal(), root.filename(), StripDsaJsonSuffix(root)});
    } else if (fs::is_directory(root)) {
      for (const fs::directory_entry& entry :
           fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
        if (!entry.is_regular_file() || !IsDsaJson(entry.path())) continue;
        const fs::path relative = fs::relative(entry.path(), root);
        const std::string case_id = FirstComponent(relative);
        if (case_id.empty() || relative.parent_path().empty()) {
          throw std::runtime_error(
              "directory input must place exports below a top-level case_id: " +
              entry.path().string());
        }
        documents.push_back({entry.path().lexically_normal(), relative, case_id});
      }
    } else {
      throw std::runtime_error("input path is neither a file nor directory: " + root.string());
    }
    if (documents.size() == before) {
      throw std::runtime_error("input path contains no *.dsa.json exports: " + root.string());
    }
  }
  std::sort(documents.begin(), documents.end(),
            [](const auto& first, const auto& second) { return first.path < second.path; });
  for (std::size_t index = 1; index < documents.size(); ++index) {
    if (documents[index - 1].path == documents[index].path) {
      throw std::runtime_error("input export was discovered more than once: " +
                               documents[index].path.string());
    }
  }
  return documents;
}

std::string ReadFile(const fs::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) throw std::runtime_error("cannot open input export: " + path.string());
  std::ostringstream output;
  output << input.rdbuf();
  if (!input.good() && !input.eof()) {
    throw std::runtime_error("failed while reading input export: " + path.string());
  }
  return output.str();
}

std::string Fnv1a64(const std::string& bytes) {
  std::uint64_t hash = UINT64_C(14695981039346656037);
  for (const char character : bytes) {
    const auto byte = static_cast<unsigned char>(character);
    hash ^= byte;
    hash *= UINT64_C(1099511628211);
  }
  std::ostringstream output;
  output << std::hex << std::setfill('0') << std::setw(16) << hash;
  return output.str();
}

std::string SanitizeComponent(std::string_view text) {
  std::string output;
  output.reserve(text.size());
  for (const char value : text) {
    const auto character = static_cast<unsigned char>(value);
    const bool safe = (character >= 'a' && character <= 'z') ||
                      (character >= 'A' && character <= 'Z') ||
                      (character >= '0' && character <= '9') || character == '-' ||
                      character == '_' || character == '.';
    output.push_back(safe ? static_cast<char>(character) : '_');
  }
  return output.empty() ? std::string("unnamed") : output;
}

std::string SourceIdentity(const fs::path& source_path) {
  fs::path without_extension = source_path;
  without_extension.replace_extension();
  std::string identity;
  for (const fs::path& component : without_extension) {
    if (!identity.empty()) identity += "::";
    identity += SanitizeComponent(component.string());
  }
  return identity;
}

void NormalizeNonSemanticProblemNames(dsa::StructuredProblemDocument* document) {
  for (dsa::Pool& pool : document->problem.pools) {
    pool.name = "pool_" + std::to_string(pool.id);
  }
  for (dsa::Buffer& buffer : document->problem.buffers) {
    buffer.name = "buffer_" + std::to_string(buffer.id);
  }
  if (!document->problem.pypto_structure) return;
  for (dsa::PyptoAliasClass& alias_class : document->problem.pypto_structure->alias_classes) {
    alias_class.members = {"alias_" + std::to_string(alias_class.buffer)};
  }
  std::map<dsa::PoolId, std::int32_t> next_group_by_pool;
  for (dsa::PyptoPipelineGroup& group : document->problem.pypto_structure->pipeline_groups) {
    group.group = next_group_by_pool[group.pool]++;
  }
}

bool ShareAllowedPool(const dsa::Buffer& first, const dsa::Buffer& second) {
  for (dsa::PoolId first_pool : first.allowed_pools) {
    if (std::find(second.allowed_pools.begin(), second.allowed_pools.end(), first_pool) !=
        second.allowed_pools.end()) {
      return true;
    }
  }
  return false;
}

ProblemStats AnalyzeProblem(const dsa::DsaProblem& problem) {
  ProblemStats stats;
  for (const dsa::Buffer& buffer : problem.buffers) {
    stats.live_intervals += buffer.live_intervals.size();
  }
  for (std::size_t first = 0; first < problem.buffers.size(); ++first) {
    for (std::size_t second = first + 1; second < problem.buffers.size(); ++second) {
      if (!ShareAllowedPool(problem.buffers[first], problem.buffers[second])) continue;
      if (dsa::HaveTemporalConflict(problem, problem.buffers[first], problem.buffers[second])) {
        ++stats.temporal_conflicts;
      } else {
        ++stats.reuse_candidates;
      }
    }
  }
  for (const dsa::Pool& pool : problem.pools) {
    stats.reserved_ranges += pool.reserved_ranges.size();
  }
  if (problem.cost_model) stats.reuse_penalties = problem.cost_model->reuse_penalties.size();
  if (problem.pypto_structure) {
    stats.pipeline_groups = problem.pypto_structure->pipeline_groups.size();
    for (const dsa::PyptoAliasClass& alias_class : problem.pypto_structure->alias_classes) {
      if (alias_class.members.size() > 1) ++stats.nontrivial_alias_classes;
    }
  }
  return stats;
}

Selection SelectProblem(const dsa::DsaProblem& problem, const ProblemStats& stats) {
  if (stats.pipeline_groups != 0) return {true, "pipeline_structure"};
  if (stats.reuse_penalties != 0) return {true, "reuse_cost"};
  if (!problem.separations.empty() || !problem.temporal_exclusions.empty() ||
      !problem.colocations.empty() || !problem.pinned_allocations.empty() ||
      stats.reserved_ranges != 0) {
    return {true, "hard_constraints"};
  }
  if (stats.nontrivial_alias_classes != 0) return {true, "semantic_aliases"};
  if (stats.live_intervals > problem.buffers.size()) return {true, "multi_interval"};
  if (problem.pools.size() > 1) return {true, "multi_pool"};
  if (problem.buffers.size() >= 4 && stats.temporal_conflicts != 0 && stats.reuse_candidates != 0) {
    return {true, "core_reuse_search"};
  }
  return {false, "trivial_no_placement_choice"};
}

void RequireFreshOutputDirectory(const fs::path& output) {
  if (fs::exists(output)) {
    if (!fs::is_directory(output)) {
      throw std::runtime_error("output exists and is not a directory: " + output.string());
    }
    if (fs::directory_iterator(output) != fs::directory_iterator()) {
      throw std::runtime_error("output directory must be empty: " + output.string());
    }
  }
  fs::create_directories(output);
}

void WriteManifest(const fs::path& path, const Options& options,
                   const std::vector<ManifestRecord>& records) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot create corpus manifest: " + path.string());
  output << "instance\tcase_id\tsource_repo\tsource_commit\tproducer_repo\tproducer_commit"
            "\tsource_path\tfamily\tdevices"
            "\texport_file\tinstance_path\tsource_fingerprint_fnv1a64\tpools\tbuffers"
            "\tproblem_fingerprint_fnv1a64\trepresentative\tselected\tselection_reason"
            "\talias_classes\tpipeline_groups\tlive_intervals\ttemporal_conflicts"
            "\treuse_candidates\n";
  for (const ManifestRecord& record : records) {
    output << record.instance << '\t' << record.case_id << '\t' << options.source_repo << '\t'
           << options.source_commit << '\t' << options.producer_repo << '\t'
           << options.producer_commit << '\t' << record.source_path << '\t' << record.family << '\t'
           << record.devices << '\t' << record.export_file << '\t' << record.instance_path << '\t'
           << record.source_fingerprint << '\t' << record.pools << '\t' << record.buffers << '\t'
           << record.problem_fingerprint << '\t' << (record.representative ? "true" : "false")
           << '\t' << (record.selected ? "true" : "false") << '\t' << record.selection_reason
           << '\t' << record.alias_classes << '\t' << record.pipeline_groups << '\t'
           << record.live_intervals << '\t' << record.temporal_conflicts << '\t'
           << record.reuse_candidates << '\n';
  }
}

void WriteCoverage(const fs::path& path, const std::map<std::string, CoverageTarget>& targets,
                   const std::map<std::string, std::size_t>& counts) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot create coverage report: " + path.string());
  output << "case_id\tsource_path\tfamily\tdevices\tminimum_documents\teligibility\treason"
            "\tdocuments\tstatus\n";
  for (const auto& [case_id, target] : targets) {
    const auto found = counts.find(case_id);
    const std::size_t count = found == counts.end() ? 0 : found->second;
    output << case_id << '\t' << target.source_path.generic_string() << '\t' << target.family
           << '\t' << target.devices << '\t' << target.minimum_documents << '\t'
           << (target.capture ? "capture" : "exclude") << '\t'
           << (target.exclusion_reason.empty() ? "-" : target.exclusion_reason) << '\t' << count
           << '\t'
           << (!target.capture ? (count == 0 ? "excluded" : "unexpected")
                               : (count >= target.minimum_documents ? "covered" : "missing"))
           << '\n';
  }
}

std::vector<ManifestRecord> ImportCorpus(const Options& options,
                                         const std::vector<InputDocument>& inputs,
                                         const std::map<std::string, CoverageTarget>& targets,
                                         std::map<std::string, std::size_t>* coverage_counts) {
  std::vector<ManifestRecord> records;
  std::set<fs::path> output_paths;
  std::set<std::string> normalized_instances;
  std::map<std::string, RepresentativeDocument> representative_by_fingerprint;
  for (const InputDocument& input : inputs) {
    CoverageTarget target;
    const auto target_it = targets.find(input.case_id);
    if (!targets.empty()) {
      if (target_it == targets.end()) {
        throw std::runtime_error("input case_id is not present in coverage targets: " +
                                 input.case_id);
      }
      target = target_it->second;
      if (!target.capture) {
        throw std::runtime_error("excluded case_id unexpectedly produced a DSA document: " +
                                 input.case_id);
      }
    } else {
      target.case_id = input.case_id;
      target.source_path = fs::path(input.case_id + ".py");
      target.family = "unclassified";
    }

    dsa::StructuredProblemDocument document = dsa::ReadStructuredProblemJsonFile(input.path);
    if (!dsa::IsPyptoProfile(document.profile)) {
      throw std::runtime_error("input export does not use a PyPTO profile: " + input.path.string());
    }
    for (const auto& [key, value] : document.metadata) {
      static_cast<void>(value);
      if (key.rfind("corpus_", 0) == 0) {
        throw std::runtime_error("input export is already corpus-normalized (metadata key '" + key +
                                 "'): " + input.path.string());
      }
    }

    const std::string raw = ReadFile(input.path);
    const std::string fingerprint = Fnv1a64(raw);
    dsa::StructuredProblemDocument shape = document;
    shape.instance = "corpus_shape";
    shape.metadata.clear();
    for (const char* semantic_key :
         {"address_reuse_contract", "lifetime_ordering", "solver_input", "target"}) {
      const auto semantic_metadata = document.metadata.find(semantic_key);
      if (semantic_metadata != document.metadata.end()) {
        shape.metadata.emplace(semantic_key, semantic_metadata->second);
      }
    }
    NormalizeNonSemanticProblemNames(&shape);
    std::ostringstream canonical_output;
    dsa::WriteStructuredProblemJson(canonical_output, shape);
    const std::string canonical_problem = canonical_output.str();
    const std::string problem_fingerprint = Fnv1a64(canonical_problem);
    const ProblemStats stats = AnalyzeProblem(document.problem);
    const Selection selection = SelectProblem(document.problem, stats);
    const std::string export_stem = SanitizeComponent(StripDsaJsonSuffix(input.path));
    const std::string original_instance = document.instance;
    bool representative = false;
    bool selected = false;
    std::string normalized_instance;
    fs::path relative_output;
    const auto existing = representative_by_fingerprint.find(problem_fingerprint);
    if (existing != representative_by_fingerprint.end()) {
      if (existing->second.canonical_problem != canonical_problem) {
        throw std::runtime_error("problem fingerprint collision for input: " + input.path.string());
      }
      normalized_instance = existing->second.instance;
      relative_output = existing->second.relative_output;
      selected = existing->second.selected;
    } else {
      representative = true;
      selected = selection.selected;
      const std::string instance_prefix =
          options.corpus_namespace + "::" + SourceIdentity(target.source_path) + "::";
      std::string normalized_stem = export_stem;
      normalized_instance = instance_prefix + normalized_stem;
      if (!normalized_instances.insert(normalized_instance).second) {
        normalized_stem += "-" + problem_fingerprint;
        normalized_instance = instance_prefix + normalized_stem;
        if (!normalized_instances.insert(normalized_instance).second) {
          throw std::runtime_error("duplicate normalized corpus identity: " + normalized_instance);
        }
      }
      document.instance = normalized_instance;
      document.metadata["corpus_case_id"] = input.case_id;
      document.metadata["corpus_export_file"] = input.relative_path.generic_string();
      document.metadata["corpus_family"] = target.family;
      document.metadata["corpus_importer"] = "dsa-corpus-v1";
      document.metadata["corpus_namespace"] = options.corpus_namespace;
      document.metadata["corpus_original_instance"] = original_instance;
      document.metadata["corpus_problem_fingerprint_fnv1a64"] = problem_fingerprint;
      document.metadata["corpus_source_commit"] = options.source_commit;
      document.metadata["corpus_source_fingerprint_fnv1a64"] = fingerprint;
      document.metadata["corpus_source_path"] = target.source_path.generic_string();
      document.metadata["corpus_source_repo"] = options.source_repo;
      if (!options.producer_repo.empty()) {
        document.metadata["corpus_producer_repo"] = options.producer_repo;
        document.metadata["corpus_producer_commit"] = options.producer_commit;
      }

      if (selected) {
        relative_output = fs::path("instances") / target.source_path;
        relative_output.replace_extension();
        relative_output /= normalized_stem + ".json";
        ValidateRelativeSourcePath(relative_output, "normalized instance path");
        const fs::path output_path = options.output / relative_output;
        if (!output_paths.insert(relative_output).second) {
          throw std::runtime_error("two exports map to the same output path: " +
                                   relative_output.generic_string());
        }
        fs::create_directories(output_path.parent_path());
        dsa::WriteStructuredProblemJsonFile(output_path, document);
      }
      representative_by_fingerprint.emplace(
          problem_fingerprint, RepresentativeDocument{canonical_problem, normalized_instance,
                                                      relative_output, selected});
    }

    const std::size_t alias_classes = document.problem.pypto_structure
                                          ? document.problem.pypto_structure->alias_classes.size()
                                          : 0;
    const std::size_t pipeline_groups =
        document.problem.pypto_structure ? document.problem.pypto_structure->pipeline_groups.size()
                                         : 0;
    records.push_back(
        {normalized_instance, input.case_id, target.source_path.generic_string(), target.family,
         target.devices, input.relative_path.generic_string(), relative_output.generic_string(),
         fingerprint, problem_fingerprint, representative, document.problem.pools.size(),
         document.problem.buffers.size(), alias_classes, pipeline_groups, stats.live_intervals,
         stats.temporal_conflicts, stats.reuse_candidates, selected, selection.reason});
    ++(*coverage_counts)[input.case_id];
  }
  std::sort(records.begin(), records.end(), [](const auto& first, const auto& second) {
    return std::tie(first.instance, first.case_id, first.export_file) <
           std::tie(second.instance, second.case_id, second.export_file);
  });
  return records;
}

void CheckCoverage(const std::map<std::string, CoverageTarget>& targets,
                   const std::map<std::string, std::size_t>& counts) {
  std::vector<std::string> missing;
  for (const auto& [case_id, target] : targets) {
    const auto found = counts.find(case_id);
    const std::size_t count = found == counts.end() ? 0 : found->second;
    if (!target.capture) {
      if (count != 0)
        missing.push_back(case_id + " (excluded, found " + std::to_string(count) + ")");
      continue;
    }
    if (count < target.minimum_documents) {
      missing.push_back(case_id + " (required " + std::to_string(target.minimum_documents) +
                        ", found " + std::to_string(count) + ")");
    }
  }
  if (!missing.empty()) {
    std::ostringstream message;
    message << "coverage target(s) are missing:";
    for (const std::string& item : missing) message << "\n  - " << item;
    throw std::runtime_error(message.str());
  }
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Options options = ParseOptions(argc, argv);
    ValidateTsvField(options.source_repo, "--source-repo");
    ValidateTsvField(options.source_commit, "--source-commit");
    if (!options.producer_repo.empty()) {
      ValidateTsvField(options.producer_repo, "--producer-repo");
      ValidateTsvField(options.producer_commit, "--producer-commit");
    }
    ValidateTsvField(options.corpus_namespace, "--namespace");
    const std::map<std::string, CoverageTarget> targets =
        options.coverage_targets ? ReadCoverageTargets(*options.coverage_targets)
                                 : std::map<std::string, CoverageTarget>{};
    const std::vector<InputDocument> inputs = DiscoverDocuments(options.inputs);
    std::map<std::string, std::size_t> discovered_counts;
    for (const InputDocument& input : inputs) {
      if (!targets.empty() && targets.count(input.case_id) == 0) {
        throw std::runtime_error("input case_id is not present in coverage targets: " +
                                 input.case_id);
      }
      ++discovered_counts[input.case_id];
    }
    CheckCoverage(targets, discovered_counts);
    RequireFreshOutputDirectory(options.output);
    std::map<std::string, std::size_t> coverage_counts;
    const std::vector<ManifestRecord> records =
        ImportCorpus(options, inputs, targets, &coverage_counts);
    WriteManifest(options.output / "manifest.tsv", options, records);
    if (!targets.empty()) WriteCoverage(options.output / "coverage.tsv", targets, coverage_counts);
    CheckCoverage(targets, coverage_counts);
    const auto representative_count =
        std::count_if(records.begin(), records.end(),
                      [](const ManifestRecord& record) { return record.representative; });
    const auto selected_count = std::count_if(
        records.begin(), records.end(),
        [](const ManifestRecord& record) { return record.representative && record.selected; });
    std::cout << "dsa-corpus: imported " << records.size() << " observations from "
              << coverage_counts.size() << " cases as " << representative_count
              << " unique problem shapes; selected " << selected_count
              << " meaningful benchmark problems into " << options.output << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "dsa-corpus: " << error.what() << '\n';
    return 1;
  }
}

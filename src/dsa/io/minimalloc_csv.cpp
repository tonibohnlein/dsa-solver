#include "dsa/io/minimalloc_csv.h"

#include <cctype>
#include <charconv>
#include <cstdint>
#include <fstream>
#include <istream>
#include <limits>
#include <map>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace dsa {
namespace {

std::string Trim(std::string_view value) {
  std::size_t begin = 0;
  while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0)
    ++begin;
  std::size_t end = value.size();
  while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) --end;
  return std::string(value.substr(begin, end - begin));
}

std::string Lowercase(std::string value) {
  for (char& character : value) {
    character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  return value;
}

std::vector<std::string> ParseCsvRow(const std::string& line, std::size_t line_number) {
  std::vector<std::string> fields;
  std::string field;
  bool quoted = false;
  for (std::size_t i = 0; i < line.size(); ++i) {
    const char character = line[i];
    if (quoted) {
      if (character == '"') {
        if (i + 1 < line.size() && line[i + 1] == '"') {
          field.push_back('"');
          ++i;
        } else {
          quoted = false;
        }
      } else {
        field.push_back(character);
      }
    } else if (character == ',') {
      fields.push_back(Trim(field));
      field.clear();
    } else if (character == '"') {
      if (!Trim(field).empty()) {
        throw std::runtime_error("CSV line " + std::to_string(line_number) +
                                 " has a quote inside an unquoted field");
      }
      field.clear();
      quoted = true;
    } else {
      field.push_back(character);
    }
  }
  if (quoted) {
    throw std::runtime_error("CSV line " + std::to_string(line_number) +
                             " has an unterminated quote");
  }
  fields.push_back(Trim(field));
  return fields;
}

template <typename Integer>
Integer ParseInteger(const std::string& text, const std::string& column, std::size_t line_number) {
  Integer value = 0;
  const char* begin = text.data();
  const char* end = text.data() + text.size();
  const auto parsed = std::from_chars(begin, end, value);
  if (text.empty() || parsed.ec != std::errc{} || parsed.ptr != end) {
    throw std::runtime_error("CSV line " + std::to_string(line_number) + " has invalid " + column +
                             " value '" + text + "'");
  }
  return value;
}

std::string EscapeCsv(std::string_view value) {
  if (value.find_first_of(",\"\n\r") == std::string_view::npos) return std::string(value);
  std::string escaped = "\"";
  for (char character : value) {
    if (character == '"') escaped.push_back('"');
    escaped.push_back(character);
  }
  escaped.push_back('"');
  return escaped;
}

}  // namespace

MiniMallocDocument ReadMiniMallocCsv(std::istream& input, PoolId pool) {
  MiniMallocDocument document;
  document.problem.pools = {{pool,
                             pool == kDefaultPool ? "default" : std::to_string(pool),
                             std::nullopt,
                             {},
                             std::nullopt}};

  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (!Trim(line).empty()) break;
  }
  if (line_number == 0 || Trim(line).empty()) throw std::runtime_error("MiniMalloc CSV is empty");

  const std::vector<std::string> header = ParseCsvRow(line, line_number);
  std::map<std::string, std::size_t> columns;
  for (std::size_t i = 0; i < header.size(); ++i) {
    const std::string name = Lowercase(Trim(header[i]));
    if (!columns.emplace(name, i).second) {
      throw std::runtime_error("MiniMalloc CSV repeats column '" + name + "'");
    }
  }
  for (const char* required : {"id", "lower", "upper", "size"}) {
    if (columns.count(required) == 0) {
      throw std::runtime_error(std::string("MiniMalloc CSV is missing required column '") +
                               required + "'");
    }
  }
  const bool has_offset = columns.count("offset") != 0;
  DsaSolution parsed_solution;

  while (std::getline(input, line)) {
    ++line_number;
    if (Trim(line).empty()) continue;
    const std::vector<std::string> fields = ParseCsvRow(line, line_number);
    auto field = [&](const std::string& name) -> const std::string& {
      const std::size_t index = columns.at(name);
      if (index >= fields.size()) {
        throw std::runtime_error("CSV line " + std::to_string(line_number) +
                                 " has fewer fields than the header");
      }
      return fields[index];
    };

    Buffer buffer;
    if (document.problem.buffers.size() > std::numeric_limits<BufferId>::max()) {
      throw std::runtime_error("MiniMalloc CSV has too many buffers for BufferId");
    }
    buffer.id = static_cast<BufferId>(document.problem.buffers.size());
    buffer.name = field("id");
    if (buffer.name.empty()) {
      throw std::runtime_error("CSV line " + std::to_string(line_number) + " has an empty id");
    }
    buffer.size = ParseInteger<std::uint64_t>(field("size"), "size", line_number);
    buffer.alignment = 1;
    buffer.live_intervals.push_back({
        ParseInteger<std::int64_t>(field("lower"), "lower", line_number),
        ParseInteger<std::int64_t>(field("upper"), "upper", line_number),
    });
    buffer.allowed_pools = {pool};
    document.problem.buffers.push_back(buffer);

    if (has_offset) {
      const std::uint64_t offset =
          ParseInteger<std::uint64_t>(field("offset"), "offset", line_number);
      parsed_solution.placements[buffer.id] = {pool, offset};
    }
  }
  if (document.problem.buffers.empty()) {
    throw std::runtime_error("MiniMalloc CSV contains no buffers");
  }
  if (has_offset) document.solution = std::move(parsed_solution);
  return document;
}

MiniMallocDocument ReadMiniMallocCsvFile(const std::filesystem::path& path, PoolId pool) {
  std::ifstream input(path);
  if (!input) throw std::runtime_error("cannot open MiniMalloc CSV input: " + path.string());
  return ReadMiniMallocCsv(input, pool);
}

void WriteMiniMallocCsv(std::ostream& output, const DsaProblem& problem,
                        const DsaSolution* solution) {
  output << "id,lower,upper,size";
  if (solution != nullptr) output << ",offset";
  output << '\n';

  for (const Buffer& buffer : problem.buffers) {
    if (buffer.live_intervals.size() != 1 || buffer.allowed_pools.size() != 1) {
      throw std::invalid_argument(
          "MiniMalloc CSV supports exactly one interval and one fixed pool per buffer");
    }
    const std::string name = buffer.name.empty() ? std::to_string(buffer.id) : buffer.name;
    output << EscapeCsv(name) << ',' << buffer.live_intervals.front().lower << ','
           << buffer.live_intervals.front().upper << ',' << buffer.size;
    if (solution != nullptr) {
      const Placement* placement = solution->Find(buffer.id);
      if (placement == nullptr) {
        throw std::invalid_argument("solution has no placement for buffer " +
                                    std::to_string(buffer.id));
      }
      if (placement->pool != buffer.allowed_pools.front()) {
        throw std::invalid_argument("solution pool cannot be represented in MiniMalloc CSV");
      }
      output << ',' << placement->offset;
    }
    output << '\n';
  }
}

void WriteMiniMallocCsvFile(const std::filesystem::path& path, const DsaProblem& problem,
                            const DsaSolution* solution) {
  std::ofstream output(path);
  if (!output) throw std::runtime_error("cannot open MiniMalloc CSV output: " + path.string());
  WriteMiniMallocCsv(output, problem, solution);
  if (!output) throw std::runtime_error("failed to write MiniMalloc CSV output: " + path.string());
}

}  // namespace dsa

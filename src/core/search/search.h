// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#pragma once

#include <absl/container/flat_hash_map.h>

#include <functional>
#include <memory>
#include <memory_resource>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>

#include "core/search/base.h"

namespace dfly::search {

struct AstNode;
struct TextIndex;

// Describes a specific index field
struct SchemaField {
  enum FieldType { TAG, TEXT, NUMERIC, VECTOR };
  enum FieldFlags : uint8_t { NOINDEX = 1 << 0, SORTABLE = 1 << 1 };

  struct VectorParams {
    bool use_hnsw = false;

    size_t dim = 0u;                              // dimension of knn vectors
    VectorSimilarity sim = VectorSimilarity::L2;  // similarity type
    size_t capacity = 1000;                       // initial capacity for hnsw world
  };

  using ParamsVariant = std::variant<std::monostate, VectorParams>;

  FieldType type;
  uint8_t flags;
  std::string short_name;  // equal to ident if none provided
  ParamsVariant special_params{std::monostate{}};
};

// Describes the fields of an index
struct Schema {
  // List of fields by identifier.
  absl::flat_hash_map<std::string /*identifier*/, SchemaField> fields;

  // Mapping for short field names (aliases).
  absl::flat_hash_map<std::string /* short name*/, std::string /*identifier*/> field_names;
};

// Collection of indices for all fields in schema
class FieldIndices {
 public:
  // Create indices based on schema
  FieldIndices(Schema schema, std::pmr::memory_resource* mr);

  void Add(DocId doc, DocumentAccessor* access);
  void Remove(DocId doc, DocumentAccessor* access);

  BaseIndex* GetIndex(std::string_view field) const;
  BaseSortIndex* GetSortIndex(std::string_view field) const;

  std::vector<TextIndex*> GetAllTextIndices() const;

  // TODO FIX
  const std::vector<DocId>& GetAllDocs() const;

  const Schema& GetSchema() const;

 private:
  void CreateIndices(std::pmr::memory_resource* mr);
  void CreateSortIndices(std::pmr::memory_resource* mr);

 private:
  Schema schema_;
  std::vector<DocId> all_ids_;
  absl::flat_hash_map<std::string, std::unique_ptr<BaseIndex>> indices_;
  absl::flat_hash_map<std::string, std::unique_ptr<BaseSortIndex>> sort_indices_;
};

struct AlgorithmProfile {
  struct ProfileEvent {
    std::string descr;
    size_t micros;         // time event took in microseconds
    size_t depth;          // tree depth of event
    size_t num_processed;  // number of results processed by the event
  };

  std::vector<ProfileEvent> events;
};

// Represents a search result returned from the search algorithm.
struct SearchResult {
  size_t total;  // how many documents were matched in total

  // number of matches before any aggregation, used by multi-shard optimizations
  size_t pre_aggregation_total;

  // The ids of the matched documents
  std::vector<DocId> ids;

  // Contains final scores if an aggregation was present
  std::vector<ResultScore> scores;

  // If profiling was enabled
  std::optional<AlgorithmProfile> profile;

  // If an error occurred, last recent one
  std::string error;
};

struct AggregationInfo {
  std::optional<size_t> limit;
  std::string_view alias;
  bool descending;
};

// SearchAlgorithm allows searching field indices with a query
class SearchAlgorithm {
 public:
  SearchAlgorithm();
  ~SearchAlgorithm();

  // Init with query and return true if successful.
  bool Init(std::string_view query, const QueryParams* params, const SortOption* sort = nullptr);

  SearchResult Search(const FieldIndices* index,
                      size_t limit = std::numeric_limits<size_t>::max()) const;

  // if enabled, return limit & alias for knn query
  std::optional<AggregationInfo> HasAggregation() const;

  void EnableProfiling();

 private:
  bool profiling_enabled_ = false;
  std::unique_ptr<AstNode> query_;
};

}  // namespace dfly::search

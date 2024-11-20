// Copyright 2023, DragonflyDB authors.  All rights reserved.
// See LICENSE for licensing terms.
//

#include "core/search/sort_indices.h"

#include <absl/container/flat_hash_set.h>
#include <absl/strings/ascii.h>
#include <absl/strings/numbers.h>
#include <absl/strings/str_split.h>

#include <algorithm>
#include <type_traits>

namespace dfly::search {

using namespace std;

namespace {}  // namespace

template <typename T>
SimpleValueSortIndex<T>::SimpleValueSortIndex(PMR_NS::memory_resource* mr) : values_{mr} {
}

template <typename T> SortableValue SimpleValueSortIndex<T>::Lookup(DocId doc) const {
  DCHECK_LT(doc, values_.size());
  if constexpr (is_same_v<T, PMR_NS::string>) {
    return std::string(values_[doc]);
  } else {
    return values_[doc];
  }
}

template <typename T>
std::vector<ResultScore> SimpleValueSortIndex<T>::Sort(std::vector<DocId>* ids, size_t limit,
                                                       bool desc) const {
  auto cb = [this, desc](const auto& lhs, const auto& rhs) {
    return desc ? (values_[lhs] > values_[rhs]) : (values_[lhs] < values_[rhs]);
  };
  std::partial_sort(ids->begin(), ids->begin() + std::min(ids->size(), limit), ids->end(), cb);

  vector<ResultScore> out(min(ids->size(), limit));
  for (size_t i = 0; i < out.size(); i++)
    out[i] = values_[(*ids)[i]];
  return out;
}

template <typename T>
bool SimpleValueSortIndex<T>::Add(DocId id, const DocumentAccessor& doc, std::string_view field) {
  auto field_value = Get(doc, field);
  if (!field_value) {
    return false;
  }

  DCHECK_LE(id, values_.size());  // Doc ids grow at most by one
  if (id >= values_.size())
    values_.resize(id + 1);

  values_[id] = field_value.value();
  return true;
}

template <typename T>
void SimpleValueSortIndex<T>::Remove(DocId id, const DocumentAccessor& doc,
                                     std::string_view field) {
  DCHECK_LT(id, values_.size());
  values_[id] = T{};
}

template <typename T> PMR_NS::memory_resource* SimpleValueSortIndex<T>::GetMemRes() const {
  return values_.get_allocator().resource();
}

template struct SimpleValueSortIndex<double>;
template struct SimpleValueSortIndex<PMR_NS::string>;

std::optional<double> NumericSortIndex::Get(const DocumentAccessor& doc, std::string_view field) {
  auto numbers_list = doc.GetNumbers(field);
  if (!numbers_list) {
    return std::nullopt;
  }
  return !numbers_list->empty() ? numbers_list->front() : 0.0;
}

std::optional<PMR_NS::string> StringSortIndex::Get(const DocumentAccessor& doc,
                                                   std::string_view field) {
  auto strings_list = doc.GetStrings(field);
  if (!strings_list) {
    return std::nullopt;
  }
  return !strings_list->empty() ? PMR_NS::string{strings_list->front(), GetMemRes()}
                                : PMR_NS::string{GetMemRes()};
}

}  // namespace dfly::search

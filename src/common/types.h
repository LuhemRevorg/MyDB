#pragma once
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>
#include "common/config.h"

// ── Record ID ────────────────────────────────────────────────────────────────

struct RID {
  page_id_t page_id{INVALID_PAGE_ID};
  uint32_t  slot_num{0};

  bool IsValid() const { return page_id != INVALID_PAGE_ID; }
  bool operator==(const RID& o) const {
    return page_id == o.page_id && slot_num == o.slot_num;
  }
};

// ── Column types ─────────────────────────────────────────────────────────────

enum class TypeId : uint8_t { INT, VARCHAR };

struct ColumnType {
  TypeId   type_id{TypeId::INT};
  uint16_t max_len{0};  // for VARCHAR only

  uint32_t GetFixedSize() const {
    return type_id == TypeId::INT ? sizeof(int32_t) : max_len;
  }

  static ColumnType Int()                   { return {TypeId::INT,     0}; }
  static ColumnType Varchar(uint16_t len)   { return {TypeId::VARCHAR, len}; }
};

struct Column {
  std::string name;
  ColumnType  type;
};

// ── Schema ───────────────────────────────────────────────────────────────────

class Schema {
 public:
  explicit Schema(std::vector<Column> cols) : columns_(std::move(cols)) {
    uint32_t off = 0;
    offsets_.reserve(columns_.size());
    for (const auto& col : columns_) {
      offsets_.push_back(off);
      off += col.type.GetFixedSize();
    }
    tuple_size_ = off;
  }

  size_t         GetColumnCount()              const { return columns_.size(); }
  const Column&  GetColumn(size_t i)           const { return columns_[i]; }
  uint32_t       GetTupleSize()                const { return tuple_size_; }
  uint32_t       GetColumnOffset(size_t i)     const { return offsets_[i]; }

  size_t GetColumnIdx(const std::string& name) const {
    for (size_t i = 0; i < columns_.size(); ++i)
      if (columns_[i].name == name) return i;
    throw std::runtime_error("Column not found: " + name);
  }

 private:
  std::vector<Column>   columns_;
  std::vector<uint32_t> offsets_;
  uint32_t              tuple_size_{0};
};

// ── Value ────────────────────────────────────────────────────────────────────

class Value {
 public:
  Value() = default;

  static Value MakeInt(int32_t v) {
    Value val;
    val.type_id_  = TypeId::INT;
    val.int_val_  = v;
    return val;
  }

  static Value MakeVarchar(std::string s, uint16_t max_len) {
    Value val;
    val.type_id_  = TypeId::VARCHAR;
    val.str_val_  = std::move(s);
    val.max_len_  = max_len;
    return val;
  }

  TypeId             GetType()    const { return type_id_; }
  int32_t            AsInt()      const { return int_val_; }
  const std::string& AsVarchar()  const { return str_val_; }

  void SerializeTo(char* dst) const {
    if (type_id_ == TypeId::INT) {
      memcpy(dst, &int_val_, sizeof(int32_t));
    } else {
      memset(dst, 0, max_len_);
      size_t n = std::min(str_val_.size(), static_cast<size_t>(max_len_));
      memcpy(dst, str_val_.data(), n);
    }
  }

  static Value DeserializeFrom(const char* src, const ColumnType& type) {
    if (type.type_id == TypeId::INT) {
      int32_t v;
      memcpy(&v, src, sizeof(int32_t));
      return MakeInt(v);
    }
    std::string s(src, strnlen(src, type.max_len));
    return MakeVarchar(std::move(s), type.max_len);
  }

  std::string ToString() const {
    return type_id_ == TypeId::INT ? std::to_string(int_val_) : str_val_;
  }

 private:
  TypeId      type_id_{TypeId::INT};
  int32_t     int_val_{0};
  std::string str_val_;
  uint16_t    max_len_{0};
};

// ── Tuple ────────────────────────────────────────────────────────────────────

class Tuple {
 public:
  Tuple() = default;

  Tuple(std::vector<Value> values, const Schema& schema) {
    data_.resize(schema.GetTupleSize());
    for (size_t i = 0; i < values.size(); ++i)
      values[i].SerializeTo(data_.data() + schema.GetColumnOffset(i));
  }

  Value GetValue(const Schema& schema, uint32_t col_idx) const {
    return Value::DeserializeFrom(
        data_.data() + schema.GetColumnOffset(col_idx),
        schema.GetColumn(col_idx).type);
  }

  uint32_t    GetLength() const { return static_cast<uint32_t>(data_.size()); }
  const char* GetData()   const { return data_.data(); }
  char*       GetData()         { return data_.data(); }

  static Tuple FromBytes(const char* data, uint32_t length) {
    Tuple t;
    t.data_.assign(data, data + length);
    return t;
  }

 private:
  std::vector<char> data_;
};

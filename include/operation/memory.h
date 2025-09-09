#ifndef REDSHOW_OPERATION_MEMORY_H
#define REDSHOW_OPERATION_MEMORY_H

#include <memory>

#include "common/utils.h"
#include "operation/operation.h"

namespace redshow {

struct MemoryRange {
  u64 start;
  u64 end;

  MemoryRange() = default;

  MemoryRange(u64 start, u64 end) : start(start), end(end) {}

  bool operator<(const MemoryRange &other) const { return start < other.start; }
};

struct Memory : public Operation {
  MemoryRange memory_range;
  size_t len;
  std::shared_ptr<u8[]> value;
  std::shared_ptr<u8[]> value_cache;

  Memory() : Operation(0, 0, OPERATION_TYPE_MEMORY) {}

  Memory(u64 op_id, i32 ctx_id)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMORY), len(0) {}

  Memory(u64 op_id, i32 ctx_id, u64 start, size_t len)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMORY), memory_range(start, start + len), len(len) {}

  Memory(u64 op_id, i32 ctx_id, MemoryRange &memory_range)
      : Operation(op_id, ctx_id, OPERATION_TYPE_MEMORY),
        memory_range(memory_range),
        len(memory_range.end - memory_range.start),
        value(new u8[len]),
        value_cache(new u8[len]) {}

  bool operator<(const Memory &other) const { return this->memory_range < other.memory_range; }
  bool operator==(const Memory &other) const {
    return this->memory_range.start == other.memory_range.start &&
           this->memory_range.end == other.memory_range.end;
  }

  virtual ~Memory() {}
};

/**
 * @brief calculate a hash for the memory region
 *
 * @param start
 * @param len
 * @return std::string
 */
std::string compute_memory_hash(u64 start, u64 len);

}  // namespace redshow


namespace std {
  template <>
  struct hash<redshow::Memory> {
    size_t operator()(const redshow::Memory& m) const noexcept {
      // 这是一个组合哈希的常用方法。
      // 选择能唯一标识一个 Memory 对象的成员进行哈希。
      // 如果只有 start 就能唯一标识，那么只哈希 start 即可。
      // 如果需要多个成员组合，就像下面这样。
      
      size_t h1 = std::hash<uint64_t>{}(m.memory_range.start);
      // size_t h2 = std::hash<int32_t>{}(m.op_id);
      
      // 将多个哈希值组合成一个。
      // 0x9e3779b9 是一个常用于哈希组合的“魔法常数”，来自 Boost 库。
      return h1; // 这是一个简单但有效的组合方式
     
    }
  };
}

#endif  // REDSHOW_OPERATION_MEMORY_H

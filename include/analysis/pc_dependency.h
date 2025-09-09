#ifndef REDSHOW_ANALYSIS_PC_DEPENDENCY_H
#define REDSHOW_ANALYSIS_PC_DEPENDENCY_H

#include <algorithm>
#include <fstream>
#include <list>
#include <map>
#include <numeric>
#include <queue>
#include <regex>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <functional>

#include "analysis.h"
#include "binutils/instruction.h"
#include "binutils/real_pc.h"
#include "common/map.h"
#include "common/utils.h"
#include "common/vector.h"
#include "redshow.h"

#define WARPSIZE 32

namespace redshow {
typedef Map<redshow::MemoryRange, std::shared_ptr<redshow::Memory>> MemoryMap;
class PcDependency final : public Analysis {
 public:
  PcDependency() : Analysis(REDSHOW_ANALYSIS_PC_DEPENDENCY) {}

  virtual ~PcDependency() = default;

 public:
  // private:
  struct ValueDistMemoryComp {
    bool operator()(const Memory &l, const Memory &r) const { return l.op_id < r.op_id; }
  };
  typedef struct AccessRecord {
      ThreadId _thread_id;
      u64 _pc;
      GPUPatchFlags _access_type;
      AccessRecord(ThreadId thread_id, u64 pc, GPUPatchFlags access_type)
          : _thread_id(thread_id), _pc(pc), _access_type(access_type) {}
      ~AccessRecord() = default;  
  } AccessRecord_t;

  typedef struct PCStats {
    uint64_t total_accesses{0};     // Total number of accesses from this PC
    uint64_t redundant_accesses{0};  // Number of redundant accesses
    GPUPatchFlags access_type;       // The type of access (READ/WRITE)
    
    PCStats(GPUPatchFlags type) : access_type(type) {}
  } PCStats_t;
  
  typedef struct PC_entry_stats {
    u64 dist[4] = {0};
    // 0: intra_thread, 
    // 1: intra_warp, 
    // 2: intra_block, 
    // 3: intra_grid

    PC_entry_stats() = default;
  } PC_entry_stats_t;


  // <page_id, count>
  typedef Map<u64, u64> AccessCount;
  // typedef std::unordered_map<Memory, AccessCount, ValueDistMemoryComp> PcDependencyCount;
  typedef std::unordered_map<Memory, AccessCount> PcDependencyCount;
  typedef std::unordered_map<Memory,std::unordered_map<u64, AccessRecord_t>> AccessHistory;
  typedef std::unordered_map<u32, AccessHistory> SharedMemoryAccessHistory; // <block id, <memory, <addr, AccessRecord_t>>>
  typedef std::unordered_map<u64, PCStats_t> PCStatsMap;
  typedef std::unordered_map<Memory, AccessRecord_t*> AccessRecordMap;//this is for O(1) indexing, need to allocate during entry of analysis_begin
  //Reuse analysis
  typedef std::unordered_map<u64, PC_entry_stats_t> PCDependencyMap_detail; // <acient_pc, stats of current pc to acient_pc>
  typedef std::unordered_map<u64, PCDependencyMap_detail> PCDependencyMap; // <pc, pc to its acient pcs stats>

  LockableMap<uint64_t, MemoryMap> *memory_snapshot_p;
  const int PAGE_SIZE = 4 * 1024;
  const int PAGE_SIZE_BITS = 12;
  long long access_count = 0;
  struct PcDependencyTrace final : public Trace {
    PcDependencyCount memory_access_count;
    AccessHistory access_history;
    AccessRecordMap access_record_map;
    SharedMemoryAccessHistory shared_memory_access_history;
    PCStatsMap pc_stats;
    PCDependencyMap pc_dependency_map;
    PcDependencyTrace() = default;

    virtual ~PcDependencyTrace() {}
  };

  // private:
  static inline std::shared_ptr<PcDependencyTrace> _trace;
  // Coarse-grained
  virtual void op_callback(OperationPtr operation);

  // Fine-grained
  virtual void analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id,
                              u32 mod_id, GPUPatchType type);

  virtual void analysis_end(u32 cpu_thread, i32 kernel_id);

  virtual void block_enter(const ThreadId &thread_id);

  virtual void block_exit(const ThreadId &thread_id);

  virtual void function_call(const ThreadId &thread_id, u64 pc, u64 target_pc);

  virtual void function_return(const ThreadId &thread_id, u64 pc, u64 target_pc);

  //  Since we don't use value, the value here will always be 0.
  virtual void unit_access(i32 kernel_id, const ThreadId &thread_id,
                           const AccessKind &access_kind, const Memory &memory,
                           u64 pc, u64 value, u64 addr, u32 index,
                           GPUPatchFlags flags);

  // Flush
  virtual void
  flush_thread(u32 cpu_thread, const std::string &output_dir,
               const LockableMap<u32, Cubin> &cubins,
               redshow_record_data_callback_func record_data_callback);

  // Flush
  virtual void
  flush_now(u32 cpu_thread, const std::string &output_dir,
            const LockableMap<u32, Cubin> &cubins,
            redshow_record_data_callback_func record_data_callback);

  virtual void flush(const std::string &output_dir,
                     const LockableMap<u32, Cubin> &cubins,
                     redshow_record_data_callback_func record_data_callback);
};
}  // namespace redshow

#endif  // REDSHOW_ANALYSIS_PC_DEPENDENCY

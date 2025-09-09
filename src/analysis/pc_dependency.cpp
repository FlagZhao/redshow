#include "analysis/pc_dependency.h"

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <tuple>
#include <utility>

#include "common/utils.h"
#include "common/vector.h"
#include "operation/kernel.h"
#include "redshow.h"

using redshow::LockableMap;
using redshow::Map;
using redshow::MemoryMap;

// template <typename T>
// T &create_NULL_ref() { return *static_cast<T *>(nullptr); }

namespace redshow {

void PcDependency::op_callback(OperationPtr operation) {
  // Do nothing
}

// Fine-grained
void PcDependency::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                                  GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_PC_DEPENDENCY);
  lock();
  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<PcDependencyTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
    this-> access_count = 0;
  }
  _trace = std::dynamic_pointer_cast<PcDependencyTrace>(this->_kernel_trace[cpu_thread][kernel_id]);
  unlock();
}

void PcDependency::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void PcDependency::block_enter(const ThreadId &thread_id) {
  // Do nothing
}

void PcDependency::block_exit(const ThreadId &thread_id) {
  // Do nothing
}

#define SANITIZER_API_DEBUG 1
#if SANITIZER_API_DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

void PcDependency::function_call(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  PRINT("redshow-> function_call pc=%lu, target_pc=%lu\n", pc, target_pc);
}

void PcDependency::function_return(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  PRINT("redshow-> function_return pc=%lu, target_pc=%lu\n", pc, target_pc);
}

void PcDependency::unit_access(i32 kernel_id, const ThreadId &thread_id,
                               const AccessKind &access_kind, const Memory &memory, u64 pc, 
                               u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  // PRINT("redshow-> kernel_id=%d, block_id=%u, thread_id=%u, unit_access pc=%llu, value=%llu, addr=%llu, index=%u, flags=%u\n", kernel_id, thread_id.flat_block_id, thread_id.flat_thread_id, pc, value, addr, index, flags);
  // addr += index * access_kind.unit_size / 8;
  // u64 page_index = addr >> PAGE_SIZE_BITS;
  // use value as size of access here for different types. aligned to every addr addr+4 recursively when value > 4. It's not a simple 4 byte unit shadow memory. It's our adaptive shadow memory. It can capture small access like 1,2,3 bytes without per-byte shadow memory. And the overhead is still O(1) because it is a hash table.
  if(value > 4){
    unit_access(kernel_id, thread_id, access_kind, memory, pc, value-4, addr+4, index, flags);
  }
  
  // PRINT("_trace %p", &_trace);
  auto &memory_access_count = _trace->memory_access_count;
  // @FindHao: moved the page processing later to drcctprof.
  memory_access_count[memory][addr] += 1;
  
  // auto& pc_acient_map = _trace->pc_dependency_map.try_emplace(pc, PC_entry_stats_t()).first->second;
  auto& pc_acient_map = _trace->pc_dependency_map[pc];
  // stats.total_accesses+=1;
  if(flags & GPU_PATCH_SHARED){
    //manage the shared memory access history
    auto last_access_record_shared = _trace->shared_memory_access_history[thread_id.flat_block_id][memory].find(addr);
    if(last_access_record_shared == _trace->shared_memory_access_history[thread_id.flat_block_id][memory].end()){
      pc_acient_map[0].dist[0] += 1; // cold miss
      _trace->shared_memory_access_history[thread_id.flat_block_id][memory].insert_or_assign(addr, AccessRecord_t(thread_id, pc, flags));
    }else{
      auto last_pc = last_access_record_shared->second._pc;
      auto last_tid = last_access_record_shared->second._thread_id;
      int topo_dist = 0;
      if(last_tid.flat_block_id == thread_id.flat_block_id){
        if(last_tid.flat_thread_id / WARPSIZE == thread_id.flat_thread_id / WARPSIZE){
          if(last_tid.flat_thread_id == thread_id.flat_thread_id){
            topo_dist = 0; // intra_thread
          }else{
            topo_dist = 1; // intra_warp
          }
        }else{
          topo_dist = 2; // intra_block
        }
      }else{
        topo_dist = 3; // intra_grid;
      }
      pc_acient_map[last_pc].dist[topo_dist] += 1;
      
      _trace->shared_memory_access_history[thread_id.flat_block_id][memory].insert_or_assign(addr, AccessRecord_t(thread_id, pc, flags));
    }
    return;
  }

  this->access_count+=1;
  auto last_access_record = _trace->access_history[memory].find(addr);
  if(last_access_record == _trace->access_history[memory].end()){
    //cold miss
    _trace->access_history[memory].insert_or_assign(addr, AccessRecord_t(thread_id, pc, flags));
    pc_acient_map[0].dist[0] += 1;
    // _trace->access_history[memory][addr] = AccessRecord_t(thread_id, pc, flags);
  }else{
    //global memory (not cold miss)
    // This is not the first access to this address
    auto last_pc = last_access_record->second._pc;
    auto last_tid = last_access_record->second._thread_id;
    int topo_dist = 0;
    if(last_tid.flat_block_id == thread_id.flat_block_id){
      if(last_tid.flat_thread_id / WARPSIZE == thread_id.flat_thread_id / WARPSIZE){
        if(last_tid.flat_thread_id == thread_id.flat_thread_id){
          topo_dist = 0; // intra_thread
        }else{
          topo_dist = 1; // intra_warp
        }
      }else{
        topo_dist = 2; // intra_block
      }
    }else{
      topo_dist = 3; // intra_grid
    }
    pc_acient_map[last_pc].dist[topo_dist] += 1;
    

    _trace->access_history[memory].insert_or_assign(addr, AccessRecord_t(thread_id, pc, flags));
  }
}

using std::cout;
using std::endl;
void PcDependency::flush_thread(u32 cpu_thread, const std::string &output_dir,
                                const LockableMap<u32, Cubin> &cubins,
                                redshow_record_data_callback_func record_data_callback) {
  PRINT("cpu_thread %d\n", cpu_thread);
  redshow::Map<redshow::i32, std::shared_ptr<redshow::Trace>> *thread_kernel_trace = nullptr;
  // std::ofstream ofs(output_dir + "/Instruction_account.csv");
  lock();
  if (this->_kernel_trace.has(cpu_thread)) {
    thread_kernel_trace = &(this->_kernel_trace.at(cpu_thread));
  } else {
    thread_kernel_trace = nullptr;
  }
  unlock();
  if (thread_kernel_trace == nullptr)
    return;
  // @findhao: for debug
  cout << std::flush << "======flush thread start=======" << endl;
  for (auto &kernel_trace_it : *thread_kernel_trace) {
    auto& pc_dependency_map = std::dynamic_pointer_cast<PcDependencyTrace>(kernel_trace_it.second)->pc_dependency_map;
    std::ofstream kernel_ofs(output_dir + "/kernel_" + std::to_string(kernel_trace_it.first) + "_pc_dependency.csv");
    for(const auto& [pc, pc_stats] : pc_dependency_map){
      kernel_ofs << std::hex << pc << std::dec << ":"<<std::endl;
      u64 pc_cold_miss = 0;
      u64 pc_intra_thread = 0;
      u64 pc_intra_warp = 0;
      u64 pc_intra_block = 0;
      u64 pc_intra_grid = 0;
      for(const auto& [acient_pc, stats] : pc_stats){
        if(acient_pc == 0){
          kernel_ofs << "\t" << "cold: "<< stats.dist[0] << std::endl;
          pc_cold_miss += stats.dist[0];
          continue;
        }
        kernel_ofs << "\t" << std::hex << acient_pc << std::dec << ":\t";
        kernel_ofs << "intra_thread: " << stats.dist[0] << ",\t"
                    << "intra_warp: " << stats.dist[1] << ",\t"
                    << "intra_block: " << stats.dist[2] << ",\t"
                    << "intra_grid: " << stats.dist[3] << std::endl;
        pc_intra_thread += stats.dist[0];
        pc_intra_warp += stats.dist[1];
        pc_intra_block += stats.dist[2];
        pc_intra_grid += stats.dist[3];
      }
      kernel_ofs << "Total: " << std::endl;
      kernel_ofs << "\t\t" << "cold: " << pc_cold_miss <<",\t"<<
                  "intra_thread: " << pc_intra_thread << ",\t"
                  "intra_warp: " << pc_intra_warp << ",\t"
                  "intra_block: " << pc_intra_block << ",\t"
                  "intra_grid: " << pc_intra_grid << std::endl<<std::endl;
    }
    kernel_ofs.close();
  }
  cout << "Access Count:" << this->access_count << endl << std::flush;
  cout << endl
       << "=====flush thread end======" << endl
       << std::flush;
}

void PcDependency::flush_now(u32 cpu_thread, const std::string &output_dir,
                             const LockableMap<u32, Cubin> &cubins,
                             redshow_record_data_callback_func record_data_callback) {
  // PRINT("cpu_thread %d\n", cpu_thread);
  // redshow::Map<redshow::i32, std::shared_ptr<redshow::Trace>> *thread_kernel_trace = nullptr;
  // std::ofstream ofs(output_dir + "/Instruction_account.csv");
  // lock();
  // if (this->_kernel_trace.has(cpu_thread)) {
  //   thread_kernel_trace = &(this->_kernel_trace.at(cpu_thread));
  // } else {
  //   thread_kernel_trace = nullptr;
  // }
  // unlock();
  // if (thread_kernel_trace == nullptr)
  //   return;
  // // clean all current data
  // for (auto &kernel_trace_it : *thread_kernel_trace) {
  //   // auto &mpc = std::dynamic_pointer_cast<PcDependencyTrace>(kernel_trace_it.second)->memory_access_count;
  //   // cout << "mpc.size " << mpc.size();
  //   // mpc.clear();
  //   // cout << "mpc.size after " << mpc.size() << endl << std::flush;
  //   cout<<"kernel:"<<kernel_trace_it.first<<endl; //print kernel id
  //   for(const auto& [pc,stats] : std::dynamic_pointer_cast<PcDependencyTrace>(kernel_trace_it.second)->pc_stats){
  //     double redundancy_rate = static_cast<double>(stats.redundant_accesses) / 
  //                               static_cast<double>(stats.total_accesses);
          
  //     // ofs << std::hex << pc << std::dec << ","
  //     //     << (stats.access_type == GPU_PATCH_WRITE ? "WRITE" : "READ") << ","
  //     //     << stats.total_accesses << ","
  //     //     << stats.redundant_accesses << ","
  //     //     << redundancy_rate << "\n";  
  //     // For debug print the ofs's path is not correct.
  //     cout  << "pc: " << std::hex << pc << std::dec 
  //           << " \ttype: " << (stats.access_type == GPU_PATCH_WRITE ? "W" : "R") 
  //           << " \ttotal_accesses: " << stats.total_accesses 
  //           << " \tredundant_accesses: " << stats.redundant_accesses 
  //           << " \tredundancy_rate: " << redundancy_rate 
  //           << endl << std::flush;
  //   }


  // }
  // cout << "Access Count:" << this->access_count << endl << std::flush;

}

void PcDependency::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                         redshow_record_data_callback_func record_data_callback) {}

}  // namespace redshow

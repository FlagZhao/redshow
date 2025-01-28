#include "analysis/redundant_write.h"

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

template <typename T>
T &create_NULL_ref() { return *static_cast<T *>(nullptr); }

namespace redshow {

void RedundantWrite::op_callback(OperationPtr operation) {
  // Do nothing
}

// Fine-grained
void RedundantWrite::analysis_begin(u32 cpu_thread, i32 kernel_id, u32 cubin_id, u32 mod_id,
                                  GPUPatchType type) {
  assert(type == GPU_PATCH_TYPE_REDUNDANT_WRITE);
  lock();
  if (!this->_kernel_trace[cpu_thread].has(kernel_id)) {
    auto trace = std::make_shared<RedundantWriteTrace>();
    trace->kernel.ctx_id = kernel_id;
    trace->kernel.cubin_id = cubin_id;
    trace->kernel.mod_id = mod_id;
    this->_kernel_trace[cpu_thread][kernel_id] = trace;
    this-> access_count = 0;
  }
  _trace = std::dynamic_pointer_cast<RedundantWriteTrace>(this->_kernel_trace[cpu_thread][kernel_id]);
  unlock();
}

void RedundantWrite::analysis_end(u32 cpu_thread, i32 kernel_id) { _trace.reset(); }

void RedundantWrite::block_enter(const ThreadId &thread_id) {
  // Do nothing
}

void RedundantWrite::block_exit(const ThreadId &thread_id) {
  // Do nothing
}

#define SANITIZER_API_DEBUG 1
#if SANITIZER_API_DEBUG
#define PRINT(...) fprintf(stderr, __VA_ARGS__)
#else
#define PRINT(...)
#endif

void RedundantWrite::function_call(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  PRINT("redshow-> function_call pc=%llu, target_pc=%llu\n", pc, target_pc);
}

void RedundantWrite::function_return(const ThreadId &thread_id, u64 pc, u64 target_pc) {
  PRINT("redshow-> function_return pc=%llu, target_pc=%llu\n", pc, target_pc);
}

void RedundantWrite::unit_access(i32 kernel_id, const ThreadId &thread_id,
                               const AccessKind &access_kind, const Memory &memory, u64 pc, 
                               u64 value, u64 addr, u32 index, GPUPatchFlags flags) {
  // PRINT("redshow-> kernel_id=%d, block_id=%u, thread_id=%u, unit_access pc=%llu, value=%llu, addr=%llu, index=%u, flags=%u\n", kernel_id, thread_id.flat_block_id, thread_id.flat_thread_id, pc, value, addr, index, flags);
  // addr += index * access_kind.unit_size / 8;
  // u64 page_index = addr >> PAGE_SIZE_BITS;
  // PRINT("_trace %p", &_trace);
  auto &memory_access_count = _trace->memory_access_count;
  // @FindHao: moved the page processing later to drcctprof.
  memory_access_count[memory][addr] += 1;
  
  auto& stat=_trace->pc_stats.try_emplace(pc, flags).first->second;
  stat.total_accesses+=1;

  this->access_count+=1;
  auto last_access_record = _trace->access_history[memory].find(addr);
  if(last_access_record == _trace->access_history[memory].end()){
    _trace->access_history[memory].emplace(addr, AccessRecord_t(thread_id, pc, flags));
    // _trace->access_history[memory][addr] = AccessRecord_t(thread_id, pc, flags);
  }else{
    if(last_access_record->second._thread_id == thread_id){
      
      if(last_access_record->second._access_type == flags){
        //Pure redundancy 
        if(flags == GPU_PATCH_WRITE){
          PRINT("Redundant W Detected: Memory:%p,\t Blockid:%d,\t Threadid:%d,\t PC:%lx\n",addr,thread_id.flat_block_id,thread_id.flat_thread_id,pc);        
        }else if(flags == GPU_PATCH_READ){
          PRINT("Redundant R Detected: Memory:%p,\t Blockid:%d,\t Threadid:%d,\t PC:%lx\n",addr,thread_id.flat_block_id,thread_id.flat_thread_id,pc);
        }
        stat.redundant_accesses+=1;
      }else{
        //Read After Write
        if(last_access_record->second.__access_type == GPU_PATCH_WRITE && flags == GPU_PATCH_READ){
          stat.redundant_accesses+=1;
          PRINT("Redundant WR Detected: Memory:%p,\t Blockid:%d,\t Threadid:%d,\t PC:%lx\n",addr,thread_id.flat_block_id,thread_id.flat_thread_id,pc);
        }
      }
    }else{
      // maybe left for locality analyze.
      // PRINT("NOT SAME THREAD: Blockid:%d,\t Threadid:%d,\t PC%lx,\t last Blockid:%d,\t Last Threadid:%d, PC%lx,\n",thread_id.flat_block_id,thread_id.flat_thread_id,pc,last_access_record->second._thread_id.flat_block_id,last_access_record->second._thread_id.flat_thread_id,last_access_record->second._pc);
    }
    // else if(last_access_record->second._access_type == flags && flags == GPU_PATCH_WRITE){
    //   PRINT("Race Detected from Memory:%p, Thread A: Blockid:%d, Threadid:%d, ThreadB: Blockid:%p, Threadid:%d, PC:%x\n",addr,thread_id.flat_block_id,thread_id.flat_thread_id,last_access_record->second.flat_block_id,last_access_record->second.flat_thread_id,pc);
    // }
    // data Race detection should be here
    _trace->access_history[memory].emplace(addr, AccessRecord_t(thread_id, pc, flags));
  }
}

using std::cout;
using std::endl;
void RedundantWrite::flush_thread(u32 cpu_thread, const std::string &output_dir,
                                const LockableMap<u32, Cubin> &cubins,
                                redshow_record_data_callback_func record_data_callback) {
  if (!this->_kernel_trace.has(cpu_thread)) {
    return;
  }
  lock();
  auto &thread_kernel_trace = this->_kernel_trace.at(cpu_thread);
  unlock();
  // @findhao: for debug
  cout << std::flush << "======flush thread start=======" << endl;
  // @findhao is it necessnary to be thread safe?
  for (auto item : this->_kernel_trace) {
    cout << "cpu thread id " << item.first << endl;
    for (auto item2 : item.second) {
      // cout << "kernel id " << item2.first << endl;
      auto trace = std::dynamic_pointer_cast<RedundantWriteTrace>(item2.second);
      auto mpc = trace->memory_access_count;
      cout << "size: " << mpc.size() << endl;
      for (auto item3 : mpc) {
        // item3: <Memory, AccessCount>
        cout << &item3 << " " << item3.first.len << " " << item3.first.memory_range.start << " " << item3.first.memory_range.end << "\t" << item3.second.size() << endl;
      }
    }
  }
  cout << endl
       << "=====flush thread end======" << endl
       << std::flush;
}

void RedundantWrite::flush_now(u32 cpu_thread, const std::string &output_dir,
                             const LockableMap<u32, Cubin> &cubins,
                             redshow_record_data_callback_func record_data_callback) {
  PRINT("cpu_thread %d\n", cpu_thread);
  redshow::Map<redshow::i32, std::shared_ptr<redshow::Trace>> *thread_kernel_trace = nullptr;
  std::ofstream ofs(output_dir + "/Instruction_account.csv");
  lock();
  if (this->_kernel_trace.has(cpu_thread)) {
    thread_kernel_trace = &(this->_kernel_trace.at(cpu_thread));
  } else {
    thread_kernel_trace = nullptr;
  }
  unlock();
  if (thread_kernel_trace == nullptr)
    return;
  // clean all current data
  for (auto &kernel_trace_it : *thread_kernel_trace) {
    auto &mpc = std::dynamic_pointer_cast<RedundantWriteTrace>(kernel_trace_it.second)->memory_access_count;
    cout << "mpc.size " << mpc.size();
    mpc.clear();
    cout << "mpc.size after " << mpc.size() << endl << std::flush;
    for(const auto& [pc,stats] : std::dynamic_pointer_cast<RedundantWriteTrace>(kernel_trace_it.second)->pc_stats){
      double redundancy_rate = static_cast<double>(stats.redundant_accesses) / 
                                static_cast<double>(stats.total_accesses);
          
      // ofs << std::hex << pc << std::dec << ","
      //     << (stats.access_type == GPU_PATCH_WRITE ? "WRITE" : "READ") << ","
      //     << stats.total_accesses << ","
      //     << stats.redundant_accesses << ","
      //     << redundancy_rate << "\n";  
      // For debug print the ofs's path is not correct.
      cout  << "pc: " << std::hex << pc << std::dec 
            << " \ttype: " << (stats.access_type == GPU_PATCH_WRITE ? "W" : "R") 
            << " \ttotal_accesses: " << stats.total_accesses 
            << " \tredundant_accesses: " << stats.redundant_accesses 
            << " \tredundancy_rate: " << redundancy_rate 
            << endl << std::flush;
    }


  }
  cout << "Access Count:" << this->access_count << endl << std::flush;

}

void RedundantWrite::flush(const std::string &output_dir, const LockableMap<u32, Cubin> &cubins,
                         redshow_record_data_callback_func record_data_callback) {}

}  // namespace redshow

#include "reassembler.hh"
#include "debug.hh"

using namespace std;

void Reassembler::insert(uint64_t first_index, string data, bool is_last_substring)
{
  debug("unimplemented insert({}, {}, {}) called", first_index, data, is_last_substring);
  
  // note: 一定先判断发过来的字节流类型，以及是否是最后一串字节流
  if (data.empty()) {
    if (is_last_substring) {
      eof_rec = true;
      eof_index = first_index;
      
      if (next_index >= eof_index) {
        output_.writer().close();
      }
    }
    return;
  }

  uint64_t len = data.size();
  
  // 处理EOF标记
  if (is_last_substring) {
    eof_rec = true;
    eof_index = first_index + len;
    
    if (next_index >= eof_index) {
      output_.writer().close();
    }
  }

  // 获取可处理的容量限制
  uint64_t total_capacity = output_.writer().available_capacity() + output_.reader().bytes_buffered();
  uint64_t available_space = output_.writer().available_capacity();
  
  // 如果缓冲区已满，不存储任何未来数据，直接返回
  if (available_space == 0) {
    return; 
  }
  /* note:数据重叠处理是最复杂的部分，因为都避免重复状态，所以都细致思考*/
  // 处理数据重叠部分 - 先处理与前面数据的重叠
  uint64_t new_index = first_index;
  
  // 如果起始位置在已处理数据之前，调整起始位置
  if (first_index < next_index) {
    // 完全重叠，直接丢弃
    if (first_index + len <= next_index) {
      return;
    }
    // 部分重叠，截取未处理部分
    uint64_t offset = next_index - first_index;
    data = data.substr(offset);
    new_index = next_index;
    len = data.size();
  }
  
  // 检查重叠情况 - 与pending_data中已有数据比较
  auto upper_bound = pending_data.upper_bound(new_index);
  if (upper_bound != pending_data.begin()) {
    auto prev = upper_bound;
    --prev;
    
    // 检查与前一个片段的重叠
    if (prev->first <= new_index && new_index < prev->first + prev->second.size()) {
      // 有重叠，调整起始位置
      uint64_t overlap = prev->first + prev->second.size() - new_index;
      if (overlap >= len) {
        // 完全被包含，丢弃
        return;
      }
      data = data.substr(overlap);
      new_index += overlap;
      len = data.size();
    }
  }
  
  // 处理与后续片段的重叠
  auto it = pending_data.lower_bound(new_index);
  while (it != pending_data.end() && new_index + len > it->first) {
    if (it->first < new_index + len) {
      if (new_index + len >= it->first + it->second.size()) {
        // 当前数据完全覆盖了这个片段
        pending_data.erase(it++);
      } else {
        // 部分重叠，截断当前数据
        len = it->first - new_index;
        data = data.substr(0, len);
        break;
      }
    } else {
      break;
    }
  }
  
  // 检查是否超出总容量限制
  if (new_index > next_index && new_index - next_index + len > total_capacity) {
    if (new_index - next_index >= total_capacity) {
      return;  // 确实超出范围才返回
    }
    uint64_t available = total_capacity - (new_index - next_index);
    data = data.substr(0, available);
    len = data.size();
  }

  // 处理数据写入
  if (new_index == next_index) {
    // 检查可用容量
    if (available_space >= len) {
      // 有足够容量，直接写入
      output_.writer().push(data);
      next_index += len;
    } else {
      // 容量不足，只写入能写入的部分，剩余部分丢弃
      string write_data = data.substr(0, available_space);
      output_.writer().push(write_data);
      next_index += available_space;
      // 不存储剩余部分到pending_data
    }
    
    // 查找并处理后续连续片段
    bool found_next = true;
    while (!pending_data.empty() && found_next && output_.writer().available_capacity() > 0) {
      found_next = false;
      auto next_it = pending_data.find(next_index);
      if (next_it != pending_data.end()) {
        // 检查可用容量
        uint64_t avail = output_.writer().available_capacity();
        string pending_data_str = next_it->second;
        
        if (avail >= pending_data_str.size()) {
          // 能全部写入
          output_.writer().push(pending_data_str);
          next_index += pending_data_str.size();
          pending_data.erase(next_it);
          found_next = true;
        } else if (avail > 0) {
          // 只能部分写入，剩余部分丢弃
          string write_part = pending_data_str.substr(0, avail);
          output_.writer().push(write_part);
          next_index += avail;
          pending_data.erase(next_it);
          // 不存储剩余部分
          break; 
        } else {
          break;
        }
      }
    }
  } else if (!data.empty() && new_index > next_index) {
    // 确保new_index与next_index之间的距离加上数据长度不超过总容量
    if (new_index - next_index + len <= total_capacity && available_space > 0) {
      // 存入pending_data
      pending_data[new_index] = data;
    }
    // 否则丢弃数据
  }
  
  // 检查是否需要关闭流
  if (eof_rec && next_index >= eof_index) {
    output_.writer().close();
  }
  return;
}

uint64_t Reassembler::count_bytes_pending() const
{
  debug("unimplemented count_bytes_pending() called");
  uint64_t count = 0;
  for (const auto& pair : pending_data) {
    count += pair.second.size();
  }
  return count;
}
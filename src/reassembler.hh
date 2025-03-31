#pragma once

#include "byte_stream.hh"
#include<map>
#include<string>
class Reassembler
{
public:
  // Construct Reassembler to write into given ByteStream.

  explicit Reassembler( ByteStream&& output ) : 
  output_( std::move( output ) ),
  pending_data(),
  next_index(0),
  eof_index(0),
  eof_rec(false){}

  /*
   * 插入一个新的子字符串以重新组装到ByteStream中。
   *   `first_index`: 子字符串的第一个字节的索引
   *   `data`: 子字符串本身
   *   `is_last_substring`: 此子字符串表示流的结束
   *   `output`: 对Writer的可变引用
   *
   * Reassembler的任务是将索引的子字符串（可能是乱序或重叠的）重新组装回原始的ByteStream。
   * 一旦Reassembler知道流中的下一个字节，它应该立即将其写入输出。
   *
   * 如果Reassembler了解到字节适合流的可用容量但还不能写入（因为前面的字节仍然未知），
   * 它应该将它们存储在内部，直到填补了空缺。
   *
   * Reassembler应该丢弃任何超出流可用容量的字节
   * （即即使前面的空缺被填补也无法写入的字节）。
   *
   * Reassembler在写入最后一个字节后应关闭流。
   */
  void insert( uint64_t first_index, std::string data, bool is_last_substring );

  // Reassembler本身存储了多少字节？
  // 此函数仅用于测试；不要为了支持它而添加额外的状态。
  uint64_t count_bytes_pending() const;

  // 访问输出流读取器
  Reader& reader() { return output_.reader(); }
  const Reader& reader() const { return output_.reader(); }

  // 访问输出流写入器，但仅为const（不能从外部写入）
  const Writer& writer() const { return output_.writer(); }

private:
  ByteStream output_;
  std::map<uint64_t, std::string> pending_data;// 存储数据的映射
  uint64_t next_index = 0; // 下一个索引
  uint64_t eof_index = 0;
  bool eof_rec = false;
  
};

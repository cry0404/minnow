#include "wrapping_integers.hh"
#include "debug.hh"

using namespace std;

Wrap32 Wrap32::wrap( uint64_t n, Wrap32 zero_point )
{
  // Your code here.
  uint32_t offset = (n + zero_point.raw_value_) % (1ULL<<32);
  //debug( "unimplemented wrap( {}, {} ) called", n, zero_point.raw_value_ );
  return Wrap32 { offset };
}

uint64_t Wrap32::unwrap( Wrap32 zero_point, uint64_t checkpoint ) const
{
  // Your code here.
  //消除最开始的随机序列号引起的偏移量，有利于找到绝对序列号
  uint32_t offset = raw_value_ - zero_point.raw_value_;
  //*清除低32位，使用位运算方法
  uint64_t base = checkpoint & ~(0xFFFFFFFFULL);//其实这里我一开始用的除法，后面让 ai 检查代码时它推荐我用位运算
  //位运算在底层还是好用的，又把之前 datalab 的知识还回去了 ：(
  uint64_t now = base + offset;
  uint64_t next = now + (1ULL<<32);
  uint64_t pre = now - (1ULL<<32);

  uint64_t distance1 = now >= checkpoint? now - checkpoint : checkpoint - now;
  uint64_t distance2 = next >= checkpoint? next - checkpoint : checkpoint - next;
  uint64_t distance3 = pre >= checkpoint? pre - checkpoint : checkpoint - pre;
  //简单的三个中选最小，懒得封装了
  if (distance1 <= distance2 && distance1 <= distance3) {
    return now;
  } else if (distance2 <= distance1 && distance2 <= distance3) {
    return next;
  } else {
    return pre;
  }
 // debug( "unimplemented unwrap( {}, {} ) called", zero_point.raw_value_, checkpoint );
  return {};
}

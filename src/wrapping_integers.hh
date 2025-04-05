#pragma once

#include <cstdint>

/*
 * Wrap32类型表示一个32位无符号整数，它：
 *    - 从任意的"零点"（初始值）开始，并且
 *    - 当它达到2^32 - 1时会绕回到零。
 */

class Wrap32
{
public:
  // 显式构造函数，接受一个32位原始值
  explicit Wrap32( uint32_t raw_value ) : raw_value_( raw_value ) {}

  /* 给定一个绝对序列号n和零点，构造一个Wrap32对象。 */
  static Wrap32 wrap( uint64_t n, Wrap32 zero_point );

  /*
   * unwrap方法返回一个绝对序列号，给定零点和一个"检查点"（另一个接近所需答案的绝对序列号），
   * 该序列号会映射到这个Wrap32。
   *
   * 可能有很多绝对序列号都会映射到同一个Wrap32。
   * unwrap方法应该返回最接近检查点的那一个。
   */
  uint64_t unwrap( Wrap32 zero_point, uint64_t checkpoint ) const;

  // 重载+运算符，返回原始值加上n的新Wrap32对象
  Wrap32 operator+( uint32_t n ) const { return Wrap32 { raw_value_ + n }; }
  
  // 重载==运算符，比较两个Wrap32对象的原始值是否相等
  bool operator==( const Wrap32& other ) const { return raw_value_ == other.raw_value_; }

protected:
  // 保护成员变量，存储32位原始值
  uint32_t raw_value_ {};
};

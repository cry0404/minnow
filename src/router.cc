#include "router.hh"
#include "debug.hh"

#include <iostream>

using namespace std;

// route_prefix: 用于匹配数据报目标地址的"最多32位"IPv4地址前缀
// prefix_length: 对于适用此路由，route_prefix的多少个高位(最高有效位)
//    需要与数据报目标地址的对应位匹配？
// next_hop: 下一跳的IP地址。如果网络直接连接到路由器，则为空
//    (在这种情况下，下一跳地址应该是数据报的最终目的地)。
// interface_num: 发送数据报的接口索引。
void Router::add_route( const uint32_t route_prefix,
                        const uint8_t prefix_length,
                        const optional<Address> next_hop,
                        const size_t interface_num )
{
  cerr << "DEBUG: 添加路由 " << Address::from_ipv4_numeric( route_prefix ).ip() << "/"
       << static_cast<int>( prefix_length ) << " => " << ( next_hop.has_value() ? next_hop->ip() : "(直连)" )
       << " 在接口 " << interface_num << "\n";

  // 创建新的路由条目并添加到路由表中
  RouteEntry entry{route_prefix, prefix_length, next_hop, interface_num};
  routing_table_.push_back(entry);
}

// 检查指定的IP地址是否匹配给定前缀
bool Router::matches_prefix(uint32_t ip, uint32_t prefix, uint8_t prefix_length) const
{
  // 如果前缀长度为0，匹配所有IP（默认路由）
  if (prefix_length == 0) {
    return true;
  }
  
  // 创建掩码，只保留前prefix_length位
  uint32_t mask = ~(static_cast<uint32_t>(0xFFFFFFFF) >> prefix_length);
  
  // 应用掩码并比较
  return (ip & mask) == (prefix & mask);
}

// 查找最长前缀匹配的路由条目
std::optional<Router::RouteEntry> Router::longest_prefix_match(uint32_t dst_ip) const
{
  std::optional<RouteEntry> best_match = std::nullopt;
  uint8_t longest_match = 0;
  
  // 遍历路由表，查找最长前缀匹配
  for (const auto &entry : routing_table_) {
    if (matches_prefix(dst_ip, entry.route_prefix, entry.prefix_length)) {
      // 如果这个前缀比之前找到的更长，则更新最佳匹配
      if (!best_match.has_value() || entry.prefix_length > longest_match) {
        best_match = entry;
        longest_match = entry.prefix_length;
      }
    }
  }
  
  return best_match;
}

// 遍历所有接口，将每个传入的数据报路由到其正确的传出接口。
void Router::route()
{
  // 遍历所有接口
  for (size_t i = 0; i < interfaces_.size(); i++) {
    // 获取当前接口
    auto &interface = interfaces_[i];
    
    // 处理所有等待中的数据报
    auto &datagrams = interface->datagrams_received();
    while (!datagrams.empty()) {
      // 获取数据报
      InternetDatagram datagram = std::move(datagrams.front());
      datagrams.pop();
      
      // 获取数据报的目标IP地址
      uint32_t dst_ip = datagram.header.dst;
      
      // TTL检查
      if (datagram.header.ttl <= 1) {
        // 如果TTL为1或0，丢弃数据报
        continue;
      }
      
      // 递减TTL
      datagram.header.ttl--;
      
      // 重新计算IP头部校验和
      datagram.header.compute_checksum();
      
      // 查找匹配的路由条目
      auto route_entry = longest_prefix_match(dst_ip);
      
      // 如果找到匹配的路由
      if (route_entry.has_value()) {
        // 如果有下一跳地址，使用它；否则使用目标IP作为下一跳
        const Address next_hop = route_entry->next_hop.has_value() ? 
                               *route_entry->next_hop : 
                               Address::from_ipv4_numeric(dst_ip);
        
        // 转发数据报到相应的接口
        interfaces_[route_entry->interface_num]->send_datagram(datagram, next_hop);
      }
      // 如果没有找到匹配的路由，则丢弃数据报
    }
  }
}


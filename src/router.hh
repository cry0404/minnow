#pragma once

#include "exception.hh"
#include "network_interface.hh"
#include <map>
#include <optional>
#include <vector>

// \brief 一个拥有多个网络接口并在它们之间
// 执行最长前缀匹配路由的路由器。
class Router
{
public:
  // 向路由器添加一个接口
  // \param[in] interface 一个已经构造好的网络接口
  // \returns 该接口被添加到路由器后的索引
  size_t add_interface( std::shared_ptr<NetworkInterface> interface )
  {
    interfaces_.push_back( notnull( "add_interface", std::move( interface ) ) );
    return interfaces_.size() - 1;
  }

  
  std::shared_ptr<NetworkInterface> interface( const size_t N ) { return interfaces_.at( N ); }

  // 添加一条路由（转发规则）
  void add_route( uint32_t route_prefix,
                  uint8_t prefix_length,
                  std::optional<Address> next_hop,
                  size_t interface_num );

  // 在接口之间路由数据包
  void route();

private:
  // 路由条目结构
  struct RouteEntry {
    uint32_t route_prefix;
    uint8_t prefix_length;
    std::optional<Address> next_hop;
    size_t interface_num;
  };
  
  // 检查IP地址是否匹配前缀
  bool matches_prefix(uint32_t ip, uint32_t prefix, uint8_t prefix_length) const;
  
  // 根据目标IP查找最佳路由条目
  std::optional<RouteEntry> longest_prefix_match(uint32_t dst_ip) const;

  // 路由器的网络接口集合
  std::vector<std::shared_ptr<NetworkInterface>> interfaces_ {};
  
  // 路由表
  std::vector<RouteEntry> routing_table_ {};
};

#include <iostream>

#include "arp_message.hh"
#include "debug.hh"
#include "ethernet_frame.hh"
#include "exception.hh"
#include "helpers.hh"
#include "network_interface.hh"

using namespace std;

//! \param[in] ethernet_address Ethernet (what ARP calls "hardware") address of the interface
//! \param[in] ip_address IP (what ARP calls "protocol") address of the interface
NetworkInterface::NetworkInterface( string_view name,
                                    shared_ptr<OutputPort> port,
                                    const EthernetAddress& ethernet_address,
                                    const Address& ip_address )
  : name_( name )
  , port_( notnull( "OutputPort", move( port ) ) )
  , ethernet_address_( ethernet_address )
  , ip_address_( ip_address )
{
  cerr << "DEBUG: Network interface has Ethernet address " << to_string( ethernet_address_ ) << " and IP address "
       << ip_address.ip() << "\n";
}

//! \param[in] dgram the IPv4 datagram to be sent
//! \param[in] next_hop the IP address of the interface to send it to (typically a router or default gateway, but
//! may also be another host if directly connected to the same network as the destination) Note: the Address type
//! can be converted to a uint32_t (raw 32-bit IP address) by using the Address::ipv4_numeric() method.
void NetworkInterface::send_datagram( const InternetDatagram& dgram, const Address& next_hop )
{
  const uint32_t next_hop_ip = next_hop.ipv4_numeric();
  const auto it = arp_table_.find(next_hop_ip);
  //先在对应的 arp_table 中查找，如果查找到直接发送
  if (it != arp_table_.end() && it->second.expiry_time > current_time_) {
    EthernetFrame frame;
    frame.header.dst = it->second.eth_addr;
    frame.header.src = ethernet_address_;
    frame.header.type = EthernetHeader::TYPE_IPv4;
    //这里需要查看 util 中的 parser.hh 中定义的序列，相当于是封装了 ip 层到链路层帧的转换这一过程
    Serializer serializer;
    dgram.serialize(serializer);
    frame.payload = serializer.finish();

    transmit(frame);
  } else {
    if (arp_request_time_.find(next_hop_ip) == arp_request_time_.end() || 
        current_time_ - arp_request_time_[next_hop_ip] >= ARP_REQUEST_TIMEOUT_) {
        
        ARPMessage arp_request;

        //这里对应的是发送出去的 opcode
        arp_request.opcode = ARPMessage::OPCODE_REQUEST;
        arp_request.sender_ethernet_address = ethernet_address_;
        arp_request.sender_ip_address = ip_address_.ipv4_numeric();
        arp_request.target_ethernet_address = {};
        arp_request.target_ip_address = next_hop_ip;
        //根据 ip 中的信息构建链路帧
        EthernetFrame frame;
        frame.header.dst = ETHERNET_BROADCAST;
        frame.header.src = ethernet_address_;
        //确定是 arp 还是 ipv4
        frame.header.type = EthernetHeader::TYPE_ARP;

        Serializer serializer;
        arp_request.serialize(serializer);
        frame.payload = serializer.finish();

        transmit(frame);
        arp_request_time_[next_hop_ip] = current_time_;
    }

    // 存储待发送的数据报及其添加时间
    pending_dgram_info_[next_hop_ip].emplace(current_time_, dgram);
  }
}


void NetworkInterface::recv_frame( EthernetFrame frame )
{
  //如果不是发给本地的包或者不是广播包的话直接丢弃
  if (frame.header.dst != ethernet_address_ && frame.header.dst != ETHERNET_BROADCAST) {
    return;
  }

  //如果是 ipv4 包则直接进行处理
  if (frame.header.type == EthernetHeader::TYPE_IPv4) {
    //这里需要阅读 parser.hh 的逻辑
    Parser parser{std::move(frame.payload)};
    InternetDatagram dgram;
    dgram.parse(parser);

    if (!parser.has_error()) {
      datagrams_received_.push(std::move(dgram));
    }
  } else if (frame.header.type == EthernetHeader::TYPE_ARP) {
    Parser parser{std::move(frame.payload)};
    ARPMessage arp_msg;
    arp_msg.parse(parser);

    if (parser.has_error() || !arp_msg.supported()) {
      return;
    }
    //处理对应是 arp 请求的，需要先解析
    const uint32_t sender_ip = arp_msg.sender_ip_address;
    const EthernetAddress sender_mac = arp_msg.sender_ethernet_address;

    arp_table_[sender_ip] = {sender_mac, current_time_ + ARP_TIMEOUT_};
    arp_request_time_.erase(sender_ip);
    
    if (arp_msg.opcode == ARPMessage::OPCODE_REQUEST &&
        arp_msg.target_ip_address == ip_address_.ipv4_numeric()) {
        ARPMessage arp_reply;
        arp_reply.opcode = ARPMessage::OPCODE_REPLY;
        arp_reply.sender_ethernet_address = ethernet_address_;
        arp_reply.sender_ip_address = ip_address_.ipv4_numeric();
        arp_reply.target_ethernet_address = sender_mac;
        arp_reply.target_ip_address = sender_ip;

        EthernetFrame reply_frame;
        reply_frame.header.dst = sender_mac;
        reply_frame.header.src = ethernet_address_;
        reply_frame.header.type = EthernetHeader::TYPE_ARP;

        Serializer serializer;
        arp_reply.serialize(serializer);
        reply_frame.payload = serializer.finish();

        transmit(reply_frame);
    }

    // 检查是否有等待发送到此 IP 的数据报
    auto pending_it = pending_dgram_info_.find(sender_ip);
    if (pending_it != pending_dgram_info_.end() && !pending_it->second.empty()) {
      auto& pending_queue = pending_it->second;
      
      // 只发送未过期的数据报（添加时间 + ARP_REQUEST_TIMEOUT_ > current_time_）
      while (!pending_queue.empty()) {
        auto [timestamp, dgram] = pending_queue.front();
        pending_queue.pop();
        
        // 检查数据报是否过期（超过 ARP_REQUEST_TIMEOUT_ 未收到响应）
        if (current_time_ - timestamp < ARP_REQUEST_TIMEOUT_) {
          
          EthernetFrame data_frame;
          data_frame.header.dst = sender_mac;
          data_frame.header.src = ethernet_address_;
          data_frame.header.type = EthernetHeader::TYPE_IPv4;
          
          Serializer serializer;
          dgram.serialize(serializer);
          data_frame.payload = serializer.finish();
          
          transmit(data_frame);
        }
        // 过期的数据报会被丢弃（什么都不做，只从队列中移除）
      }
      
      // 移除空队列
      pending_dgram_info_.erase(sender_ip);
    }
  }
}

//! \param[in] ms_since_last_tick the number of milliseconds since the last call to this method
void NetworkInterface::tick( const size_t ms_since_last_tick )
{
  current_time_ += ms_since_last_tick;

  // 处理 ARP 表项过期
  for (auto it = arp_table_.begin(); it != arp_table_.end(); ) {
    if (current_time_ >= it->second.expiry_time) {
      it = arp_table_.erase(it);
    } else {
      ++it;
    }
  }

  // 处理 ARP 请求超时和重发
  for (auto it = arp_request_time_.begin(); it != arp_request_time_.end(); ) {
    const uint32_t next_hop_ip = it->first;
    const uint64_t last_request_time = it->second;
    
    // 如果自上次请求以来已超过 ARP_REQUEST_TIMEOUT_
    if (current_time_ - last_request_time >= ARP_REQUEST_TIMEOUT_) {
      // 检查是否有尚未过期的待发送数据报
      bool has_valid_pending_datagrams = false;
      auto pending_it = pending_dgram_info_.find(next_hop_ip);
      
      if (pending_it != pending_dgram_info_.end()) {
        // 创建一个临时队列来保存未过期的数据报
        std::queue<std::pair<uint64_t, InternetDatagram>> valid_datagrams;
        
        // 检查所有待发送的数据报
        while (!pending_it->second.empty()) {
          auto [timestamp, dgram] = pending_it->second.front();
          pending_it->second.pop();
          
          // 如果数据报未过期，保留它
          if (current_time_ - timestamp < ARP_REQUEST_TIMEOUT_) {
            valid_datagrams.push({timestamp, std::move(dgram)});
            has_valid_pending_datagrams = true;
          }
          // 过期的数据报会被丢弃
        }
        
        // 用未过期的数据报替换原队列
        pending_it->second = std::move(valid_datagrams);
        
        // 如果没有有效的数据报，移除整个条目，相当于去掉过期了
        if (pending_it->second.empty()) {
          pending_dgram_info_.erase(pending_it);
        }
      }
      
      // 如果仍有未过期的数据报，重新发送 ARP 请求
      if (has_valid_pending_datagrams) {
        ARPMessage arp_request;
        arp_request.opcode = ARPMessage::OPCODE_REQUEST;
        arp_request.sender_ethernet_address = ethernet_address_;
        arp_request.sender_ip_address = ip_address_.ipv4_numeric();
        arp_request.target_ethernet_address = {};
        arp_request.target_ip_address = next_hop_ip;
        
        EthernetFrame frame;
        frame.header.dst = ETHERNET_BROADCAST;
        frame.header.src = ethernet_address_;
        frame.header.type = EthernetHeader::TYPE_ARP;
        
        Serializer serializer;
        arp_request.serialize(serializer);
        frame.payload = serializer.finish();
        
        transmit(frame);
        
        // 更新请求时间
        it->second = current_time_;
        ++it;
      } else {
        // 如果没有未过期的数据报，移除 ARP 请求条目
        it = arp_request_time_.erase(it);
      }
    } else {
      ++it;
    }
  }
}

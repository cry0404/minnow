#include "tcp_sender.hh"
#include "debug.hh"
#include "tcp_config.hh"

using namespace std;

// This function is for testing only; don't add extra state to support it.
//跟踪已经发送但未被确认的序列号数量
uint64_t TCPSender::sequence_numbers_in_flight() const
{
  return bytes_in_flight_;
}

//记录连续重传的数量
uint64_t TCPSender::consecutive_retransmissions() const
{

  return retransmissions_;
}
//实现数据的发送，先要考虑窗口大小等限制
void TCPSender::push( const TransmitFunction& transmit )
{
  //首先检查流是否有错
  if (reader().has_error()) {
    TCPSenderMessage msg;
    msg.seqno = isn_ + next_seqno_;
    msg.RST = true;
    transmit(msg);
    return;
  }
  uint64_t window_size = window_size_ > 0 ? window_size_ : 1;
  //如果有确认号的话就将其解包转换
  uint64_t abs_ackno = ackno_.has_value() ? ackno_.value().unwrap(isn_, next_seqno_) : 0;
  uint64_t window_end = abs_ackno + window_size;
  
  uint64_t available_window = window_end > next_seqno_ ? window_end - next_seqno_ : 0;

  // 如果最开始没有发送 syn 的话就先发送
  if (next_seqno_ == 0 && available_window > 0) {
    TCPSenderMessage msg;
    msg.seqno = isn_;
    msg.SYN = true;
    
    // 尝试添加数据负载 
    size_t payload_capacity = available_window > 1 ? available_window - 1 : 0; // SYN占用1个序列号
    payload_capacity = std::min(payload_capacity, static_cast<uint64_t>(TCPConfig::MAX_PAYLOAD_SIZE));
    
    if (payload_capacity > 0 && (reader().peek().size()>0)) {
      size_t read_size = std::min(payload_capacity, reader().peek().size());
      if (read_size > 0) {
        msg.payload = reader().peek().substr(0, read_size);
        reader().pop(read_size);
      }
    }
    
    // 检查是否应该在同一个包中发送 FIN
    if (reader().is_finished() && !fin_sent_ && 
        available_window > 1 + msg.payload.size()) { // 需要额外的窗口空间给FIN
        msg.FIN = true;
        fin_sent_ = true;
    }

    transmit(msg);

    size_t segment_size = 1 + msg.payload.size() + (msg.FIN ? 1 : 0); // SYN占1，payload占其大小，FIN占1
    outstanding_segments_.push({msg, 0});
    bytes_in_flight_ += segment_size;
    next_seqno_ += segment_size;
    
    if (!timer_running_) {
      timer_running_ = true;
      time_since_last_activity_ = 0;
      RTO_ms_ = initial_RTO_ms_;
    }
    
    available_window -= segment_size;
    
    // 如果已发送FIN，不再发送数据
    if (msg.FIN) {
      return;
    }
  }
  
  while(available_window > 0 ){
    if(reader().peek().size() == 0 && (!reader().is_finished() || fin_sent_)) {
      break;
    }
    TCPSenderMessage msg;
    msg.seqno = isn_ + next_seqno_;
    //这里需要去找 tcpconfig 中对于 tcp 数据段的要求
    size_t payload_size = std::min(available_window,static_cast<uint64_t>(TCPConfig::MAX_PAYLOAD_SIZE));
    payload_size = std::min(payload_size,reader().peek().size());

    if(payload_size > 0){
      msg.payload = reader().peek().substr(0,payload_size);
      reader().pop(payload_size);
    }

    if (reader().is_finished() && !fin_sent_ && available_window > msg.payload.size()) {
      msg.FIN = true;
      fin_sent_ = true;
    }

    if (msg.payload.empty() && !msg.SYN && !msg.FIN) {
      break;
    }
    
    // 发送消息
    transmit(msg);
    size_t segment_size = msg.payload.size() + (msg.FIN ? 1 : 0);
    outstanding_segments_.push({msg, 0});
    bytes_in_flight_ += segment_size;
    next_seqno_ += segment_size;
    
    // 启动计时器
    if (!timer_running_) {
      timer_running_ = true;
      time_since_last_activity_ = 0;
      RTO_ms_ = initial_RTO_ms_;
    }
    
    available_window -= segment_size;
    
    // 如果已发送FIN，不再发送数据
    if (msg.FIN) {
      break;
    }
  }
  return;
}
//创建不包含数据的 tcp 消息，也就是只有个头
TCPSenderMessage TCPSender::make_empty_message() const
{
  
  TCPSenderMessage msg;
  msg.seqno = isn_ + next_seqno_;
  if (reader().has_error()) {
    msg.RST = true;
  }
  return msg;
}

void TCPSender::receive(const TCPReceiverMessage& msg)
{
  // 检查是否收到RST标志
  if (msg.RST) {
    writer().set_error();
    return;
  }

  window_size_ = msg.window_size;

  if(msg.ackno.has_value()){
    uint64_t abs_ackno = msg.ackno.value().unwrap(isn_,next_seqno_);

    if(abs_ackno <= next_seqno_){
      ackno_ = msg.ackno;
    

    bool segments_acknowledged = false;
    //然后就是经典的队列遍历了
    while(!outstanding_segments_.empty()){
      const auto& segment = outstanding_segments_.front();
      uint64_t seg_seqno = segment.msg.seqno.unwrap(isn_,next_seqno_);
      uint64_t seg_end = seg_seqno + segment.msg.payload.size() + 
                          (segment.msg.SYN ? 1 : 0) + 
                          (segment.msg.FIN ? 1 : 0);
      if (seg_end <= abs_ackno) {
          // 完全确认此段
          bytes_in_flight_ -= (segment.msg.payload.size() + 
                             (segment.msg.SYN ? 1 : 0) + 
                             (segment.msg.FIN ? 1 : 0));
          
          outstanding_segments_.pop();
          segments_acknowledged = true;
        } else {
          break;
        }
      }
      
      // 如果确认了新的段，重置重传计数和RTO
      if (segments_acknowledged) {
        retransmissions_ = 0;
        RTO_ms_ = initial_RTO_ms_;
        
        // 重置计时器（如果还有未确认的段）
        if (!outstanding_segments_.empty()) {
          time_since_last_activity_ = 0;
        } else {
          timer_running_ = false;
        }                    
    }
  }
 }
}

void TCPSender::tick( uint64_t ms_since_last_tick, const TransmitFunction& transmit )
{
  //同样的，发送消息前都应该检查流是否有错
  if (reader().has_error()) {
    TCPSenderMessage msg;
    msg.seqno = isn_ + next_seqno_;
    msg.RST = true;
    transmit(msg);
    return;
  }
  if (timer_running_) {
    time_since_last_activity_ += ms_since_last_tick;
    
    // 检查是否超时
    if (time_since_last_activity_ >= RTO_ms_ && !outstanding_segments_.empty()) {
      // 重传最早的未确认段
      transmit(outstanding_segments_.front().msg);
      
      // 窗口非零时增加连续重传计数并更新RTO
      if (window_size_ > 0) {
       retransmissions_++;
        RTO_ms_ *= 2;  // 指数退避
      }
      
      // 重置计时器
      time_since_last_activity_ = 0;
    }
  }
}

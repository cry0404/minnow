#include "tcp_receiver.hh"
#include "debug.hh"

using namespace std;

void TCPReceiver::receive( TCPSenderMessage message )
{
  // Your code here.
  //*note 先按影响程度做判断,如果遇到 rst 应该重置连接
  if(message.RST){
    is_syn_received_ = false;
    is_fin_received_ = false;
    zero_point_ = std::nullopt;
    has_error_ = true;  // 设置错误标志
   //简单重置状态
   reassembler_.reader().set_error() ;  // 设置ByteStream错误
    return;
  }
  if(message.SYN){
    if(!is_syn_received_){//第一次收到才设置零点
      is_syn_received_ = true;
      zero_point_ = message.seqno;
    }
  }
  if(!is_syn_received_ || !zero_point_.has_value()){
    return; //丢弃所有没有 syn 值的包
  }
  uint64_t checkpoint = reassembler_.writer().bytes_pushed();
  uint64_t abs_seqno = message.seqno.unwrap(zero_point_.value(),checkpoint);
  
  uint64_t stream_index = abs_seqno;
  if(message.SYN){
    stream_index = 0;
  }
  else{
    stream_index = abs_seqno - 1;
  }

  if(message.FIN){
    is_fin_received_ = true;
  }
  if (!message.SYN && abs_seqno == 0) {
    // 忽略具有SYN序列号但不是SYN的无效数据包
    return;
  }
  reassembler_.insert(stream_index, message.payload, message.FIN);
  //debug( "unimplemented receive() called" );
  
}

TCPReceiverMessage TCPReceiver::send() const
{
  // Your code here.
  TCPReceiverMessage response {};
  //有错的话直接重来
  if (has_error_ || reassembler_.reader().has_error()) {
    response.RST = true;
    response.ackno = nullopt;
    return response;
  }
  
 
  uint64_t window_size = reassembler_.writer().available_capacity();
  response.window_size = min(window_size, static_cast<uint64_t>(UINT16_MAX));

  // 如果连接未建立，不设置确认号
  if (!is_syn_received_ || !zero_point_.has_value()) {
    return response;
  }

  // 计算确认号
  uint64_t abs_ackno = reassembler_.writer().bytes_pushed() + 1;  // +1 是因为SYN
  
  // 如果流已结束且已完全接收，再加1（FIN占一个序列号）
  if (reassembler_.writer().is_closed()) {
    abs_ackno += 1;
  }
  
  // 转换为相对序列号
  response.ackno = zero_point_->wrap(abs_ackno, *zero_point_);
  
  return response;
  //debug( "unimplemented send() called" );
  
}

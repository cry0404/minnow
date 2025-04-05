#pragma once

#pragma once

#include "reassembler.hh"
#include "tcp_receiver_message.hh"
#include "tcp_sender_message.hh"
#include <optional>

class TCPReceiver
{
public:
  // Construct with given Reassembler
  explicit TCPReceiver( Reassembler&& reassembler ) : 
    reassembler_( std::move( reassembler ) ),
    is_syn_received_( false ),
    is_fin_received_( false ),
    has_error_( false ),
    zero_point_( std::nullopt ) {}

  /*
   * The TCPReceiver receives TCPSenderMessages, inserting their payload into the Reassembler
   * at the correct stream index.
   */
  void receive( TCPSenderMessage message );

  // The TCPReceiver sends TCPReceiverMessages to the peer's TCPSender.
  TCPReceiverMessage send() const;

  // Access the output
  const Reassembler& reassembler() const { return reassembler_; }
  Reader& reader() { return reassembler_.reader(); }
  const Reader& reader() const { return reassembler_.reader(); }
  const Writer& writer() const { return reassembler_.writer(); }

private:
  Reassembler reassembler_;
  bool is_syn_received_; // 标记是否已收到SYN（连接是否已建立）
  bool is_fin_received_; // 判断是否收到 FIN
  bool has_error_;
  std::optional<Wrap32> zero_point_; // 初始序列号(ISN)，在收到SYN前是未定义的
  //学到的定义方式，用 has_value 来判断
};

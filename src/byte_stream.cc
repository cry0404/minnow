#include "byte_stream.hh"

using namespace std;

ByteStream::ByteStream( uint64_t capacity ) 
    : capacity_( capacity ) 
    , error_( false )
    , buffer_()
    , closed_( false )
    , pushed_count_( 0 )
    , popped_count_( 0 ) 
{}

void Writer::push( string data )
{
  if ( is_closed() ) {
    return;
  } // 关了自然就不读了

  size_t available = available_capacity();
  if ( data.size() > available ) {
    data = data.substr( 0, available );
  }
  buffer_ += data;
  pushed_count_ += data.size();
  //(void)data; // Your code here.
}

void Writer::close()
{
  // Your code here.
  closed_ = true; // 关闭直接将刚才设计的成员变量关掉即可
  return;
}

bool Writer::is_closed() const
{
  return closed_; // Your code here.
}

uint64_t Writer::available_capacity() const
{

  return capacity_ - buffer_.size(); // Your code here.
}

uint64_t Writer::bytes_pushed() const
{

  return pushed_count_; // Your code here.
}

string_view Reader::peek() const
{
  string_view view( buffer_ );
  return view; // Your code here.
}

void Reader::pop( uint64_t len )
{
  len = min( len, buffer_.size() ); // 确保不超过当前缓冲区的大小
  buffer_ = buffer_.substr( len );
  popped_count_ += len;
  //(void)len; // Your code here.
  return;
}

bool Reader::is_finished() const
{
  return is_stream_closed() && buffer_.empty(); // Your code here.
}

uint64_t Reader::bytes_buffered() const
{
  return buffer_.size(); // Your code here.
}

uint64_t Reader::bytes_popped() const
{
  return popped_count_; // Your code here.
}

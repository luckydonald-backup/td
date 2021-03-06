//
// Copyright Aliaksei Levin (levlam@telegram.org), Arseny Smirnov (arseny30@gmail.com) 2014-2017
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
#include "td/mtproto/HttpTransport.h"

#include "td/net/HttpHeaderCreator.h"

#include "td/utils/buffer.h"
#include "td/utils/logging.h"
#include "td/utils/Slice.h"
#include "td/utils/Status.h"

// TODO: do I need \r\n as delimiter?

#include <cstring>

namespace td {
namespace mtproto {
namespace http {

Result<size_t> Transport::read_next(BufferSlice *message, uint32 *quick_ack) {
  CHECK(can_read());
  auto r_size = reader_.read_next(&http_query_);
  if (r_size.is_error() || r_size.ok() != 0) {
    return r_size;
  }
  if (http_query_.type_ != HttpQuery::Type::RESPONSE) {
    return Status::Error("Unexpected http query type");
  }
  if (http_query_.container_.size() != 2u) {
    return Status::Error("Wrong response");
  }
  *message = std::move(http_query_.container_[1]);
  turn_ = Write;
  return 0;
}

void Transport::write(BufferWriter &&message, bool quick_ack) {
  CHECK(can_write());
  CHECK(!quick_ack);
  /*
   * POST /api HTTP/1.1
   * Content-Length: [message->size()]
   * Host: url
   */
  HttpHeaderCreator hc;
  hc.init_post("/api");
  hc.add_header("Host", "");
  hc.set_keep_alive();
  hc.set_content_size(message.size());
  auto r_head = hc.finish();
  if (r_head.is_error()) {
    UNREACHABLE();
  }
  Slice src = r_head.ok();
  MutableSlice dst = message.prepare_prepend();
  CHECK(dst.size() >= src.size()) << dst.size() << " >= " << src.size();
  std::memcpy(dst.end() - src.size(), src.begin(), src.size());
  message.confirm_prepend(src.size());
  output_->append(message.as_buffer_slice());
  turn_ = Read;
}

bool Transport::can_read() const {
  return turn_ == Read;
}

bool Transport::can_write() const {
  return turn_ == Write;
}

size_t Transport::max_prepend_size() const {
  return MAX_PREPEND_SIZE;
}

}  // namespace http
}  // namespace mtproto
}  // namespace td

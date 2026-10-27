/* SPDX-License-Identifier: Apache-2.0 */
#include "plugin_api/session.hpp"

#include <cassert>
#include <sys/socket.h>
#include <unistd.h>

using namespace aeraui::plugin_api;

static void SendWorker(int fd, Kind kind, uint32_t request = 0,
                       uint32_t value = 0, uint32_t flags = 0) {
  Message message;
  message.kind = kind;
  message.request_id = request;
  message.value = value;
  message.flags = flags;
  assert(send(fd, &message, sizeof(message), MSG_NOSIGNAL) ==
         static_cast<ssize_t>(sizeof(message)));
}

static Message ReceiveHost(int fd) {
  Message message;
  assert(recv(fd, &message, sizeof(message), MSG_WAITALL) ==
         static_cast<ssize_t>(sizeof(message)));
  assert(Valid(message, false));
  return message;
}

int main() {
  int pair[2];
  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  Session session;
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, 0, 2, 2);
  assert(session.Poll().empty());
  assert(session.Negotiated());
  const Message hello = ReceiveHost(pair[1]);
  assert(hello.kind == Kind::kHelloAck);
  assert(hello.value == kProtocolVersion);
  assert(hello.flags == 3); // Strict old workers must receive the original ACK.
  assert(!session.Supports(kFeatureBackground));
  assert((hello.flags & kFeatureMetrics) != 0);
  assert((hello.flags & kFeatureBackNavigation) != 0);
  assert(!session.Supports(kFeatureDesktop));
  assert(!session.Supports(kFeatureClipboard));
  const Message resume = ReceiveHost(pair[1]);
  assert(resume.kind == Kind::kLifecycle);
  assert(resume.value == static_cast<uint32_t>(Lifecycle::kResume));
  SendWorker(pair[1], Kind::kBeginPage);
  const auto page = session.Poll();
  assert(page.size() == 1 && page[0].kind == Kind::kBeginPage);
  SendWorker(pair[1], Kind::kAddMetric, 7, 0, kMetricAccent);
  const auto metric = session.Poll();
  assert(metric.size() == 1 && metric[0].kind == Kind::kAddMetric);
  assert(metric[0].request_id == 7 && metric[0].flags == kMetricAccent);
  SendWorker(pair[1], Kind::kSetBackAction, 42);
  const auto back = session.Poll();
  assert(back.size() == 1 && back[0].kind == Kind::kSetBackAction);
  assert(back[0].request_id == 42);
  assert(!session.SendDesktop(100, 0, 1));
  close(pair[1]);
  session.Close();
  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello,
      kFeatureRequestMagic | kFeatureDesktop | kFeatureDesktopResolution, 2, 2);
  session.Poll();
  assert(ReceiveHost(pair[1]).flags == (3 | kFeatureDesktop | kFeatureDesktopResolution));
  (void)ReceiveHost(pair[1]);
  static_assert(static_cast<uint32_t>(Kind::kSetBackground) == 12);
  static_assert(static_cast<uint32_t>(Kind::kBackgroundResult) == 68);
  static_assert(static_cast<uint32_t>(Kind::kOpenDesktop) == 21);
  static_assert(static_cast<uint32_t>(Kind::kDesktopReady) == 72);
  SendWorker(pair[1], Kind::kOpenDesktop, 100);
  const auto desktop = session.Poll();
  assert(desktop.size() == 1 && desktop[0].kind == Kind::kOpenDesktop);
  int descriptors[2];
  assert(pipe(descriptors) == 0);
  assert(session.SendDesktop(100, descriptors[0], descriptors[1],1920,1080));
  Message reply;
  iovec io{&reply, sizeof(reply)};
  alignas(cmsghdr) char ancillary[CMSG_SPACE(sizeof(int) * 2)]{};
  msghdr packet{};
  packet.msg_iov = &io; packet.msg_iovlen = 1;
  packet.msg_control = ancillary; packet.msg_controllen = sizeof(ancillary);
  assert(recvmsg(pair[1], &packet, 0) == sizeof(reply));
  assert(reply.kind == Kind::kDesktopReady && reply.request_id == 100);
  assert(reply.value == 1920 && reply.flags == 1080);
  auto *header = CMSG_FIRSTHDR(&packet);
  assert(header && header->cmsg_level == SOL_SOCKET && header->cmsg_type == SCM_RIGHTS);
  assert(header->cmsg_len == CMSG_LEN(sizeof(int) * 2));
  int received[2];
  memcpy(received, CMSG_DATA(header), sizeof(received));
  assert(write(received[1], "x", 1) == 1);
  char value = 0;
  assert(read(received[0], &value, 1) == 1 && value == 'x');
  close(received[0]); close(received[1]);
  close(descriptors[0]); close(descriptors[1]);
  close(pair[1]);
  assert(session.Poll().empty());
  assert(!session.Connected());
  assert(!session.Supports(kFeatureMetrics));

  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, kFeatureRequestMagic | kOptionalFeatures | (1U << 12), 2, 2);
  assert(session.Poll().empty());
  const Message extended = ReceiveHost(pair[1]);
  assert(extended.flags == (3 | kOptionalFeatures));
  assert(session.Supports(kOptionalFeatures));
  (void)ReceiveHost(pair[1]);
  static_assert(static_cast<uint32_t>(Kind::kGetClipboard) == 22);
  static_assert(static_cast<uint32_t>(Kind::kClipboardResult) == 73);
  SendWorker(pair[1], Kind::kGetClipboard, 71);
  assert(session.Poll().at(0).kind == Kind::kGetClipboard);
  assert(session.Send(Kind::kClipboardResult, 71, 0, kClipboardComplete));
  assert(ReceiveHost(pair[1]).kind == Kind::kClipboardResult);
  SendWorker(pair[1], Kind::kSetBackground, 10, 1);
  assert(session.Poll().at(0).kind == Kind::kSetBackground);
  SendWorker(pair[1], Kind::kAddProgress, 9, 6500);
  assert(session.Poll().at(0).value == 6500);
  assert(session.Send(Kind::kActivityCancel, 42));
  assert(ReceiveHost(pair[1]).request_id == 42);
  close(pair[1]);
  session.Close();
  assert(!session.Supports(kOptionalFeatures));

  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, kFeatureRequestMagic | kFeatureActivities, 2, 2);
  session.Poll();
  assert(ReceiveHost(pair[1]).flags == (3 | kFeatureActivities));
  (void)ReceiveHost(pair[1]);
  assert(!session.Supports(kFeatureBackground));
  close(pair[1]);
  session.Close();

  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, kOptionalFeatures, 2, 2);
  session.Poll();
  assert(ReceiveHost(pair[1]).flags == 3);
  (void)ReceiveHost(pair[1]);
  close(pair[1]);
  session.Close();

  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, 0, 3, 3);
  assert(session.Poll().empty());
  assert(!session.Connected());
  close(pair[1]);

  assert(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, pair) == 0);
  assert(session.Adopt(pair[0]));
  SendWorker(pair[1], Kind::kHello, 0, 2, 2);
  session.Poll();
  (void)ReceiveHost(pair[1]);
  (void)ReceiveHost(pair[1]);
  for (unsigned index = 0; index < 129 && session.Connected(); ++index) {
    SendWorker(pair[1], Kind::kSetStatus);
    (void)session.Poll();
  }
  assert(!session.Connected());
  close(pair[1]);
}

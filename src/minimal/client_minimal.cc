// Copyright (c) 2017 The Chromium Embedded Framework Authors. All rights
// reserved. Use of this source code is governed by a BSD-style license that
// can be found in the LICENSE file.

#include "src/minimal/client_minimal.h"
#include <X11/X.h>

#include "nn.h"
#include "pair.h"
#include "src/protobuf/windowmanager.pb.h"
#include "src/shared/client_util.h"

#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_message_router.h"
#include "src/shared/client_util.h"
#include "src/shared/resource_util.h"

namespace minimal {

class MessageHandler : public CefMessageRouterBrowserSide::Handler {
 public:
  explicit MessageHandler(int sock) : ipc_sock(sock) {}

  int ipc_sock;

  bool OnQuery(CefRefPtr<CefBrowser> browser,
               CefRefPtr<CefFrame> frame,
               int64_t query_id,
               const CefString& request,
               bool persistent,
               CefRefPtr<Callback> callback) override {
    char* buf = NULL;
    int result = nn_recv(ipc_sock, &buf, NN_MSG, 0);
    if (result > 0) {
      Packet packet;
      packet.ParseFromArray(buf, result);

      // todo handle packet parsing

      nn_freemsg(buf);
    }

    const std::string& url = frame->GetURL();

    const std::string& message_name = request;

    callback->Success(request);
    return true;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MessageHandler);
};

Client::Client() {}

void Client::OnTitleChange(CefRefPtr<CefBrowser> browser,
                           const CefString& title) {
  // Call the default shared implementation.
  shared::OnTitleChange(browser, "fox-desktop");
}

bool Client::OnProcessMessageReceived(CefRefPtr<CefBrowser> browser,
                                      CefRefPtr<CefFrame> frame,
                                      CefProcessId source_process,
                                      CefRefPtr<CefProcessMessage> message) {
  CEF_REQUIRE_UI_THREAD();

  return message_router_->OnProcessMessageReceived(browser, frame,
                                                   source_process, message);
}

void Client::OnAfterCreated(CefRefPtr<CefBrowser> browser) {
  CEF_REQUIRE_UI_THREAD();

#if defined(OS_LINUX)
  ::Window window = browser->GetHost()->GetWindowHandle();

  int sock;
  if ((sock = nn_socket(AF_SP, NN_PAIR)) < 0) {
    printf("nn_socket\n");
  }
  if (nn_connect(sock, "ipc:///tmp/noko.ipc") < 0) {
    printf("nn_connect\n");
  }

  // non-blocking
  int to = 0;
  if (nn_setsockopt(sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to)) < 0) {
    printf("ipc failed\n");
  }

  Packet packet;
  packet.mutable_window_request()->set_window(window);

  size_t len = packet.ByteSizeLong();
  char* buf = (char*)malloc(len);
  packet.SerializeToArray(buf, len);

  nn_send(sock, buf, len, 0);

  free(buf);
#endif

  if (!message_router_) {
    // Create the browser-side router for query handling.
    CefMessageRouterConfig config;
    message_router_ = CefMessageRouterBrowserSide::Create(config);

    // Register handlers with the router.
    message_handler_.reset(new MessageHandler(sock));
    message_router_->AddHandler(message_handler_.get(), false);
  }

  // Call the default shared implementation.
  shared::OnAfterCreated(browser);
}

bool Client::DoClose(CefRefPtr<CefBrowser> browser) {
  // Call the default shared implementation.
  return shared::DoClose(browser);
}

void Client::OnBeforeClose(CefRefPtr<CefBrowser> browser) {
  // Call the default shared implementation.
  return shared::OnBeforeClose(browser);
}

}  // namespace minimal

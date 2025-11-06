#pragma once
#include <GL/glew.h>
#include <GL/glx.h>

// x11 has conflicts with grpc
#include <X11/X.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/Xcomposite.h>
#include <X11/extensions/Xfixes.h>
#include <X11/extensions/shape.h>
#include <nn.h>
#include <pair.h>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

#undef Status
#undef Bool
#undef True
#undef False
#undef None
#undef Always
#undef Success

#include "../protobuf/windowmanager.pb.h"

#include <sys/time.h>
#include <cstdint>
#include <optional>
#include <vector>

typedef GLXContext (*glXCreateContextAttribsARB_t)(Display*,
                                                   GLXFBConfig,
                                                   GLXContext,
                                                   bool,
                                                   const int*);

typedef void (*glXBindTexImageEXT_t)(Display*, GLXDrawable, int, const int*);
typedef void (*glXReleaseTexImageEXT_t)(Display*, GLXDrawable, int);

typedef void (*glXSwapIntervalEXT_t)(Display*, GLXDrawable, int);

struct NokoWindow {
  int exists;
  Window window;

  int visible;

  float opacity;
  int x, y;
  int width, height;

  Pixmap x_pixmap;
  GLXPixmap pixmap;

  int index_count;
  GLuint vao, vbo, ibo;
};

class NokoWindowManager {
 public:
  static std::optional<NokoWindowManager*> create();

  void run();

  void register_base_window(Window base);

  void ipc_step() {
    char* buf = NULL;
    int result = nn_recv(ipc_sock, &buf, NN_MSG, 0);
    if (result > 0) {
      Packet packet;
      packet.ParseFromArray(buf, result);

      if (packet.data_case() == Packet::kWindowRequest) {
        register_base_window(packet.window_request().window());
      }

      nn_freemsg(buf);
    }
  }

  NokoWindowManager() {
    if ((ipc_sock = nn_socket(AF_SP, NN_PAIR)) < 0) {
      printf("ipc sock failed\n");
    }
    if (nn_bind(ipc_sock, "ipc:///tmp/noko.ipc") < 0) {
      printf("ipc bind failed\n");
    }

    // non-blocking
    int to = 0;
    if (nn_setsockopt(ipc_sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to)) <
        0) {
      printf("ipc non_block failed\n");
    }
  }
  ~NokoWindowManager() {}

 private:
  int ipc_sock;

  Display* display;
  int screen;
  Window root_window;
  Window overlay_window;
  Window output_window;

  std::optional<Window> base_window;
  Atom client_list_atom;

  std::vector<Window> blacklisted_windows;
  std::unordered_map<Window, NokoWindow> windows;

  GLXFBConfig* glx_configs;
  int glx_config_count;
  GLXContext glx_context;

  GLuint shader;
  GLuint texture_uniform;

  GLuint opacity_uniform;
  GLuint depth_uniform;

  GLuint position_uniform;
  GLuint size_uniform;

  glXBindTexImageEXT_t glXBindTexImageEXT;
  glXReleaseTexImageEXT_t glXReleaseTexImageEXT;

  uint32_t screen_width;
  uint32_t screen_height;

  struct timeval previous_time;

  bool process_events();

  void render_window(unsigned window_id);

  void bind_window_texture(Window window_index);

  void unbind_window_texture(Window window_index);

  float width_dimension_to_float(int pixels);
  float height_dimension_to_float(int pixels);

  float x_coordinate_to_float(int pixels);
  float y_coordinate_to_float(int pixels);

  int float_to_width_dimension(float x);
  int float_to_height_dimension(float x);

  int float_to_x_coordinate(float x);
  int float_to_y_coordinate(float x);

  void update_client_list() {
    Window* client_list = (Window*)malloc(windows.size() * sizeof(Window));

    size_t i = 0;
    for (auto window : windows) {
      client_list[i] = window.second.window;
      i++;
    }

    XChangeProperty(display, root_window, client_list_atom, XA_WINDOW, 32,
                    PropModeReplace, (unsigned char*)client_list,
                    windows.size());
    free(client_list);
  }
  void focus_window(Window window_id) {
    Window window = windows[window_id].window;

    if (base_window.has_value()) {
      XLowerWindow(display, base_window.value());
    }

    if (base_window.has_value() && window == base_window.value())
      return;

    XSetInputFocus(display, window, RevertToParent, CurrentTime);
    XMapRaised(display, window);
  }
};

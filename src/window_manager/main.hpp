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
#include <nanomsg/nn.h>
#include <nanomsg/pair.h>
#include <sys/inotify.h>
#include <unistd.h>
#include <condition_variable>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <queue>
#include <unordered_map>

#undef Status
#undef Bool
#undef True
#undef False
#undef None
#undef Always
#undef Success

#include "../protobuf/starting_send.h"
#include "windowmanager.pb.h"

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

struct DoteWindowBorder {
  int x, y;
  int width, height;
};

struct DoteWindow {
  int exists;
  Window window;

  std::optional<std::string> name;
  WindowType type;

  std::optional<std::string> icon;  // base64 png

  int visible;

  float opacity;
  int x, y;
  int width, height;
  double depth;

  std::optional<DoteWindowBorder> border;

  Pixmap x_pixmap;
  GLXPixmap pixmap;

  int index_count;
  GLuint vao, vbo, ibo;
};

class DoteWindowManager {
 public:
  static std::optional<DoteWindowManager*> create();

  void run();

  int ipc_step() {
    constexpr size_t inotify_buf_len =
        (1024 * (sizeof(struct inotify_event) + 16));

    char inotify_buf[inotify_buf_len];
    int len, i = 0;

    len = read(inotify_fd, inotify_buf, inotify_buf_len);

    if (i < len) {
      // just grab the first event because frankly we dont gaf
      struct inotify_event* event;

      event = (struct inotify_event*)&inotify_buf[i];

      if (watched_files.find(event->wd) != watched_files.end()) {
        printf("file updated %s\n", watched_files[event->wd].c_str());
      }

      Packet packet;
      auto segment = packet.add_segments();
      // initialize
      segment->mutable_reload_reply();

      size_t len = packet.ByteSizeLong();
      char* buf = (char*)malloc(len);
      packet.SerializeToArray(buf, len);

      send_wrapper(ipc_sock, buf, len, 0);
      free(buf);
    }

    int count = 0;
    while (true) {
      Packet packet;
      {
        std::lock_guard<std::mutex> packet_gaurd(packet_lock);
        if (packet_queue.empty()) {
          break;
        }
        packet = packet_queue.front();
        packet_queue.pop();
      }

      can_receive--;

      if (can_receive == 0) {
        can_receive = START_CAN_SEND;

        Packet packet2;
        auto request = packet2.add_segments();
        auto processed = request->mutable_processed_reply();
        processed->set_can_send(can_receive);
        size_t len2 = packet2.ByteSizeLong();
        char* buf2 = (char*)malloc(len2);
        packet2.SerializeToArray(buf2, len2);

        nn_send(ipc_sock, buf2, len2, 0);
        free(buf2);
      }

      for (auto segment : packet.segments()) {
        count++;
        if (segment.data_case() == DataSegment::kProcessedRequest) {
          can_send = segment.processed_request().can_send();
        } else if (segment.data_case() == DataSegment::kWindowRequest) {
          register_base_window(segment.window_request().window());
        } else if (segment.data_case() == DataSegment::kWindowMapRequest) {
          configure_window(segment.window_map_request().window(),
                           segment.window_map_request().x(),
                           segment.window_map_request().y(),
                           segment.window_map_request().width(),
                           segment.window_map_request().height());
        } else if (segment.data_case() == DataSegment::kWindowReorderRequest) {
          printf("reordering\n");

          double window_count =
              segment.window_reorder_request().windows().size();
          double inc = (1 / window_count) * 0.8;
          double depth = 0.8;
          for (uint64_t window : segment.window_reorder_request().windows()) {
            if (windows.find(window) == windows.end()) {
              printf("Window %lu skipped\n", window);
              continue;
            }
            windows[window].depth = depth;
            printf("Setting window %lu depth to %f\n", window, depth);
            depth -= inc;
          }
        } else if (segment.data_case() == DataSegment::kWindowFocusRequest) {
          focus_window(segment.mutable_window_focus_request()->window(), false);
        } else if (segment.data_case() ==
                   DataSegment::kWindowRegisterBorderRequest) {
          register_border(
              segment.mutable_window_register_border_request()->window(),
              segment.mutable_window_register_border_request()->x(),
              segment.mutable_window_register_border_request()->y(),
              segment.mutable_window_register_border_request()->width(),
              segment.mutable_window_register_border_request()->height());
        } else if (segment.data_case() == DataSegment::kRenderRequest) {
        } else if (segment.data_case() == DataSegment::kWindowCloseRequest) {
          XDestroyWindow(display,
                         segment.mutable_window_close_request()->window());

        } else if (segment.data_case() == DataSegment::kRunProgramRequest) {
          int pid = fork();
          if (pid == 0) {
            std::vector<char*> args;
            for (const auto& cmd : segment.run_program_request().command()) {
              args.push_back((char*)(cmd.c_str()));
            }
            args.push_back(nullptr);

            execvp(args[0], args.data());
            perror("execvp failed");
            exit(1);
          }
        } else if (segment.data_case() == DataSegment::kFileRegisterRequest) {
          watched_files[inotify_add_watch(
              inotify_fd,
              segment.mutable_file_register_request()->file_path().c_str(),
              IN_MODIFY)] =
              segment.mutable_file_register_request()->file_path();
        } else if (segment.data_case() == DataSegment::kBrowserStartRequest) {
          printf("resending all windows\n");
          Packet packet;
          for (auto window : windows) {
            if (std::find(blacklisted_windows.begin(),
                          blacklisted_windows.end(),
                          window.first) != blacklisted_windows.end())
              continue;
            if (base_window.value() == window.first)
              continue;

            printf("%lu\n", window.first);

            auto segment = packet.add_segments();
            auto reply = segment->mutable_window_map_reply();
            reply->set_window(window.second.window);
            reply->set_visible(window.second.visible);
            reply->set_x(window.second.x);
            reply->set_y(window.second.y);
            if (window.second.name.has_value()) {
              reply->set_name(window.second.name.value());
            }
            reply->set_width(window.second.width);
            reply->set_height(window.second.height);
          }
          size_t len = packet.ByteSizeLong();
          char* buf = (char*)malloc(len);
          packet.SerializeToArray(buf, len);

          send_wrapper(ipc_sock, buf, len, 0);

          free(buf);
        }
      }
    }
    return count;
  }

  DoteWindowManager() {
    if ((ipc_sock = nn_socket(AF_SP, NN_PAIR)) < 0) {
      printf("ipc sock failed\n");
    }
    if (nn_bind(ipc_sock, "ipc:///tmp/dote.ipc") < 0) {
      printf("ipc bind failed\n");
    }

    // non-blocking
    int to = 0;
    if (nn_setsockopt(ipc_sock, NN_SOL_SOCKET, NN_RCVTIMEO, &to, sizeof(to)) <
        0) {
      printf("ipc non_block failed\n");
    }

    inotify_fd = inotify_init1(IN_NONBLOCK);

    nanomsg_thread = std::thread([this]() { nanomsg_watch(); });
  }
  ~DoteWindowManager() {
    should_stop = true;
    if (nanomsg_thread.joinable()) {
      nanomsg_thread.join();
    }
    close(inotify_fd);
    nn_close(ipc_sock);
  }

 private:
  std::queue<Packet> packet_queue;
  std::mutex packet_lock;
  std::thread nanomsg_thread;
  std::atomic<bool> should_stop{false};

  void nanomsg_watch() {
    char* buf = NULL;
    int result;

    while (!should_stop) {
      result = nn_recv(ipc_sock, &buf, NN_MSG, NN_DONTWAIT);

      if (result > 0) {
        Packet packet;
        packet.ParseFromArray(buf, result);

        {
          std::lock_guard<std::mutex> packet_guard(packet_lock);
          packet_queue.push(packet);
        }

        nn_freemsg(buf);
      } else if (result < 0 && nn_errno() == EAGAIN) {
        // no data available, sleep briefly to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      } else if (result < 0) {
        fprintf(stderr, "nn_recv error: %s\n", nn_strerror(nn_errno()));
      }
    }
  }

  uint64_t can_send = START_CAN_SEND;
  uint64_t can_receive = START_CAN_SEND;
  void send_wrapper(int s, const void* buf, size_t len, int flags) {
    std::lock_guard<std::mutex> packet_gaurd(packet_lock);
    if (can_send == 0)
      return;
    can_send--;
    nn_send(s, buf, len, flags);
  }

  int ipc_sock;

  std::unordered_map<int, std::string> watched_files = {};
  int inotify_fd;

  Display* display;
  int screen;
  Window root_window;
  Window overlay_window;
  Window output_window;

  int xi_opcode;

  std::optional<Window> base_window;
  Atom client_list_atom;

  std::vector<Window> blacklisted_windows;
  std::unordered_map<Window, DoteWindow> windows;
  std::unordered_map<Window, Window> border_window;
  std::vector<Window> render_order;

  GLXFBConfig* glx_configs;
  int glx_config_count;
  GLXContext glx_context;

  GLuint shader;
  GLuint texture_uniform;

  GLuint opacity_uniform;
  GLuint depth_uniform;

  GLuint position_uniform;
  GLuint size_uniform;

  GLuint cropped_position_uniform;
  GLuint cropped_size_uniform;

  glXBindTexImageEXT_t glXBindTexImageEXT;
  glXReleaseTexImageEXT_t glXReleaseTexImageEXT;

  uint32_t screen_width;
  uint32_t screen_height;

  struct timeval previous_time;

  bool process_events();

  void register_base_window(Window base);
  void register_border(Window window,
                       int32_t x,
                       int32_t y,
                       int32_t width,
                       int32_t height);
  void configure_window(Window window,
                        uint32_t x,
                        uint32_t y,
                        uint32_t width,
                        uint32_t height);

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

  void update_client_list();

  std::optional<Window> focused_window;
  void focus_window(Window window_id, bool send_event);
};

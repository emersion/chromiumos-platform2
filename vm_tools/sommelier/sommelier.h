// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_SOMMELIER_H_
#define VM_TOOLS_SOMMELIER_SOMMELIER_H_

#include <sys/types.h>
#include <wayland-server.h>
#include <wayland-util.h>
#include <xcb/xcb.h>

#define SOMMELIER_VERSION "0.20"

struct sl_global;
struct sl_compositor;
struct sl_shm;
struct sl_shell;
struct sl_data_device_manager;
struct sl_data_offer;
struct sl_data_source;
struct sl_xdg_shell;
struct sl_subcompositor;
struct sl_aura_shell;
struct sl_viewporter;
struct sl_linux_dmabuf;
struct sl_keyboard_extension;
struct sl_window;
struct zaura_shell;

enum {
  ATOM_WM_S0,
  ATOM_WM_PROTOCOLS,
  ATOM_WM_STATE,
  ATOM_WM_DELETE_WINDOW,
  ATOM_WM_TAKE_FOCUS,
  ATOM_WM_CLIENT_LEADER,
  ATOM_WL_SURFACE_ID,
  ATOM_UTF8_STRING,
  ATOM_MOTIF_WM_HINTS,
  ATOM_NET_FRAME_EXTENTS,
  ATOM_NET_STARTUP_ID,
  ATOM_NET_SUPPORTING_WM_CHECK,
  ATOM_NET_WM_NAME,
  ATOM_NET_WM_MOVERESIZE,
  ATOM_NET_WM_STATE,
  ATOM_NET_WM_STATE_FULLSCREEN,
  ATOM_NET_WM_STATE_MAXIMIZED_VERT,
  ATOM_NET_WM_STATE_MAXIMIZED_HORZ,
  ATOM_CLIPBOARD,
  ATOM_CLIPBOARD_MANAGER,
  ATOM_TARGETS,
  ATOM_TIMESTAMP,
  ATOM_TEXT,
  ATOM_INCR,
  ATOM_WL_SELECTION,
  ATOM_LAST = ATOM_WL_SELECTION,
};

struct sl_context {
  char** runprog;
  struct wl_display* display;
  struct wl_display* host_display;
  struct wl_client* client;
  struct sl_compositor* compositor;
  struct sl_subcompositor* subcompositor;
  struct sl_shm* shm;
  struct sl_shell* shell;
  struct sl_data_device_manager* data_device_manager;
  struct sl_xdg_shell* xdg_shell;
  struct sl_aura_shell* aura_shell;
  struct sl_viewporter* viewporter;
  struct sl_linux_dmabuf* linux_dmabuf;
  struct sl_keyboard_extension* keyboard_extension;
  struct wl_list outputs;
  struct wl_list seats;
  struct wl_event_source* display_event_source;
  struct wl_event_source* display_ready_event_source;
  struct wl_event_source* sigchld_event_source;
  struct wl_array dpi;
  int shm_driver;
  int data_driver;
  int wm_fd;
  int virtwl_fd;
  int virtwl_ctx_fd;
  int virtwl_socket_fd;
  struct wl_event_source* virtwl_ctx_event_source;
  struct wl_event_source* virtwl_socket_event_source;
  const char* drm_device;
  struct gbm_device* gbm;
  int xwayland;
  pid_t xwayland_pid;
  pid_t child_pid;
  pid_t peer_pid;
  struct xkb_context* xkb_context;
  struct wl_list accelerators;
  struct wl_list registries;
  struct wl_list globals;
  struct wl_list host_outputs;
  int next_global_id;
  xcb_connection_t* connection;
  struct wl_event_source* connection_event_source;
  const xcb_query_extension_reply_t* xfixes_extension;
  xcb_screen_t* screen;
  xcb_window_t window;
  struct wl_list windows, unpaired_windows;
  struct sl_window* host_focus_window;
  int needs_set_input_focus;
  double desired_scale;
  double scale;
  const char* application_id;
  int exit_with_child;
  const char* sd_notify;
  int clipboard_manager;
  uint32_t frame_color;
  int has_frame_color;
  struct sl_host_seat* default_seat;
  xcb_window_t selection_window;
  xcb_window_t selection_owner;
  int selection_incremental_transfer;
  xcb_selection_request_event_t selection_request;
  xcb_timestamp_t selection_timestamp;
  struct wl_data_device* selection_data_device;
  struct sl_data_offer* selection_data_offer;
  struct sl_data_source* selection_data_source;
  int selection_data_source_send_fd;
  struct wl_event_source* selection_send_event_source;
  xcb_get_property_reply_t* selection_property_reply;
  int selection_property_offset;
  struct wl_event_source* selection_event_source;
  struct wl_array selection_data;
  int selection_data_offer_receive_fd;
  int selection_data_ack_pending;
  union {
    const char* name;
    xcb_intern_atom_cookie_t cookie;
    xcb_atom_t value;
  } atoms[ATOM_LAST + 1];
  xcb_visualid_t visual_ids[256];
  xcb_colormap_t colormaps[256];
};

struct sl_viewport {
  struct wl_list link;
  wl_fixed_t src_x;
  wl_fixed_t src_y;
  wl_fixed_t src_width;
  wl_fixed_t src_height;
  int32_t dst_width;
  int32_t dst_height;
};

struct sl_host_surface {
  struct sl_context* ctx;
  struct wl_resource* resource;
  struct wl_surface* proxy;
  struct wp_viewport* viewport;
  uint32_t contents_width;
  uint32_t contents_height;
  int32_t contents_scale;
  struct wl_list contents_viewport;
  struct sl_mmap* contents_shm_mmap;
  int has_role;
  int has_output;
  uint32_t last_event_serial;
  struct sl_output_buffer* current_buffer;
  struct wl_list released_buffers;
  struct wl_list busy_buffers;
};

struct sl_viewporter {
  struct sl_context* ctx;
  uint32_t id;
  struct sl_global* host_viewporter_global;
  struct wp_viewporter* internal;
};

struct sl_aura_shell {
  struct sl_context* ctx;
  uint32_t id;
  uint32_t version;
  struct sl_global* host_gtk_shell_global;
  struct zaura_shell* internal;
};

struct sl_global* sl_global_create(struct sl_context* ctx,
                                   const struct wl_interface* interface,
                                   int version,
                                   void* data,
                                   wl_global_bind_func_t bind);

struct sl_global* sl_viewporter_global_create(struct sl_context* ctx);

struct sl_global* sl_gtk_shell_global_create(struct sl_context* ctx);

#endif  // VM_TOOLS_SOMMELIER_SOMMELIER_H_

#include <cstdio>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <algorithm>
#include <charconv>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#include <linux/input-event-codes.h>

#include "Layout.hpp"

extern "C" {
#include <wayland-server-core.h>
#include <wlr/backend.h>
#include <wlr/backend/wayland.h>
#include <wlr/render/allocator.h>
#include <wlr/render/vulkan.h>
#include <wlr/render/wlr_renderer.h>
#include <wlr/types/wlr_compositor.h>
#include <wlr/types/wlr_cursor.h>
#include <wlr/types/wlr_data_device.h>
#include <wlr/types/wlr_input_device.h>
#include <wlr/types/wlr_keyboard.h>
#include <wlr/types/wlr_output.h>
#include <wlr/types/wlr_output_layout.h>
#include <wlr/types/wlr_pointer.h>
#include <wlr/types/wlr_scene.h>
#include <wlr/types/wlr_seat.h>
#include <wlr/types/wlr_subcompositor.h>
#include <wlr/types/wlr_xcursor_manager.h>
#include <wlr/types/wlr_xdg_shell.h>
#include <wlr/util/edges.h>
#include <wlr/util/log.h>
#include <xkbcommon/xkbcommon.h>
}

namespace {

struct Hook {
    wl_listener listener{}; // Must be first: callbacks recover Hook from wl_listener.
    void *owner = nullptr;

    void add(wl_signal *signal, void *object, void (*notify)(wl_listener *, void *))
    {
        owner = object;
        listener.notify = notify;
        wl_signal_add(signal, &listener);
    }

    void remove()
    {
        if (listener.link.next) {
            wl_list_remove(&listener.link);
            listener.link = {};
        }
    }

    template<class T> static T &from(wl_listener *listener)
    {
        return *static_cast<T *>(reinterpret_cast<Hook *>(listener)->owner);
    }
};

struct Server;

struct Output {
    Server &server;
    wlr_output *output;
    wlr_scene_output *sceneOutput;
    bool valid = false;
    int width = 0, height = 0;
    Hook frame, requestState, commit, destroy;

    Output(Server &server, wlr_output *output);
    ~Output();
};

struct Toplevel {
    Server &server;
    wlr_xdg_toplevel *xdg;
    wlr_scene_tree *tree;
    Hook commit, map, unmap, destroy, maximize, fullscreen;
    bool mapped = false;
    unsigned id;
    flowstate::Rect tile{};
    bool hasTile = false;
    flowstate::Rect geometry{};

    Toplevel(Server &server, wlr_xdg_toplevel *xdg);
    ~Toplevel();
};

struct Keyboard {
    Server &server;
    wlr_keyboard *keyboard;
    Hook key, modifiers, destroy;
    std::unordered_set<uint32_t> interceptedKeys;

    Keyboard(Server &server, wlr_input_device *device);
    ~Keyboard();
};

struct Server {
    wl_display *display = nullptr;
    wlr_backend *backend = nullptr;
    wlr_renderer *renderer = nullptr;
    wlr_allocator *allocator = nullptr;
    wlr_scene *scene = nullptr;
    wlr_scene_output_layout *sceneLayout = nullptr;
    wlr_output_layout *layout = nullptr;
    wlr_cursor *cursor = nullptr;
    wlr_xcursor_manager *cursorManager = nullptr;
    wlr_seat *seat = nullptr;
    wlr_scene_rect *background = nullptr;
    Output *activeOutput = nullptr;
    Toplevel *focused = nullptr;
    std::vector<Toplevel *> mapped;
    unsigned nextWindowId = 1;
    int gap = 12;
    bool outputFailed = false;
    bool clientFrameSeen = false;
    Hook newOutput, newInput, newToplevel;
    Hook motion, motionAbsolute, button, axis, pointerFrame, requestCursor;

    ~Server();
    void start(const char *socket);
    void pointerMotion(uint32_t time);
    void relayout();
    void focus(Toplevel *window);
    void cycleFocus(int direction);
    Toplevel *windowAtCursor() const;
};

Output::Output(Server &server, wlr_output *output)
    : server(server), output(output), sceneOutput(nullptr)
{
    if (!wlr_output_init_render(output, server.allocator, server.renderer))
        return;

    wlr_output_state state;
    wlr_output_state_init(&state);
    wlr_output_state_set_enabled(&state, true);
    wlr_output_state_set_custom_mode(&state, 960, 600, 0);
    const bool configured = wlr_output_commit_state(output, &state);
    wlr_output_state_finish(&state);
    if (!configured)
        return;

    wlr_wl_output_set_title(output, "Flowstate — nested Vulkan compositor");
    wlr_wl_output_set_app_id(output, "flowstate-nested");
    auto *layoutOutput = wlr_output_layout_add_auto(server.layout, output);
    sceneOutput = wlr_scene_output_create(server.scene, output);
    wlr_scene_output_layout_add_output(server.sceneLayout, layoutOutput, sceneOutput);
    server.activeOutput = this;
    wlr_output_effective_resolution(output, &width, &height);
    server.relayout();

    frame.add(&output->events.frame, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Output>(listener);
        const bool committed = wlr_scene_output_commit(self.sceneOutput, nullptr);
        if (!committed)
            std::fputs("Flowstate: Vulkan frame commit failed\n", stderr);
        if (committed && !self.server.mapped.empty() && !self.server.clientFrameSeen) {
            self.server.clientFrameSeen = true;
            std::fputs("Flowstate: Vulkan frame with client committed\n", stderr);
        }
        timespec now{};
        clock_gettime(CLOCK_MONOTONIC, &now);
        wlr_scene_output_send_frame_done(self.sceneOutput, &now);
    });
    requestState.add(&output->events.request_state, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Output>(listener);
        auto *event = static_cast<wlr_output_event_request_state *>(data);
        wlr_output_commit_state(self.output, event->state);
    });
    commit.add(&output->events.commit, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Output>(listener);
        int width, height;
        wlr_output_effective_resolution(self.output, &width, &height);
        if (width != self.width || height != self.height) {
            self.width = width;
            self.height = height;
            self.server.relayout();
            std::fprintf(stderr, "Flowstate: output resized %dx%d\n", width, height);
        }
    });
    destroy.add(&output->events.destroy, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Output>(listener);
        self.server.activeOutput = nullptr;
        wl_display_terminate(self.server.display);
        delete &self;
    });
    valid = true;
}

Output::~Output()
{
    frame.remove();
    requestState.remove();
    commit.remove();
    destroy.remove();
}

Toplevel::Toplevel(Server &server, wlr_xdg_toplevel *xdg)
    : server(server), xdg(xdg), tree(wlr_scene_xdg_surface_create(&server.scene->tree, xdg->base)),
      id(server.nextWindowId++)
{
    // The scene helper's origin is the xdg window geometry, including any
    // client-side decoration offset. Tile coordinates can be used directly.
    wlr_scene_node_set_enabled(&tree->node, false);
    commit.add(&xdg->base->surface->events.commit, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Toplevel>(listener);
        if (self.xdg->base->initial_commit && self.server.activeOutput) {
            int width, height;
            wlr_output_effective_resolution(self.server.activeOutput->output, &width, &height);
            auto tiles = flowstate::tile(width, height, self.server.mapped.size() + 1,
                                         self.server.gap);
            const auto &rect = tiles.back();
            wlr_xdg_toplevel_set_size(self.xdg, rect.width, rect.height);
            wlr_xdg_toplevel_set_tiled(self.xdg, WLR_EDGE_TOP | WLR_EDGE_BOTTOM |
                                                WLR_EDGE_LEFT | WLR_EDGE_RIGHT);
        }
        if (self.mapped) {
            const auto &box = self.xdg->base->geometry;
            const flowstate::Rect actual{box.x, box.y, box.width, box.height};
            if (actual != self.geometry) {
                self.geometry = actual;
                std::fprintf(stderr,
                             "Flowstate: geometry id=%u offset=%d,%d width=%d height=%d\n",
                             self.id, box.x, box.y, box.width, box.height);
            }
        }
    });
    map.add(&xdg->base->surface->events.map, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Toplevel>(listener);
        self.mapped = true;
        self.server.mapped.push_back(&self);
        self.server.clientFrameSeen = false;
        wlr_scene_node_set_enabled(&self.tree->node, true);
        self.server.relayout();
        self.server.focus(&self);
        std::fprintf(stderr, "Flowstate: Wayland window mapped id=%u app=%s\n", self.id,
                     self.xdg->app_id ? self.xdg->app_id : "unknown");
    });
    unmap.add(&xdg->base->surface->events.unmap, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Toplevel>(listener);
        self.mapped = false;
        wlr_scene_node_set_enabled(&self.tree->node, false);
        auto &windows = self.server.mapped;
        windows.erase(std::remove(windows.begin(), windows.end(), &self), windows.end());
        if (self.server.focused == &self)
            self.server.focus(windows.empty() ? nullptr : windows.back());
        self.server.relayout();
        std::fprintf(stderr, "Flowstate: Wayland window unmapped id=%u\n", self.id);
    });
    maximize.add(&xdg->events.request_maximize, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Toplevel>(listener);
        wlr_xdg_toplevel_set_maximized(self.xdg, false);
        if (self.xdg->base->initialized)
            wlr_xdg_surface_schedule_configure(self.xdg->base);
    });
    fullscreen.add(&xdg->events.request_fullscreen, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Toplevel>(listener);
        wlr_xdg_toplevel_set_fullscreen(self.xdg, false);
        if (self.xdg->base->initialized)
            wlr_xdg_surface_schedule_configure(self.xdg->base);
    });
    destroy.add(&xdg->events.destroy, this, [](wl_listener *listener, void *) {
        delete &Hook::from<Toplevel>(listener);
    });
}

Toplevel::~Toplevel()
{
    commit.remove();
    map.remove();
    unmap.remove();
    maximize.remove();
    fullscreen.remove();
    destroy.remove();
    if (mapped) {
        auto &windows = server.mapped;
        windows.erase(std::remove(windows.begin(), windows.end(), this), windows.end());
        if (server.focused == this)
            server.focus(windows.empty() ? nullptr : windows.back());
        server.relayout();
    }
}

Keyboard::Keyboard(Server &server, wlr_input_device *device)
    : server(server), keyboard(wlr_keyboard_from_input_device(device))
{
    auto *context = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    auto *keymap = xkb_keymap_new_from_names(context, nullptr, XKB_KEYMAP_COMPILE_NO_FLAGS);
    wlr_keyboard_set_keymap(keyboard, keymap);
    xkb_keymap_unref(keymap);
    xkb_context_unref(context);
    wlr_keyboard_set_repeat_info(keyboard, 25, 600);
    wlr_seat_set_keyboard(server.seat, keyboard);
    wlr_seat_set_capabilities(server.seat,
                              WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (server.focused) {
        wlr_seat_keyboard_notify_enter(server.seat, server.focused->xdg->base->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }

    key.add(&keyboard->events.key, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Keyboard>(listener);
        auto *event = static_cast<wlr_keyboard_key_event *>(data);
        if (event->state == WL_KEYBOARD_KEY_STATE_RELEASED &&
            self.interceptedKeys.erase(event->keycode))
            return;
        const auto modifiers = wlr_keyboard_get_modifiers(self.keyboard);
        if (event->state == WL_KEYBOARD_KEY_STATE_PRESSED &&
            (modifiers & WLR_MODIFIER_ALT)) {
            const xkb_keysym_t *symbols = nullptr;
            const int count = xkb_state_key_get_syms(self.keyboard->xkb_state,
                                                       event->keycode + 8, &symbols);
            for (int i = 0; i < count; ++i) {
                if (symbols[i] == XKB_KEY_Tab || symbols[i] == XKB_KEY_ISO_Left_Tab) {
                    self.server.cycleFocus((modifiers & WLR_MODIFIER_SHIFT) ? -1 : 1);
                    self.interceptedKeys.insert(event->keycode);
                    return;
                }
                if (modifiers & WLR_MODIFIER_CTRL) {
                    if (symbols[i] == XKB_KEY_j || symbols[i] == XKB_KEY_J) {
                        self.server.cycleFocus(1);
                        self.interceptedKeys.insert(event->keycode);
                        return;
                    }
                    if (symbols[i] == XKB_KEY_k || symbols[i] == XKB_KEY_K) {
                        self.server.cycleFocus(-1);
                        self.interceptedKeys.insert(event->keycode);
                        return;
                    }
                }
            }
        }
        wlr_seat_set_keyboard(self.server.seat, self.keyboard);
        wlr_seat_keyboard_notify_key(self.server.seat, event->time_msec,
                                     event->keycode, event->state);
    });
    modifiers.add(&keyboard->events.modifiers, this, [](wl_listener *listener, void *) {
        auto &self = Hook::from<Keyboard>(listener);
        wlr_seat_set_keyboard(self.server.seat, self.keyboard);
        wlr_seat_keyboard_notify_modifiers(self.server.seat, &self.keyboard->modifiers);
    });
    destroy.add(&device->events.destroy, this, [](wl_listener *listener, void *) {
        delete &Hook::from<Keyboard>(listener);
    });
}

Keyboard::~Keyboard()
{
    key.remove();
    modifiers.remove();
    destroy.remove();
}

void Server::relayout()
{
    if (!activeOutput)
        return;
    const int width = activeOutput->width;
    const int height = activeOutput->height;
    if (background)
        wlr_scene_rect_set_size(background, width, height);
    const auto tiles = flowstate::tile(width, height, mapped.size(), gap);
    for (std::size_t i = 0; i < mapped.size(); ++i) {
        auto &window = *mapped[i];
        const auto &rect = tiles[i];
        if (window.hasTile && window.tile == rect)
            continue;
        window.tile = rect;
        window.hasTile = true;
        wlr_scene_node_set_position(&window.tree->node, rect.x, rect.y);
        wlr_xdg_toplevel_set_size(window.xdg, rect.width, rect.height);
        std::fprintf(stderr, "Flowstate: tile id=%u x=%d y=%d width=%d height=%d\n",
                     window.id, rect.x, rect.y, rect.width, rect.height);
    }
}

void Server::focus(Toplevel *window)
{
    if (window == focused)
        return;
    if (focused && focused->mapped)
        wlr_xdg_toplevel_set_activated(focused->xdg, false);
    focused = window;
    if (!window) {
        wlr_seat_keyboard_clear_focus(seat);
        std::fputs("Flowstate: focus cleared\n", stderr);
        return;
    }
    wlr_scene_node_raise_to_top(&window->tree->node);
    wlr_xdg_toplevel_set_activated(window->xdg, true);
    auto *keyboard = wlr_seat_get_keyboard(seat);
    if (keyboard) {
        wlr_seat_keyboard_notify_enter(seat, window->xdg->base->surface,
                                       keyboard->keycodes, keyboard->num_keycodes,
                                       &keyboard->modifiers);
    }
    std::fprintf(stderr, "Flowstate: focus id=%u keyboard=%d\n", window->id,
                 seat->keyboard_state.focused_surface == window->xdg->base->surface);
}

void Server::cycleFocus(int direction)
{
    if (mapped.empty())
        return;
    auto it = std::find(mapped.begin(), mapped.end(), focused);
    const auto index = it == mapped.end() ? 0 : static_cast<std::size_t>(it - mapped.begin());
    const auto next = (index + mapped.size() + direction) % mapped.size();
    focus(mapped[next]);
}

Toplevel *Server::windowAtCursor() const
{
    double sx = 0, sy = 0;
    auto *node = wlr_scene_node_at(&scene->tree.node, cursor->x, cursor->y, &sx, &sy);
    while (node && node != &scene->tree.node) {
        for (auto *window : mapped) {
            if (node == &window->tree->node)
                return window;
        }
        node = node->parent ? &node->parent->node : nullptr;
    }
    return nullptr;
}

void Server::pointerMotion(uint32_t time)
{
    double sx = 0, sy = 0;
    auto *node = wlr_scene_node_at(&scene->tree.node, cursor->x, cursor->y, &sx, &sy);
    auto *buffer = node && node->type == WLR_SCENE_NODE_BUFFER
        ? wlr_scene_buffer_from_node(node) : nullptr;
    auto *surface = buffer ? wlr_scene_surface_try_from_buffer(buffer) : nullptr;
    if (surface) {
        wlr_seat_pointer_notify_enter(seat, surface->surface, sx, sy);
        wlr_seat_pointer_notify_motion(seat, time, sx, sy);
    } else {
        wlr_seat_pointer_clear_focus(seat);
        wlr_cursor_set_xcursor(cursor, cursorManager, "default");
    }
}

void Server::start(const char *socket)
{
    display = wl_display_create();
    if (!display)
        throw std::runtime_error("Could not create Wayland display");
    auto *eventLoop = wl_display_get_event_loop(display);
    auto terminate = [](int, void *data) {
        wl_display_terminate(static_cast<wl_display *>(data));
        return 0;
    };
    wl_event_loop_add_signal(eventLoop, SIGINT, terminate, display);
    wl_event_loop_add_signal(eventLoop, SIGTERM, terminate, display);
    wl_event_loop_add_signal(eventLoop, SIGUSR1, [](int, void *data) {
        auto &self = *static_cast<Server *>(data);
        if (!self.activeOutput)
            return 0;
        auto *output = self.activeOutput;
        const bool compact = output->width != 800 || output->height != 500;
        wlr_output_state state;
        wlr_output_state_init(&state);
        wlr_output_state_set_custom_mode(&state, compact ? 800 : 960,
                                          compact ? 500 : 600, 0);
        const bool resized = wlr_output_commit_state(output->output, &state);
        wlr_output_state_finish(&state);
        if (!resized)
            std::fputs("Flowstate: output resize failed\n", stderr);
        return 0;
    }, this);
    wl_event_loop_add_signal(eventLoop, SIGUSR2, [](int, void *data) {
        static_cast<Server *>(data)->cycleFocus(1);
        return 0;
    }, this);
    backend = wlr_wl_backend_create(eventLoop, nullptr);
    if (!backend)
        throw std::runtime_error("Could not connect nested backend to parent Wayland display");

    setenv("WLR_RENDERER", "vulkan", 1);
    renderer = wlr_renderer_autocreate(backend);
    if (!renderer || !wlr_renderer_is_vk(renderer))
        throw std::runtime_error("Vulkan renderer unavailable; Flowstate will not use a fallback");
    if (!wlr_renderer_init_wl_display(renderer, display))
        throw std::runtime_error("Could not advertise Vulkan-compatible Wayland buffers");
    allocator = wlr_allocator_autocreate(backend, renderer);
    if (!allocator)
        throw std::runtime_error("Could not create Vulkan output allocator");

    wlr_compositor_create(display, 5, renderer);
    wlr_subcompositor_create(display);
    wlr_data_device_manager_create(display);
    layout = wlr_output_layout_create(display);
    scene = wlr_scene_create();
    sceneLayout = wlr_scene_attach_output_layout(scene, layout);
    const float background[4] = {0.025f, 0.04f, 0.07f, 1.0f};
    this->background = wlr_scene_rect_create(&scene->tree, 1, 1, background);
    auto *shell = wlr_xdg_shell_create(display, 3);
    newToplevel.add(&shell->events.new_toplevel, this, [](wl_listener *listener, void *data) {
        new Toplevel(Hook::from<Server>(listener), static_cast<wlr_xdg_toplevel *>(data));
    });

    seat = wlr_seat_create(display, "seat0");
    wlr_seat_set_capabilities(seat, WL_SEAT_CAPABILITY_POINTER);
    cursor = wlr_cursor_create();
    wlr_cursor_attach_output_layout(cursor, layout);
    cursorManager = wlr_xcursor_manager_create(nullptr, 24);
    newOutput.add(&backend->events.new_output, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *output = new Output(self, static_cast<wlr_output *>(data));
        if (!output->valid) {
            self.outputFailed = true;
            delete output;
        }
    });
    newInput.add(&backend->events.new_input, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *device = static_cast<wlr_input_device *>(data);
        if (device->type == WLR_INPUT_DEVICE_KEYBOARD)
            new Keyboard(self, device);
        else if (device->type == WLR_INPUT_DEVICE_POINTER)
            wlr_cursor_attach_input_device(self.cursor, device);
    });
    motion.add(&cursor->events.motion, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *event = static_cast<wlr_pointer_motion_event *>(data);
        wlr_cursor_move(self.cursor, &event->pointer->base, event->delta_x, event->delta_y);
        self.pointerMotion(event->time_msec);
    });
    motionAbsolute.add(&cursor->events.motion_absolute, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *event = static_cast<wlr_pointer_motion_absolute_event *>(data);
        wlr_cursor_warp_absolute(self.cursor, &event->pointer->base, event->x, event->y);
        self.pointerMotion(event->time_msec);
    });
    button.add(&cursor->events.button, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *event = static_cast<wlr_pointer_button_event *>(data);
        if (event->button == BTN_LEFT && event->state == WL_POINTER_BUTTON_STATE_PRESSED)
            self.focus(self.windowAtCursor());
        wlr_seat_pointer_notify_button(self.seat, event->time_msec, event->button, event->state);
    });
    axis.add(&cursor->events.axis, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *event = static_cast<wlr_pointer_axis_event *>(data);
        wlr_seat_pointer_notify_axis(self.seat, event->time_msec, event->orientation,
                                     event->delta, event->delta_discrete,
                                     event->source, event->relative_direction);
    });
    pointerFrame.add(&cursor->events.frame, this, [](wl_listener *listener, void *) {
        wlr_seat_pointer_notify_frame(Hook::from<Server>(listener).seat);
    });
    requestCursor.add(&seat->events.request_set_cursor, this, [](wl_listener *listener, void *data) {
        auto &self = Hook::from<Server>(listener);
        auto *event = static_cast<wlr_seat_pointer_request_set_cursor_event *>(data);
        if (self.seat->pointer_state.focused_client == event->seat_client)
            wlr_cursor_set_surface(self.cursor, event->surface, event->hotspot_x, event->hotspot_y);
    });

    if (wl_display_add_socket(display, socket) < 0)
        throw std::runtime_error("Could not create isolated Wayland socket");
    if (!wlr_backend_start(backend))
        throw std::runtime_error("Could not start nested Vulkan output");
    if (!wlr_wl_output_create(backend))
        throw std::runtime_error("Could not create parent Wayland window");
    if (outputFailed)
        throw std::runtime_error("Vulkan output allocation failed");
    std::fprintf(stderr, "Flowstate nested compositor ready: WAYLAND_DISPLAY=%s\n", socket);
    wl_display_run(display);
}

Server::~Server()
{
    if (display)
        wl_display_destroy_clients(display);
    if (activeOutput && activeOutput->sceneOutput)
        wlr_scene_output_commit(activeOutput->sceneOutput, nullptr);
    newOutput.remove(); newInput.remove(); newToplevel.remove();
    motion.remove(); motionAbsolute.remove(); button.remove(); axis.remove();
    pointerFrame.remove(); requestCursor.remove();
    if (scene)
        wlr_scene_node_destroy(&scene->tree.node);
    if (cursorManager)
        wlr_xcursor_manager_destroy(cursorManager);
    if (cursor)
        wlr_cursor_destroy(cursor);
    if (backend)
        wlr_backend_destroy(backend);
    if (allocator)
        wlr_allocator_destroy(allocator);
    if (renderer)
        wlr_renderer_destroy(renderer);
    if (layout)
        wlr_output_layout_destroy(layout);
    if (display)
        wl_display_destroy(display);
}

} // namespace

int main(int argc, char **argv)
{
    const std::string socket = argc > 1 ? argv[1] : "flowstate-0";
    wlr_log_init(WLR_INFO, nullptr);
    try {
        Server server;
        if (argc > 2) {
            if (argc != 4 || std::string(argv[2]) != "--gap")
                throw std::runtime_error("Usage: flowstate-nested [socket] [--gap 0..128]");
            const std::string value = argv[3];
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(),
                                                      server.gap);
            if (error != std::errc{} || end != value.data() + value.size() ||
                server.gap < 0 || server.gap > 128)
                throw std::runtime_error("Gap must be an integer from 0 to 128");
        }
        server.start(socket.c_str());
    } catch (const std::exception &error) {
        std::fprintf(stderr, "Flowstate nested compositor: %s\n", error.what());
        return 1;
    }
}

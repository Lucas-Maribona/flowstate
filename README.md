# Flowstate — nested Vulkan tiling prototype

The repository has two small executables. `flowstate` renders a Qt Quick QML
panel with Vulkan. `flowstate-nested` creates a Wayland compositor inside one
window on the current desktop and composes real client surfaces with wlroots'
Vulkan renderer. `make run` opens the QML panel and two terminals on that
compositor's isolated socket, so all three appear in its tiled output. Both
executables fail when their required Vulkan renderer is unavailable. This is a
working Vulkan-only composition path, but the QML panel is currently a Wayland
client, not an in-process compositor overlay.

## Arch Linux prerequisites

```sh
sudo pacman -Syu --needed gcc cmake ninja qt6-base qt6-declarative \
    qt6-wayland vulkan-icd-loader vulkan-intel vulkan-tools \
    vulkan-validation-layers vulkan-headers wayland wayland-protocols \
    wlroots0.20 pkgconf python foot util-linux
```

`qt6-wayland` provides the Qt Wayland client plugin used by the nested test.
On this machine, `.flowstate-local.mk` points `make` to a temporary Qt 6.12
package extraction in the user cache because system Qt requires a privileged
install. Remove that ignored local file after installing the packages above.

## Build, test and run inside a Wayland desktop

```sh
vulkaninfo --summary
make
make test
make test-all
make run
FLOWSTATE_GAP=20 make run
```

`make` uses CMake and Ninja for incremental builds with `RelWithDebInfo`.
`make debug` selects an unoptimized build with symbols. `make help` lists the
targets; `make clean` removes the generated `build` directory. Build failures,
test failures, and unexpected compositor exits propagate a nonzero status.

`make run` opens one nested compositor window under the current Wayland desktop
(including Hyprland), with the QML panel and two `foot` terminals tiled inside
it. It creates a unique socket and prints `WAYLAND_DISPLAY=<socket>` so another
terminal can launch more clients, for example `WAYLAND_DISPLAY=<socket> foot`.
Only the child processes and that socket are stopped by Ctrl+C. `make nested`
keeps the earlier three-terminal demo; closing all three ends it. The compositor
uses the parent Wayland display only for its own output window.

`make test` runs Qt Test layout/invalid-input cases, Vulkan Qt tests, the nested
client smoke test, and the real three-window tests. The mixed test saves a PNG
from Qt's Vulkan-rendered QML window and checks exact panel/background pixels;
it also verifies tiling with two terminals, keyboard delivery to the Qt
window, and reflow after a terminal is killed. The three-terminal test checks
configure sizes, decoration offsets, hover/keyboard focus and activation,
runtime output resize, retiling after close, and clean exit. One terminal asks
to maximize; Flowstate keeps it tiled. `make test-all` additionally runs rapid
resize/focus cycling and four consecutive compositor starts/stops. CTest labels
are `unit`, `integration`, `graphical`, and `stress`:

```sh
ctest --test-dir build -L unit --output-on-failure  # works without a desktop
ctest --test-dir build -L graphical --output-on-failure
QSG_INFO=1 ./build/flowstate --probe-visual
```

Graphical/integration tests require a live parent Wayland session and a Vulkan
driver; this wlroots Wayland backend cannot run those tests with no parent
compositor. Qt's `offscreen` QPA plugin does not provide a usable Vulkan target
on the tested machine. If `VK_LAYER_KHRONOS_validation` is installed, CMake
adds a validation-layer smoke test to `make test-all`; it checks the compositor
log for validation errors. The layer manifest was absent on the tested machine,
so that test was not available there. The QML pixel test uses a GPU readback
only in test mode. It verifies actual Qt content but does not capture the final
wlroots output; a compositor-output screenshot test remains to be added.

## Tiling and focus

`src/Layout.hpp` and `src/Layout.cpp` contain a pure C++26 layout function. One
window fills the usable output area. With two or more windows, the first mapped
window takes the left half and later windows share a vertical stack on the
right. A small output that cannot fit that shape uses a grid instead, so
positive-sized tiles never overlap. If there are more windows than output
pixels, the compositor temporarily hides them until there is room. The default
outer and inner gap is 12 logical pixels; pass
`--gap 0..128` to `flowstate-nested` or set `FLOWSTATE_GAP` for `make nested`.
The gap shrinks automatically when the output is too small. Layout order stays
stable when focus changes. Mapping, unmapping, and output resizing trigger a
new layout. The compositor sends xdg size configures and places each scene
tree at the requested tile origin. wlroots' xdg scene helper already anchors
the tree at the *window geometry* origin, so client-side decoration offsets
are accounted for without manually subtracting them.

New windows receive keyboard focus. Moving the pointer over a window or
clicking it focuses that window. Use
**Ctrl+Alt+J/K** to cycle forward/backward, or use **Alt+Tab** and
**Alt+Shift+Tab** if your parent desktop passes those shortcuts through.
Closing the focused window focuses the last remaining mapped window.

For a resize/debug check, start the compositor manually, then send it
`SIGUSR1` to toggle its nested output between 960×600 and 800×500.
`SIGUSR2` warps the cursor to the next tile to exercise hover focus and
`SIGRTMIN` cycles keyboard focus for integration testing. `SIGRTMIN+1` injects
an A key through the normal Wayland seat path so the focused Qt client can
confirm keyboard event delivery:

```sh
./build/flowstate-nested flowstate-debug --gap 12
# In another terminal:
WAYLAND_DISPLAY=flowstate-debug foot
kill -USR1 <flowstate-nested-pid>
kill -USR2 <flowstate-nested-pid>
kill -RTMIN <flowstate-nested-pid>
```

The compositor logs `tile`, `geometry`, `focus`, and `output resized` events to
stderr. `keyboard=1` on a focus line means the seat's keyboard focus reached
that window. If a client does not redraw at a requested size, inspect its
`geometry` log; xdg sizes are requests and clients can apply size limits.

The Wayland display owns clients and their xdg surfaces. Each small C++ window,
output, and keyboard handler registers listeners with those objects and removes
its listeners when the wlroots object is destroyed. `Server` owns the display,
backend, scene, allocator, and Vulkan renderer; it destroys clients first,
commits the now-empty scene, and then releases rendering resources. The layout
module has no wlroots or Qt dependency.

## What the nested prototype proves

`flowstate-nested` uses the parent Wayland display only for its output window.
It creates a separate socket for clients. It advertises `wl_compositor`,
`xdg_wm_base`, and `linux-dmabuf`, and rejects a non-Vulkan wlroots renderer.
The automated nested test verifies a mapped Qt Wayland client and a committed
Vulkan frame. The mixed test verifies the Qt panel and two terminals coexist in
the same Vulkan-composed output, while the Qt client itself verifies its rendered
pixels. Workspaces and an in-process Qt Quick shell are not implemented yet.

For manual inspection, start `./build/flowstate-nested flowstate-0` in one
terminal. In another, run `WAYLAND_DISPLAY=flowstate-0 wayland-info` and
`WAYLAND_DISPLAY=flowstate-0 foot`. Stop the compositor with Ctrl+C. Keep the
parent display's `WAYLAND_DISPLAY` for the compositor process itself.

The remaining feasibility checks include explicit-sync behavior, a dedicated
Vulkan DMA-BUF client test, and Qt Quick shell rendering on the compositor's
Vulkan device. wlroots 0.20 exposes five
C99 array annotations that C++26 cannot parse; CMake generates equivalent
C++ declarations in the build directory without changing installed headers.

Qt Wayland Compositor's published non-SHM client-buffer path exposes OpenGL
textures and its Linux DMA-BUF integration imports through EGL. The Vulkan
server-buffer plugin is for buffers sent **from** the compositor to clients
and also exposes an OpenGL texture. We should not adopt Qt Wayland Compositor
for Flowstate until a Vulkan-only client-surface path has been demonstrated.

## Qt Quick/Vulkan integration investigation

Qt's [QQuickRenderControl](https://doc.qt.io/qt-6/qquickrendercontrol.html)
can drive an invisible QQuickWindow into an offscreen target. The relevant
steps are `polishItems()`, `beginFrame()`, `sync()`, `render()`, and `endFrame()`.
[QQuickGraphicsDevice::fromDeviceObjects](https://doc.qt.io/qt-6/qquickgraphicsdevice.html)
can reference an existing Vulkan physical device, logical device, and queue
family; [QQuickRenderTarget::fromVulkanImage](https://doc.qt.io/qt-6/qquickrendertarget.html)
can target a caller-owned VkImage. wlroots 0.20 exposes its Vulkan handles
through [`wlr_vk_renderer_get_*`](https://wlroots.pages.freedesktop.org/wlroots/wlr/render/vulkan.h.html).
Qt also needs a QVulkanInstance adopted from the same VkInstance. Qt's
[documentation](https://doc.qt.io/qt-6/qvulkaninstance.html) requires
`VK_KHR_surface` and an appropriate platform surface extension for that
adoption path. However, [wlroots 0.20.2's instance creation](https://sources.debian.org/src/wlroots/0.20.2-1/render/vulkan/vulkan.c)
enables only optional debug utils, not those surface extensions. wlroots' public
Vulkan API exposes its handles but has no constructor that adopts a caller's
instance and device. Thus unmodified wlroots 0.20.2 does not provide a proven
same-device Qt Quick path through Qt's documented APIs.

The unresolved boundary is putting Qt's rendered image into the wlroots scene.
`wlr_scene_buffer_create()` accepts a `wlr_buffer`, not a VkImage. A likely
GPU-copy-free path is a Vulkan image with exportable DMA-BUF memory, wrapped as
a wlroots buffer and synchronized before the scene samples it. This is an API
inference, **not yet a proven implementation**. A shared-device version first
requires a wlroots Vulkan instance creation change to enable Qt's needed
extensions; a separate Qt Vulkan device could share DMA-BUFs but would add
another device and synchronization path. The next rendering milestone should
test instance compatibility, image formats and modifiers, queue ownership, and
timeline synchronization with a small offscreen Qt Quick texture before adding
shell UI. The current fallback runs Qt Quick as a Vulkan Wayland client, which
lets wlroots import its committed surface through the existing scene pipeline.
It avoids new code for GPU readback or copying in the running compositor, but
Qt and wlroots own separate Vulkan devices and we have not measured whether
the Wayland buffer import is zero-copy on every driver. No OpenGL path is used
by the current executables.

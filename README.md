# Flowstate — nested Vulkan tiling prototype

The repository has two small executables. `flowstate` checks that Qt Quick
renders a QML scene with Vulkan. `flowstate-nested` creates a Wayland compositor
inside one window on the current desktop and composes real client surfaces with
wlroots' Vulkan renderer. Both fail when their required Vulkan renderer is
unavailable. The nested compositor now tiles xdg toplevels and manages focus;
it is not yet a complete desktop shell.

## Arch Linux prerequisites

```sh
sudo pacman -Syu --needed gcc cmake ninja qt6-base qt6-declarative \
    qt6-wayland vulkan-icd-loader vulkan-intel vulkan-tools \
    vulkan-validation-layers vulkan-headers wayland wayland-protocols \
    wlroots0.20 pkgconf python foot
```

`qt6-wayland` provides the Qt Wayland client plugin used by the nested test.
On this machine, `.flowstate-local.mk` points `make` to a temporary Qt 6.12
package extraction in the user cache because system Qt requires a privileged
install. Remove that ignored local file after installing the packages above.

## Build and run inside a Wayland desktop

```sh
vulkaninfo --summary
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
QSG_INFO=1 ./build/flowstate
make test
make nested
FLOWSTATE_GAP=20 make nested
```

`make test` checks the standalone layout, opens the Qt Quick Vulkan probe,
runs it as a client of the nested compositor, and then opens **three real
`foot` Wayland clients**. The three-client test verifies tile coordinates,
client configure/geometry sizes, a nonzero client-side decoration offset,
focus changes, runtime output resize, retiling after a close, and clean exit.
`make nested` opens three interactive terminals inside one Flowstate window;
closing all three ends the demo. These commands need a live Wayland session and
Vulkan driver. `QSG_INFO=1` prints Qt's scene graph diagnostics.

## Tiling and focus

`src/Layout.hpp` and `src/Layout.cpp` contain a pure C++26 layout function. One
window fills the usable output area. With two or more windows, the first mapped
window takes the left half and later windows share a vertical stack on the
right. The default outer and inner gap is 12 logical pixels; pass
`--gap 0..128` to `flowstate-nested` or set `FLOWSTATE_GAP` for `make nested`.
The gap shrinks automatically when the output is too small. Layout order stays
stable when focus changes. Mapping, unmapping, and output resizing trigger a
new layout. The compositor sends xdg size configures and places each scene
tree at the requested tile origin. wlroots' xdg scene helper already anchors
the tree at the *window geometry* origin, so client-side decoration offsets
are accounted for without manually subtracting them.

New windows receive keyboard focus. Click a window to focus it, use
**Ctrl+Alt+J/K** to cycle forward/backward, or use **Alt+Tab** and
**Alt+Shift+Tab** if your parent desktop passes those shortcuts through.
Closing the focused window focuses the last remaining mapped window.

For a resize/debug check, start the compositor manually, then send it
`SIGUSR1` to toggle its nested output between 960×600 and 800×500.
`SIGUSR2` cycles focus for headless integration testing:

```sh
./build/flowstate-nested flowstate-debug --gap 12
# In another terminal:
WAYLAND_DISPLAY=flowstate-debug foot
kill -USR1 <flowstate-nested-pid>
kill -USR2 <flowstate-nested-pid>
```

The compositor logs `tile`, `geometry`, `focus`, and `output resized` events to
stderr. `keyboard=1` on a focus line means the seat's keyboard focus reached
that window. If a client does not redraw at a requested size, inspect its
`geometry` log; xdg sizes are requests and clients can apply size limits.

## What the nested prototype proves

`flowstate-nested` uses the parent Wayland display only for its output window.
It creates a separate socket for clients. It advertises `wl_compositor`,
`xdg_wm_base`, and `linux-dmabuf`, and rejects a non-Vulkan wlroots renderer.
The automated nested test verifies a mapped Qt Wayland client and a committed
Vulkan frame. The three-window test verifies tiling and focus in the same
compositor. Workspaces and the Qt Quick shell are not implemented yet.

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
Qt also needs a QVulkanInstance adopted from the same VkInstance; its
[documentation](https://doc.qt.io/qt-6/qvulkaninstance.html) requires suitable
instance extensions that cannot be added after creation.

The unresolved boundary is putting Qt's rendered image into the wlroots scene.
`wlr_scene_buffer_create()` accepts a `wlr_buffer`, not a VkImage. A likely
zero-copy path is a Vulkan image with exportable DMA-BUF memory, wrapped as a
wlroots buffer and synchronized before the scene samples it. This is an API
inference, **not yet a proven implementation**. The next rendering milestone
should validate instance extensions, image formats/modifiers, queue ownership,
and synchronization on the existing wlroots Vulkan device before adding shell
UI. No OpenGL path is used by the current executables.

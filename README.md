# Flowstate — nested Vulkan prototype

The repository has two small executables. `flowstate` checks that Qt Quick
renders a QML scene with Vulkan. `flowstate-nested` creates a Wayland compositor
inside one window on the current desktop and composes real client surfaces with
wlroots' Vulkan renderer. Both fail when their required Vulkan renderer is
unavailable. This is a compositor prototype, not yet a complete desktop shell.

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
```

`make test` opens the Qt Quick probe, then starts `flowstate-nested`, runs that
same Qt application as a **client of the nested server**, checks that a client
frame was committed, and closes both processes. `make nested` leaves the
compositor window open with `foot` running inside it. Closing the terminal
ends this demo. These commands need a live Wayland session and Vulkan driver.
`QSG_INFO=1` prints Qt's scene graph diagnostics.

## What the nested prototype proves

`flowstate-nested` uses the parent Wayland display only for its output window.
It creates a separate socket for clients. It advertises `wl_compositor`,
`xdg_wm_base`, and `linux-dmabuf`, and rejects a non-Vulkan wlroots renderer.
The automated nested test verifies a mapped Qt Wayland client and a committed
Vulkan frame. A manual `foot` run also mapped and committed a frame. The
compositor does not yet implement tiling, workspaces, or a Qt Quick shell.

For manual inspection, start `./build/flowstate-nested flowstate-0` in one
terminal. In another, run `WAYLAND_DISPLAY=flowstate-0 wayland-info` and
`WAYLAND_DISPLAY=flowstate-0 foot`. Stop the compositor with Ctrl+C. Keep the
parent display's `WAYLAND_DISPLAY` for the compositor process itself.

The remaining feasibility checks include explicit-sync behavior, a dedicated
Vulkan DMA-BUF client test, client resize and lifecycle stress, and Qt Quick
shell rendering on the compositor's Vulkan device. wlroots 0.20 exposes five
C99 array annotations that C++26 cannot parse; CMake generates equivalent
C++ declarations in the build directory without changing installed headers.

Qt Wayland Compositor's published non-SHM client-buffer path exposes OpenGL
textures and its Linux DMA-BUF integration imports through EGL. The Vulkan
server-buffer plugin is for buffers sent **from** the compositor to clients
and also exposes an OpenGL texture. We should not adopt Qt Wayland Compositor
for Flowstate until a Vulkan-only client-surface path has been demonstrated.
The next integration step is rendering Qt Quick shell UI through Vulkan into
the compositor scene. Device ownership and synchronization remain to be proved.

# pi-Stomp dev/build image.
#
# WHY this exact base: the Pi 5 runs Debian 12 "bookworm" on aarch64. The Mac is
# Apple Silicon (also arm64), so this image runs NATIVELY in Docker Desktop's
# Linux VM -- no QEMU emulation, no cross-compiler, no sysroot. Binaries built
# here link against the same glibc (2.36) and the same library versions that are
# on the Pi, so they run there unmodified.
#
# RULE: keep this on bookworm. Bumping to trixie/sid would build against a newer
# glibc than the Pi has, and the binaries would fail to start on the Pi.
FROM debian:bookworm-slim

ENV DEBIAN_FRONTEND=noninteractive

# Toolchain + the exact dev libs the sandbox/app use. These -dev packages carry
# the headers that match the runtime libs already installed on the Pi:
#   libasound2-dev -> ALSA / libasound (JUCE audio + tone/passthrough/vu code)
#   libgpiod-dev   -> libgpiod v1 (bookworm ships 1.6.x = the v1 C API the
#                     sandbox already targets; do not mix with v2 examples)
# spidev needs no package: its uapi header (linux/spi/spidev.h) comes with
# libc6-dev, pulled in by build-essential.
RUN apt-get update && apt-get install -y --no-install-recommends \
      build-essential \
      ccache \
      cmake \
      ninja-build \
      pkg-config \
      git \
      ca-certificates \
      rsync \
      openssh-client \
      gdb \
      libasound2-dev \
      libgpiod-dev \
      libfreetype6-dev \
      libfontconfig1-dev \
      libx11-dev \
      libxext-dev \
      libxrandr-dev \
      libxinerama-dev \
      libxcursor-dev \
    && rm -rf /var/lib/apt/lists/*

# Why the X11/freetype dev libs above (even though we build JUCE headless): any
# JUCE CMake project first builds JUCE's own asset tool, "juceaide", and juceaide
# always compiles juce_gui_basics -- which #includes X11/Xlib.h and ft2build.h.
# So these are required to COMPILE any JUCE target on Linux. They are build-time
# only: pistomp_app links no GUI modules and never opens a window at runtime.
# (We still skip GTK/WebKit: JUCE_WEB_BROWSER=0 in app/CMakeLists.txt avoids them.)

WORKDIR /work

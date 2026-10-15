# The Linux client on this Mac's own GPU (docs/plans/linux-release.md): podman's libkrun machine
# hands the M1's GPU to Linux through virtio-gpu, and Mesa's Venus driver speaks Vulkan to it,
# which krunkit runs on MoltenVK -- the Linux twin of Wine's winevulkan. Venus needs the patched
# Mesa of libkrun's author (COPR slp/mesa-krunkit), built for EL9 only, hence AlmaLinux 9.
#   podman machine ssh mu2gpu podman build -t mu2gpu -f <repo>/tools/linux/gpu.Containerfile
FROM docker.io/library/almalinux:9
RUN dnf -y -q install dnf-plugins-core epel-release && dnf config-manager --set-enabled crb \
 && dnf -y -q copr enable slp/mesa-krunkit epel-9-aarch64 \
 && dnf -y -q install mesa-vulkan-drivers vulkan-tools vulkan-loader-devel \
      gcc-toolset-14-gcc-c++ cmake ninja-build python3 git \
      libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel libXext-devel \
      libxkbcommon-devel wayland-devel wayland-protocols-devel \
      zlib-devel sqlite-devel libcurl-devel alsa-lib-devel pulseaudio-libs-devel \
      xorg-x11-server-Xvfb x11vnc xwd ImageMagick procps-ng \
 && dnf clean all
# bgfx's CMake asks for GL's headers even when only Vulkan is drawn with.
RUN dnf -y -q install libglvnd-devel && dnf clean all
# xdotool: clicks and keys on the Xvfb display, to play the game as a player would.
RUN dnf -y -q install xdotool && dnf clean all
ENV PATH=/opt/rh/gcc-toolset-14/root/usr/bin:$PATH

# RIFE frame interpolation tool

`build.sh` installs the Homebrew formulas molten-vk and vulkan-headers when they are missing,
clones `https://github.com/nihui/rife-ncnn-vulkan` into `.cache/rife/src` with its ncnn and
libwebp submodules, builds it, and copies the result to `.cache/rife/bin/rife-ncnn-vulkan`
with the `rife-v4.6` model folder next to it. Re-running the script skips the clone and
reuses the build directory. The pinned commit is `a7532fc3f9f8f008cd6eecd6f2ffe2a9698e0cf7`.

`smoke.sh` draws a 20x20 white square on a 128x128 black image, once centered at x=20 and once
at x=60. It asks for the frame halfway between the two, then measures where the bright pixels
sit. The square must land within 8 pixels of x=40. The script prints PASS or FAIL and exits
accordingly. `png_util.py` writes and reads those PNGs with `zlib`, so Pillow is not needed.

## CMake flags that worked

    -G Ninja
    -DCMAKE_BUILD_TYPE=Release
    -DCMAKE_OSX_ARCHITECTURES=arm64
    -DUSE_STATIC_MOLTENVK=ON
    -DVulkan_INCLUDE_DIR=/opt/homebrew/opt/vulkan-headers/include
    -DVulkan_LIBRARY=/opt/homebrew/opt/molten-vk/lib/libMoltenVK.a
    -DCMAKE_POLICY_VERSION_MINIMUM=3.5

`USE_STATIC_MOLTENVK=ON` only adds the Metal and Cocoa frameworks to the link line, so the two
`Vulkan_` paths point the build at Homebrew's static MoltenVK. Without the policy flag, cmake
4.4 refuses ncnn, which still asks for compatibility with cmake 3.4.

MoltenVK opens the Apple M4 Pro GPU over SSH with no display attached. The first inference
took 8.71 seconds because it compiles shaders. Later runs took 1.74 seconds.

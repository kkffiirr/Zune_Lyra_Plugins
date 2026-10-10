# Shaders for the OpenZDK (GPU) build

`blit.vs` / `blit.fs` draw the game frame as one textured quad (the fragment shader swaps red/blue because the frame is
BGRA in memory). The Zune's driver cannot compile GLSL, so they are compiled ahead of time with NVIDIA's Tegra tools
(`cgc.exe` and `shaderfix.exe` from `ce6_tegra_250_5265393.msi`, sha256 pinned in the sm64-zune project's `tools/fetch.py`):

    cgc -profile ar20vp -ogles -o blit.vs.cgbin blit.vs -po filename ; shaderfix -o blit.nvbv blit.vs.cgbin
    cgc -profile ar20fp -ogles -o blit.fs.cgbin blit.fs -po filename ; shaderfix -o blit.nvbf blit.fs.cgbin

The two binaries are already embedded as `backends/platform/zune/zdk_shaders.h` in `scummvm-zune-port-zdk.patch`.
The NVIDIA tools are not part of this repository.

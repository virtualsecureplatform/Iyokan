![Iyokan logo](https://user-images.githubusercontent.com/33079554/73909483-1af77f80-48f0-11ea-880b-55039781cca2.png)

[[Wiki](https://github.com/virtualsecureplatform/Iyokan/wiki)]

# Iyokan

Iyokan is a generic engine for evaluating logical circuits, such as processors,
over fully homomorphic encryption like TFHE.
Currently, Iyokan supports [TFHEpp](https://github.com/virtualsecureplatform/TFHEpp)
and [cuFHE](https://github.com/virtualsecureplatform/cuFHE)
(TFHE implementation for CPU and GPU, respectively).

## Build

```
$ mkdir build && cd build
$ cmake -DCMAKE_BUILD_TYPE=Release ..
$ make
```

See binaries in `build/bin/`.

If you want to enable CUDA support, use CMake option `IYOKAN_ENABLE_CUDA`.
You may have to tell CMake where to find CUDA by `CMAKE_CUDA_COMPILER` and
`CMAKE_CUDA_HOST_COMPILER` like this:

```
$ cmake -DIYOKAN_ENABLE_CUDA=On -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc     \
        -DCMAKE_CUDA_HOST_COMPILER=/usr/bin/clang-8        \
        ..
```

CMake builds Iyokan with 128-bit security by default.
If you want weaker (but faster and more memory efficient) security,
use `-DIYOKAN_80BIT_SECURITY=On`.

The default TFHE parameter family uses Block Binary keys and Subset Keys
(`USE_BLOCK_BINARY=ON`, `USE_SUBSET_KEY=ON`). Existing secret/evaluation keys,
encrypted packets, and snapshots made with the former defaults are not
compatible: regenerate them after upgrading. CPU and CUDA builds use the same
split Fourier archive layout, so the regenerated artifacts can be shared
between Iyokan, Tangor, and KVSP backends.

## Test

Run Ruby script `test.rb` at Iyokan's root directory like this
(assume that target binaries are in `build/bin/`):

```
$ sudo gem install toml-rb
$ ruby test.rb build/bin
```

If you want to run slow but detailed tests including ones for CUDA support:

```
$ ruby test.rb build/bin slow cuda
```

## Verilog and Yosys script requirements

The reset port must be named `reset`. Iyokan treats `$_SDFF_PP0_` and
`$_SDFF_PP1_` as initialization-only DFFs: the cell suffix selects the fresh
execution value, and the `R` input is not added to the runtime gate graph.
Consequently, asserting `reset` after execution starts does not reset these
cells. Ordinary `$_DFF_P_` cells and legacy reset logic remain supported.

`tools/yosys-init-only-reset.py` transfers reset values from a resetful mapped
netlist to initialization-only SDFFs. Reset gates eliminated by Yosys are kept
as zero-cost `$_BUF_` aliases so that Iyokan's cycle graph retains the original
topology. The tool rejects designs whose reset value cannot be proven constant
for every DFF or whose eliminated reset logic cannot be represented by an
alias.

## See Also

See our [wiki](https://github.com/virtualsecureplatform/Iyokan/wiki) for tutorials and more!

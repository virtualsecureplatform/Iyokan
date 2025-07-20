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

## Verilog and Yosys script Requirements

Here, we explicitly specify two requirements to correctly synthesize the Verilog file to supported JSON format. 

1. The name of reset must be `reset` not `rst` or any other name. 
- We treat ``reset'' as a special name. Currently, we remove all `reset'` related logic and the DFF initalization at the beggining. If you use other name, you will see a strange initizalization behavior. 
2. Use only $_DFF_P_ as a flipflop. Make it sure by running `dfflegalize -cell $_DFF_P_ 01`
- At least for Yosys 0.55 (we forgot exact version for this change), the flipflop will be synthesized as $_SDFF_PP0_ or $_SDFF_PP1_ in the default setting. However, we decided to stick supporting only $_DFF_P_ because we treat the `reset` in a special manner. This might be merely interface issue, so if you really neeed $_SDFF_PP0_ and $_SDFF_PP1_, please make PR or issue.

## See Also

See our [wiki](https://github.com/virtualsecureplatform/Iyokan/wiki) for tutorials and more!

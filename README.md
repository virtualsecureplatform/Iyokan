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

## Tutorial

Write a circuit you want to run in [Chisel3](https://www.chisel-lang.org/)
like this (`test/test-div-8bit.scala`):

```scala
import chisel3._

class Divider8bitPort extends Bundle {
  val in_a = Input(UInt(8.W))
  val in_b = Input(UInt(8.W))
  val out = Output(UInt(8.W))
}

class Divider8bit extends Module {
  val io = IO(new Divider8bitPort)

  io.out := io.in_a / io.in_b
}

object Elaborate extends App {
  chisel3.Driver.execute(args, () => new Divider8bit())
}
```

Compile this code with `sbt` (Chisel's tool) to Verilog file.
Then compile it to net list in JSON with [Yosys](http://www.clifford.at/yosys/),
and again compile it to DAG in JSON with
[Iyokan-L1](https://github.com/virtualsecureplatform/Iyokan-L1).
`test/test-div-8bit.json` is the one after all conversions.

Write a configuration file in TOML,
which includes specification of inputs/outputs (`test/test-div-8bit.toml`):

```toml
# Add DAG's JSON file as a module.
[[file]]
type = "iyokanl1-json"
path = "test-div-8bit.json"  # Compiled JSON's path
name = "core"

# Connect the module and Iyokan's core ports.
[connect]
"core/reset" = "@reset"
"core/io_in_a[0:7]" = "@A[0:7]"
"core/io_in_b[0:7]" = "@B[0:7]"
"@out[0:7]" = "core/io_out[0:7]"
```

Now we will run this circuit with test inputs.
The inputs have to be 'packed' into a single file with `iyokan-packet`.
Write another TOML file like this (`test/test05.in`):

```toml
[[bits]]
name = "A"      # To input port 'A'
size = 8        # 8-bit-wide (1-byte-wide) integer
bytes = [159]   # Input 159

[[bits]]
name = "B"      # To input port 'B'
size = 8        # 8-bit-wide (1-byte-wide) integer
bytes = [53]    # Input 53
```

Then convert this TOML file into 'packet' with `iyokan-packet`:

```
$ iyokan-packet toml2packet --in test/test05.in --out request.plain.packet
```

Okay. Run Iyokan _in plaintext mode_ with this packet as input to check it's correct:

```
$ iyokan plain --blueprint test/test-div-8bit.toml \
               -i request.plain.packet -o result.plain.packet -c 1
```

The result will be stored into `result.plain.packet`. Let's inspect its content:

```
$ iyokan-packet packet2toml --in result.plain.packet
rom = []
cycles = 1
ram = []
bits = [
{bytes=[3],size=8,name="out"},
]
Done. (0 seconds)
```

`bytes=[3]` means the output of the circuit is 3, which is exactly the same as 159/53.

Now, we will run Iyokan with the same but encrypted packet as input.
First, generate a secret key to encrypt the data:

```
$ iyokan-packet genkey --type tfhepp --out secret.key
```

Next, encrypt the packet:

```
$ iyokan-packet genbkey --in secret.key --out bootstrapping.key
$ iyokan-packet enc --key secret.key --in request.plain.packet --out request.enc.packet
```

Finally, run Iyokan with it:

```
# Use option `--enable-gpu` if your machine has a NVIDIA GPU such as V100.
$ iyokan tfhe --blueprint test/test-div-8bit.toml --bkey bootstrapping.key \
              -i request.enc.packet -o result.enc.packet -c 1
```

Decrypt the result to get its content:

```
$ iyokan-packet dec --key secret.key --in result.enc.packet --out result.plain.packet
$ iyokan-packet packet2toml --in result.plain.packet
rom = []
cycles = 1
ram = []
bits = [
{bytes=[3],size=8,name="out"},
]
Done. (0 seconds)
```

Finally we could get the same consequence as that in plaintext using ciphertext!

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

## See Also

See our [wiki](https://github.com/virtualsecureplatform/Iyokan/wiki).

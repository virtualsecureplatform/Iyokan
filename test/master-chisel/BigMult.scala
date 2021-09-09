import chisel3._

class BigMult extends Module {
    val io = IO(new Bundle {
        val inA = Input(UInt(300.W))
        val inB = Input(UInt(300.W))
        val out = Output(UInt(600.W))
    })

    io.out := io.inA * io.inB
}


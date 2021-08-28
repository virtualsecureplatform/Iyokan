import chisel3._

class Addr4bit extends Module {
    val io = IO(new Bundle {
        val inA = Input(UInt(4.W))
        val inB = Input(UInt(4.W))
        val out = Output(UInt(4.W))
    })

    io.out := io.inA + io.inB
}
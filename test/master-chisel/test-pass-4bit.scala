import chisel3._

class Pass4bit extends Module {
    val io = IO(new Bundle {
        val in = Input(UInt(4.W))
        val out = Output(UInt(4.W))
    })

    io.out := io.in
}
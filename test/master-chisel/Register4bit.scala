import chisel3._

class Register4bit extends Module {
    val io = IO(new Bundle {
        val in = Input(UInt(4.W))
        val out = Output(UInt(4.W))
    })

    val reg = RegInit(0.U(4.W))
    reg := io.in
    io.out := reg
}
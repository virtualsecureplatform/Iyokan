import chisel3._

class RegisterInit4bit extends Module {
    val io = IO(new Bundle {
        val in = Input(UInt(4.W))
        val out = Output(UInt(4.W))
    })

    val reg = RegInit(9.U(4.W))
    reg := io.in
    io.out := reg
}
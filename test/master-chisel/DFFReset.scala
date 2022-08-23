import chisel3._

class DFFReset extends Module {
    val io = IO(new Bundle {
      val out = Output(Bool())
    })

    val reg = RegInit(true.B)
    reg := false.B
    io.out := reg
}


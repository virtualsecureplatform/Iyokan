import chisel3._

class Const4bit extends Module {
  val io = IO(new Bundle {
    val out = Output(UInt(4.W))
  })

  io.out := 5.U(4.W)
}

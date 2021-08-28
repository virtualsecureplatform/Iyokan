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
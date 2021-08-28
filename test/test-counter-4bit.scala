import chisel3._

class Counter4bit extends Module {
    val io = IO(new Bundle {
        val out = Output(UInt(4.W))
    })

    val cnt = RegInit(0.U(4.W))
    cnt := cnt + 1.U(4.W)

    io.out := cnt
}
import chisel3._

class And4_2bit extends Module {
    val io = IO(new Bundle {
        val inA = Input(UInt(4.W))
        val inB = Input(UInt(4.W))
        val out = Output(UInt(2.W))
    })

    val bitwise_and = Wire(UInt(4.W))
    bitwise_and := io.inA & io.inB

    io.out := Cat(bitwise_and(3) & bitwise_and(2), bitwise_and(1) & bitwise_and(0))
}
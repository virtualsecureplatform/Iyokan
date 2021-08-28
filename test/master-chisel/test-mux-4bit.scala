import chisel3._

class Mux4bit extends Module {
    val io = IO(new Bundle {
        val inA = Input(UInt(4.W))
        val inB = Input(UInt(4.W))
        val sel = Input(Bool())
        val out = Output(UInt(4.W))
    })

    when(io.sel){
        io.out := io.inB
    }.otherwise{
        io.out := io.inA
    }
}
import chisel3._

object Elaborate extends App {
    chisel3.Driver.execute(args, () => new Addr4bit)
    chisel3.Driver.execute(args, () => new And4_2bit)
    chisel3.Driver.execute(args, () => new And4bit)
    chisel3.Driver.execute(args, () => new BigMult)
    chisel3.Driver.execute(args, () => new Counter4bit)
    chisel3.Driver.execute(args, () => new Divider8bit)
    chisel3.Driver.execute(args, () => new Mux4bit)
    chisel3.Driver.execute(args, () => new Pass4bit)
    chisel3.Driver.execute(args, () => new Register4bit)
    chisel3.Driver.execute(args, () => new RegisterInit4bit)
}

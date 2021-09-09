#!/usr/bin/bash -xeu

mkdir -p build
cp *.scala build.sbt build/
cd build

# Compile chisel code (*.scala) to Verilog (*.v)
sbt run

# Synthesize Verilog code (*.v) into netlist (*.json)
SRC="Addr4bit.v And4_2bit.v And4bit.v Counter4bit.v Divider8bit.v Mux4bit.v Pass4bit.v Register4bit.v"
for file in $SRC; do
    module=${file%.*}
    cat <<EOS > _build.ys
# read design
read_verilog $module.v

# elaborate design hierarchy
hierarchy -check -top $module

# the high-level stuff
proc; opt; fsm; opt; memory; opt

# mapping to internal cell library
techmap; opt

#To make easy to parse for V2TT
flatten;

# mapping logic to gates.
abc -g gates,MUX

# cleanup
clean -purge

# write synthesized design
write_json $module.json
EOS
    yosys _build.ys
done

mv Addr4bit.json addr-4bit-yosys.json
mv And4_2bit.json addr-4_2bit-yosys.json
mv And4bit.json and-4bit-yosys.json
mv Counter4bit.json counter-4bit-yosys.json
mv Divider8bit.json div-8bit-yosys.json
mv Mux4bit.json mux-4bit-yosys.json
mv Pass4bit.json pass-4bit-yosys.json
mv Register4bit.json register-4bit-yosys.json

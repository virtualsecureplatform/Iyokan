#!/usr/bin/bash -xeu

failwith(){
    echo "[ERROR] $1" >&2
    exit 1
}

if ! command -v sbt &> /dev/null; then
    failwith "sbt not found"
fi
if ! command -v yosys &> /dev/null; then
    failwith "yosys not found"
fi
if ! command -v jq &> /dev/null; then
    failwith "jq not found"
fi

mkdir -p build
cp *.scala build.sbt build/
cd build

# Compile chisel code (*.scala) to Verilog (*.v)
sbt run

# Synthesize Verilog code (*.v) into netlist (*.json)
SRC=""
SRC+=" Addr4bit.v"
SRC+=" And4_2bit.v"
SRC+=" And4bit.v"
#SRC+=" BigMult.v"
SRC+=" Const4bit.v"
SRC+=" Counter4bit.v"
SRC+=" Divider8bit.v"
SRC+=" Mux4bit.v"
SRC+=" Pass4bit.v"
SRC+=" Register4bit.v"
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
write_json ${module}.json
EOS
    yosys _build.ys
    # Thanks to: https://stackoverflow.com/a/7729087
    cat "${module}.json" | \
        jq -c "del(..|.port_directions?) | `
              `del(..|.parameters?) | `
              `del(..|.attributes?) | `
              `del(..|.hide_name?)" > "${module}.min.json"
    rm "${module}.json"
done

mv Addr4bit.min.json addr-4bit-yosys.json
mv And4_2bit.min.json addr-4_2bit-yosys.json
mv And4bit.min.json and-4bit-yosys.json
#mv BigMult.min.json big-mult-yosys.json
mv Const4bit.min.json const-4bit-yosys.json
mv Counter4bit.min.json counter-4bit-yosys.json
mv Divider8bit.min.json div-8bit-yosys.json
mv Mux4bit.min.json mux-4bit-yosys.json
mv Pass4bit.min.json pass-4bit-yosys.json
mv Register4bit.min.json register-4bit-yosys.json

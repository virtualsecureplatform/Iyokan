# Synthesize Verilog code (*.v) into netlist (*.json)
SRC="RegisterInit4bit.v"
for file in $SRC; do
    module=${file%.*}
    cat <<EOS > _build_sdff.ys
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

dfflegalize -cell \$_SDFF_PP0_ 01 -cell \$_SDFF_PP1_ 01

# mapping logic to gates.
abc -g gates,MUX

# cleanup
clean -purge

# write synthesized design
write_json $module.json
EOS
    /usr/local/bin/yosys _build_sdff.ys
done

mv RegisterInit4bit.json test-register-init-4bit-yosys.json

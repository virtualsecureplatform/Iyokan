#!/usr/bin/env python3
"""Specialize a Yosys gate netlist for KVSP's initialization-only reset.

The resetful netlist is evaluated with reset asserted to recover every DFF's
reset value.  Matching DFFs are encoded as $_SDFF_PP0_ or $_SDFF_PP1_, and an
already reset-folded netlist identifies gates that can become zero-cost aliases.
Iyokan uses the suffix as the initial value and does not add the R input to its
runtime evaluation graph.
"""

import argparse
import json
import os
import tempfile


GATES = {
    "$_NOT_",
    "$_AND_",
    "$_ANDNOT_",
    "$_NAND_",
    "$_OR_",
    "$_ORNOT_",
    "$_NOR_",
    "$_XOR_",
    "$_XNOR_",
    "$_MUX_",
}
DFF = "$_DFF_P_"


class SpecializationError(RuntimeError):
    pass


def only_module(root, path):
    modules = root.get("modules")
    if not isinstance(modules, dict) or len(modules) != 1:
        raise SpecializationError(f"{path}: expected exactly one module")
    return next(iter(modules.values()))


def bit_value(bit, values):
    if bit == "0":
        return 0
    if bit == "1":
        return 1
    if isinstance(bit, int):
        return values.get(bit)
    raise SpecializationError(f"unsupported signal bit {bit!r}")


def unary_not(value):
    return None if value is None else 1 - value


def binary_and(a, b):
    if a == 0 or b == 0:
        return 0
    if a == 1 and b == 1:
        return 1
    return None


def binary_or(a, b):
    if a == 1 or b == 1:
        return 1
    if a == 0 and b == 0:
        return 0
    return None


def evaluate_gate(cell, values):
    cell_type = cell["type"]
    conn = cell["connections"]

    def get(port):
        bits = conn.get(port)
        if not isinstance(bits, list) or len(bits) != 1:
            raise SpecializationError(
                f"{cell_type}: expected one bit on port {port}"
            )
        return bit_value(bits[0], values)

    a = get("A")
    if cell_type == "$_NOT_":
        return unary_not(a)
    b = get("B")
    if cell_type == "$_AND_":
        return binary_and(a, b)
    if cell_type == "$_ANDNOT_":
        return binary_and(a, unary_not(b))
    if cell_type == "$_NAND_":
        return unary_not(binary_and(a, b))
    if cell_type == "$_OR_":
        return binary_or(a, b)
    if cell_type == "$_ORNOT_":
        return binary_or(a, unary_not(b))
    if cell_type == "$_NOR_":
        return unary_not(binary_or(a, b))
    if cell_type in ("$_XOR_", "$_XNOR_"):
        result = None if a is None or b is None else a ^ b
        return unary_not(result) if cell_type == "$_XNOR_" else result
    if cell_type == "$_MUX_":
        select = get("S")
        if select == 0:
            return a
        if select == 1:
            return b
        return a if a is not None and a == b else None
    raise SpecializationError(f"unsupported gate type {cell_type}")


def one_bit(connection, cell_name, port):
    bits = connection.get(port)
    if not isinstance(bits, list) or len(bits) != 1:
        raise SpecializationError(
            f"{cell_name}: expected one bit on DFF port {port}"
        )
    return bits[0]


def reset_values(module, reset_name, asserted):
    ports = module.get("ports", {})
    reset = ports.get(reset_name)
    if not isinstance(reset, dict) or reset.get("direction") != "input":
        raise SpecializationError(f"missing input port {reset_name!r}")
    reset_bits = reset.get("bits")
    if not isinstance(reset_bits, list) or len(reset_bits) != 1:
        raise SpecializationError("reset port must contain exactly one bit")
    if not isinstance(reset_bits[0], int):
        raise SpecializationError("reset input must not be constant")

    values = {reset_bits[0]: asserted}
    pending = []
    dffs = {}
    for name, cell in module.get("cells", {}).items():
        cell_type = cell.get("type")
        if cell_type == DFF:
            dffs[name] = cell
        elif cell_type in GATES:
            pending.append((name, cell))
        else:
            raise SpecializationError(
                f"{name}: unsupported cell type in mapped netlist: {cell_type}"
            )

    while True:
        changed = False
        for name, cell in pending:
            output = one_bit(cell["connections"], name, "Y")
            if not isinstance(output, int) or output in values:
                continue
            value = evaluate_gate(cell, values)
            if value is not None:
                values[output] = value
                changed = True
        if not changed:
            break

    result = {}
    unknown = []
    for name, cell in dffs.items():
        data = one_bit(cell["connections"], name, "D")
        value = bit_value(data, values)
        if value is None:
            unknown.append(name)
        else:
            result[name] = value
    if unknown:
        sample = ", ".join(sorted(unknown)[:8])
        raise SpecializationError(
            f"reset did not resolve {len(unknown)} DFF input(s): {sample}"
        )
    return result, reset_bits[0]


def specialize(resetful_root, folded_root, reset_name, asserted):
    resetful = only_module(resetful_root, "resetful netlist")
    folded = only_module(folded_root, "folded netlist")
    initial, reset_bit = reset_values(resetful, reset_name, asserted)

    folded_port = folded.get("ports", {}).get(reset_name)
    if not isinstance(folded_port, dict) or folded_port.get("direction") != "input":
        raise SpecializationError(f"folded netlist is missing input {reset_name!r}")
    folded_reset = folded_port.get("bits")
    if not isinstance(folded_reset, list) or len(folded_reset) != 1:
        raise SpecializationError("folded reset port must contain exactly one bit")
    if not isinstance(folded_reset[0], int):
        raise SpecializationError("folded reset input must not be constant")

    resetful_cells = resetful.get("cells", {})
    folded_cells = folded.get("cells", {})
    folded_dffs = {
        name: cell for name, cell in folded_cells.items() if cell.get("type") == DFF
    }
    if set(initial) != set(folded_dffs):
        missing = sorted(set(initial) - set(folded_dffs))
        extra = sorted(set(folded_dffs) - set(initial))
        raise SpecializationError(
            "DFF set changed while folding reset "
            f"(missing={missing[:4]}, extra={extra[:4]})"
        )

    added = set(folded_cells) - set(resetful_cells)
    if added:
        raise SpecializationError(
            f"reset folding added unexpected cells: {sorted(added)[:4]}"
        )

    inactive = 1 - asserted
    inactive_bit = str(inactive)
    for cell in resetful_cells.values():
        directions = cell.get("port_directions", {})
        for port, bits in cell.get("connections", {}).items():
            if directions.get(port) == "output":
                continue
            cell["connections"][port] = [
                inactive_bit if bit == reset_bit else bit for bit in bits
            ]

    removed = set(resetful_cells) - set(folded_cells)
    for name in removed:
        cell = resetful_cells[name]
        conn = cell.get("connections", {})
        cell_type = cell.get("type")
        source = None
        if cell_type == "$_MUX_" and conn.get("S") == [inactive_bit]:
            source = conn.get("A" if inactive == 0 else "B")
        elif cell_type == "$_OR_":
            if conn.get("A") == ["0"]:
                source = conn.get("B")
            elif conn.get("B") == ["0"]:
                source = conn.get("A")
        if not isinstance(source, list) or len(source) != 1:
            raise SpecializationError(
                f"{name}: cannot preserve removed {cell_type} as an alias"
            )
        output = conn.get("Y")
        if not isinstance(output, list) or len(output) != 1:
            raise SpecializationError(f"{name}: alias output is not one bit")
        cell["type"] = "$_BUF_"
        cell["connections"] = {"A": source, "Y": output}
        cell["port_directions"] = {"A": "input", "Y": "output"}

    for name, value in initial.items():
        cell = resetful_cells[name]
        cell["type"] = f"$_SDFF_PP{value}_"
        cell.setdefault("connections", {})["R"] = [reset_bit]
        directions = cell.setdefault("port_directions", {})
        directions.update({"C": "input", "D": "input", "Q": "output", "R": "input"})

    return len(initial), sum(initial.values()), len(removed)


def load(path):
    with open(path, "r", encoding="utf-8") as source:
        return json.load(source)


def write_atomic(path, root):
    directory = os.path.dirname(os.path.abspath(path))
    os.makedirs(directory, exist_ok=True)
    fd, temporary = tempfile.mkstemp(prefix=".iyokan-init-", dir=directory)
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as destination:
            json.dump(root, destination, separators=(",", ":"))
            destination.write("\n")
        os.replace(temporary, path)
    except BaseException:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
        raise


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--resetful", required=True)
    parser.add_argument("--folded", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--reset", default="reset")
    parser.add_argument("--asserted", choices=("0", "1"), default="1")
    args = parser.parse_args()

    resetful = load(args.resetful)
    folded = load(args.folded)
    count, ones, aliases = specialize(
        resetful, folded, args.reset, int(args.asserted)
    )
    write_atomic(args.output, resetful)
    print(
        f"initialized {count} DFFs ({ones} one, {count - ones} zero); "
        f"replaced {aliases} reset gates with aliases"
    )


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError, KeyError, TypeError, SpecializationError) as error:
        raise SystemExit(f"yosys-init-only-reset: {error}")

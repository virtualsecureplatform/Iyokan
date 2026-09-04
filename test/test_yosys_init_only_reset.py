#!/usr/bin/env python3

import copy
import importlib.util
import pathlib
import unittest


TOOL = pathlib.Path(__file__).parents[1] / "tools" / "yosys-init-only-reset.py"
SPEC = importlib.util.spec_from_file_location("yosys_init_only_reset", TOOL)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


def cell(cell_type, connections):
    return {"type": cell_type, "connections": connections}


def netlist(cells):
    return {
        "modules": {
            "top": {
                "ports": {
                    "reset": {"direction": "input", "bits": [2]},
                    "data": {"direction": "input", "bits": [3]},
                },
                "cells": cells,
            }
        }
    }


class InitOnlyResetTest(unittest.TestCase):
    def setUp(self):
        self.resetful = netlist(
            {
                "reset_mux": cell(
                    "$_MUX_", {"A": [3], "B": ["0"], "S": [2], "Y": [4]}
                ),
                "zero_dff": cell(
                    "$_DFF_P_", {"C": [7], "D": [4], "Q": [5]}
                ),
                "one_dff": cell(
                    "$_DFF_P_", {"C": [7], "D": [2], "Q": [6]}
                ),
            }
        )
        self.folded = netlist(
            {
                "zero_dff": cell(
                    "$_DFF_P_", {"C": [7], "D": [3], "Q": [5]}
                ),
                "one_dff": cell(
                    "$_DFF_P_", {"C": [7], "D": [3], "Q": [6]}
                ),
            }
        )

    def test_encodes_zero_and_one_initial_values(self):
        resetful = copy.deepcopy(self.resetful)
        folded = copy.deepcopy(self.folded)
        count, ones, aliases = MODULE.specialize(resetful, folded, "reset", 1)
        cells = resetful["modules"]["top"]["cells"]

        self.assertEqual((count, ones, aliases), (2, 1, 1))
        self.assertEqual(cells["zero_dff"]["type"], "$_SDFF_PP0_")
        self.assertEqual(cells["one_dff"]["type"], "$_SDFF_PP1_")
        self.assertEqual(cells["zero_dff"]["connections"]["R"], [2])
        self.assertEqual(cells["one_dff"]["port_directions"]["R"], "input")
        self.assertEqual(cells["reset_mux"]["type"], "$_BUF_")
        self.assertEqual(
            cells["reset_mux"]["connections"], {"A": [3], "Y": [4]}
        )

    def test_rejects_unknown_reset_value(self):
        resetful = copy.deepcopy(self.resetful)
        resetful["modules"]["top"]["cells"]["zero_dff"]["connections"][
            "D"
        ] = [3]
        with self.assertRaisesRegex(MODULE.SpecializationError, "did not resolve"):
            MODULE.specialize(resetful, self.folded, "reset", 1)

    def test_rejects_changed_dff_set(self):
        folded = copy.deepcopy(self.folded)
        del folded["modules"]["top"]["cells"]["one_dff"]
        with self.assertRaisesRegex(MODULE.SpecializationError, "DFF set changed"):
            MODULE.specialize(self.resetful, folded, "reset", 1)


if __name__ == "__main__":
    unittest.main()

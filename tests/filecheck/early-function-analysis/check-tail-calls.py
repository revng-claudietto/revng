#
# This file is distributed under the MIT License. See LICENSE.md for details.
#

import sys
from pathlib import Path

import yaml

model = yaml.safe_load(Path(sys.argv[1]).read_text())
bundles = yaml.safe_load(Path(sys.argv[2]).read_text())
functions = [yaml.safe_load(value)["MainFunction"] for value in bundles.values()]
cfgs = {function["Name"]: function for function in functions}
models = {function["Name"]: function for function in model["Functions"]}


def successors(name):
    function = cfgs[name]
    entry = next(block for block in function["Blocks"] if block["ID"] == function["Entry"])
    return entry["Successors"]


def tail_call(name, callee):
    calls = [edge for edge in successors(name) if edge["Kind"] == "CallEdge"]
    assert len(calls) == 1, (name, calls)
    assert calls[0]["Destination"] == callee, (name, calls)
    assert calls[0]["IsTailCall"], (name, calls)


tail_call("direct", models["callee"]["Entry"])
assert len(cfgs["direct"]["Blocks"]) == 1
assert len(successors("direct")) == 1

tail_call("conditional", models["callee"]["Entry"])
assert len(cfgs["conditional"]["Blocks"]) == 2
assert any(edge["Type"] == "DirectBranch" for edge in successors("conditional"))

assert len(successors("bad_stack")) == 1
assert successors("bad_stack")[0]["Type"] == "DirectBranch"

tail_call("indirect", ":Invalid")
assert models["indirect"]["CallSites"][0]["IsTailCall"]

assert not successors("ordinary")[0].get("IsTailCall", False)
assert len(cfgs["ordinary"]["Blocks"]) == 2
assert cfgs["ordinary"]["Blocks"][1]["Successors"][0]["Type"] == "Return"

assert successors("unmarked")[0]["Type"] == "DirectBranch"
assert len(cfgs["unmarked"]["Blocks"]) == 2

tail_call("recursive", models["recursive"]["Entry"])
assert len(cfgs["recursive"]["Blocks"]) == 2


tail_call("noreturn_tail", models["noreturn_callee"]["Entry"])
assert "NoReturn" in models["noreturn_tail"]["Attributes"]
assert len(successors("unknown_stack")) == 1
assert successors("unknown_stack")[0]["Type"] == "DirectBranch"
tail_call("restored_stack", models["callee"]["Entry"])
assert len(cfgs["restored_stack"]["Blocks"]) == 1


# Only the taken edge is a tail call, falling through into the next function
# keeps it local
tail_call("falls_into_next", models["callee"]["Entry"])
assert len(cfgs["falls_into_next"]["Blocks"]) == 2
branches = [edge for edge in successors("falls_into_next") if edge["Type"] == "DirectBranch"]
assert [edge["Destination"] for edge in branches] == [models["next_function"]["Entry"]]

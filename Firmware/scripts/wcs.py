#!/usr/bin/env python3
"""
Worst-Case Stack (WCS) analyzer.

Ported from ../STM32/WCS.py and adapted for the Zephyr CMake build layout
(object files end in `.c.obj`, stack-usage files end in `.c.su`, RTL dumps
end in `.c.<NNN>r.dfinish`).

Walks the build directory passed on argv[1] (default: ./build), pairs each
TU's three artefacts, builds a call graph from the RTL dumps, reads per-
function local stack from the .su files, and computes the worst-case stack
depth reachable from every function. The output is sorted: largest WCS at
the top, "unbounded" (function-pointer call or recursion) above bounded
values.

Manual overrides for symbols GCC can't see through (memcpy, snprintf, HAL
inline asm) live in scripts/wcs_manual.msu — one `<symbol> <bytes>` pair
per line.

Usage:
    scripts/wcs.py [build_dir]

Pre-requisite: build with -fstack-usage and -fdump-rtl-dfinish on the TUs
of interest. The top-level CMakeLists.txt already wires these for the app
target.
"""

# pylint: disable = invalid-name, too-few-public-methods

import os
import pprint
import re
import sys
from subprocess import check_output
from typing import Any, Dict, List, Tuple

# ---- Extensions ----

# Zephyr/CMake names object files <stem>.c.obj rather than <stem>.o.
OBJ_EXT = ".c.obj"
SU_EXT = ".c.su"
# The RTL dump's numeric prefix varies by GCC version (e.g. .349r.dfinish on
# arm-none-eabi 11, .386r.dfinish on arm-zephyr-eabi 14). Discover at runtime.
RTL_EXT_TAIL = ".dfinish"
# Manual-stack-usage overrides: one TU-wide file (not per-TU like .msu in old script)
DEFAULT_MANUAL_FILE = os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "wcs_manual.msu"
)

READ_ELF = os.getenv("READELF", "readelf")
STDOUT_ENCODING = "utf-8"


def _validated_path(raw: str) -> str:
    """Resolve a caller-supplied path and reject obviously malformed input.

    This script is invoked with trusted build-directory paths (argv,
    scripts/stack_analysis.sh) — absolute paths and arbitrary build dirs
    are legal, so this is a sanity check ahead of the filesystem access,
    not a jail. "File doesn't exist" stays a normal, separately-handled
    case; only an empty string / embedded NUL is treated as malformed.
    """
    if not raw or "\0" in raw:
        raise ValueError(f"invalid path argument: {raw!r}")
    resolved = os.path.realpath(raw)
    if not resolved:
        raise ValueError(f"invalid path argument: {raw!r}")
    return resolved


# ---- Data classes ----


class Printable:
    def __repr__(self) -> str:
        return "<" + type(self).__name__ + "> " + pprint.pformat(
            vars(self), indent=4, width=1
        )


class Symbol(Printable):
    type: str = "uninitialized"
    binding: str = "uninitialized"
    name: str = "uninitialized"


CallNode = Dict[str, Any]


# ---- Recursive WCS calculation ----


def calc_wcs(node: CallNode, parents: List[CallNode]) -> None:
    """Compute worst-case stack reachable from `node`. Sets node['wcs']."""
    if "wcs" in node:
        return

    if node["has_ptr_call"]:
        node["wcs"] = "unbounded"
        return

    if node in parents:
        node["wcs"] = "unbounded"
        return

    call_max = 0
    for call_dict in node["r_calls"]:
        calc_wcs(call_dict, parents + [node])
        if call_dict["wcs"] != "unbounded":
            call_max = max(call_max, call_dict["wcs"])
        for unresolved in call_dict["unresolved_calls"]:
            node["unresolved_calls"].add(unresolved)

    node["wcs"] = call_max + node["local_stack"]


# ---- Call graph ----


class CallGraph:
    def __init__(self) -> None:
        self.globals: Dict[str, CallNode] = {}
        self.locals: Dict[str, Dict[str, CallNode]] = {}
        self.weak: Dict[str, CallNode] = {}

    def read_obj(self, tu_obj: str, tu_label: str) -> None:
        """Read symbols (bindings) from the object file for one TU."""
        for s in read_symbols(tu_obj):
            if s.type != "FUNC":
                continue
            if s.binding == "GLOBAL":
                if s.name in self.globals or s.name in self.locals:
                    raise Exception(f"Multiple declarations of {s.name}")
                self.globals[s.name] = {
                    "tu": tu_label,
                    "name": s.name,
                    "binding": s.binding,
                }
            elif s.binding == "LOCAL":
                if s.name in self.locals and tu_label in self.locals[s.name]:
                    raise Exception(f"Multiple declarations of {s.name}")
                self.locals.setdefault(s.name, {})[tu_label] = {
                    "tu": tu_label,
                    "name": s.name,
                    "binding": s.binding,
                }
            elif s.binding == "WEAK":
                if s.name in self.weak:
                    # Weak duplicates are common (e.g. default fault handler);
                    # keep the first.
                    continue
                self.weak[s.name] = {
                    "tu": tu_label,
                    "name": s.name,
                    "binding": s.binding,
                }
            else:
                raise Exception(
                    f'Unknown binding "{s.binding}" for symbol {s.name}'
                )

    def find_fxn(self, tu: str, fxn: str):
        if fxn in self.globals:
            return self.globals[fxn]
        try:
            return self.locals[fxn][tu]
        except KeyError:
            return None

    def find_demangled_fxn(self, tu: str, fxn: str):
        for f in self.globals.values():
            if f.get("demangledName") == fxn:
                return f
        for f_per_tu in self.locals.values():
            if tu in f_per_tu and f_per_tu[tu].get("demangledName") == fxn:
                return f_per_tu[tu]
        return None

    def read_rtl(self, tu_rtl: str, tu_label: str) -> None:
        """Walk the RTL dump and populate calls / has_ptr_call per function."""
        function = re.compile(
            r"^;; Function (.*) \((\S+), funcdef_no=\d+(, [a-z_]+=\d+)*\)"
            r"( \([a-z ]+\))?$"
        )
        static_call = re.compile(r'^.*\(call.*"(.*)".*$')
        other_call = re.compile(r"^.*call .*$")

        node: CallNode | None = None
        with open(tu_rtl, "rt", encoding="latin_1") as fh:
            for line in fh:
                m = function.match(line)
                if m:
                    fxn_name = m.group(2)
                    node = self.find_fxn(tu_label, fxn_name)
                    if not node:
                        # Function appears in RTL but not in symbol table —
                        # likely inlined or static-folded. Skip.
                        continue
                    node["demangledName"] = m.group(1)
                    node["calls"] = set()
                    node["has_ptr_call"] = False
                    continue

                if node is None:
                    continue

                m = static_call.match(line)
                if m:
                    node["calls"].add(m.group(1))
                    continue

                m = other_call.match(line)
                if m:
                    node["has_ptr_call"] = True

    def read_su(self, tu_su: str, tu_label: str) -> None:
        """Read per-function local stack usage from a .su file."""
        su_line = re.compile(r"^(.+):(\d+):(\d+):(.+)\t(\d+)\t(\S+)$")
        with open(tu_su, "rt", encoding="latin_1") as fh:
            for i, line in enumerate(fh, start=1):
                m = su_line.match(line)
                if not m:
                    print(f"warn: cannot parse {tu_su}:{i}")
                    continue
                fxn = m.group(4)
                node = self.find_demangled_fxn(tu_label, fxn)
                if node:
                    node["local_stack"] = int(m.group(5))

    def read_manual(self, path: str) -> None:
        resolved = _validated_path(path)
        if not os.path.exists(resolved):
            return
        with open(resolved, "rt", encoding="latin_1") as fh:
            for line in fh:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                parts = line.split()
                if len(parts) != 2:
                    continue
                fxn, stack_sz = parts
                if fxn in self.globals:
                    # Don't override an already-known global; manual entries
                    # are meant for unresolved externs.
                    continue
                self.globals[fxn] = {
                    "wcs": int(stack_sz),
                    "calls": set(),
                    "has_ptr_call": False,
                    "local_stack": int(stack_sz),
                    "is_manual": True,
                    "name": fxn,
                    "demangledName": fxn,
                    "tu": "#MANUAL",
                    "binding": "GLOBAL",
                }

    def materialise_weaks(self) -> None:
        for fxn in self.weak.values():
            if fxn["name"] not in self.globals:
                self.globals[fxn["name"]] = fxn

    def resolve_all_calls(self) -> None:
        def resolve(node: CallNode) -> None:
            node.setdefault("calls", set())
            node["r_calls"] = []
            node["unresolved_calls"] = set()
            for call in node["calls"]:
                target = self.find_fxn(node["tu"], call)
                if target:
                    node["r_calls"].append(target)
                else:
                    node["unresolved_calls"].add(call)

        for fxn in self.globals.values():
            resolve(fxn)
        for per_tu in self.locals.values():
            for fxn in per_tu.values():
                resolve(fxn)

    def validate(self) -> None:
        """Backfill defaults for functions whose RTL dump didn't list them.

        GCC sometimes emits `.part.<N>` clones (cold-split optimisation)
        that appear in the symbol table but not in the RTL .dfinish dump,
        and weak/external functions never get a dump. Fill in sane
        defaults so calc_wcs treats them as zero-stack leaves with no
        callees — the caller's WCS still propagates correctly.
        """
        def fill(d: CallNode) -> None:
            d.setdefault("local_stack", 0)
            d.setdefault("calls", set())
            d.setdefault("has_ptr_call", False)
            d.setdefault("demangledName", d.get("name", "?"))

        for fxn in self.globals.values():
            fill(fxn)
        for per_tu in self.locals.values():
            for fxn in per_tu.values():
                fill(fxn)

    def calc_all(self) -> None:
        for fxn in self.globals.values():
            calc_wcs(fxn, [])
        for per_tu in self.locals.values():
            for fxn in per_tu.values():
                calc_wcs(fxn, [])

    def print_table(self, top: int | None = None) -> None:
        rows: List[CallNode] = list(self.globals.values())
        for per_tu in self.locals.values():
            rows.extend(per_tu.values())

        def sort_key(row: CallNode):
            v = row.get("wcs", 0)
            return 1 if v == "unbounded" else -v

        rows.sort(key=sort_key)
        if top:
            rows = rows[:top]

        tu_w = max(max((len(r["tu"]) for r in rows), default=0), 16)
        name_w = max(
            max((len(r.get("demangledName", r["name"])) for r in rows), default=0),
            13,
        )
        fmt = (
            "{:<" + str(tu_w + 2) + "}  {:<" + str(name_w + 2)
            + "}  {:>14}  {:<}"
        )
        print(fmt.format("Translation Unit", "Function Name", "Stack",
                         "Unresolved Dependencies"))
        print("-" * (tu_w + name_w + 50))
        for r in rows:
            stack = str(r.get("wcs", "?"))
            unresolved = r.get("unresolved_calls", set())
            if unresolved and stack != "unbounded":
                stack = "unbounded:" + stack
            urstr = "(" + " ,".join(sorted(unresolved)) + ")" if unresolved else ""
            print(fmt.format(r["tu"], r.get("demangledName", r["name"]),
                             stack, urstr))


# ---- File discovery ----


def read_symbols(file: str) -> List[Symbol]:
    output = check_output([READ_ELF, "-s", "-W", file]).decode(STDOUT_ENCODING)
    lines = output.splitlines()[3:]
    out: List[Symbol] = []
    for line in lines:
        v = line.split()
        if len(v) < 5:
            continue
        s = Symbol()
        s.type = v[3]
        s.binding = v[4]
        s.name = v[7] if len(v) >= 8 else ""
        out.append(s)
    return out


def find_tus(build_dir: str) -> List[Tuple[str, str, str, str]]:
    """Find (obj, su, rtl, tu_label) tuples for every TU with all three files.

    The Zephyr/CMake build emits each TU as:
        <stem>.c.obj
        <stem>.c.su
        <stem>.c.<NNN>r.dfinish

    where <NNN> is the GCC-version-dependent pass number (.349r on
    arm-zephyr-eabi 14.3, .386r on some others). We anchor on .c.obj and
    glob for the matching dfinish in the same directory.

    `tu_label` is a human-readable path (relative to build_dir) used in
    the report.
    """
    tus: List[Tuple[str, str, str, str]] = []
    for root, _, files in os.walk(build_dir):
        for f in files:
            if not f.endswith(OBJ_EXT):
                continue
            base = f[: -len(OBJ_EXT)]  # e.g. "foo" from "foo.c.obj"
            obj = os.path.join(root, f)
            su = os.path.join(root, base + SU_EXT)
            if not os.path.exists(su):
                continue
            # Find the matching dfinish — any file whose name starts with
            # base + ".c." and ends with ".dfinish".
            rtl = None
            prefix = base + ".c."
            for cand in files:
                if cand.startswith(prefix) and cand.endswith(RTL_EXT_TAIL):
                    rtl = os.path.join(root, cand)
                    break
            if rtl is None:
                continue
            tu_label = "./" + os.path.relpath(
                os.path.join(root, base + ".c"), build_dir
            )
            tus.append((obj, su, rtl, tu_label))
    return tus


# ---- Main ----


def main() -> None:
    build_dir = sys.argv[1] if len(sys.argv) > 1 else "build"
    if not os.path.isdir(build_dir):
        sys.exit(f"build dir '{build_dir}' not found")

    manual_file = sys.argv[2] if len(sys.argv) > 2 else DEFAULT_MANUAL_FILE

    tus = find_tus(build_dir)
    if not tus:
        sys.exit("No TUs with .c.obj + .c.su + .dfinish triples found.")

    cg = CallGraph()
    for obj, _su, _rtl, label in tus:
        cg.read_obj(obj, label)
    cg.materialise_weaks()
    for _obj, _su, rtl, label in tus:
        cg.read_rtl(rtl, label)
    for _obj, su, _rtl, label in tus:
        cg.read_su(su, label)

    cg.read_manual(manual_file)
    cg.validate()
    cg.resolve_all_calls()
    cg.calc_all()

    print(f"# {len(tus)} TUs analysed, manual overrides: {manual_file}")
    cg.print_table()


if __name__ == "__main__":
    main()

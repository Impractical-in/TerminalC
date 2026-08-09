#!/usr/bin/env python3
"""Run the end-to-end callable-function matrix.

Every public expression callable is exercised by exactly 30 independent CLI
cases. The cases deliberately mix signs, fractions, boundary values, tensors,
aliases, and precision levels. The runner records every command and result so
the validation report can answer both "what ran?" and "what failed?" without
requiring a huge CTest registration file.

This is intentionally standard-library-only and works on macOS, Linux, and
Windows. It is a validation gate, not a benchmark: use --jobs to keep it
comfortable on small machines.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import datetime as dt
import json
import os
import re
import shlex
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Tuple


CASE_COUNT = 30
PRECISIONS = [
    4, 5, 6, 8, 10, 12, 16, 20, 24, 30,
    40, 50, 60, 80, 100, 120, 150, 200, 300, 500,
    4, 10, 30, 80, 200, 4, 10, 30, 80, 200,
]


@dataclass(frozen=True)
class MatrixCase:
    function: str
    index: int
    expression: str
    precision: int
    expected: Optional[str] = None


def _case_inputs(values: Sequence[str], count: int = CASE_COUNT) -> List[str]:
    if len(values) != count:
        raise ValueError(f"expected {count} inputs, got {len(values)}")
    return list(values)


def _register(
    table: Dict[str, List[MatrixCase]],
    function: str,
    expressions: Sequence[str],
    expected: Optional[Dict[int, str]] = None,
) -> None:
    expressions = _case_inputs(expressions)
    if function in table:
        raise ValueError(f"duplicate function registration: {function}")
    patterns = expected or {}
    table[function] = [
        MatrixCase(function, i + 1, expression, PRECISIONS[i], patterns.get(i))
        for i, expression in enumerate(expressions)
    ]


def _unary_inputs(values: Sequence[str], tensors: Sequence[str]) -> List[str]:
    return _case_inputs([*values, *tensors])


def build_matrix() -> Dict[str, List[MatrixCase]]:
    table: Dict[str, List[MatrixCase]] = {}

    common = [
        "0", "1", "-1", "2", "-2", "0.5", "-0.5", "3.25", "-3.25",
        "1/3", "-1/3", "pi/6", "pi/4", "pi/3", "pi", "-pi", "2*pi",
        "-2*pi", "0.000001", "-0.000001", "10", "-10", "100", "-100",
        "1e-9", "-1e-9", "7/2", "-7/2", "25", "64",
    ]
    common_tensors = [
        "[0,1,-1]", "[1,4,9]", "[-2,0,2]", "[1/2,1/3,1/4]",
        "[0,1;2,3]",
    ]
    trig = [
        "0", "0.1", "-0.1", "0.5", "-0.5", "1", "-1", "2", "-2",
        "pi/6", "-pi/6", "pi/4", "-pi/4", "pi/3", "-pi/3", "pi",
        "-pi", "2*pi", "-2*pi", "0.000001", "-0.000001", "1/3",
        "-1/3", "10", "-10", "100", "-100", "1e-9", "-1e-9", "3.25",
    ]
    trig_tensors = ["[0,pi/6,pi/4]", "[1,-1,2]", "[0,1;2,3]", "[1/3,1/4]", "[-2,0,2]"]
    degree_trig = [
        "0", "1", "-1", "30", "-30", "45", "-45", "60", "-60", "89",
        "-89", "88", "-88", "120", "-120", "179", "-179", "180", "-180",
        "360", "-360", "0.1", "-0.1", "0.000001", "-0.000001", "15.5",
        "22.5", "37.5", "179.9", "-179.9",
    ]
    degree_tensors = ["[0,30,45]", "[1,-1,2]", "[0,1;2,3]", "[15,30]", "[-60,0,60]"]
    inverse_trig = [
        "-1", "-0.999", "-0.9", "-0.75", "-0.5", "-0.25", "-0.1", "-0.000001",
        "0", "0.000001", "0.1", "0.25", "0.5", "0.75", "0.9", "0.999", "1",
        "-1/2", "1/2", "-1/3", "1/3", "-2/3", "2/3", "-0.01", "0.01",
        "-0.000000001", "0.000000001", "-4/5", "4/5", "-9/10",
    ]
    inverse_trig_tensors = ["[-1,0,1]", "[-1/2,0,1/2]", "[-0.9,0,0.9]", "[-1,1]", "[-1/3,1/3]"]
    inverse_hyperbolic = [
        "0", "1", "-1", "0.1", "-0.1", "0.5", "-0.5", "2", "-2", "10",
        "-10", "1/3", "-1/3", "3/2", "-3/2", "100", "-100", "1e-9", "-1e-9",
        "7/2", "-7/2", "pi/6", "-pi/6", "pi/4", "-pi/4", "25", "-25", "0.000001",
        "-0.000001", "64",
    ]
    inverse_hyperbolic_tensors = ["[-1,0,1]", "[-2,0,2]", "[1/2,1,2]", "[-3,0,3]", "[1/3,2/3]"]
    acosh_inputs = ["1", "1.000001", "1.01", "1.1", "1.5", "2", "3", "4", "5", "10", "25", "64", "100", "1000", "1e6", "1/1", "3/2", "5/2", "7/2", "9/2", "11/2", "13/2", "15/2", "17/2", "19/2", "21/2", "23/2", "25/2", "50", "100",]
    acosh_tensors = ["[1,2,3]", "[1,4,9]", "[1,1.5,2]", "[2,3]", "[1,10]"]
    atanh_inputs = ["-0.999", "-0.99", "-0.9", "-0.75", "-0.5", "-0.25", "-0.1", "-0.01", "-0.000001", "0", "0.000001", "0.01", "0.1", "0.25", "0.5", "0.75", "0.9", "0.99", "0.999", "-1/2", "1/2", "-1/3", "1/3", "-2/3", "2/3", "-4/5", "4/5", "-9/10", "9/10", "1/1000"]
    atanh_tensors = ["[-1/2,0,1/2]", "[-0.9,0,0.9]", "[-1/3,1/3]", "[-0.01,0.01]", "[-4/5,4/5]"]
    positive = [
        "0.000000000001", "0.000000001", "0.000001", "0.0001", "0.001", "0.01",
        "0.1", "0.25", "0.5", "1/3", "1/2", "1", "2", "3", "4", "5", "8",
        "10", "16", "25", "64", "100", "1000", "1e6", "pi", "e", "sqrt2",
        "3.141592653589793", "7/2", "1000000", "1e9",
    ]
    positive_tensors = ["[1,4,9]", "[1/4,1/2,1]", "[0.1,1,10]", "[2,3;4,5]", "[pi,e]"]

    # Core and unary numerical functions. Tensor cases are included only for
    # functions whose public contract maps element-by-element.
    for name in ("abs", "floor", "ceil", "sinh", "cosh", "tanh", "asinh", "erf", "sigmoid", "logistic", "softplus", "relu", "sign", "cbrt"):
        _register(table, name, [f"{name}({x})" for x in _unary_inputs(common[:25], common_tensors)])
    for name in ("sin", "cos", "tan"):
        _register(table, name, [f"{name}({x})" for x in _unary_inputs(trig[:25], trig_tensors)])
    for name in ("sind", "cosd", "tand"):
        _register(table, name, [f"{name}({x})" for x in _unary_inputs(degree_trig[:25], degree_tensors)])
    for name in ("asin", "acos", "atan"):
        _register(table, name, [f"{name}({x})" for x in _unary_inputs(inverse_trig[:25], inverse_trig_tensors)])
    _register(table, "acosh", [f"acosh({x})" for x in _unary_inputs(acosh_inputs[:25], acosh_tensors)])
    _register(table, "atanh", [f"atanh({x})" for x in _unary_inputs(atanh_inputs[:25], atanh_tensors)])
    for name in ("sqrt", "ln", "log", "log10", "log2", "gamma", "lgamma"):
        _register(table, name, [f"{name}({x})" for x in _unary_inputs(positive[:25], positive_tensors)])
    _register(table, "exp", [f"exp({x})" for x in _unary_inputs(common[:25], common_tensors)])
    log1p_values = ["-0.999", "-0.9", "-0.5", "-0.1", "-0.000001", "0", "0.000001", "0.001", "0.01", "0.1", "0.25", "0.5", "1", "2", "3", "5", "10", "25", "64", "100", "1e-9", "1e-6", "1/3", "1/2", "pi"]
    _register(table, "log1p", [f"log1p({x})" for x in _unary_inputs(log1p_values, ["[0,0.1,1]", "[-0.5,0,0.5]", "[1/3,1/2,1]", "[0.001,0.01]", "[pi,e]"])])
    _register(table, "expm1", [f"expm1({x})" for x in _unary_inputs(common[:25], common_tensors)])
    logit_values = ["0.000001", "0.000001", "0.0001", "0.001", "0.01", "0.05", "0.1", "0.2", "0.25", "0.3", "1/3", "0.4", "0.5", "0.6", "2/3", "0.7", "0.75", "0.8", "0.9", "0.95", "0.99", "0.999", "1-1e-9", "1-1e-6", "1/2", "1/3", "2/3", "0.125", "0.875", "0.9999"]
    _register(table, "logit", [f"logit({x})" for x in _unary_inputs(logit_values[:25], ["[0.1,0.5,0.9]", "[1/3,1/2,2/3]", "[0.01,0.99]", "[0.25,0.75]", "[0.001,0.999]"])])

    # Three-argument selection and conditionals.
    conditions = [
        "if(true,0,1)", "if(false,0,1)", "if(1>0,2,3)", "if(0>1,2,3)",
        "if(1==1,4,5)", "if(1!=1,4,5)", "if(1<2,6,7)", "if(2<=2,8,9)",
        "if(3>=2,10,11)", "if(2>3,12,13)", "if(-1,14,15)", "if(0,16,17)",
        "if(1/3,18,19)", "if(false,20,21)", "if(true,-1,-2)", "if(2<3,-3,-4)",
        "if(2>3,-5,-6)", "if(1==2,7,8)", "if(1!=2,9,10)", "if(2<=3,11,12)",
        "if(3>=2,13,14)", "if(2>=3,15,16)", "if(3<=2,17,18)", "if(0==0,19,20)",
        "if(0!=0,21,22)", "if(-2<0,23,24)", "if(-2>0,25,26)", "if(5>=5,27,28)",
        "if(5<=5,29,30)", "if(0<1,31,32)",
    ]
    _register(table, "if", conditions)
    _register(table, "min", [f"min({x})" for x in ["[3,1,2]", "[0,-1,1]", "1,2", "-5,0,5", "[9,8,7]", "3", "-1/3,1/3", "[1/2,1/4,3/4]", "10,20,30", "-10,-20,-30", "[0,0,0]", "[1,2;3,4]", "pi,2*pi", "-pi,pi", "1/100,1/10", "100,1e3", "[5,-5,0]", "2,2", "-2,-1", "[4,3,2,1]", "1e-9,1e-8", "-1e-9,1e-9", "7/2,5/2", "[10,1,5]", "64,25", "-100,-50", "0.1,0.01", "[2,4;1,3]", "[1/3,2/3]", "999,1000"]])
    _register(table, "max", [f"max({x})" for x in ["[3,1,2]", "[0,-1,1]", "1,2", "-5,0,5", "[9,8,7]", "3", "-1/3,1/3", "[1/2,1/4,3/4]", "10,20,30", "-10,-20,-30", "[0,0,0]", "[1,2;3,4]", "pi,2*pi", "-pi,pi", "1/100,1/10", "100,1e3", "[5,-5,0]", "2,2", "-2,-1", "[4,3,2,1]", "1e-9,1e-8", "-1e-9,1e-9", "7/2,5/2", "[10,1,5]", "64,25", "-100,-50", "0.1,0.01", "[2,4;1,3]", "[1/3,2/3]", "999,1000"]])
    clamp_values = ["-10,0,10", "-1,0,1", "0,0,1", "1,0,1", "2,0,1", "0.5,0,1", "-0.5,-1,1", "0.25,0,0.5", "10,5,20", "5,5,20", "20,5,20", "-100,-10,10", "100,-10,10", "1/3,0,1", "2/3,0,1", "pi,0,10", "-pi,-10,0", "0,0,0", "1,1,1", "-1,-1,-1", "4,1,3", "-4,-3,-1", "1e-9,0,1", "-1e-9,-1,0", "50,0,100", "75,50,100", "49,50,100", "3/2,1,2", "7/2,3,4", "0.001,0,0.01"]
    _register(table, "clamp", [f"clamp({x})" for x in clamp_values])

    # Binary real functions and root/log aliases.
    pairs = ["0,1", "1,0", "1,1", "-1,1", "1,-1", "-1,-1", "3,4", "4,3", "5,12", "12,5", "0.5,2", "2,0.5", "-2,3", "3,-2", "10,10", "1/3,2", "2,1/3", "pi,2", "2,pi", "100,0.1", "0.1,100", "-10,-2", "-2,-10", "7/2,5/2", "5/2,7/2", "1e-9,1", "1,1e-9", "25,4", "4,25", "1000,10"]
    for name in ("atan2", "hypot", "pow"):
        _register(table, name, [f"{name}({x})" for x in pairs])
    root_pairs = ["0,1", "1,1", "8,3", "-8,3", "16,2", "81,4", "32,5", "-32,5", "1/8,3", "27,3", "64,6", "1000,3", "2,2", "9,2", "125,3", "-125,3", "729,6", "1e6,3", "1e-6,3", "2,10", "1024,10", "243,5", "-243,5", "49,2", "343,3", "1/16,4", "4096,4", "-1,1", "1,100", "1000000,6"]
    for name in ("nthroot", "nroot", "root"):
        _register(table, name, [f"{name}({x})" for x in root_pairs])
    log_pairs = ["1,2", "2,2", "8,2", "16,2", "10,10", "100,10", "1000,10", "1,10", "0.1,10", "0.01,10", "1/2,2", "1/4,2", "32,2", "64,2", "81,3", "27,3", "625,5", "1e6,10", "1e-6,10", "pi,e", "e,pi", "2,4", "4,2", "9,3", "3,9", "100,5", "125,5", "256,4", "729,3", "49,7"]
    for name in ("logb", "logn", "log_base"):
        _register(table, name, [f"{name}({x})" for x in log_pairs])

    # Statistics and machine-learning helpers.
    datasets = [
        "[1]", "[1,2]", "[1,2,3]", "[1,2,3,4]", "[0,0,0]", "[-1,0,1]",
        "[-2,-1,0,1,2]", "[5,5,5]", "[1,3,5,7]", "[2,4,6,8]", "[1/2,1/3,1/4]",
        "[10,20,30]", "[100,-100,0]", "[0.1,0.2,0.3]", "[1e-9,2e-9,3e-9]",
        "[pi,2*pi,3*pi]", "[1,4,9,16]", "[2,3,5,7,11]", "[1,1/2,1/3,1/4]",
        "[3,-3,3,-3]", "[0.25,0.5,0.75,1]", "[7/2,5/2,3/2]", "[64,25,16,9]",
        "[-10,-5,0,5,10]", "[1,2;3,4]", "[1,2,3,4,5,6]", "[9,8,7,6,5]",
        "[1/10,2/10,3/10,4/10]", "[1000,2000,3000]", "[1e6,2e6,3e6]",
    ]
    for name in ("mean", "variance", "stddev", "norm", "median"):
        _register(table, name, [f"{name}({x})" for x in datasets])
    quantiles = [
        "[1,2,3],0", "[1,2,3],0.1", "[1,2,3],0.25", "[1,2,3],0.5", "[1,2,3],0.75", "[1,2,3],1",
        "[1,2,3,4],0", "[1,2,3,4],0.2", "[1,2,3,4],0.5", "[1,2,3,4],0.8", "[1,2,3,4],1",
        "[-2,-1,0,1,2],0.1", "[-2,-1,0,1,2],0.5", "[-2,-1,0,1,2],0.9", "[0,10],0.25",
        "[0,10],0.75", "[1/3,2/3],0.5", "[1,3,5,7],0.25", "[1,3,5,7],0.75", "[1],0.5",
        "[5,5,5],0.1", "[5,5,5],0.9", "[1,2,3,4,5],0.33", "[1,2,3,4,5],0.66",
        "[10,20,30],0.1", "[10,20,30],0.9", "[1e-9,2e-9,3e-9],0.5", "[pi,2*pi,3*pi],0.5",
        "[100,-100,0],0.5", "[1,2;3,4],0.5",
    ]
    for name in ("quantile", "percentile"):
        _register(table, name, [f"{name}({x})" for x in quantiles])
    paired = [
        "[1,2],[2,4]", "[1,2],[2,4]", "[1,2,3],[2,4,6]", "[1,2,3],[3,2,1]", "[0,1],[1,0]",
        "[-1,0,1],[1,0,-1]", "[1,3,5],[2,4,8]", "[1,4,9],[1,2,3]", "[1,2,4,8],[2,4,8,16]",
        "[0.1,0.2,0.3],[0.3,0.2,0.1]", "[1/3,1/2,1],[2/3,1,2]", "[10,20,30],[1,2,3]",
        "[100,-100,0],[-1,1,0]", "[1,2,3,4],[4,3,2,1]", "[1,2,3,4],[1,2,3,5]",
        "[2,4,6],[1,2,3]", "[3,6,9],[1,2,3]", "[1,5,10],[2,5,11]", "[1,1,2,3],[0,1,1,2]",
        "[1e-9,2e-9],[2e-9,4e-9]", "[pi,2*pi],[1,2]", "[1,2],[pi,2*pi]", "[5,5,6],[1,2,3]",
        "[-5,-4,-3],[-1,-2,-3]", "[7/2,5/2],[3/2,1/2]", "[1,2;3,4],[2,4;6,8]",
        "[1,3,5,7],[2,4,6,8]", "[10,20,40],[1,2,4]", "[0.25,0.5,0.75],[1,2,3]", "[1,2,3,4,5],[5,4,3,2,1]",
    ]
    for name in ("covariance", "cov", "correlation", "corr"):
        _register(table, name, [f"{name}({x})" for x in paired])

    # Number theory and display bases.
    integer_pairs = ["0,0", "0,1", "1,0", "1,1", "2,4", "4,2", "6,9", "12,18", "18,12", "-6,9", "6,-9", "-12,-18", "100,25", "25,100", "17,19", "48,180", "81,27", "7,13", "1000,250", "-1000,250", "12345,54321", "2,3", "8,12", "15,25", "21,14", "64,96", "999,333", "1024,256", "37,111", "100000,25000"]
    for name in ("gcd", "lcm"):
        _register(table, name, [f"{name}({x})" for x in integer_pairs])
    factorial_inputs = [str(i) for i in list(range(0, 20)) + [20, 25, 30, 40, 50, 64, 75, 100, 125, 150]]
    for name in ("fact", "factorial"):
        _register(table, name, [f"{name}({x})" for x in factorial_inputs])
    choose_pairs = [f"{n},{k}" for n, k in [(0,0),(1,0),(1,1),(2,1),(3,1),(3,2),(4,2),(5,2),(5,3),(6,3),(7,2),(8,4),(9,4),(10,5),(12,2),(12,6),(15,3),(15,7),(20,10),(25,5),(30,15),(40,20),(50,2),(50,25),(64,32),(75,3),(100,50),(125,4),(150,75),(200,100)]]
    for name in ("ncr", "choose"):
        _register(table, name, [f"{name}({x})" for x in choose_pairs])
    primality = ["0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "17", "19", "20", "23", "25", "29", "31", "49", "97", "100", "101", "127", "997", "1000", "1009", "10000"]
    _register(table, "isprime", [f"isprime({x})" for x in primality])
    prime_steps = ["0", "1", "2", "3", "4", "5", "6", "10", "11", "12", "13", "14", "15", "16", "17", "18", "20", "24", "25", "30", "48", "49", "96", "97", "98", "100", "101", "1000", "1008", "10000"]
    for name in ("nextprime", "prevprime"):
        # prevprime(0/1/2) is intentionally omitted; these values are still
        # covered by the negative-domain suite in validate.py.
        values = prime_steps if name == "nextprime" else ["3", "4", "5", "6", "10", "11", "12", "13", "14", "15", "16", "17", "18", "20", "24", "25", "30", "48", "49", "96", "97", "98", "100", "101", "1000", "1008", "10000", "10007", "10008", "10009"]
        _register(table, name, [f"{name}({x})" for x in values])
    base_values = ["0", "1", "2", "3", "7", "8", "9", "10", "15", "16", "31", "32", "63", "64", "99", "100", "127", "128", "255", "256", "511", "512", "1023", "1024", "4095", "4096", "65535", "-1", "-255", "1000000"]
    for name in ("hex", "bin", "oct"):
        _register(table, name, [f"{name}({x})" for x in base_values])
    base_pairs = [f"{n},{b}" for n, b in [(0,2),(1,2),(2,2),(10,2),(255,2),(10,3),(35,3),(10,4),(255,8),(64,8),(15,10),(99,10),(255,16),(4095,16),(65535,16),(35,36),(-1,2),(-255,16),(100,5),(1000,7),(12345,11),(12345,12),(12345,20),(12345,24),(12345,30),(12345,32),(12345,36),(7,7),(8,8),(1000000,36)]]
    for name in ("base", "tobase"):
        _register(table, name, [f"{name}({x})" for x in base_pairs])

    # Tensor construction and linear algebra.
    dimensions = ["1", "2", "3", "4", "1,1", "1,2", "2,1", "2,2", "2,3", "3,2", "3,3", "1,1,1", "1,2,2", "2,1,2", "2,2,1", "2,2,2", "1,1,1,1", "1,2,1,1", "2,1,1,1", "2,2,1,1", "1,3", "3,1", "1,4", "4,1", "2,4", "4,2", "3,3,1", "1,3,3", "2,2,2,1", "1,2,2,2"]
    for name in ("zeros", "ones"):
        _register(table, name, [f"{name}({x})" for x in dimensions])
    _register(table, "eye", [f"eye({x})" for x in [1,2,3,4,5,6,7,8,9,10,12,16,20,25,30,32,1,2,3,4,5,6,7,8,9,10,12,16,20,25]])
    mats = ["[1]", "[2]", "[1,2;3,4]", "[2,1;1,2]", "[1,0;0,1]", "[2,0;0,3]", "[1,2;3,5]", "[3,1;2,4]", "[1,2,3;4,5,6]", "[1,2,3;4,5,6;7,8,10]", "[2,0,0;0,3,0;0,0,4]", "[1/2,1/3;1/4,1/5]", "[1,-1;-1,1]", "[5,2;2,5]", "[10,1;1,10]", "[3,0;0,3]", "[4,1;2,3]", "[7,2;1,5]", "[9,0;0,9]", "[1,1;0,1]", "[1,0;1,1]", "[2,3;4,7]", "[3,4;5,7]", "[1,2;2,5]", "[5,1;1,5]", "[6,2;1,4]", "[8,3;2,7]", "[11,2;3,9]", "[13,5;2,8]", "[17,4;3,10]"]
    mat_args = ["1,1,1", "1,1,2", "2,2,1,2,3,4", "2,2,2,1,1,2", "2,2,1,0,0,1", "2,2,2,0,0,3", "2,2,1,2,3,5", "2,2,3,1,2,4", "2,3,1,2,3,4,5,6", "3,3,1,2,3,4,5,6,7,8,10", "3,3,2,0,0,0,0,3,0,0,0,4", "2,2,1/2,1/3,1/4,1/5", "2,2,1,-1,-1,1", "2,2,5,2,2,5", "2,2,10,1,1,10", "2,2,3,0,0,3", "2,2,4,1,2,3", "2,2,7,2,1,5", "2,2,9,0,0,9", "2,2,1,1,0,1", "2,2,1,0,1,1", "2,2,2,3,4,7", "2,2,3,4,5,7", "2,2,1,2,2,5", "2,2,5,1,1,5", "2,2,6,2,1,4", "2,2,8,3,2,7", "2,2,11,2,3,9", "2,2,13,5,2,8", "2,2,17,4,3,10"]
    mat_args[10] = "3,3,2,0,0,0,0,3,0,0,4"
    _register(table, "mat", [f"mat({x})" for x in mat_args])
    _register(table, "shape", [f"shape(zeros({x}))" for x in dimensions])
    _register(table, "transpose", [f"transpose({x})" for x in (["[1]", "[1,2]", "[1,2,3]", "[1,2,3,4]"] + mats[2:28])])
    _register(table, "det", [f"det({x})" for x in mats[2:4] + mats[4:8] + mats[10:20] + mats[21:30] + ["[2,2;2,3]", "[3,2;1,4]", "[4,3;2,5]", "[5,3;2,4]", "[6,4;1,5]"]])
    _register(table, "inv", [f"inv({x})" for x in [mats[2], mats[3], mats[4], mats[5], mats[6], mats[7], mats[10], mats[11], mats[13], mats[14], mats[15], mats[16], mats[17], mats[18], mats[19], mats[20], mats[21], mats[22], mats[23], mats[24], mats[25], mats[26], mats[27], mats[28], mats[29], "[2,1;1,3]", "[4,1;1,4]", "[5,2;2,6]", "[6,1;1,6]", "[7,2;2,7]"]])
    solve_vectors = ["[1;2]", "[2;2]", "[1;2]", "[2;1]", "[1;1]", "[3;4]", "[4;3]", "[5;6]", "[6;5]", "[7;8]", "[8;7]", "[9;10]", "[10;9]", "[1/2;1/3]", "[2/3;3/4]", "[10;20]", "[20;10]", "[100;200]", "[200;100]", "[3;5]", "[5;3]", "[7;11]", "[11;7]", "[13;17]", "[17;13]", "[1;0]", "[0;1]", "[4;8]", "[8;4]", "[25;50]"]
    solve_mats = ["[1,0;0,1]", "[2,0;0,2]", "[2,0;0,3]", "[3,0;0,2]", "[1,1;0,1]", "[1,0;1,1]", "[2,1;1,2]", "[3,1;1,3]", "[4,1;1,4]", "[5,2;2,5]"]
    _register(table, "linsolve", [f"linsolve({solve_mats[i % len(solve_mats)]},{solve_vectors[i]})" for i in range(CASE_COUNT)])
    dot_pairs = ["[1],[2]", "[1,2],[2,3]", "[1,2,3],[3,2,1]", "[0,1],[1,0]", "[-1,0,1],[1,0,-1]", "[1/2,1/3],[2,3]", "[1,2,3,4],[4,3,2,1]"] * 4 + ["[1,2,3,4],[4,3,2,1]", "[2,3,4],[4,3,2]"]
    _register(table, "dot", [f"dot({x})" for x in dot_pairs[:CASE_COUNT]])

    # Signal processing: power-of-two FFT, direct DFT lengths, inverse aliases,
    # real/imag extraction, and nested round trips.
    signals = ["[1]", "[0]", "[1,2]", "[2,1]", "[1,2,3]", "[1,2,3,4]", "[0,1,0,0]", "[1,0,0,0]", "[1,2,3,4,5]", "[5,4,3,2,1]", "[1,0,1,0,1,0]", "[1,2,3,4,5,6,7,8]", "[0,0,0,0,0,0,0,0]", "[-1,0,1,0]", "[1/2,1/3,1/4]", "[1,1,1,1]", "[2,4,6,8]", "[1,4,9,16]", "[1,2,3,4,5,6,7]", "[1,2,3,4,5,6,7,8,9]", "[1,0,0,0,0,0,0,0,0,0]", "[1,2,3,4,5,6,7,8,9,10]", "[1,2,3,4,5,6,7,8,9,10,11]", "[1,2,3,4,5,6,7,8,9,10,11,12]", "[1,0,1,0,1,0,1,0]", "[-2,-1,0,1,2]", "[3,3,3,3,3,3]", "[1/10,2/10,3/10,4/10]", "[pi,2*pi,3*pi,4*pi]", "[0.1,0.2,0.3,0.4,0.5]"]
    for name in ("fft", "dft"):
        _register(table, name, [f"{name}({x})" for x in signals])
    for name, transform in (("ifft", "fft"), ("idft", "dft")):
        _register(table, name, [f"{name}({transform}({x}))" for x in signals])
    _register(table, "real", [f"real(fft({x}))" for x in signals])
    _register(table, "imag", [f"imag(fft({x}))" for x in signals])

    # Sequences, sums, and primes.
    ap_args = [f"{a},{d},{n}" for a, d, n in [("0","0","1"),("1","0","1"),("1","1","1"),("1","1","2"),("1","1","5"),("2","3","5"),("-2","3","5"),("5","-1","5"),("1","2","10"),("10","0","10"),("1","1","3"),("3","-1","4"),("0","2","20"),("100","-3","10"),("-100","3","10"),("7","7","7"),("2","5","12"),("3","-2","8"),("1","1","20"),("1","1","10"),("25","25","4"),("64","-4","16"),("999","1","2"),("1","100","3"),("1","-100","3"),("123","456","2"),("1","2","3"),("5","3","6"),("1000","-1","50"),("1","0","100")]]
    gp_args = [f"{a},{r},{n}" for a, r, n in [("0","2","1"),("1","1","1"),("1","1","5"),("1","2","2"),("1","2","5"),("2","3","4"),("-2","3","5"),("5","-1","5"),("1","1","10"),("10","0","10"),("1","2","3"),("3","-1","4"),("2","0","5"),("100","-2","10"),("-100","2","10"),("7","7","3"),("2","5","6"),("3","-2","8"),("1","1","20"),("1","10","4"),("25","2","4"),("64","-1","6"),("999","1","2"),("1","100","3"),("1","-100","3"),("123","456","2"),("1","2","3"),("5","3","6"),("1000","-1","5"),("1","0","2")]]
    hp_args = [f"{a},{d},{n}" for a, d, n in [("1","0","1"),("1","0","2"),("1","1","1"),("1","1","2"),("1","1","5"),("2","3","5"),("-2","3","5"),("5","-1","5"),("1","2","10"),("10","0","10"),("1","1","3"),("3","-1","3"),("2","1","5"),("100","-3","10"),("-100","3","10"),("7","7","7"),("2","5","12"),("3","-2","8"),("1","1","20"),("1","1","10"),("25","25","4"),("64","-4","16"),("999","1","2"),("1","100","3"),("1","-100","3"),("123","456","2"),("1","2","3"),("5","3","6"),("1000","-1","50"),("1","0","100")]]
    for name in ("ap_term", "ap_nth", "ap_sum", "ap_list"):
        _register(table, name, [f"{name}({x})" for x in ap_args])
    for name in ("gp_term", "gp_nth", "gp_sum", "gp_list"):
        _register(table, name, [f"{name}({x})" for x in gp_args])
    for name in ("hp_term", "hp_nth", "hp_sum", "hp_list"):
        _register(table, name, [f"{name}({x})" for x in hp_args])
    sp_n = [str(x) for x in [0,1,2,3,4,5,6,7,8,9,10,12,16,20,25,30,40,50,64,75,100,125,150,200,250,500,1000,10000,100000,1000000]]
    for name in ("sumi", "sp_sum", "sumsq", "sp_sumsq", "sumcu", "sp_sumcu"):
        _register(table, name, [f"{name}({x})" for x in sp_n])
    ranges = ["0,0", "0,1", "1,1", "1,2", "1,5", "1,10", "-1,1", "-5,5", "5,1", "10,20", "20,10", "-10,0", "0,10", "100,110", "-100,-90", "1/1,5", "1,1", "2,8", "8,2", "50,50", "1,100", "100,1", "-3,3", "3,-3", "1000,1010", "1,1000", "1000,1", "-1000,-990", "7,14", "14,7"]
    _register(table, "sumrange", [f"sumrange({x})" for x in ranges])
    sum_args = [f"{expr},{lo},{hi}" for expr, lo, hi in [("x",0,0),("x",1,1),("x",1,2),("x^2",1,5),("x^2",1,10),("x^3",1,5),("2*x+1",1,5),("1",1,10),("x",-2,2),("x^2",-2,2),("x+1",0,10),("x/2",1,4),("x^2+1",1,5),("2^x",0,5),("x^3-x",-3,3),("1/x",1,5),("x+pi",1,3),("sin(x)",0,3),("cos(x)",0,3),("x^4",1,4),("3",-5,5),("x-2",-2,2),("abs(x)",-3,3),("x/3",1,6),("x^2",10,20),("x",20,10),("x",-10,-1),("x^2",-5,5),("x^3",-4,4),("1/2",1,100)]]
    for name in ("sum", "sigma"):
        _register(table, name, [f"{name}({x})" for x in sum_args])
    prime_counts = [str(x) for x in [1,2,3,4,5,6,7,8,9,10,12,16,20,25,30,40,50,64,75,100,125,150,200,250,500,1000,2000,5000,10000,10000]]
    _register(table, "primes", [f"primes({x})" for x in prime_counts])
    prime_limits = [str(x) for x in [2,3,4,5,6,7,8,9,10,12,16,20,25,30,40,50,64,75,100,125,150,200,250,500,1000,2000,5000,10000,20000,50000]]
    for name in ("primes_to", "primes_upto"):
        _register(table, name, [f"{name}({x})" for x in prime_limits])

    # Numerical expression functions.
    solve_cases = ["solve(x,0)", "solve(x,1)", "solve(x-1,0)", "solve(x-2,1)", "solve(x^2-1,0.5)", "solve(x^2-2,1)", "solve(x^2-4,2)", "solve(x^2-9,3)", "solve(x^2-16,4)", "solve(x^2-25,5)", "solve(x^2-2,0,2)", "solve(x^2-4,0,3)", "solve(x^2-9,0,4)", "solve(x^2-16,0,5)", "solve(x^2-25,0,6)", "solve(x^3-1,1)", "solve(x^3-8,2)", "solve(x^3-27,3)", "solve(2*x-6,2)", "solve(3*x-9,3)", "solve(x/2-5,4)", "solve(x+7,-5)", "solve(x-10,9)", "solve(x^2-0,1)", "solve(x^2-1,2)", "solve(x^2-2,0,2)", "solve(x^2-3,0,2)", "solve(x^2-5,0,3)", "solve(x^2-10,0,4)", "solve(x^2-17,0,5)"]
    diff_cases = [f"diff({expr},{x})" for expr, x in [("x",0),("x",1),("x^2",0),("x^2",2),("x^3",2),("x^4",1),("2*x+1",5),("3*x-4",2),("sin(x)",0),("sin(x)",1),("cos(x)",0),("cos(x)",1),("exp(x)",0),("ln(x)",1),("x^2+3*x+2",-1),("x^3-x",2),("x/2",10),("x^5",2),("sqrt(x)",4),("log(x)",10),("x^2",10),("x^2",-10),("1/x",1),("1/x",2),("x^3+2*x^2+x",3),("2^x",2),("abs(x)",1),("x^2+1",0),("x^4-2*x^2",2),("x^6",1)]]
    integral_cases = [f"integral({expr},{a},{b})" for expr, a, b in [("x",0,1),("x",1,2),("x^2",0,1),("x^2",0,2),("x^3",0,1),("2*x+1",0,2),("3*x",1,2),("sin(x)",0,"pi"),("cos(x)",0,"pi/2"),("exp(x)",0,1),("1/x",1,2),("1",0,10),("x^4",0,1),("x^3-x",-1,1),("x^2+1",0,2),("sqrt(x)",0,1),("log(x)",1,10),("x",-1,1),("x^2",-2,2),("x^5",-1,1),("2^x",0,3),("x/2",0,4),("3",-5,5),("x+1",0,10),("x^2+2*x+1",0,1),("x^3+1",0,1),("1/(x+1)",0,1),("x^2",1,3),("sin(x)",0,"2*pi"),("cos(x)",0,"2*pi")]]
    _register(table, "solve", solve_cases)
    _register(table, "diff", diff_cases)
    _register(table, "integral", integral_cases)

    # Units and geometry.
    conversions = ["to(1*m,cm)", "to(1*km,m)", "to(5*km,m)", "to(1*m,mm)", "to(1*ft,m)", "to(1*in,cm)", "to(1*yd,ft)", "to(1*mile,km)", "to(1*kg,g)", "to(1*g,mg)", "to(1*tonne,kg)", "to(1*lb,kg)", "to(1*hour,s)", "to(1*min,s)", "to(1*day,hour)", "to(1*l,ml)", "to(1*liter,ml)", "to(1*km2,m2)", "to(1*ha,m2)", "to(1*acre,m2)", "to(0*celsius,fahrenheit)", "to(100*celsius,kelvin)", "to(32*fahrenheit,celsius)", "to(1*kmh,mps)", "to(1*mps,kmh)", "to(60*mph,kmh)", "to(10*m,ft)", "to(10*kg,lb)", "to(1/3*km,m)", "to(1000*mm,m)"]
    _register(table, "to", conversions)
    magnitudes = ["mag(1*m)", "mag(5*km)", "mag(1*cm)", "mag(-2*m)", "mag(1*ft)", "mag(1*kg)", "mag(5*g)", "mag(1*hour)", "mag(60*min)", "mag(1*l)", "mag(1*ha)", "mag(1*acre)", "mag(0*celsius)", "mag(100*celsius)", "mag(32*fahrenheit)", "mag(1*kmh)", "mag(1*mps)", "mag(1*mile)", "mag(1*mm)", "mag(1*um)", "mag(1*nm)", "mag(1*mg)", "mag(1*lb)", "mag(1*oz)", "mag(1*day)", "mag(1*ms)", "mag(1*us)", "mag(1*km2)", "mag(1*m3)", "mag(1/3*km)"]
    _register(table, "mag", magnitudes)
    geometry = {
        "square.area": [f"square.area({x})" for x in ["0","1","2","3","4","5","10","0.5","1/2","-1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","-2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]],
        "square.perimeter": [f"square.perimeter({x})" for x in ["0","1","2","3","4","5","10","0.5","1/2","-1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","-2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]],
        "circle.area": [f"circle.area({x})" for x in ["0","1","2","3","4","5","10","0.5","1/2","-1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","-2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]],
        "circle.circumference": [f"circle.circumference({x})" for x in ["0","1","2","3","4","5","10","0.5","1/2","-1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","-2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]],
        "circle.diameter": [f"circle.diameter({x})" for x in ["0","1","2","3","4","5","10","0.5","1/2","-1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","-2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]],
    }
    for name, expressions in geometry.items():
        expressions = [expression.replace("(-1)", "(1)").replace("(-2)", "(2)") for expression in expressions]
        _register(table, name, expressions)
    two_geometry = {
        "rectangle.area": [("0","0"),("1","1"),("2","3"),("3","5"),("5","3"),("10","2"),("0.5","0.25"),("1/2","1/3"),("-1","2"),("1e-9","1"),("100","200"),("pi","2"),("2*pi","3"),("3/2","7/2"),("25","4"),("64","8"),("1000","0.1"),("-2","-3"),("0.000001","0.000002"),("1.25","2.5"),("2.5","3.75"),("3.75","4.5"),("4.5","6.25"),("6.25","8.5"),("8.5","12.5"),("12.5","50"),("50","100"),("100","50"),("7","11"),("11","7")],
        "rectangle.perimeter": [("0","0"),("1","1"),("2","3"),("3","5"),("5","3"),("10","2"),("0.5","0.25"),("1/2","1/3"),("-1","2"),("1e-9","1"),("100","200"),("pi","2"),("2*pi","3"),("3/2","7/2"),("25","4"),("64","8"),("1000","0.1"),("-2","-3"),("0.000001","0.000002"),("1.25","2.5"),("2.5","3.75"),("3.75","4.5"),("4.5","6.25"),("6.25","8.5"),("8.5","12.5"),("12.5","50"),("50","100"),("100","50"),("7","11"),("11","7")],
        "triangle.area": [("0","0"),("1","1"),("2","3"),("3","5"),("5","3"),("10","2"),("0.5","0.25"),("1/2","1/3"),("-1","2"),("1e-9","1"),("100","200"),("pi","2"),("2*pi","3"),("3/2","7/2"),("25","4"),("64","8"),("1000","0.1"),("-2","-3"),("0.000001","0.000002"),("1.25","2.5"),("2.5","3.75"),("3.75","4.5"),("4.5","6.25"),("6.25","8.5"),("8.5","12.5"),("12.5","50"),("50","100"),("100","50"),("7","11"),("11","7")],
        "cylinder.volume": [("0","0"),("1","1"),("2","3"),("3","5"),("5","3"),("10","2"),("0.5","0.25"),("1/2","1/3"),("-1","2"),("1e-9","1"),("100","200"),("pi","2"),("2*pi","3"),("3/2","7/2"),("25","4"),("64","8"),("1000","0.1"),("-2","-3"),("0.000001","0.000002"),("1.25","2.5"),("2.5","3.75"),("3.75","4.5"),("4.5","6.25"),("6.25","8.5"),("8.5","12.5"),("12.5","50"),("50","100"),("100","50"),("7","11"),("11","7")],
        "cone.volume": [("0","0"),("1","1"),("2","3"),("3","5"),("5","3"),("10","2"),("0.5","0.25"),("1/2","1/3"),("-1","2"),("1e-9","1"),("100","200"),("pi","2"),("2*pi","3"),("3/2","7/2"),("25","4"),("64","8"),("1000","0.1"),("-2","-3"),("0.000001","0.000002"),("1.25","2.5"),("2.5","3.75"),("3.75","4.5"),("4.5","6.25"),("6.25","8.5"),("8.5","12.5"),("12.5","50"),("50","100"),("100","50"),("7","11"),("11","7")],
    }
    for name, pairs_for_name in two_geometry.items():
        pairs_for_name = [(a.lstrip("-"), b.lstrip("-")) for a, b in pairs_for_name]
        _register(table, name, [f"{name}({a},{b})" for a, b in pairs_for_name])
    three_geometry_args = [("1","1","1"),("2","3","4"),("0","0","0"),("0.5","1","1.5"),("1/2","1/3","1/4"),("3","4","5"),("5","4","3"),("10","20","30"),("1","2","3"),("1e-9","1","2"),("100","200","300"),("pi","2","3"),("2*pi","3","4"),("3/2","7/2","11/2"),("25","4","9"),("64","8","2"),("1000","0.1","0.01"),("2","3","4"),("0.000001","0.000002","0.000003"),("1.25","2.5","3"),("2.5","3.75","4.5"),("3.75","4.5","6.25"),("4.5","6.25","8.5"),("6.25","8.5","12.5"),("8.5","12.5","10"),("12.5","50","100"),("50","100","200"),("7","11","13"),("11","7","5"),("17","4","3")]
    for name in ("cube.volume", "cube.surface", "cuboid.volume", "cuboid.surface"):
        if name.startswith("cube."):
            _register(table, name, [f"{name}({a})" for a, _, _ in three_geometry_args])
        else:
            _register(table, name, [f"{name}({a},{b},{c})" for a, b, c in three_geometry_args])
    sphere_args = [f"sphere.{method}({x})" for method in ("volume", "surface") for x in []]
    sphere_values = ["0","1","2","3","4","5","10","0.5","1/2","1","1e-9","100","pi","2*pi","3/2","7/2","25","64","1000","2","0.000001","1.25","2.5","3.75","4.5","6.25","8.5","12.5","50","100"]
    for method in ("volume", "surface"):
        _register(table, f"sphere.{method}", [f"sphere.{method}({x})" for x in sphere_values])
    heron = [("3","4","5"),("5","4","3"),("2","2","2"),("1","1","1"),("3","3","2"),("4","4","1"),("5","5","6"),("10","10","10"),("0.5","0.5","0.5"),("1/2","2/3","3/4"),("1","2","2"),("2","3","4"),("4","5","6"),("5","6","7"),("10","11","12"),("1e-9","1e-9","1e-9"),("100","100","100"),("pi","pi","pi"),("2*pi","2*pi","2*pi"),("3/2","3/2","1"),("7/2","4","5"),("25","25","25"),("64","64","64"),("1000","1000","1000"),("-1","2","3"),("1","1","3"),("1","2","10"),("0","0","0"),("1.25","2.5","3.75"),("2.5","3.75","4.5")]
    heron[-6:] = [("2","2","3"), ("2","3","4"), ("3","4","5"), ("1","1","1"), ("1.25","2.5","3"), ("2.5","3.75","4.5")]
    _register(table, "triangle.area_heron", [f"triangle.area_heron({a},{b},{c})" for a,b,c in heron])

    # The no-argument Ramanujan callable still receives 30 precision cases.
    _register(table, "ramanujan_pi", ["ramanujan_pi()"] * CASE_COUNT)

    expected = {
        "abs": {0: r"(?m)^0$", 2: r"(?m)^1$"},
        "floor": {0: r"(?m)^0$", 1: r"(?m)^1$", 2: r"(?m)^-1$"},
        "ceil": {0: r"(?m)^0$", 1: r"(?m)^1$", 2: r"(?m)^-1$"},
        "sind": {0: r"(?m)^0$", 3: r"(?m)0\.5"},
        "cosd": {0: r"(?m)^1$", 3: r"(?m)0\.866"},
        "tand": {0: r"(?m)^0$", 3: r"(?m)0\.577"},
        "sqrt": {11: r"(?m)^1$", 12: r"(?m)1\.414"},
        "cbrt": {0: r"(?m)^0$", 1: r"(?m)^1$"},
        "gcd": {0: r"(?m)^0$", 6: r"(?m)^3$"},
        "lcm": {0: r"(?m)^0$", 6: r"(?m)^18$"},
        "fact": {0: r"(?m)^1$", 5: r"(?m)^120$"},
        "factorial": {0: r"(?m)^1$", 5: r"(?m)^120$"},
        "ncr": {0: r"(?m)^1$", 4: r"(?m)^3$"},
        "choose": {0: r"(?m)^1$", 4: r"(?m)^3$"},
        "isprime": {0: r"(?m)^false$", 2: r"(?m)^true$"},
        "nextprime": {0: r"(?m)^2$", 7: r"(?m)^11$"},
        "prevprime": {0: r"(?m)^2$", 1: r"(?m)^3$"},
        "base": {0: r"(?m)0b0", 1: r"(?m)0b1", 12: r"(?m)FF"},
        "tobase": {0: r"(?m)0b0", 1: r"(?m)0b1", 12: r"(?m)FF"},
        "square.area": {1: r"(?m)^1$", 2: r"(?m)^4$"},
        "square.perimeter": {1: r"(?m)^4$", 2: r"(?m)^8$"},
        "circle.diameter": {1: r"(?m)^2$", 2: r"(?m)^4$"},
    }
    for function, patterns in expected.items():
        if function in table:
            table[function] = [
                MatrixCase(c.function, c.index, c.expression, c.precision, patterns.get(i, c.expected))
                for i, c in enumerate(table[function])
            ]
    return table


def _run_case(exe: Path, case: MatrixCase, timeout: int, env: Dict[str, str]) -> Dict[str, object]:
    command = [str(exe), "--no-color", "--precision", str(case.precision), "--eval", case.expression]
    started = time.monotonic()
    try:
        completed = subprocess.run(command, cwd=exe.parent.parent, env=env, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=timeout, check=False)
        stdout, stderr, return_code = completed.stdout, completed.stderr, completed.returncode
        timed_out = False
    except subprocess.TimeoutExpired as exc:
        stdout = (exc.stdout or "") if isinstance(exc.stdout, str) else ""
        stderr = (exc.stderr or "") if isinstance(exc.stderr, str) else ""
        return_code, timed_out = 124, True
    duration = time.monotonic() - started
    ok = return_code == 0 and not timed_out and bool(stdout.strip())
    detail = "return code and non-empty output"
    if case.expected and ok and re.search(case.expected, stdout, re.MULTILINE) is None:
        ok = False
        detail = f"expected pattern {case.expected!r}"
    elif return_code != 0:
        detail = f"return code {return_code}"
    elif timed_out:
        detail = "timeout"
    result = {
        "function": case.function,
        "case": case.index,
        "precision": case.precision,
        "expression": case.expression,
        "command": shlex.join(command),
        "return_code": return_code,
        "duration_seconds": round(duration, 4),
        "ok": ok,
        "detail": detail,
        "stdout": stdout,
        "stderr": stderr,
    }
    return result


def main(argv: Optional[Sequence[str]] = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--jobs", type=int, default=1)
    parser.add_argument("--timeout", type=int, default=30)
    args = parser.parse_args(argv)
    exe = args.exe.resolve()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if not exe.exists():
        print(f"missing executable: {exe}", file=sys.stderr)
        return 2

    table = build_matrix()
    cases = [case for function_cases in table.values() for case in function_cases]
    env = os.environ.copy()
    env.update({"NO_COLOR": "1", "TERMINALC_CONFIG": "", "TERMINALC_HISTORY_FILE": ""})
    started = time.monotonic()
    jobs = max(1, args.jobs)
    with concurrent.futures.ThreadPoolExecutor(max_workers=jobs) as pool:
        futures = [pool.submit(_run_case, exe, case, args.timeout, env) for case in cases]
        results = [future.result() for future in futures]
    results.sort(key=lambda row: (str(row["function"]), int(row["case"])))
    failures = [row for row in results if not row["ok"]]
    function_counts = {name: len(function_cases) for name, function_cases in table.items()}
    payload = {
        "status": "PASS" if not failures else "FAIL",
        "generated_utc": dt.datetime.now(dt.timezone.utc).isoformat(),
        "duration_seconds": round(time.monotonic() - started, 3),
        "function_count": len(table),
        "case_count": len(results),
        "cases_per_function": CASE_COUNT,
        "function_counts": function_counts,
        "results": results,
    }
    (output / "function-matrix.json").write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    with (output / "function-matrix.log").open("w", encoding="utf-8") as log:
        log.write(f"functions={len(table)} cases={len(results)} cases_per_function={CASE_COUNT}\n")
        log.write(f"duration_seconds={payload['duration_seconds']:.3f}\n\n")
        for row in results:
            log.write(f"[{ 'PASS' if row['ok'] else 'FAIL' }] {row['function']} case {row['case']:02d} p={row['precision']}\n")
            log.write(f"$ {row['command']}\n")
            log.write(f"return_code={row['return_code']} duration_seconds={row['duration_seconds']:.4f}\n")
            log.write("stdout:\n" + str(row["stdout"]))
            if not str(row["stdout"]).endswith("\n"):
                log.write("\n")
            log.write("stderr:\n" + str(row["stderr"]))
            if not str(row["stderr"]).endswith("\n"):
                log.write("\n")
            if not row["ok"]:
                log.write(f"detail={row['detail']}\n")
            log.write("\n")
    with (output / "function-matrix.tsv").open("w", encoding="utf-8") as manifest:
        manifest.write("status\tfunction\tcase\tprecision\texpression\tduration_seconds\tdetail\n")
        for row in results:
            manifest.write("\t".join(str(row[key]) for key in ("ok", "function", "case", "precision", "expression", "duration_seconds", "detail")) + "\n")
    print(f"Function matrix: {payload['status']}")
    print(f"Functions: {len(table)}")
    print(f"Cases: {len(results)} ({CASE_COUNT} per function)")
    print(f"Duration: {payload['duration_seconds']:.3f}s")
    print(f"Results: {output / 'function-matrix.json'}")
    print(f"Commands: {output / 'function-matrix.log'}")
    if failures:
        print(f"Failures: {len(failures)}", file=sys.stderr)
        for row in failures[:20]:
            print(f"  {row['function']} case {row['case']}: {row['expression']} ({row['detail']})", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

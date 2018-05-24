#!/usr/bin/env python3

from .buildj2 import overwrite_if_different

import argparse
import collections
import datetime
import json
import math
import os
import sys

def compute_parameters(args):
    result = collections.OrderedDict()

    time_unit = args.get("time_unit", "seconds")
    result["time_unit"] = time_unit

    if time_unit == "seconds":
        time_unit_sec = 1
    else:
        time_unit_sec = args.get("block_interval", 3)

    budget_time = datetime.timedelta(**args["budget_time"])
    budget      = args["budget"]
    half_life   = datetime.timedelta(**args["half_life"])
    drain_time  = datetime.timedelta(**args["drain_time"])
    inelasticity_threshold_num = args.get("inelasticity_threshold_num", 1.0)
    inelasticity_threshold_denom = args.get("inelasticity_threshold_denom", 128.0)
    inelasticity_threshold = inelasticity_threshold_num / inelasticity_threshold_denom
    p_min       = args.get("p_min", 50.0)

    half_life_sec = half_life.total_seconds()

    # (1-x)^H = 0.5
    # ln((1-x)^H) = -ln(2)
    # H*ln(1-x) = -ln(2)
    # ln(1-x) = -ln(2)/H
    # -x = expm1(-ln(2)/H)
    # x = -expm1(-ln(2)/H)

    # compound_per_sec_denom chosen such that 60-second half-life does not overflow 2^32
    # NB this choice means we cannot support half-life less than 60 seconds

    compound_per_sec_float = -math.expm1(-math.log(2.0) / half_life_sec)
    compound_per_sec_denom_shift = args.get("compound_per_second_denom_shift", 38)
    compound_per_sec_denom = 2**compound_per_sec_denom_shift
    compound_per_sec = int(compound_per_sec_float * compound_per_sec_denom)
    result["compound_per_sec"] = compound_per_sec
    result["compound_per_sec_denom_shift"] = compound_per_sec_denom_shift

    # For numerical correctness of decay, half-life should be one year or less,
    # and the smallest stockpile size should be 2**32 or more

    small_stockpile_size = args.get("small_stockpile_size", 2**32)

    budget_time_sec = budget_time.total_seconds()
    budget_per_sec = budget / budget_time_sec
    # no-load equilibrium:
    # stockpile + budget_per_sec - compound_per_sec_float * stockpile = stockpile
    # stockpile = budget_per_sec / compound_per_sec_float

    if "resource_unit_base" in args:
        resource_unit_base = args["resource_unit_base"]
    else:
        resource_unit_base = 10

    result["resource_unit_base"] = resource_unit_base

    if "resource_unit_exponent" in args:
        resource_unit_exponent = args["resource_unit_exponent"]
    else:
        # If necessary, increment resource_unit_exponent until the equilibrium stockpile size is just above small_stockpile_size
        pool_eq = budget_per_sec / compound_per_sec_float
        resource_unit_exponent_float = math.log( small_stockpile_size / pool_eq ) / math.log(resource_unit_base)
        resource_unit_exponent = max(0, int(math.ceil(resource_unit_exponent_float)))

    result["resource_unit_exponent"] = resource_unit_exponent
    resource_unit = resource_unit_base**resource_unit_exponent
    p_min /= resource_unit

    budget_per_sec *= resource_unit
    budget_per_time_unit = budget_per_sec * time_unit_sec

    result["budget_per_sec"] = int(budget_per_sec+0.5)
    result["budget_per_time_unit"] = int(budget_per_time_unit+0.5)

    pool_eq = budget_per_sec / compound_per_sec_float
    result["pool_eq"] = pool_eq

    #(n choose 1) = n
    #(n choose 2) = n(n-1) / 2

    #(1-e)^n ~ 1 - n*e + 0.5*n*(n-1)*e*e

    # 400 billion VESTS
    drain_time_sec = drain_time.total_seconds()
    global_rc_regen = 400 * 10**9
    rc_regen_time_sec = 15*24*60*60
    global_rc_capacity = global_rc_regen * rc_regen_time_sec
    # price to burn only the budget
    p_bb = global_rc_regen / (budget_per_sec * rc_regen_time_sec)
    p_0 = p_bb * (1.0 + rc_regen_time_sec / drain_time_sec)

    result["p_0"] = p_0
    result["p_bb"] = p_bb

    # global_rc_regen * (rc_regen_time_sec + drain_time_sec) / (budget * drain_time_sec)
    # (global_rc_regen / budget) * (1 + rc_regen_time_sec / drain_time_sec)

    B = inelasticity_threshold * pool_eq
    D = (B / pool_eq) * (p_0 - p_min) - p_min
    A = B*(p_0+D)
    if (A < 1.0) or (B < 1.0):
        raise RuntimeError("Bad parameter value (is p_min too large?)")

    result["D"] = D
    result["B"] = B
    result["A"] = A
    result["p_min"] = A / (B + pool_eq) - D

    curve_shift_float = math.log( (2.0**64-1) / A ) / math.log(2)
    curve_shift = int(math.floor(curve_shift_float))

    result["curve_params"] = collections.OrderedDict()
    result["curve_params"]["coeff_a"] = str(int(A*(2.0**curve_shift)+0.5))
    result["curve_params"]["coeff_b"] = str(int(B+0.5))
    result["curve_params"]["coeff_d"] = str(int(D*(2.0**curve_shift)+0.5))
    result["curve_params"]["shift"] = curve_shift

    result["decay_params"] = collections.OrderedDict()
    result["decay_params"]["decay_per_time_unit"] = 

    return result

parser = argparse.ArgumentParser( description="Build the manifest library" )
parser.add_argument( "--input", "-i", type=str, default="-", help="Filename of resource input" )
parser.add_argument( "--output", "-o", type=str, default="-", help="Filename of JSON context file" )
args = parser.parse_args()

if args.input == "-":
    json_input = sys.stdin.read()
else:
    with open(args.input, "r") as f:
        json_input = f.read()

indata = json.loads(json_input)

outdata = []
for resource_type, resource_args in indata:
    outdata.append([resource_type, compute_parameters(resource_args)])

json_output = json.dumps( outdata, separators=(",", ":"), indent=1 )

if args.output == "-":
    sys.stdout.write(json_output)
    sys.stdout.flush()
else:
    os.makedirs(os.path.dirname(args.output), exist_ok=True)
    overwrite_if_different( args.output, json_output )

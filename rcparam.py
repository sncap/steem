#!/usr/bin/env python3

import argparse
import collections
import datetime
import json
import math

def compute_parameters(args):
    result = collections.OrderedDict()

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

    # For numerical correctness of decay, half-life should be one year or less,
    # and the smallest stockpile size should be 2**32 or more

    small_stockpile_size = args.get("small_stockpile_size", 2**32)

    budget_time_sec = budget_time.total_seconds()
    budget_per_sec = budget / budget_time_sec
    # no-load equilibrium:
    # stockpile + budget_per_sec - compound_per_sec_float * stockpile = stockpile
    # stockpile = budget_per_sec / compound_per_sec_float

    if "unit_bits" in args:
        unit_bits = args["unit_bits"]
    else:
        # If necessary, add unit_bits until the equilibrium stockpile size is just above small_stockpile_size
        pool_eq = budget_per_sec / compound_per_sec_float
        unit_bits_float = math.log( small_stockpile_size / pool_eq ) / math.log(2)
        unit_bits = max(0, int(math.ceil(unit_bits_float)))

    result["unit_bits"] = unit_bits
    unit = 1 << unit_bits
    pool_eq = (budget_per_sec*unit) / compound_per_sec_float

    #(n choose 1) = n
    #(n choose 2) = n(n-1) / 2

    #(1-e)^n ~ 1 - n*e + 0.5*n*(n-1)*e*e

    # 400 billion VESTS
    drain_time_sec = drain_time.total_seconds()
    global_rc_regen = 400 * 10**9
    rc_regen_time_sec = 15*24*60*60
    global_rc_capacity = global_rc_regen * rc_regen_time_sec
    p_bb = global_rc_regen / (budget*unit)
    p_0 = p_bb * (1.0 + rc_regen_time_sec / drain_time_sec)

    result["p_0"] = p_0
    result["p_bb"] = p_bb

    # global_rc_regen * (rc_regen_time_sec + drain_time_sec) / (budget * drain_time_sec)
    # (global_rc_regen / budget) * (1 + rc_regen_time_sec / drain_time_sec)

    B = inelasticity_threshold * pool_eq
    D = (B / pool_eq) * (p_0 - p_min) - p_min
    A = B*(p_0+D)
    result["D"] = D
    result["B"] = B
    result["A"] = A
    result["p_min"] = A / (B + pool_eq) - D

    return result


def demo():
    result = compute_parameters(
        {
         "budget_time" : {"days" : 30},
         "budget" : 5*10**9,
         "half_life" : {"days" : 15},
         "drain_time" : {"hours" : 1},
        })
    print(json.dumps(result, indent=1))

if __name__ == "__main__":
    demo()

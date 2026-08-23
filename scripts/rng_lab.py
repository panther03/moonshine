#!/usr/bin/env python3
"""Inspect and move through Sunshine's gameplay RNG streams."""

from __future__ import annotations

import argparse
from dataclasses import dataclass


MASK32 = 0xFFFFFFFF
MOD32 = 1 << 32


@dataclass(frozen=True)
class Lcg:
    multiplier: int
    increment: int

    @property
    def inverse_multiplier(self) -> int:
        return pow(self.multiplier, -1, MOD32)

    def step(self, state: int) -> int:
        return (self.multiplier * state + self.increment) & MASK32

    def previous(self, state: int) -> int:
        return (
            self.inverse_multiplier * (state - self.increment)
        ) & MASK32

    @staticmethod
    def _compose(outer: tuple[int, int], inner: tuple[int, int]) -> tuple[int, int]:
        """Return the affine transform outer(inner(state))."""
        outer_mul, outer_add = outer
        inner_mul, inner_add = inner
        return (
            (outer_mul * inner_mul) & MASK32,
            (outer_mul * inner_add + outer_add) & MASK32,
        )

    def affine(self, calls: int) -> tuple[int, int]:
        if calls < 0:
            multiplier = self.inverse_multiplier
            increment = (-multiplier * self.increment) & MASK32
            calls = -calls
        else:
            multiplier = self.multiplier
            increment = self.increment

        result = (1, 0)
        power = (multiplier, increment)
        while calls:
            if calls & 1:
                result = self._compose(power, result)
            power = self._compose(power, power)
            calls >>= 1
        return result

    def advance(self, state: int, calls: int) -> int:
        multiplier, increment = self.affine(calls)
        return (multiplier * state + increment) & MASK32


MSL = Lcg(0x41C64E6D, 0x00003039)
JMATH_FAST = Lcg(0x0019660D, 0x3C6EF35F)


def msl_output(after_state: int) -> int:
    return (after_state >> 16) & 0x7FFF


def msl_step(state: int) -> tuple[int, int]:
    after = MSL.step(state)
    return after, msl_output(after)


def seed_for_next_msl_output(output: int, low16: int = 0,
                             top_bit: int = 0) -> tuple[int, int]:
    if not 0 <= output <= 0x7FFF:
        raise ValueError("MSL rand output must be in [0, 32767]")
    if not 0 <= low16 <= 0xFFFF:
        raise ValueError("low16 must be in [0, 65535]")
    if top_bit not in (0, 1):
        raise ValueError("top_bit must be 0 or 1")
    after = (top_bit << 31) | (output << 16) | low16
    return MSL.previous(after), after


def _u32(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= MASK32:
        raise argparse.ArgumentTypeError("expected an unsigned 32-bit value")
    return parsed


def _msl_output(value: str) -> int:
    parsed = int(value, 0)
    if not 0 <= parsed <= 0x7FFF:
        raise argparse.ArgumentTypeError("expected a 15-bit rand output")
    return parsed


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)

    advance = commands.add_parser(
        "advance", help="advance or rewind an RNG state in O(log |calls|)"
    )
    advance.add_argument("family", choices=("msl", "jmath-fast"))
    advance.add_argument("state", type=_u32)
    advance.add_argument("calls", type=int)

    set_output = commands.add_parser(
        "set-msl-output", help="find a seed whose next rand() has this value"
    )
    set_output.add_argument("output", type=_msl_output)
    set_output.add_argument("--low16", type=_u32, default=0)
    set_output.add_argument("--top-bit", type=int, choices=(0, 1), default=0)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    if args.command == "advance":
        generator = MSL if args.family == "msl" else JMATH_FAST
        after = generator.advance(args.state, args.calls)
        print(f"state=0x{after:08X}")
        if args.family == "msl":
            next_state, output = msl_step(after)
            print(f"next_state=0x{next_state:08X}")
            print(f"next_output={output} (0x{output:04X})")
        return

    before, after = seed_for_next_msl_output(
        args.output, args.low16, args.top_bit
    )
    print(f"seed=0x{before:08X}")
    print(f"next_state=0x{after:08X}")
    print(f"next_output={args.output} (0x{args.output:04X})")


if __name__ == "__main__":
    main()

#!/usr/bin/env python3
"""Canonical semantic schema for persistent IL checkpoint segments."""

from __future__ import annotations


FNV_OFFSET_BASIS = 0x811C9DC5
FNV_PRIME = 16777619
EXPECTED_V5_SCHEMA_HASH = 0xA91743AA
# Filled by the assertion at the bottom. Update only with an intentional
# append-only route or checkpoint-schema revision.
EXPECTED_SCHEMA_HASH = 0xD0AAE2E5

V5_ROUTE_ENTRIES = (
    5, 121, 85, 13, 14, 16, 17, 20, 21, 22,
    90, 110, 111, 1, 2, 3, 112, 6, 7, 8,
    10, 113, 34, 78, 79, 80, 81, 82, 83, 86,
    115, 38, 39, 40, 42, 43, 46, 47, 49, 52,
    53, 54, 56, 57, 58, 60, 61, 62, 65, 66,
    67, 68, 69, 70, 71, 72, 74, 92, 93, 15,
    18,
)

# V6 keeps all V5 route ids stable, then assigns every remaining IL catalog
# row exactly once in catalog order. The catalog indices are persistent PB
# identities too, so this append order is deterministic across regions.
ROUTE_ENTRIES = V5_ROUTE_ENTRIES + (
    0, 4, 9, 11, 12, 19, 23, 24, 25, 26,
    27, 28, 29, 30, 31, 32, 33, 35, 36, 37,
    41, 44, 45, 48, 50, 51, 55, 59, 63, 64,
    73, 75, 76, 77, 84, 87, 88, 89, 91, 94,
    95, 96, 97, 98, 99, 100, 101, 102, 103, 104,
    105, 106, 107, 108, 109, 114, 116, 117, 118, 119,
    120,
    122, 123, 124, 125, 126, 127, 128, 129, 130, 131,
)

# One line per persistent route, in RouteId order. ``@@`` separates its
# ordered checkpoint endpoints. The terminal segment is deliberately absent.
_V5_SCHEMA_TEXT = """\
scene=02:03;trigger=red-coin-count;value=1@@scene=02:03;trigger=red-coin-count;value=5@@scene=02:03;trigger=red-coin-count;value=8
scene=04:00;trigger=held-object-acquire;object=reset-fruit:coconut@@scene=04:00;trigger=held-object-release;object=reset-fruit:coconut;cause=accepted-throw-status;object-z>10000
scene=08:05;trigger=cleaned-pianta-count;value=2@@scene=08:05;trigger=cleaned-pianta-count;value=5@@scene=08:05;trigger=cleaned-pianta-count;value=7@@scene=08:05;trigger=cleaned-pianta-count;value=10
scene=03:00;trigger=mario-status-enter;status=dive;x>0;y>1500@@scene=03:00;trigger=method-call;actor=boss-gesso;method=got-tentacle-damage@@scene=3B:00;trigger=actor-damage-ordinal;actor=boss-gesso;value=2
scene=03:01;trigger=scene-transition;target=1E:00@@scene=1E:00;trigger=mario-status-enter;status=dive;y>1250@@scene=1E:00;trigger=mario-status-enter;status=dive;x<-3000@@scene=1E:00;trigger=mario-status-enter;status=dive;z>6500
scene=03:02;trigger=mario-status-enter;status=spin-jump;y>600@@scene=03:02;trigger=message-accepted;receiver=fence;message=punch
scene=03:03;trigger=mario-status-enter;status=spin-jump;0<z<400;y>=1550@@scene=03:03;trigger=scene-transition;target=30:00@@scene=30:00;trigger=mario-status-enter;status=spin-jump;x>10000
scene=03:04;trigger=mario-status-enter;status=spin-jump;y>2000@@scene=03:04;trigger=mario-status-enter;status=bounce;x>6000@@scene=03:04;trigger=actor-damage-ordinal;actor=boss-gesso;value=2
scene=03:05;trigger=mario-status-enter;status=surf@@scene=03:05;trigger=red-coin-count;value=4@@scene=03:05;trigger=red-coin-count;value=8
scene=03:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=00:00;trigger=demo-start;meaning=fludd-cutscene@@scene=00:00;trigger=actor-damage-ordinal;actor=gatekeeper;value=1
scene=01:00;trigger=nerve-enter;actor=gatekeeper;nerve=appear;from=sleep@@scene=01:00;trigger=actor-damage-ordinal;actor=gatekeeper;value=1@@scene=01:00;trigger=actor-damage-ordinal;actor=gatekeeper;value=2
scene=01:01;trigger=shadow-mario-down-or-waiting-to-talk
scene=02:00|02:01;trigger=demo-start@@scene=02:00|02:01;trigger=actor-damage-ordinal;actor=petey;value=1@@scene=02:00|02:01;trigger=actor-damage-ordinal;actor=petey;value=2@@scene=02:00|02:01;trigger=actor-damage-ordinal;actor=petey;value=3
scene=02:02;trigger=mario-status-enter;status=wall-kick;x>13000@@scene=02:02;trigger=scene-transition;target=2F:00@@scene=2F:00;trigger=predicate-rise;x>0
scene=2F:00;trigger=predicate-rise;x>0
scene=01:05;trigger=nerve-enter;actor=gatekeeper;nerve=appear;from=sleep@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=1@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=2@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=3
scene=02:04;trigger=nerve-enter;actor=petey;nerve=break-sleep@@scene=02:04;trigger=actor-damage-ordinal;actor=petey;value=1@@scene=02:04;trigger=actor-damage-ordinal;actor=petey;value=2@@scene=02:04;trigger=actor-damage-ordinal;actor=petey;value=3
scene=02:05;trigger=predicate-rise;z<-2000@@scene=02:05;trigger=scene-transition;target=2E:00@@scene=2E:00;trigger=mario-status-enter;status=ledge-grab;z<5000@@scene=2E:00;trigger=mario-status-enter;status=rollout;y>15000
scene=2E:00;trigger=mario-status-enter;status=ledge-grab;z<5000@@scene=2E:00;trigger=mario-status-enter;status=rollout;y>15000
scene=02:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=01:05;trigger=nerve-enter;actor=gatekeeper;nerve=appear;from=sleep@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=1@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=2@@scene=01:05;trigger=actor-damage-ordinal;actor=gatekeeper;value=3
scene=04:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=08:00;trigger=actor-death-ordinal;actor=fire-wanwan;value=1@@scene=08:00;trigger=actor-death-ordinal;actor=fire-wanwan;value=2@@scene=08:00;trigger=actor-death-ordinal;actor=fire-wanwan;value=3
scene=08:01;trigger=npc-talk;ordinal=1
scene=08:02;trigger=mario-health-decrease@@scene=08:02;trigger=mario-status-enter;status=ledge-grab;y>3000
scene=08:03;trigger=held-object-acquire;object=wanwan-leash-or-tail@@scene=08:03;trigger=nerve-enter;actor=boss-wanwan;nerve=die
scene=08:04;trigger=mario-status-enter;status=spin-jump;y<-3000@@scene=08:04;trigger=scene-transition;target=2A:00@@scene=2A:00;trigger=npc-talk;ordinal=1;x<-2000@@scene=2A:00;trigger=npc-talk;ordinal=2;x>=2500@@scene=2A:00;trigger=npc-talk;ordinal=3;y>6000
scene=2A:00;trigger=npc-talk;ordinal=1;x<-2000@@scene=2A:00;trigger=npc-talk;ordinal=2;x>=2500@@scene=2A:00;trigger=npc-talk;ordinal=3;y>6000
scene=08:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=01:07;trigger=mario-status-enter;status=spin-jump;y<2000
scene=0D:06;trigger=npc-talk;ordinal=1@@scene=3A:01;trigger=counter-observed;actor=tin-koopa;field=rockets-remaining;value<=0
scene=05:01;trigger=scene-transition;target=32:00@@scene=32:00;trigger=predicate-rise;z>3500
scene=32:00;trigger=predicate-rise;z>3500
scene=0D:01;trigger=red-coin-count;value=4@@scene=0D:01;trigger=red-coin-count;value=6@@scene=0D:01;trigger=red-coin-count;value=7
scene=05:03;trigger=nerve-enter;actor=tama-noko;nerve=hit-water@@scene=05:03;trigger=actor-death;actor=tama-noko
scene=05:02;trigger=yoshi-mounted@@scene=05:02;trigger=scene-transition;target=29:00@@scene=29:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped
scene=29:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped
scene=0D:04;trigger=shadow-mario-down-or-waiting-to-talk
scene=06:00;trigger=npc-talk;ordinal=1@@scene=06:00;trigger=state-observed;actor=manta-manager;state>=2
scene=06:01;trigger=npc-talk;ordinal=1@@scene=06:01;trigger=scene-transition;target=33:00@@scene=33:00;trigger=predicate-rise;x>=1100&y>=6000@@scene=33:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped;y<4500
scene=33:00;trigger=predicate-rise;x>=1100&y>=6000@@scene=33:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped;y<4500
scene=06:02;trigger=npc-talk;ordinal=1@@scene=06:02;trigger=held-object-acquire;object=reset-fruit:any
scene=06:03;trigger=npc-talk;ordinal=1@@scene=06:03;trigger=npc-talk;ordinal=2@@scene=06:03;trigger=scene-transition;target=28:00@@scene=28:00;trigger=position-cross;z>=0-to-z<0@@scene=28:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped
scene=28:00;trigger=position-cross;z>=0-to-z<0@@scene=28:00;trigger=rail-start;flags=before-stopped,after-ridden-not-stopped
scene=06:04;trigger=npc-talk;ordinal=1@@scene=06:04;trigger=npc-talk;ordinal=2@@scene=06:04;trigger=actor-damage-ordinal;actor=boss-telesa;value=1@@scene=06:04;trigger=actor-damage-ordinal;actor=boss-telesa;value=2@@scene=06:04;trigger=actor-damage-ordinal;actor=boss-telesa;value=3
scene=06:05;trigger=predicate-fall;pollution-degree<18768@@scene=06:05;trigger=predicate-fall;pollution-degree<600
scene=06:06;trigger=npc-talk;ordinal=1@@scene=06:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=09:00;trigger=mario-status-enter;status=wall-kick;y>3500@@scene=09:00;trigger=predicate-rise;y>7500@@scene=09:00;trigger=mario-status-enter;status=ledge-grab;y>8000@@scene=09:00;trigger=nerve-enter;actor=cannon;nerve=damage-demo
scene=09:01;trigger=mario-status-enter;status=wall-kick;y>3000@@scene=09:01;trigger=mario-status-enter;status=wall-kick;y>7000@@scene=09:01;trigger=held-object-acquire;object=any
scene=2C:00;trigger=red-coin-count;value=4@@scene=2C:00;trigger=red-coin-count;value=8
scene=09:03;trigger=scene-transition;target=39:00@@scene=39:00;trigger=nerve-leave;actor=boss-eel;nerve=wait-appear@@scene=39:00;trigger=cleaned-eel-tooth-count;value=1;tooth-hp=2-to-1@@scene=39:00;trigger=cleaned-eel-tooth-count;value=8;tooth-hp=2-to-1
scene=39:00;trigger=nerve-leave;actor=boss-eel;nerve=wait-appear@@scene=39:00;trigger=cleaned-eel-tooth-count;value=1;tooth-hp=2-to-1@@scene=39:00;trigger=cleaned-eel-tooth-count;value=8;tooth-hp=2-to-1
scene=09:04;trigger=npc-talk;ordinal=1
scene=09:05;trigger=predicate-rise;y>4000@@scene=09:05;trigger=scene-transition;target=1F:00@@scene=1F:00;trigger=mario-status-enter;status=rollout;y>9000@@scene=1F:00;trigger=mario-status-enter;status=wall-kick;y>10000
scene=1F:00;trigger=mario-status-enter;status=rollout;y>9000@@scene=1F:00;trigger=mario-status-enter;status=wall-kick;y>10000
scene=09:06;trigger=shadow-mario-down-or-waiting-to-talk
scene=34:00;trigger=predicate-rise;z<-3500@@scene=34:00;trigger=rocket-nozzle-collected@@scene=34:00;trigger=demo-start;meaning=bowser-loading-trigger
scene=3C:00;trigger=bathtub-grips-dead-count;value=1@@scene=3C:00;trigger=bathtub-grips-dead-count;value=2@@scene=3C:00;trigger=bathtub-grips-dead-count;value=3@@scene=3C:00;trigger=bathtub-grips-dead-count;value=4
scene=1E:00;trigger=mario-status-enter;status=dive;y>1250@@scene=1E:00;trigger=mario-status-enter;status=dive;x<-3000@@scene=1E:00;trigger=mario-status-enter;status=dive;z>6500
scene=30:00;trigger=mario-status-enter;status=spin-jump;x>10000
"""

V5_CHECKPOINTS = tuple(
    tuple(route.split("@@")) for route in _V5_SCHEMA_TEXT.splitlines()
)

# Airstrip and Pinna 1 deliberately retain their stable route ids but no
# longer expose unreliable event splits. Every newly covered IL is likewise a
# terminal-only route until explicit checkpoints are added in a future schema.
_current = list(V5_CHECKPOINTS)
_current[10] = ()
_current[31] = ()
# Bianco 2 runs in Bianco 1's physical scene. The FMV trigger is unreliable,
# so the three Petey hits follow the rollout directly.
_current[13] = (
    "scene=02:00;trigger=mario-status-enter;status=rollout;y>=3200",
    "scene=02:00;trigger=actor-damage-ordinal;actor=petey;value=1",
    "scene=02:00;trigger=actor-damage-ordinal;actor=petey;value=2",
    "scene=02:00;trigger=actor-damage-ordinal;actor=petey;value=3",
)
_current.extend([()] * (len(ROUTE_ENTRIES) - len(V5_ROUTE_ENTRIES)))
CHECKPOINTS = tuple(_current)


def hash_word(value: int, word: int) -> int:
    return ((value ^ word) * FNV_PRIME) & 0xFFFFFFFF


def hash_ascii(text: str) -> int:
    value = FNV_OFFSET_BASIS
    for byte in text.encode("ascii"):
        value = hash_word(value, byte)
    return value


def schema_hash() -> int:
    value = FNV_OFFSET_BASIS
    for route, (entry, checkpoints) in enumerate(
        zip(ROUTE_ENTRIES, CHECKPOINTS, strict=True)
    ):
        # Hash the route even when it has no explicit endpoints.
        descriptor = (route << 24) | (0xFF << 16) | (entry << 8) | len(checkpoints)
        value = hash_word(value, descriptor)
        for local, semantic in enumerate(checkpoints):
            identity = (
                (route << 24)
                | (local << 16)
                | (entry << 8)
                | len(checkpoints)
            )
            value = hash_word(value, identity)
            value = hash_word(value, hash_ascii(semantic))
    return value


def v5_schema_hash() -> int:
    value = FNV_OFFSET_BASIS
    for route, (entry, checkpoints) in enumerate(
        zip(V5_ROUTE_ENTRIES, V5_CHECKPOINTS, strict=True)
    ):
        for local, semantic in enumerate(checkpoints):
            identity = (
                (route << 24)
                | (local << 16)
                | (entry << 8)
                | len(checkpoints)
            )
            value = hash_word(value, identity)
            value = hash_word(value, hash_ascii(semantic))
    return value


assert len(V5_ROUTE_ENTRIES) == len(V5_CHECKPOINTS) == 61
assert v5_schema_hash() == EXPECTED_V5_SCHEMA_HASH
assert len(ROUTE_ENTRIES) == len(CHECKPOINTS) == 132
assert len(set(ROUTE_ENTRIES)) == 132
assert set(ROUTE_ENTRIES) == set(range(132))
assert sum(map(len, CHECKPOINTS)) == 152
assert schema_hash() == EXPECTED_SCHEMA_HASH

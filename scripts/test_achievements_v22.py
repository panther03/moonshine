#!/usr/bin/env python3
"""Exact source contracts for the user-approved V2.2 achievement roster."""

from __future__ import annotations

import ast
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
RECORDS = ROOT / "src" / "records.cpp"
RECORDS_HEADER = ROOT / "include" / "susamune" / "records.hxx"
ASSIST_HEADER = ROOT / "include" / "susamune" / "assist.hxx"
ILING = ROOT / "src" / "iling.cpp"
ILING_HEADER = ROOT / "include" / "susamune" / "iling.hxx"
ILING_ENTRIES = ROOT / "src" / "iling_entries.inc"
GHOST = ROOT / "src" / "ghost.cpp"
GHOST_HEADER = ROOT / "include" / "susamune" / "ghost.hxx"
RNG = ROOT / "src" / "rng_control.cpp"


def source(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def braced_body(text: str, declaration: str) -> str:
    match = re.search(declaration + r"[^=;]*=\s*\{", text)
    if match is None:
        raise AssertionError(f"declaration not found: {declaration}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : end]
    raise AssertionError(f"unterminated declaration: {declaration}")


def block_body(text: str, declaration: str) -> str:
    """Return a braced block that is not introduced by an initializer `=`."""
    match = re.search(declaration + r"[^;{]*\{", text)
    if match is None:
        raise AssertionError(f"block not found: {declaration}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start + 1 : end]
    raise AssertionError(f"unterminated block: {declaration}")


def function_body(text: str, signature: str) -> str:
    match = re.search(signature + r"\s*\{", text)
    if match is None:
        raise AssertionError(f"function not found: {signature}")
    start = match.end() - 1
    depth = 0
    for end in range(start, len(text)):
        if text[end] == "{":
            depth += 1
        elif text[end] == "}":
            depth -= 1
            if depth == 0:
                return text[start : end + 1]
    raise AssertionError(f"unterminated function: {signature}")


def strip_comments(text: str) -> str:
    text = re.sub(r"//.*", "", text)
    return re.sub(r"/\*.*?\*/", "", text, flags=re.DOTALL)


def enum_values(text: str, enum_name: str) -> tuple[dict[str, int], list[str]]:
    body = block_body(text, rf"enum\s+{enum_name}(?:\s*:\s*\w+)?")
    body = strip_comments(body)
    values: dict[str, int] = {}
    order: list[str] = []
    current = -1
    for item in body.split(","):
        item = item.strip()
        if not item:
            continue
        match = re.fullmatch(r"([A-Z][A-Z0-9_]*)(?:\s*=\s*(.+))?", item)
        if match is None:
            raise AssertionError(f"cannot parse {enum_name} item: {item!r}")
        name, expression = match.groups()
        if expression is None:
            current += 1
        elif re.fullmatch(r"0x[0-9a-fA-F]+|\d+", expression.strip()):
            current = int(expression, 0)
        elif expression.strip() in values:
            current = values[expression.strip()]
        else:
            raise AssertionError(
                f"unsupported {enum_name} value for {name}: {expression}"
            )
        values[name] = current
        order.append(name)
    return values, order


def packed_strings(text: str, name: str) -> list[str]:
    match = re.search(
        rf"constexpr\s+char\s+{re.escape(name)}\[\]\s*=\s*(.*?);",
        text,
        re.DOTALL,
    )
    if match is None:
        raise AssertionError(f"packed string pool not found: {name}")
    pieces = re.findall(r'"(?:\\.|[^"\\])*"', match.group(1))
    value = "".join(ast.literal_eval(piece) for piece in pieces)
    return value.split("\0")


def struct_rows(text: str, name: str) -> list[list[str]]:
    body = strip_comments(braced_body(text, re.escape(name)))
    rows: list[list[str]] = []
    for row in re.findall(r"\{([^{}]+)\}", body):
        rows.append([field.strip() for field in row.split(",")])
    return rows


def numeric_array(text: str, name: str) -> list[int]:
    body = strip_comments(braced_body(text, re.escape(name)))
    return [int(token, 0) for token in re.findall(r"0x[0-9a-fA-F]+|\d+", body)]


def numeric_matrix(text: str, name: str) -> list[list[int]]:
    body = strip_comments(braced_body(text, re.escape(name)))
    return [
        [int(token, 0) for token in re.findall(r"0x[0-9a-fA-F]+|\d+", row)]
        for row in re.findall(r"\{([^{}]+)\}", body)
    ]


def entry_labels() -> list[str]:
    return re.findall(
        r'^\s*[A-Z][A-Z0-9_]*\(\s*"([^"]+)"',
        source(ILING_ENTRIES),
        re.MULTILINE,
    )


V22_ENUM_ORDER = [
    # Times, tier-grouped.
    "ACH_DOCK_KNOCK",
    "ACH_CHAIN_REACTION",
    "ACH_SQUID_PRO_QUO",
    "ACH_LEAF_ME_HERE",
    "ACH_PRAISE_THE_SUN",
    "ACH_UNCORKED",
    "ACH_GOOPY_BUSINESS",
    "ACH_STOP_THIEF",
    "ACH_HILLSIDE_HEIST",
    "ACH_TRIPLE_TERROR",
    "ACH_I_HAVE_THE_HIGH_GROUND",
    "ACH_STOP_RIGHT_THERE_CRIMINAL_SCUM",
    "ACH_SECURITY",
    "ACH_BY_ORDER_OF_THE_ELDER",
    "ACH_THE_HOUSE_ALWAYS_WINS",
    "ACH_MANTASTIC",
    "ACH_BOMBASTIC_BALLOONS",
    "ACH_SKIPPED_SUMI",
    "ACH_PERFECT_PETER",
    "ACH_THE_CULMINATION",
    # Challenges.
    "ACH_LOST_AND_FLUDD",
    "ACH_A_NEW_LEAF",
    "ACH_REEF_RUNNER",
    "ACH_RUINS_RAMPAGE",
    "ACH_NO_SAFETY_NET",
    "ACH_ORANGES_QUEST",
    # Course Mastery.
    "ACH_PLAZA_BEGINNINGS",
    "ACH_PLAZA_SPECIALIST",
    "ACH_PLAZA_GRADUATE",
    "ACH_PLAZA_MASTER",
    "ACH_SHADOW_SLAYER",
    # Streaks.
    "ACH_ROOTED",
    "ACH_NO_REFUNDS",
    "ACH_SCOOBY_DOOBY_DOO",
    "ACH_PIECES_ROUGES_DE_LA_GROTTE",
    "ACH_SURFS_UP",
    "ACH_ROBBING_THE_VILLAGE",
    "ACH_HOTEL_LOYALTY",
    "ACH_BRUSH_YOUR_TEETH",
    "ACH_ROUND_AND_ROUND",
    "ACH_CLEAN_SWEEP",
    "ACH_HE_LOVES_SQUIDS",
    "ACH_MOTION_SICKNESS",
    "ACH_BAYWATCH",
    "ACH_THANKS_FOR_MY_GUN",
    "ACH_A_CERTAIN_SOMEONE",
    # Special.
    "ACH_NEW_KID_ON_THE_BLOCK",
    "ACH_BY_A_NOSE",
    "ACH_MIRROR_MATCH",
    "ACH_UPSTART",
    "ACH_SERIOUS_BUSINESS",
    "ACH_MAGNATE",
    "ACH_INDUSTRIALIST",
    "ACH_STRANGER_DANGER",
    "ACH_DEAD_HEAT",
    "ACH_ROBBER_BARON",
    "ACH_MOGUL",
    "ACH_TYCOON",
    "ACH_EMPEROR",
]


EXPECTED_NAMES = {
    "kV22TimeNames": [
        "Dock Knock",
        "Chain Reaction",
        "Squid Pro Quo",
        "Leaf Me Here",
        "Praise the Sun",
        "Uncorked",
        "Goopy Business",
        "Stop, Thief!",
        "Hillside Heist",
        "Triple Terror",
        "I Have the High Ground!",
        "Stop Right There, Criminal Scum",
        "Security!!",
        "By the Order of the Elder!",
        "The House Always Wins",
        "Mantastic",
        "Bombastic Balloons",
        "Skipped Sumi",
        "Perfect Peter",
        "The Culmination",
    ],
    "kV22ChallengeNames": [
        "Lost and FLUDD",
        "A New Leaf",
        "Reef Runner",
        "Ruins Rampage",
        "No Safety Net",
        "Orange's Quest",
    ],
    "kV22MasteryNames": [
        "Plaza Beginnings",
        "Plaza Specialist",
        "Plaza Graduate",
        "Plaza Master",
        "Shadow Slayer",
    ],
    "kV22StreakNames": [
        "Rooted",
        "No Refunds",
        "Scooby Dooby Doo",
        # ASCII is deliberate: the JP retail font cannot safely render è.
        "Pieces rouges de la grotte",
        "Surf's Up",
        "Robbing the Village",
        "Hotel Loyalty",
        "Brush Your Teeth",
        "Round and Round",
        "Clean Sweep",
        "He Loves Squids",
        "Motion Sickness",
        "Baywatch",
        "Thanks for My Gun",
        "A Certain Someone",
    ],
    "kV22SpecialNames": [
        "New Kid on the Block",
        "By a Nose",
        "Mirror Match",
        "Upstart",
        "Serious Business",
        "Magnate",
        "Industrialist",
        "Stranger Danger",
        "Dead Heat",
        "Robber Baron",
        "Mogul",
        "Tycoon",
        "Emperor",
    ],
}


EXPECTED_TIERS = {
    "kV22TimeNames": (
        ["Bronze"] * 2
        + ["Silver"] * 2
        + ["Gold"]
        + ["Diamond"] * 3
        + ["Demon"] * 8
        + ["Frontier"] * 4
    ),
    "kV22ChallengeNames": ["Silver", "Gold", "Gold", "Diamond", "Diamond", "Demon"],
    "kV22MasteryNames": ["Bronze", "Silver", "Gold", "Diamond", "Demon"],
    "kV22StreakNames": (
        ["Bronze"] * 4
        + ["Silver"] * 2
        + ["Gold"] * 3
        + ["Diamond"] * 3
        + ["Demon"]
        + ["Frontier"] * 2
    ),
    "kV22SpecialNames": (
        ["Bronze"]
        + ["Silver"] * 4
        + ["Gold"] * 2
        + ["Diamond"] * 3
        + ["Demon"]
        + ["Frontier"] * 2
    ),
}


# (name, player-facing IL label, centiseconds, PB slot)
TIME_RULES = [
    ("Dock Knock", "Ricco 1", 6500, 10),
    ("Chain Reaction", "Pianta 1", 6000, 60),
    ("Squid Pro Quo", "Ricco 2 (Race)", 4000, 19),
    ("Leaf Me Here", "Lily Pad", 4000, 91),
    ("Praise the Sun", "Pianta Hidden", 4000, 69),
    ("Uncorked", "Noki 1", 5500, 50),
    ("Goopy Business", "Pianta 3", 3000, 62),
    ("Stop, Thief!", "Gelato 7", 1800, 26),
    ("Hillside Heist", "Bianco 3 (Full)", 3670, 2),
    ("Triple Terror", "Bianco 6 (Secret)", 3900, 71),
    ("I Have the High Ground!", "Bianco 7", 1600, 6),
    ("Stop Right There, Criminal Scum", "Ricco 7", 1503, 16),
    ("Security!!", "Sirena 7", 2175, 46),
    ("By the Order of the Elder!", "Noki 7", 1560, 56),
    ("The House Always Wins", "Sirena 5", 12000, 44),
    ("Mantastic", "Sirena 1", 8700, 40),
    ("Bombastic Balloons", "Pinna 8", 6000, 37),
    ("Skipped Sumi", "Ricco 1", 4640, 10),
    ("Perfect Peter", "Bianco 5", 13000, 4),
    ("The Culmination", "Ricco 5", 5200, 14),
]


# (name, player-facing IL label, value, goal, rule kind)
# Normal values are centiseconds. Thanks for My Gun deliberately stores raw QF.
STREAK_RULES = [
    ("Rooted", "Bianco 1", 0, 5, "STREAK_FINISH"),
    ("No Refunds", "Pachinko", 0, 3, "STREAK_FINISH"),
    ("Scooby Dooby Doo", "Sirena 8", 0, 5, "STREAK_FINISH"),
    (
        "Pieces rouges de la grotte",
        "Bianco 3 Reds",
        6000,
        5,
        "STREAK_QFT",
    ),
    ("Surf's Up", "Ricco 2 (Race)", 4000, 5, "STREAK_QFT"),
    ("Robbing the Village", "Pianta 100 (E5)", 12000, 5, "STREAK_QFT"),
    ("Hotel Loyalty", "Sirena 3", 2500, 5, "STREAK_QFT"),
    ("Brush Your Teeth", "Noki 4 (Eel Only)", 8300, 5, "STREAK_QFT"),
    ("Round and Round", "Pinna 6 Reds", 6000, 5, "STREAK_QFT"),
    ("Clean Sweep", "Pinna 8", 9000, 7, "STREAK_QFT"),
    ("He Loves Squids", "Ricco 5", 5600, 5, "STREAK_QFT"),
    ("Motion Sickness", "Pinna 3", 3500, 5, "STREAK_QFT"),
    ("Baywatch", "Noki 100 (E2)", 9000, 3, "STREAK_QFT"),
    ("Thanks for My Gun", "Bianco 3 (Secret)", 1656, 3, "STREAK_QFT_EXACT"),
    ("A Certain Someone", "Ricco 1", 4800, 10, "STREAK_QFT"),
]


class RosterContracts(unittest.TestCase):
    def test_exact_59_item_append_only_roster_and_retired_ids(self) -> None:
        header = source(RECORDS_HEADER)
        records = source(RECORDS)
        ids, order = enum_values(header, "AchievementId")
        by_value = {
            value: name
            for name, value in ids.items()
            if name.startswith("ACH_") and value != 0xFFFF
        }
        self.assertEqual([by_value[value] for value in range(169, 228)], V22_ENUM_ORDER)
        self.assertEqual(ids["ACHIEVEMENT_ID_END"], 228)
        self.assertEqual(ids["ACH_RETIRED_CHUCKSTER_CHANGE"], 127)
        self.assertEqual(ids["ACH_RETIRED_NO_KIDDING"], 139)
        self.assertEqual(ids["ACH_RETIRED_TOWER_TITAN"], 140)

        valid = function_body(records, r"bool\s+validAchievement\([^)]*\)")
        for retired in (
            "ACH_RETIRED_INDIANA_JONES",
            "ACH_RETIRED_CHUCKSTER_CHANGE",
            "ACH_RETIRED_NO_KIDDING",
            "ACH_RETIRED_TOWER_TITAN",
        ):
            self.assertIn(f"id != Records::{retired}", valid)

        self.assertRegex(
            header,
            r"ACHIEVEMENT_ACTIVE_COUNT\s*=\s*"
            r"ACHIEVEMENT_ID_END\s*-\s*ACHIEVEMENT_FIRST\s*-\s*4",
        )
        self.assertEqual(228 - 14 - 4, 210)
        self.assertIn("ACHIEVEMENT_ACTIVE_COUNT == 210", records)
        self.assertNotIn('"Chuckster Change', records)
        self.assertNotIn('"No Kidding', records)
        self.assertNotIn('"Tower Titan', records)
        self.assertEqual(packed_strings(records, "kRCTimeNames")[9], "")
        self.assertEqual(packed_strings(records, "kRC2TimeNames")[1:3], ["", ""])
        self.assertEqual(numeric_matrix(records, "kRCTierCounts")[0], [1, 3, 1, 1, 2, 1])
        self.assertEqual(numeric_matrix(records, "kRC2TierCounts")[0], [1, 4, 5, 3, 8, 2])
        self.assertEqual(numeric_matrix(records, "kRC2TierFirst")[0][:2], [138, 141])
        # Guard against an accidental second V2.2 append hidden elsewhere.
        first = order.index(V22_ENUM_ORDER[0])
        self.assertEqual(order[first : first + 59], V22_ENUM_ORDER)
        self.assertEqual(order[first + 59], "ACHIEVEMENT_ID_END")

    def test_retired_duplicate_progress_moves_to_the_surviving_achievement(self) -> None:
        records = source(RECORDS)
        migrate = function_body(
            records, r"bool\s+migrateAchievementBit\([^)]*\)"
        )
        self.assertIn("if (!(retiredSlot & retiredMask)) return false;", migrate)
        self.assertIn("retiredSlot &= (u8)~retiredMask;", migrate)
        self.assertIn("sAchievements[replacementValue >> 3] |=", migrate)

        adopt = function_body(records, r"void\s+adopt\([^)]*\)")
        for retired, replacement in (
            ("ACH_RETIRED_CHUCKSTER_CHANGE", "ACH_DOOTSTERS"),
            ("ACH_RETIRED_NO_KIDDING", "ACH_SCROOGE"),
            ("ACH_RETIRED_TOWER_TITAN", "ACH_RED_TIDE"),
        ):
            self.assertRegex(
                adopt,
                rf"migrated\s*\|=\s*migrateAchievementBit\(\s*"
                rf"{retired}\s*,\s*{replacement}\s*\)",
            )
        self.assertIn("sDirty = migrated;", adopt)
        self.assertIn("sAchievementDirty = migrated;", adopt)

    def test_category_and_tier_totals_are_exact(self) -> None:
        records = source(RECORDS)
        self.assertEqual(numeric_array(records, "kCategoryCounts"), [81, 15, 33, 41, 40])
        self.assertEqual(
            numeric_matrix(records, "kTierCounts"),
            [
                [9, 14, 12, 12, 24, 10],
                [2, 3, 5, 4, 1, 0],
                [8, 8, 8, 8, 1, 0],
                [8, 5, 6, 7, 10, 5],
                [4, 15, 7, 6, 4, 4],
            ],
        )
        self.assertEqual(
            numeric_matrix(records, "kV22TierCounts"),
            [
                [2, 2, 1, 3, 8, 4],
                [0, 1, 2, 2, 1, 0],
                [1, 1, 1, 1, 1, 0],
                [4, 2, 3, 3, 1, 2],
                [1, 4, 2, 3, 1, 2],
            ],
        )
        self.assertEqual(
            numeric_matrix(records, "kV22TierFirst"),
            [
                [169, 171, 173, 174, 177, 185],
                [0, 189, 190, 192, 194, 0],
                [195, 196, 197, 198, 199, 0],
                [200, 204, 206, 209, 212, 213],
                [215, 216, 220, 222, 225, 226],
            ],
        )
        self.assertEqual(sum(numeric_array(records, "kCategoryCounts")), 210)
        for row, total in zip(
            numeric_matrix(records, "kTierCounts"),
            numeric_array(records, "kCategoryCounts"),
        ):
            self.assertEqual(sum(row), total)

    def test_all_requested_names_are_exact_and_no_extra_name_was_added(self) -> None:
        records = source(RECORDS)
        actual: list[str] = []
        for pool, expected in EXPECTED_NAMES.items():
            names = packed_strings(records, pool)
            self.assertEqual(names, expected, pool)
            actual.extend(names)
        expected = [name for names in EXPECTED_NAMES.values() for name in names]
        self.assertEqual(actual, expected)
        self.assertEqual(len(actual), 59)
        self.assertEqual(len(set(actual)), 59)

    def test_every_requested_item_has_its_exact_category_and_tier(self) -> None:
        records = source(RECORDS)
        header = source(RECORDS_HEADER)
        ids, _ = enum_values(header, "AchievementId")
        first = numeric_matrix(records, "kV22TierFirst")
        counts = numeric_matrix(records, "kV22TierCounts")
        tier_names = ["Bronze", "Silver", "Gold", "Diamond", "Demon", "Frontier"]
        groups = (
            ("kV22TimeNames", V22_ENUM_ORDER[0:20], 0),
            ("kV22ChallengeNames", V22_ENUM_ORDER[20:26], 1),
            ("kV22MasteryNames", V22_ENUM_ORDER[26:31], 2),
            ("kV22StreakNames", V22_ENUM_ORDER[31:46], 3),
            ("kV22SpecialNames", V22_ENUM_ORDER[46:59], 4),
        )
        for pool, enum_names, category in groups:
            actual_tiers: list[str] = []
            for enum_name in enum_names:
                value = ids[enum_name]
                matching = [
                    tier
                    for tier in range(6)
                    if counts[category][tier]
                    and first[category][tier] <= value
                    < first[category][tier] + counts[category][tier]
                ]
                self.assertEqual(len(matching), 1, enum_name)
                actual_tiers.append(tier_names[matching[0]])
            self.assertEqual(actual_tiers, EXPECTED_TIERS[pool], pool)

        category_for = function_body(records, r"Records::Category\s+categoryFor\([^)]*\)")
        for start, end, category in (
            ("ACH_DOCK_KNOCK", "ACH_THE_CULMINATION", "CATEGORY_TIMES"),
            ("ACH_LOST_AND_FLUDD", "ACH_ORANGES_QUEST", "CATEGORY_CHALLENGES"),
            ("ACH_PLAZA_BEGINNINGS", "ACH_SHADOW_SLAYER", "CATEGORY_COURSE_MASTERY"),
            ("ACH_ROOTED", "ACH_A_CERTAIN_SOMEONE", "CATEGORY_STREAKS"),
            ("ACH_NEW_KID_ON_THE_BLOCK", "ACH_EMPEROR", "CATEGORY_SPECIAL"),
        ):
            self.assertRegex(
                re.sub(r"\s+", " ", category_for),
                rf"id >= Records::{start} && id <= Records::{end}[^;]+"
                rf"return Records::{category};",
            )


class TimeAndStreakContracts(unittest.TestCase):
    def test_all_20_time_rules_have_exact_route_threshold_and_pb_slot(self) -> None:
        records = source(RECORDS)
        labels = entry_labels()
        rows = struct_rows(records, "kTimeRules")[-20:]
        self.assertEqual(len(rows), 20)
        actual = []
        for fields in rows:
            self.assertEqual(len(fields), 3)
            centis, entry, slot = fields
            self.assertNotIn("TIME_IGT", slot)
            actual.append((labels[int(entry)], int(centis), int(slot)))
        expected = [(route, centis, slot) for _, route, centis, slot in TIME_RULES]
        self.assertEqual(actual, expected)
        self.assertEqual(
            packed_strings(records, "kV22TimeNames"),
            [name for name, *_ in TIME_RULES],
        )

    def test_all_15_streak_rules_have_exact_route_threshold_goal_and_mode(self) -> None:
        records = source(RECORDS)
        labels = entry_labels()
        rows = struct_rows(records, "kStreakRules")[-15:]
        self.assertEqual(len(rows), 15)
        actual = []
        for value, entry, goal, kind, padding in rows:
            self.assertEqual(padding, "0")
            actual.append((labels[int(entry)], int(value), int(goal), kind))
        expected = [(route, value, goal, kind) for _, route, value, goal, kind in STREAK_RULES]
        self.assertEqual(actual, expected)
        self.assertEqual(
            packed_strings(records, "kV22StreakNames"),
            [name for name, *_ in STREAK_RULES],
        )

    def test_under_means_strict_for_times_and_streaks(self) -> None:
        records = source(RECORDS)
        strict = function_body(records, r"(?:__attribute__\(\(noinline\)\)\s*)?s32\s+strictQfForCentis\([^)]*\)")
        self.assertRegex(
            re.sub(r"\s+", " ", strict),
            r"\(centis \* 1200u - 1u\) / 1001u",
        )
        result = function_body(records, r"void\s+onILResult\([^)]*\)")
        self.assertIn("qf >= 0 && qf <= strictQfForCentis(value)", result)
        streak = function_body(records, r"bool\s+streakSucceeded\([^)]*\)")
        self.assertRegex(
            streak,
            r"case STREAK_QFT:\s*return qf >= 0 && qf <= "
            r"strictQfForCentis\(value\);",
        )
        for _, _, centis, _ in TIME_RULES:
            maximum_qf = (centis * 1200 - 1) // 1001
            self.assertLess(maximum_qf * 1001, centis * 1200)
            self.assertGreaterEqual((maximum_qf + 1) * 1001, centis * 1200)
        for _, _, value, _, kind in STREAK_RULES:
            if kind != "STREAK_QFT":
                continue
            maximum_qf = (value * 1200 - 1) // 1001
            self.assertLess(maximum_qf * 1001, value * 1200)
            self.assertGreaterEqual((maximum_qf + 1) * 1001, value * 1200)

    def test_thanks_for_my_gun_is_raw_1656_qf_not_centiseconds(self) -> None:
        records = source(RECORDS)
        fields = struct_rows(records, "kStreakRules")[-2]
        self.assertEqual(fields, ["1656", "3", "3", "STREAK_QFT_EXACT", "0"])
        self.assertRegex(
            source(ILING_ENTRIES),
            r'SHINE_SECRET\("Bianco 3 \(Secret\)",\s*0x2F,\s*0,\s*2,\s*2,\s*'
            r'GROUP_BIANCO,\s*70\)',
        )
        streak = function_body(records, r"bool\s+streakSucceeded\([^)]*\)")
        self.assertRegex(streak, r"case STREAK_QFT_EXACT:\s*return qf == value;")
        description = function_body(records, r"const char \*makeDescription\([^)]*\)")
        self.assertIn('"%s: exactly 0:13.81 QFT, %u in a row."', description)


class AssistAndChallengeContracts(unittest.TestCase):
    def test_only_the_named_boss_assists_are_allowed_for_their_own_time(self) -> None:
        records = source(RECORDS)
        assist = source(ASSIST_HEADER)
        rng = source(RNG)
        iling = source(ILING)
        self.assertIn("OTHER             = 1 << 0", assist)
        self.assertIn("KING_BOO_FRUIT    = 1 << 1", assist)
        self.assertIn("PETEY_NO_TORNADO  = 1 << 2", assist)
        self.assertIn("PETEY_ROUTE       = 1 << 3", assist)

        allowed = function_body(records, r"bool\s+timeEligibleWithAssist\([^)]*\)")
        self.assertIn("if (introSkipEnabled()) return false;", allowed)
        self.assertIn("index == kHouseAlwaysWinsTimeIndex", allowed)
        self.assertIn("reasons == Assist::KING_BOO_FRUIT", allowed)
        self.assertIn("index == kPerfectPeterTimeIndex", allowed)
        self.assertIn("Assist::PETEY_NO_TORNADO | Assist::PETEY_ROUTE", allowed)
        self.assertIn("reasons != 0 && !(reasons & ~petey)", allowed)
        self.assertRegex(allowed, r"return false;\s*\}")

        self.assertIn("reasons |= Assist::KING_BOO_FRUIT", rng)
        self.assertIn("reasons |= Assist::PETEY_NO_TORNADO", rng)
        self.assertIn("reasons |= Assist::PETEY_ROUTE", rng)
        self.assertIn("ILing::invalidateForAssist(reasons);", rng)
        invalidator = function_body(iling, r"void\s+invalidateForAssist\([^)]*\)")
        self.assertIn("const u8 added = reasons & ~sAssistReasons;", invalidator)
        self.assertIn("sAssistReasons |= reasons;", invalidator)
        self.assertIn("Records::invalidateAttempt(added);", invalidator)
        recorder = function_body(records, r"void\s+invalidateAttempt\([^)]*\)")
        self.assertIn("sAttemptAssistReasons |=", recorder)
        self.assertIn("Assist::OTHER", recorder)

        update = function_body(iling, r"void\s+update\(\)")
        self.assertIn("const u8 globalAssistReasons = liveGlobalAssistReasons();", update)
        self.assertIn("invalidateForAssist(globalAssistReasons);", update)
        self.assertNotIn("sRunning && sRecordsEligible &&", update)
        arm = function_body(iling, r"void\s+armAttempt\([^)]*\)")
        self.assertIn("sAssistReasons = liveGlobalAssistReasons();", arm)
        self.assertIn("Records::invalidateAttempt(sAssistReasons);", arm)

        records_update = function_body(records, r"void\s+update\([^)]*\)")
        self.assertIn("!(sAttemptAssistReasons & Assist::OTHER)", records_update)
        self.assertIn("invalidateAttempt(Assist::OTHER);", records_update)

        result = function_body(records, r"void\s+onILResult\([^)]*\)")
        assist_check = result.index("!eligible && !timeEligibleWithAssist(i)")
        ordinary_exit = result.index("if (!eligible)")
        self.assertLess(assist_check, ordinary_exit)
        self.assertNotIn("timeEligibleWithAssist", result[ordinary_exit + 1 :])

    def test_action_tracking_records_only_fired_nozzles(self) -> None:
        records = source(RECORDS)
        enum = block_body(records, r"enum\s+ActionFlags")
        for action in ("ACTION_HOVER", "ACTION_ROCKET", "ACTION_TURBO", "ACTION_YOSHI", "ACTION_SPRAY"):
            self.assertIn(action, enum)
        update = function_body(records, r"void\s+update\([^)]*\)")
        self.assertIn("fludd->mIsEmitWater", update)
        self.assertIn("fludd->mCurrentNozzle == TWaterGun::Hover", update)
        self.assertIn("fludd->mCurrentNozzle == TWaterGun::Rocket", update)
        self.assertIn("fludd->mCurrentNozzle == TWaterGun::Spray", update)
        self.assertIn("sActionFlags |= ACTION_SPRAY", update)

    def test_all_six_challenge_rules_are_exact(self) -> None:
        records = source(RECORDS)
        result = function_body(records, r"void\s+onILResult\([^)]*\)")
        compact = re.sub(r"\s+", " ", result)
        expected_fragments = (
            "routeEntry == 80 && !(sActionFlags & ACTION_HOVER)",
            "routeEntry == 97 && !(sActionFlags & ACTION_SPRAY)",
            "routeEntry == 33 && sReefStarted && !sReefTouched",
            "routeEntry == 66 && qf >= 0 && qf <= strictQfForCentis(5000) && !(sActionFlags & (ACTION_HOVER | ACTION_SPRAY))",
            "routeEntry == 95 && !(sActionFlags & ACTION_HOVER)",
            "routeEntry == 103 && !(sActionFlags & (ACTION_ROCKET | ACTION_YOSHI))",
        )
        for fragment in expected_fragments:
            self.assertIn(fragment, compact)
        for achievement in (
            "ACH_LOST_AND_FLUDD",
            "ACH_A_NEW_LEAF",
            "ACH_REEF_RUNNER",
            "ACH_RUINS_RAMPAGE",
            "ACH_NO_SAFETY_NET",
            "ACH_ORANGES_QUEST",
        ):
            self.assertEqual(result.count(f"unlock({achievement})"), 1)

        reef = function_body(records, r"void\s+updateReefRunner\([^)]*\)")
        self.assertIn("sAttemptEntry != 33", reef)
        self.assertIn("mRedCoinCount > 0", reef)
        self.assertIn("mario->mAttributes.mIsWater", reef)
        self.assertIn("mario->mFloorTriangle", reef)
        self.assertIn("mario->mTranslation.y <= mario->mFloorBelow + 5.0f", reef)


class MasteryGhostAndMetaContracts(unittest.TestCase):
    def test_plaza_and_shadow_mastery_use_the_exact_pb_sets(self) -> None:
        records = source(RECORDS)
        any_slots = numeric_array(records, "kAnySlots")
        all_slots = numeric_array(records, "kAllSlots")
        world_rows = numeric_matrix(records, "kWorldPBRules")
        self.assertEqual(world_rows[7], [42, 13, 91, 31])
        self.assertEqual(
            any_slots[42 : 42 + 13],
            [119, 80, 81, 82, 83, 84, 85, 108, 109, 120, 110, 111, 86],
        )
        self.assertEqual(
            all_slots[91 : 91 + 31],
            [
                86, 88, 119, 124, 87, 89, 90, 91, 92, 93, 94, 95, 96,
                97, 98, 99, 107, 116, 117, 118, 80, 81, 82, 83, 84, 85,
                108, 109, 120, 110, 111,
            ],
        )
        # The all-IL set is intentionally 31 slots; listing it explicitly also
        # catches accidental duplication or omission when the IL catalog grows.
        self.assertEqual(len(all_slots[91 : 91 + 31]), 31)
        self.assertEqual(numeric_array(records, "kShadowSlots"), [6, 16, 26, 36, 46, 56, 66])
        self.assertRegex(records, r"kShadowSlayerCentis\s*=\s*14500")

        evaluate = function_body(records, r"void\s+evaluatePBProfile\([^)]*\)")
        for exact in (
            "delfinoAny == delfino.anyCount",
            "strictQfForCentis(48000u)",
            "delfinoAll == delfino.allCount",
            "strictQfForCentis(120000u)",
            "(shadowMetrics >> 24) == sizeof(kShadowSlots)",
            "strictQfForCentis(kShadowSlayerCentis)",
        ):
            self.assertIn(exact, evaluate)
        for achievement in (
            "ACH_PLAZA_BEGINNINGS",
            "ACH_PLAZA_SPECIALIST",
            "ACH_PLAZA_GRADUATE",
            "ACH_PLAZA_MASTER",
            "ACH_SHADOW_SLAYER",
        ):
            self.assertIn(f"unlock(Records::{achievement}, notify)", evaluate)

    def test_by_a_nose_is_exactly_one_30fps_frame_or_four_qf(self) -> None:
        records = source(RECORDS)
        header = source(RECORDS_HEADER)
        iling = source(ILING)
        callback = function_body(records, r"void\s+onPBAccepted\([^)]*\)")
        self.assertIn("previousQf >= 0", callback)
        self.assertIn("previousQf - newQf == 4", callback)
        self.assertIn("unlock(ACH_BY_A_NOSE)", callback)
        self.assertRegex(
            header,
            r"void onPBAccepted\(int entry, u8 profile, s32 previousQf, s32 newQf\);",
        )
        self.assertIn(
            "Records::onPBAccepted(entry, sActivePbProfile, previous, qf);",
            iling,
        )

    def test_ghost_achievements_use_selected_type_target_and_starting_pb(self) -> None:
        records = source(RECORDS)
        iling = source(ILING)
        ghost = source(GHOST)
        ghost_header = source(GHOST_HEADER)
        result = function_body(records, r"void\s+onILResult\([^)]*\)")
        compact = re.sub(r"\s+", " ", result)
        self.assertIn("ghostSource != GHOST_RACE_NONE && ghostQf >= 0", compact)
        self.assertIn("qf == ghostQf", compact)
        self.assertIn("unlock(ACH_DEAD_HEAT)", compact)
        self.assertIn("qf < ghostQf && priorPbQf >= 0", compact)
        self.assertIn("ghostSource == GHOST_RACE_PERSONAL && ghostQf == priorPbQf", compact)
        self.assertIn("unlock(ACH_MIRROR_MATCH)", compact)
        self.assertIn("ghostSource == GHOST_RACE_IMPORTED && ghostQf < priorPbQf", compact)
        self.assertIn("unlock(ACH_STRANGER_DANGER)", compact)

        # Integration must snapshot this context when the race starts. Reading
        # the current PB or current selection only at finish changes the target.
        self.assertIn("enum RaceSource : u8", ghost_header)
        self.assertIn("RACE_SOURCE_PERSONAL", ghost_header)
        self.assertIn("RACE_SOURCE_IMPORTED", ghost_header)
        self.assertIn("struct RaceContext", ghost_header)
        frozen = function_body(ghost, r"void\s+captureRaceContext\([^)]*\)")
        for fragment in (
            "sPlaybackPinned",
            "sPlayback.valid",
            "sPlayback.completed",
            "sPlaybackRaceSource == RACE_SOURCE_NONE",
            "sPlaybackRaceToken != sPlaybackToken",
            "sRaceContext.attemptSerial = sAttemptSerial",
            "sRaceContext.targetQf = sPlayback.resultQf",
            "sRaceContext.startingPbQf = -1",
            "sRaceContext.routeVariant = sPlayback.parentEpisode",
            "sRaceContext.ilEntry = -1",
            "sRaceContext.area = sPlayback.area",
            "sRaceContext.episode = sPlayback.episode",
            "sRaceContext.routeParentArea = sPlayback.routeParentArea",
            "sRaceContext.routeFlags = sPlayback.routeFlags",
            "sRaceContext.source = sPlaybackRaceSource",
            "sRaceContextPlaybackToken = sPlaybackToken",
            "sRaceContextValid = true",
        ):
            self.assertIn(fragment, frozen)
        imported = function_body(ghost, r"bool\s+importPlayback\([^)]*\)")
        self.assertIn(
            "sPlaybackRaceSource = imported ? RACE_SOURCE_IMPORTED",
            imported,
        )
        self.assertIn(": RACE_SOURCE_PERSONAL", imported)
        self.assertIn("sPlaybackRaceToken = sPlaybackToken", imported)

        begin = function_body(ghost, r"void\s+beginAttempt\([^)]*\)")
        self.assertLess(begin.index("clearRaceContext();"),
                        begin.index("captureRaceContext();"))
        read_context = function_body(ghost, r"bool\s+raceContext\([^)]*\)")
        for fragment in (
            "sRaceContextValid",
            "sPlaybackPinned",
            "sRaceContextPlaybackToken != sPlaybackToken",
            "sRaceContext.source != sPlaybackRaceSource",
        ):
            self.assertIn(fragment, read_context)
        bound = function_body(ghost, r"bool\s+bindRaceContext\([^)]*\)")
        for fragment in (
            "sRaceContextValid",
            "sPlaybackPinned",
            "sRaceContextPlaybackToken != sPlaybackToken",
            "sRaceContext.source != sPlaybackRaceSource",
            "sRaceContext.attemptSerial != gQFTTimer.attemptSerial()",
        ):
            self.assertIn(fragment, bound)
        self.assertIn("sRaceContext.ilEntry = ilEntry", bound)
        self.assertIn("sRaceContext.startingPbQf = startingPbQf", bound)

        capture = function_body(iling, r"void\s+captureGhostRace\([^)]*\)")
        for fragment in (
            "Ghost::RaceContext race",
            "Ghost::raceContext(&race)",
            "race.attemptSerial != gQFTTimer.attemptSerial()",
            "race.targetQf > 0x7fffffffu",
            "race.area != start.area",
            "race.episode != start.episode",
            "race.routeVariant != start.gameInt3",
            "race.routeParentArea != parentArea",
            "race.routeFlags != routeFlags",
            "Ghost::bindRaceContext(static_cast<s16>(entry)",
            "activePBs()[pbSlot(entry)]",
        ):
            self.assertIn(fragment, capture)
        arm = function_body(iling, r"void\s+armAttempt\([^)]*\)")
        update = function_body(iling, r"void\s+update\(\)")
        restart_capture = update.index("captureGhostRace(entry);")
        serial_write = update.rfind("sAttemptSerial = serial;", 0, restart_capture)
        self.assertGreaterEqual(serial_write, 0)
        self.assertLess(serial_write, restart_capture)
        self.assertLess(restart_capture,
                        update.index("Records::onILAttemptStarted(entry);", restart_capture))
        # Restart, child conversion, and the first armed attempt must all bind
        # the frozen opponent after the new QFT serial is live.
        self.assertEqual(update.count("captureGhostRace(entry);"), 3)
        il_result_call = re.search(r"Records::onILResult\((.*?)\);", iling, re.DOTALL)
        self.assertIsNotNone(il_result_call)
        assert il_result_call is not None
        arguments = re.sub(r"\s+", " ", il_result_call.group(1))
        record_result = function_body(iling, r"void\s+recordResult\([^)]*\)")
        self.assertIn("Ghost::raceContext(&race)", record_result)
        self.assertIn("race.ilEntry == entry", record_result)
        self.assertIn("race.attemptSerial == sAttemptSerial", record_result)
        self.assertIn("ghostQf = static_cast<s32>(race.targetQf)", record_result)
        self.assertIn("startingPbQf = race.startingPbQf", record_result)
        for fragment in (
            "race.source == Ghost::RACE_SOURCE_IMPORTED",
            "Records::GHOST_RACE_IMPORTED",
            "race.source == Ghost::RACE_SOURCE_PERSONAL",
            "Records::GHOST_RACE_PERSONAL",
        ):
            self.assertIn(fragment, record_result)
        self.assertIn("sRecordsEligible, raceSource, ghostQf, startingPbQf", arguments)
        self.assertGreaterEqual(arguments.count(","), 7)

    def test_milestones_count_prior_unlocks_but_emperor_excludes_itself(self) -> None:
        records = source(RECORDS)
        evaluate = function_body(records, r"void\s+evaluateMetaAchievements\([^)]*\)")
        every = function_body(records, r"bool\s+everyOtherAchievementUnlocked\([^)]*\)")
        self.assertIn("sEvaluatingMeta", evaluate)
        for goal in (25, 50, 75, 100, 125, 150, 175, 200):
            self.assertRegex(evaluate, rf"\b{goal}\b")
        # "Have N achievements" counts already-earned lower milestones. The
        # re-entry guard prevents a new milestone from recursively evaluating
        # itself, while the ascending pass intentionally lets it contribute to
        # later totals.
        self.assertIn("sUnlockedCount >= goals[i]", evaluate)
        self.assertLess(evaluate.index("ACH_NEW_KID_ON_THE_BLOCK"),
                        evaluate.index("ACH_UPSTART"))
        descriptions = packed_strings(records, "kV22SpecialDescriptions")
        for index, goal in zip((0, 3, 4, 5, 6, 9, 10, 11),
                               (25, 50, 75, 100, 125, 150, 175, 200)):
            self.assertEqual(descriptions[index], f"Have {goal} achievements.")
        self.assertIn("id != Records::ACH_EMPEROR", every)
        self.assertIn("validAchievement(id)", every)
        self.assertIn("!bitUnlocked(id)", every)
        self.assertIn("everyOtherAchievementUnlocked()", evaluate)
        self.assertIn("unlock(Records::ACH_EMPEROR, notify)", evaluate)


if __name__ == "__main__":
    unittest.main()

"""
Genera report CSV e JSON dai file analytics di Nico.

Esempi:

    python analytics_report.py
    python analytics_report.py --days 30
    python analytics_report.py --days 90 --output-dir analytics/reports_90
"""

import argparse
import csv
import json
import re
import unicodedata
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Dict, Iterable, List, Optional


BASE_DIR = Path(__file__).resolve().parent
DEFAULT_ANALYTICS_DIR = BASE_DIR / "analytics"
DEFAULT_MALL_DATA = BASE_DIR / "mall_data.json"


def parse_datetime(value: Optional[str]) -> Optional[datetime]:
    if not value:
        return None

    try:
        parsed = datetime.fromisoformat(
            value.replace("Z", "+00:00")
        )

        if parsed.tzinfo is None:
            parsed = parsed.replace(
                tzinfo=timezone.utc
            )

        return parsed.astimezone(timezone.utc)

    except ValueError:
        return None


def read_jsonl(path: Path) -> List[Dict[str, Any]]:
    if not path.exists():
        return []

    records: List[Dict[str, Any]] = []

    with path.open("r", encoding="utf-8") as file:
        for line in file:
            stripped = line.strip()

            if not stripped:
                continue

            try:
                value = json.loads(stripped)
            except json.JSONDecodeError:
                continue

            if isinstance(value, dict):
                records.append(value)

    return records


def filter_by_days(
    records: Iterable[Dict[str, Any]],
    days: int
) -> List[Dict[str, Any]]:
    cutoff = (
        datetime.now(timezone.utc)
        - timedelta(days=max(1, days))
    )

    result: List[Dict[str, Any]] = []

    for record in records:
        timestamp = parse_datetime(
            record.get("timestamp_utc")
        )

        if timestamp is None or timestamp >= cutoff:
            result.append(record)

    return result


def write_csv(
    path: Path,
    rows: Iterable[Dict[str, Any]],
    fieldnames: List[str]
) -> None:
    path.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    with path.open(
        "w",
        encoding="utf-8-sig",
        newline=""
    ) as file:
        writer = csv.DictWriter(
            file,
            fieldnames=fieldnames,
            extrasaction="ignore"
        )
        writer.writeheader()

        for row in rows:
            writer.writerow(row)


def normalize_question(text: str) -> str:
    text = text.lower().strip()
    text = re.sub(r"\s+", " ", text)
    return text


def normalize_name_key(text: str) -> str:
    decomposed = unicodedata.normalize(
        "NFD",
        text.lower()
    )

    without_accents = "".join(
        character
        for character in decomposed
        if unicodedata.category(character) != "Mn"
    )

    return re.sub(
        r"[^a-z0-9]+",
        "",
        without_accents
    )


def load_json(
    path: Path
) -> Dict[str, Any]:
    if not path.exists():
        return {}

    with path.open(
        "r",
        encoding="utf-8"
    ) as file:
        value = json.load(file)

    return value if isinstance(value, dict) else {}


def exposure_window_count(
    record: Dict[str, Any],
    cutoff_date: str
) -> int:
    return sum(
        int(count)
        for date_key, count
        in record.get("daily_mentions", {}).items()
        if date_key >= cutoff_date
    )


def main() -> None:
    parser = argparse.ArgumentParser()

    parser.add_argument(
        "--days",
        type=int,
        default=30
    )
    parser.add_argument(
        "--analytics-dir",
        type=Path,
        default=DEFAULT_ANALYTICS_DIR
    )
    parser.add_argument(
        "--mall-data",
        type=Path,
        default=DEFAULT_MALL_DATA
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=None
    )

    args = parser.parse_args()

    days = max(1, args.days)
    analytics_dir = args.analytics_dir
    output_dir = (
        args.output_dir
        or analytics_dir / "reports"
    )

    interactions = filter_by_days(
        read_jsonl(
            analytics_dir / "interactions.jsonl"
        ),
        days
    )

    exposure_state = load_json(
        analytics_dir / "shop_exposure.json"
    )

    mall_data = load_json(
        args.mall_data
    )

    interaction_rows: List[Dict[str, Any]] = []
    intent_counter: Counter[str] = Counter()
    question_counter: Counter[str] = Counter()
    shop_counter: Counter[str] = Counter()
    mode_counter: Counter[str] = Counter()
    daily: Dict[str, Dict[str, Any]] = defaultdict(
        lambda: {
            "interactions": 0,
            "sessions": set(),
            "shops": set(),
            "response_ms_total": 0.0,
            "response_ms_count": 0,
            "clarifications": 0,
            "out_of_scope": 0
        }
    )

    for record in interactions:
        intents = record.get("intents", [])
        shops = record.get(
            "mentioned_shops",
            []
        )

        intent_counter.update(intents)
        shop_counter.update(shops)

        question = record.get(
            "question",
            ""
        )
        normalized_question = (
            record.get("question_normalized")
            or normalize_question(question)
        )

        if normalized_question:
            question_counter[
                normalized_question
            ] += 1

        mode = record.get(
            "response_mode",
            "other"
        )
        mode_counter[mode] += 1

        local_date = (
            record.get("local_date")
            or str(
                record.get(
                    "timestamp_local",
                    ""
                )
            )[:10]
            or "unknown"
        )

        day = daily[local_date]
        day["interactions"] += 1
        day["sessions"].add(
            record.get("session_id", "")
        )
        day["shops"].update(shops)

        response_ms = float(
            record.get("response_ms", 0.0)
        )

        day["response_ms_total"] += response_ms
        day["response_ms_count"] += 1
        day["clarifications"] += int(
            bool(
                record.get(
                    "needs_clarification",
                    False
                )
            )
        )
        day["out_of_scope"] += int(
            bool(
                record.get(
                    "is_out_of_scope",
                    False
                )
            )
        )

        interaction_rows.append({
            "timestamp_local": record.get(
                "timestamp_local",
                ""
            ),
            "session_id": record.get(
                "session_id",
                ""
            ),
            "interaction_source": record.get(
                "interaction_source",
                ""
            ),
            "question": question,
            "answer": record.get(
                "answer",
                ""
            ),
            "intents": "|".join(intents),
            "mentioned_shops": "|".join(shops),
            "mentioned_services": "|".join(
                record.get(
                    "mentioned_services",
                    []
                )
            ),
            "response_mode": mode,
            "confidence": record.get(
                "confidence",
                ""
            ),
            "needs_clarification": record.get(
                "needs_clarification",
                False
            ),
            "is_out_of_scope": record.get(
                "is_out_of_scope",
                False
            ),
            "response_ms": response_ms
        })

    write_csv(
        output_dir / "interactions.csv",
        interaction_rows,
        [
            "timestamp_local",
            "session_id",
            "interaction_source",
            "question",
            "answer",
            "intents",
            "mentioned_shops",
            "mentioned_services",
            "response_mode",
            "confidence",
            "needs_clarification",
            "is_out_of_scope",
            "response_ms"
        ]
    )

    cutoff_date = (
        datetime.now().date()
        - timedelta(days=days - 1)
    ).isoformat()

    state_shops = exposure_state.get(
        "shops",
        {}
    )

    coverage_rows: List[Dict[str, Any]] = []

    for shop in mall_data.get("shops", []):
        name = shop.get("name", "")
        key = normalize_name_key(name)

        record = state_shops.get(
            key,
            {}
        )

        coverage_rows.append({
            "name": name,
            "category": shop.get(
                "category",
                ""
            ),
            "area": shop.get(
                "area",
                ""
            ),
            "mentions_period": exposure_window_count(
                record,
                cutoff_date
            ),
            "total_mentions": int(
                record.get(
                    "total_mentions",
                    0
                )
            ),
            "answer_mentions": int(
                record.get(
                    "answer_mentions",
                    0
                )
            ),
            "featured_mentions": int(
                record.get(
                    "featured_mentions",
                    0
                )
            ),
            "first_mentioned_utc": record.get(
                "first_mentioned_utc",
                ""
            ),
            "last_mentioned_utc": record.get(
                "last_mentioned_utc",
                ""
            ),
            "status": (
                "mai nominato"
                if int(
                    record.get(
                        "total_mentions",
                        0
                    )
                ) == 0
                else "nominato"
            )
        })

    coverage_rows.sort(
        key=lambda row: (
            row["mentions_period"],
            row["total_mentions"],
            row["name"]
        )
    )

    write_csv(
        output_dir / "shop_coverage.csv",
        coverage_rows,
        [
            "name",
            "category",
            "area",
            "mentions_period",
            "total_mentions",
            "answer_mentions",
            "featured_mentions",
            "first_mentioned_utc",
            "last_mentioned_utc",
            "status"
        ]
    )

    write_csv(
        output_dir / "intent_summary.csv",
        [
            {
                "intent": name,
                "count": count
            }
            for name, count
            in intent_counter.most_common()
        ],
        ["intent", "count"]
    )

    write_csv(
        output_dir / "question_summary.csv",
        [
            {
                "question_normalized": name,
                "count": count
            }
            for name, count
            in question_counter.most_common()
        ],
        [
            "question_normalized",
            "count"
        ]
    )

    daily_rows: List[Dict[str, Any]] = []

    for date_key in sorted(daily):
        value = daily[date_key]
        average_response = (
            value["response_ms_total"]
            / value["response_ms_count"]
            if value["response_ms_count"]
            else 0.0
        )

        daily_rows.append({
            "date": date_key,
            "interactions": value[
                "interactions"
            ],
            "unique_sessions": len(
                value["sessions"] - {""}
            ),
            "unique_shops_mentioned": len(
                value["shops"]
            ),
            "average_response_ms": round(
                average_response,
                2
            ),
            "clarifications": value[
                "clarifications"
            ],
            "out_of_scope": value[
                "out_of_scope"
            ]
        })

    write_csv(
        output_dir / "daily_kpi.csv",
        daily_rows,
        [
            "date",
            "interactions",
            "unique_sessions",
            "unique_shops_mentioned",
            "average_response_ms",
            "clarifications",
            "out_of_scope"
        ]
    )

    total_shops = len(coverage_rows)
    covered_shops = sum(
        1
        for row in coverage_rows
        if row["mentions_period"] > 0
    )

    response_values = [
        float(
            record.get(
                "response_ms",
                0.0
            )
        )
        for record in interactions
    ]

    summary = {
        "generated_at_utc": (
            datetime.now(timezone.utc).isoformat()
        ),
        "period_days": days,
        "interactions": len(interactions),
        "unique_sessions": len({
            record.get("session_id", "")
            for record in interactions
            if record.get("session_id")
        }),
        "average_response_ms": (
            round(
                sum(response_values)
                / len(response_values),
                2
            )
            if response_values
            else 0.0
        ),
        "shop_coverage": {
            "total_shops": total_shops,
            "shops_mentioned": covered_shops,
            "shops_not_mentioned": (
                total_shops - covered_shops
            ),
            "coverage_percent": (
                round(
                    covered_shops
                    / total_shops
                    * 100.0,
                    2
                )
                if total_shops
                else 0.0
            )
        },
        "top_intents": intent_counter.most_common(15),
        "top_shops": shop_counter.most_common(15),
        "response_modes": dict(mode_counter),
        "least_exposed_shops": (
            coverage_rows[:20]
        )
    }

    output_dir.mkdir(
        parents=True,
        exist_ok=True
    )

    with (
        output_dir / "summary.json"
    ).open(
        "w",
        encoding="utf-8"
    ) as file:
        json.dump(
            summary,
            file,
            ensure_ascii=False,
            indent=2
        )

    print(
        f"Report generati in: {output_dir}"
    )
    print(
        f"Interazioni analizzate: "
        f"{len(interactions)}"
    )
    print(
        "Copertura negozi: "
        f"{covered_shops}/{total_shops}"
    )


if __name__ == "__main__":
    main()

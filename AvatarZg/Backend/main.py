# start server
# uvicorn main:app --host 127.0.0.1 --port 8010
import csv
import hashlib
import io
import json
import os
import re
import threading
import time
import unicodedata
import uuid
from collections import Counter, defaultdict
from datetime import datetime, timedelta, timezone
from pathlib import Path
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError
from typing import Any, Dict, List, Optional, Sequence, Tuple

import requests
from dotenv import load_dotenv
from fastapi import FastAPI, Response
from openai import OpenAI
from pydantic import BaseModel


# ---------------------------------------------------------------------
# ENV / CONFIG
# ---------------------------------------------------------------------

load_dotenv()

OPENAI_API_KEY = os.getenv("OPENAI_API_KEY", "")
OPENAI_MODEL = os.getenv("OPENAI_MODEL", "gpt-5.5")
USE_OPENAI = os.getenv("USE_OPENAI", "false").lower() == "true"

ELEVENLABS_API_KEY = os.getenv("ELEVENLABS_API_KEY", "")
ELEVENLABS_VOICE_ID = os.getenv("ELEVENLABS_VOICE_ID", "")
ELEVENLABS_MODEL_ID = os.getenv(
    "ELEVENLABS_MODEL_ID",
    "eleven_multilingual_v2"
)
ELEVENLABS_OUTPUT_FORMAT = os.getenv(
    "ELEVENLABS_OUTPUT_FORMAT",
    "mp3_44100_128"
)

MAX_ANSWER_CHARS = 350
MAX_RECOMMENDATIONS = 3

GENERAL_KNOWLEDGE_MAX_CHARS = int(
    os.getenv("GENERAL_KNOWLEDGE_MAX_CHARS", "170")
)

MALL_HELP_SUFFIX = (
    "Posso aiutarti con negozi, servizi, orari, eventi, "
    "ristorazione e idee per lo shopping del Gran Shopping Molfetta."
)

MALL_TIMEZONE = os.getenv(
    "MALL_TIMEZONE",
    "Europe/Rome"
)

WEATHER_LOCATION_NAME = os.getenv(
    "WEATHER_LOCATION_NAME",
    "Molfetta"
)

WEATHER_COUNTRY_CODE = os.getenv(
    "WEATHER_COUNTRY_CODE",
    "IT"
)

WEATHER_ADMIN1 = os.getenv(
    "WEATHER_ADMIN1",
    "Puglia"
)

WEATHER_LATITUDE = os.getenv(
    "WEATHER_LATITUDE",
    ""
)

WEATHER_LONGITUDE = os.getenv(
    "WEATHER_LONGITUDE",
    ""
)

WEATHER_GEOCODING_URL = os.getenv(
    "WEATHER_GEOCODING_URL",
    "https://geocoding-api.open-meteo.com/v1/search"
)

WEATHER_API_BASE_URL = os.getenv(
    "WEATHER_API_BASE_URL",
    "https://api.open-meteo.com/v1/forecast"
)

WEATHER_API_KEY = os.getenv(
    "WEATHER_API_KEY",
    ""
)

WEATHER_CACHE_SECONDS = int(
    os.getenv("WEATHER_CACHE_SECONDS", "300")
)

ENABLE_ANALYTICS = (
    os.getenv("ENABLE_ANALYTICS", "true").lower() == "true"
)

ANALYTICS_DIR_NAME = os.getenv(
    "ANALYTICS_DIR",
    "analytics"
)

ANALYTICS_HASH_SESSION_IDS = (
    os.getenv(
        "ANALYTICS_HASH_SESSION_IDS",
        "true"
    ).lower() == "true"
)

ANALYTICS_REDACT_PII = (
    os.getenv(
        "ANALYTICS_REDACT_PII",
        "true"
    ).lower() == "true"
)

ANALYTICS_INCLUDE_DEBUG_INFO = (
    os.getenv(
        "ANALYTICS_INCLUDE_DEBUG_INFO",
        "false"
    ).lower() == "true"
)

ANALYTICS_DEFAULT_DAYS = int(
    os.getenv("ANALYTICS_DEFAULT_DAYS", "30")
)

EXPOSURE_WINDOW_DAYS = int(
    os.getenv("EXPOSURE_WINDOW_DAYS", "30")
)

FAIRNESS_ENABLED = (
    os.getenv("FAIRNESS_ENABLED", "true").lower() == "true"
)

FAIRNESS_BASE_BOOST = float(
    os.getenv("FAIRNESS_BASE_BOOST", "10")
)

FAIRNESS_NEVER_MENTIONED_BOOST = float(
    os.getenv("FAIRNESS_NEVER_MENTIONED_BOOST", "12")
)

FAIRNESS_STALE_BOOST_MAX = float(
    os.getenv("FAIRNESS_STALE_BOOST_MAX", "6")
)

FAIRNESS_STALE_DAYS = max(
    1,
    int(os.getenv("FAIRNESS_STALE_DAYS", "7"))
)

FAIRNESS_MAX_BOOST = float(
    os.getenv("FAIRNESS_MAX_BOOST", "24")
)

openai_client = (
    OpenAI(api_key=OPENAI_API_KEY)
    if OPENAI_API_KEY
    else None
)

app = FastAPI(title="AvatarZG Nico Shopping Assistant Backend")


# ---------------------------------------------------------------------
# MODELS
# ---------------------------------------------------------------------

class AssistantRequest(BaseModel):
    SessionId: Optional[str] = None
    sessionId: Optional[str] = None

    MallId: Optional[str] = None
    mallId: Optional[str] = None

    UserText: Optional[str] = None
    userText: Optional[str] = None

    Language: Optional[str] = "it"
    language: Optional[str] = "it"

    InteractionSource: Optional[str] = None
    interactionSource: Optional[str] = None

    ConversationHistory: Optional[List[str]] = None
    conversationHistory: Optional[List[str]] = None


class TtsRequest(BaseModel):
    Text: Optional[str] = None
    text: Optional[str] = None


class FeaturedShopRequest(BaseModel):
    Channel: Optional[str] = "idle_rotation"
    channel: Optional[str] = "idle_rotation"


# ---------------------------------------------------------------------
# DATA LOADING
# ---------------------------------------------------------------------

BASE_DIR = Path(__file__).resolve().parent
MALL_DATA_PATH = BASE_DIR / "mall_data.json"

_analytics_dir_candidate = Path(ANALYTICS_DIR_NAME)
ANALYTICS_DIR_PATH = (
    _analytics_dir_candidate
    if _analytics_dir_candidate.is_absolute()
    else BASE_DIR / _analytics_dir_candidate
)

INTERACTION_LOG_PATH = (
    ANALYTICS_DIR_PATH / "interactions.jsonl"
)

FEATURED_EXPOSURE_LOG_PATH = (
    ANALYTICS_DIR_PATH / "featured_exposures.jsonl"
)

SHOP_EXPOSURE_STATE_PATH = (
    ANALYTICS_DIR_PATH / "shop_exposure.json"
)

_ANALYTICS_LOCK = threading.RLock()
_EXPOSURE_STATE_CACHE: Optional[Dict[str, Any]] = None


def load_mall_data() -> Dict[str, Any]:
    if not MALL_DATA_PATH.exists():
        raise FileNotFoundError(
            f"Missing mall data file: {MALL_DATA_PATH}"
        )

    with MALL_DATA_PATH.open("r", encoding="utf-8") as file:
        return json.load(file)


def get_mall_data() -> Dict[str, Any]:
    return load_mall_data()


# ---------------------------------------------------------------------
# NORMALIZATION
# ---------------------------------------------------------------------

def remove_accents(text: str) -> str:
    normalized = unicodedata.normalize("NFD", text)

    return "".join(
        character
        for character in normalized
        if unicodedata.category(character) != "Mn"
    )


def normalize_text(text: str) -> str:
    text = remove_accents(text.lower())
    text = text.replace("’", "'")
    text = text.replace("make-up", "makeup")
    text = text.replace("make up", "makeup")
    text = re.sub(r"\s+", " ", text)

    return text.strip()


def normalize_name_key(text: str) -> str:
    """
    Normalizzazione rigorosa per i nomi ufficiali.

    MediaWorld  -> mediaworld
    Mediaworld  -> mediaworld
    DentalPro   -> dentalpro
    Dental Pro  -> dentalpro
    H&M         -> hm
    """
    return re.sub(
        r"[^a-z0-9]+",
        "",
        normalize_text(text)
    )


def tokenize(text: str) -> List[str]:
    return re.findall(
        r"[a-z0-9]+",
        normalize_text(text)
    )


def contains_phrase(text: str, phrase: str) -> bool:
    normalized_text = f" {normalize_text(text)} "
    normalized_phrase = normalize_text(phrase)

    return f" {normalized_phrase} " in normalized_text


# ---------------------------------------------------------------------
# ITALIAN NUMBER WORDS
# ---------------------------------------------------------------------

UNITS = [
    "zero",
    "uno",
    "due",
    "tre",
    "quattro",
    "cinque",
    "sei",
    "sette",
    "otto",
    "nove"
]

TEENS = {
    10: "dieci",
    11: "undici",
    12: "dodici",
    13: "tredici",
    14: "quattordici",
    15: "quindici",
    16: "sedici",
    17: "diciassette",
    18: "diciotto",
    19: "diciannove"
}

TENS = {
    20: "venti",
    30: "trenta",
    40: "quaranta",
    50: "cinquanta",
    60: "sessanta",
    70: "settanta",
    80: "ottanta",
    90: "novanta"
}


def integer_to_italian(number: int) -> str:
    if number < 0:
        return "meno " + integer_to_italian(abs(number))

    if number < 10:
        return UNITS[number]

    if number < 20:
        return TEENS[number]

    if number < 100:
        tens_value = (number // 10) * 10
        unit_value = number % 10

        tens_word = TENS[tens_value]

        if unit_value in {1, 8}:
            tens_word = tens_word[:-1]

        if unit_value == 0:
            return tens_word

        return tens_word + UNITS[unit_value]

    if number < 1000:
        hundreds_value = number // 100
        remainder = number % 100

        if hundreds_value == 1:
            hundreds_word = "cento"
        else:
            hundreds_word = (
                integer_to_italian(hundreds_value)
                + "cento"
            )

        if 80 <= remainder < 90:
            hundreds_word = hundreds_word[:-1]

        if remainder == 0:
            return hundreds_word

        return hundreds_word + integer_to_italian(remainder)

    if number < 1_000_000:
        thousands_value = number // 1000
        remainder = number % 1000

        if thousands_value == 1:
            thousands_word = "mille"
        else:
            thousands_word = (
                integer_to_italian(thousands_value)
                + "mila"
            )

        if remainder == 0:
            return thousands_word

        return thousands_word + integer_to_italian(remainder)

    if number < 1_000_000_000:
        millions_value = number // 1_000_000
        remainder = number % 1_000_000

        if millions_value == 1:
            millions_word = "un milione"
        else:
            millions_word = (
                integer_to_italian(millions_value)
                + " milioni"
            )

        if remainder == 0:
            return millions_word

        return (
            millions_word
            + " "
            + integer_to_italian(remainder)
        )

    return str(number)


def spoken_integer_to_italian(number: int) -> str:
    word = integer_to_italian(number)

    if (
        number > 3
        and number % 10 == 3
        and word.endswith("tre")
    ):
        word = word[:-3] + "tré"

    return word


def time_match_to_words(match: re.Match[str]) -> str:
    hour = int(match.group(1))
    minute = int(match.group(2))

    hour_words = spoken_integer_to_italian(hour)

    if minute == 0:
        return hour_words

    return (
        hour_words
        + " e "
        + spoken_integer_to_italian(minute)
    )


def numbers_to_words(text: str) -> str:
    text = re.sub(
        r"\b(\d{1,2}):(\d{2})\b",
        time_match_to_words,
        text
    )

    def integer_match_to_words(
        match: re.Match[str]
    ) -> str:
        digits = match.group(0)

        if len(digits) >= 7:
            return " ".join(
                UNITS[int(digit)]
                for digit in digits
            )

        return spoken_integer_to_italian(int(digits))

    text = re.sub(
        r"\d+",
        integer_match_to_words,
        text
    )

    return text


# ---------------------------------------------------------------------
# ANSWER SANITIZATION
# ---------------------------------------------------------------------

def strip_urls_and_citations(text: str) -> str:
    text = re.sub(r"https?://\S+", "", text)
    text = re.sub(r"www\.\S+", "", text)
    text = re.sub(r"【[^】]*】", "", text)
    text = re.sub(
        r"\[[^\]]*(?:source|fonte|file)[^\]]*\]",
        "",
        text,
        flags=re.IGNORECASE
    )

    return text


def truncate_at_word_boundary(
    text: str,
    max_chars: int
) -> str:
    if len(text) <= max_chars:
        return text

    shortened = text[:max_chars].rstrip()

    if " " in shortened:
        shortened = shortened.rsplit(" ", 1)[0]

    return shortened.rstrip(" ,;:-")


def sanitize_answer(text: str) -> str:
    text = strip_urls_and_citations(text)

    text = text.replace("+", " più ")
    text = text.replace('"', "")
    text = text.replace("“", "")
    text = text.replace("”", "")
    text = text.replace("«", "")
    text = text.replace("»", "")

    text = numbers_to_words(text)

    text = re.sub(r"\s+", " ", text)
    text = re.sub(r"\s+([,.;:!?])", r"\1", text)
    text = re.sub(r",(?=\S)", ", ", text)
    text = re.sub(r";(?=\S)", "; ", text)
    text = text.strip()

    return truncate_at_word_boundary(
        text,
        MAX_ANSWER_CHARS
    )


# ---------------------------------------------------------------------
# CATALOG
# ---------------------------------------------------------------------

def build_catalog(
    mall_data: Dict[str, Any]
) -> List[Dict[str, Any]]:
    catalog: List[Dict[str, Any]] = []

    for item_type, collection_name in (
        ("shop", "shops"),
        ("service", "services"),
        ("event", "events")
    ):
        for item in mall_data.get(collection_name, []):
            entry = {
                "type": item_type,
                "source": f"mall_data.json:{collection_name}",
                "item": item,
                "name_key": normalize_name_key(
                    item.get("name", "")
                ),
                "search_text": normalize_text(
                    " ".join([
                        item.get("name", ""),
                        item.get("category", ""),
                        item.get("floor", ""),
                        item.get("area", ""),
                        item.get("notes", ""),
                        item.get("opening_hours", ""),
                        item.get("date", "")
                    ])
                )
            }

            catalog.append(entry)

    return catalog


def build_name_index(
    catalog: Sequence[Dict[str, Any]]
) -> Dict[str, Dict[str, Any]]:
    return {
        entry["name_key"]: entry
        for entry in catalog
        if entry["name_key"]
    }


def official_names_in_text(
    text: str,
    catalog: Sequence[Dict[str, Any]]
) -> List[Dict[str, Any]]:
    normalized = f" {normalize_text(text)} "
    matches: List[Dict[str, Any]] = []

    for entry in catalog:
        official_name = entry["item"].get("name", "")
        normalized_name = normalize_text(official_name)

        if not normalized_name:
            continue

        if f" {normalized_name} " in normalized:
            matches.append(entry)
            continue

        # Consente MediaWorld/Mediaworld e DentalPro/Dental Pro.
        if (
            len(entry["name_key"]) >= 5
            and entry["name_key"]
            in normalize_name_key(text)
        ):
            matches.append(entry)

    matches.sort(
        key=lambda entry: len(
            entry["item"].get("name", "")
        ),
        reverse=True
    )

    return matches


def get_area_phrase(item: Dict[str, Any]) -> str:
    area = item.get("area", "").strip()

    if not area:
        return ""

    normalized_area = normalize_text(area)

    if normalized_area.startswith("posizione mappa"):
        return area

    if "posizioni mappa" in normalized_area:
        return area

    return area


def first_sentence(text: str) -> str:
    text = re.sub(r"\s+", " ", text).strip()

    if not text:
        return ""

    parts = re.split(r"(?<=[.!?])\s+", text)

    return parts[0].strip()


# ---------------------------------------------------------------------
# ANALYTICS / SHOP EXPOSURE
# ---------------------------------------------------------------------

def ensure_analytics_storage() -> None:
    if not ENABLE_ANALYTICS:
        return

    ANALYTICS_DIR_PATH.mkdir(
        parents=True,
        exist_ok=True
    )


def atomic_write_json(
    path: Path,
    payload: Dict[str, Any]
) -> None:
    path.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    temporary_path = path.with_suffix(
        path.suffix + ".tmp"
    )

    with temporary_path.open(
        "w",
        encoding="utf-8"
    ) as file:
        json.dump(
            payload,
            file,
            ensure_ascii=False,
            indent=2
        )
        file.flush()
        os.fsync(file.fileno())

    temporary_path.replace(path)


def safe_parse_datetime(
    value: Optional[str]
) -> Optional[datetime]:
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


def analytics_utc_now() -> datetime:
    return datetime.now(timezone.utc)


def analytics_local_now() -> datetime:
    try:
        return get_mall_now()
    except Exception:
        return datetime.now().astimezone()


def analytics_date_key(
    value: Optional[datetime] = None
) -> str:
    current = value or analytics_local_now()
    return current.date().isoformat()


def pii_safe_text(text: str) -> str:
    if not ANALYTICS_REDACT_PII:
        return text

    text = re.sub(
        r"\b[A-Z0-9._%+-]+@[A-Z0-9.-]+\.[A-Z]{2,}\b",
        "[email]",
        text,
        flags=re.IGNORECASE
    )

    text = re.sub(
        r"(?<!\w)(?:\+?\d[\d\s().-]{6,}\d)(?!\w)",
        "[telefono]",
        text
    )

    return text


def analytics_session_id(
    session_id: str
) -> str:
    normalized = session_id.strip()

    if not normalized:
        return "anonymous"

    if not ANALYTICS_HASH_SESSION_IDS:
        return normalized

    digest = hashlib.sha256(
        normalized.encode("utf-8")
    ).hexdigest()

    return digest[:20]


def default_shop_exposure_record(
    entry: Dict[str, Any]
) -> Dict[str, Any]:
    item = entry["item"]

    return {
        "name": item.get("name", ""),
        "name_key": entry["name_key"],
        "category": item.get("category", ""),
        "area": item.get("area", ""),
        "total_mentions": 0,
        "answer_mentions": 0,
        "featured_mentions": 0,
        "first_mentioned_utc": None,
        "last_mentioned_utc": None,
        "daily_mentions": {}
    }


def create_default_exposure_state(
    mall_data: Dict[str, Any]
) -> Dict[str, Any]:
    shops: Dict[str, Any] = {}

    for entry in build_catalog(mall_data):
        if entry["type"] != "shop":
            continue

        shops[entry["name_key"]] = (
            default_shop_exposure_record(entry)
        )

    return {
        "version": 1,
        "updated_at_utc": analytics_utc_now().isoformat(),
        "window_days": EXPOSURE_WINDOW_DAYS,
        "shops": shops
    }


def sync_exposure_catalog(
    state: Dict[str, Any],
    mall_data: Dict[str, Any]
) -> bool:
    changed = False
    shops = state.setdefault("shops", {})

    current_keys: set[str] = set()

    for entry in build_catalog(mall_data):
        if entry["type"] != "shop":
            continue

        key = entry["name_key"]
        current_keys.add(key)

        if key not in shops:
            shops[key] = default_shop_exposure_record(
                entry
            )
            changed = True
            continue

        item = entry["item"]
        record = shops[key]

        expected_values = {
            "name": item.get("name", ""),
            "name_key": key,
            "category": item.get("category", ""),
            "area": item.get("area", "")
        }

        for field_name, expected_value in expected_values.items():
            if record.get(field_name) != expected_value:
                record[field_name] = expected_value
                changed = True

        record.setdefault("total_mentions", 0)
        record.setdefault("answer_mentions", 0)
        record.setdefault("featured_mentions", 0)
        record.setdefault("first_mentioned_utc", None)
        record.setdefault("last_mentioned_utc", None)
        record.setdefault("daily_mentions", {})

    # Mantiene i record storici di negozi eventualmente rimossi,
    # ma li marca come non più presenti nel catalogo.
    for key, record in shops.items():
        is_active = key in current_keys

        if record.get("active", True) != is_active:
            record["active"] = is_active
            changed = True

    return changed


def load_exposure_state(
    mall_data: Optional[Dict[str, Any]] = None,
    force_reload: bool = False
) -> Dict[str, Any]:
    global _EXPOSURE_STATE_CACHE

    mall_data = mall_data or get_mall_data()

    with _ANALYTICS_LOCK:
        if (
            _EXPOSURE_STATE_CACHE is not None
            and not force_reload
        ):
            sync_exposure_catalog(
                _EXPOSURE_STATE_CACHE,
                mall_data
            )
            return _EXPOSURE_STATE_CACHE

        ensure_analytics_storage()

        state: Dict[str, Any]

        if SHOP_EXPOSURE_STATE_PATH.exists():
            try:
                with SHOP_EXPOSURE_STATE_PATH.open(
                    "r",
                    encoding="utf-8"
                ) as file:
                    loaded = json.load(file)

                state = (
                    loaded
                    if isinstance(loaded, dict)
                    else create_default_exposure_state(
                        mall_data
                    )
                )

            except (
                OSError,
                json.JSONDecodeError
            ):
                state = create_default_exposure_state(
                    mall_data
                )
        else:
            state = create_default_exposure_state(
                mall_data
            )

        changed = sync_exposure_catalog(
            state,
            mall_data
        )

        state["window_days"] = EXPOSURE_WINDOW_DAYS

        _EXPOSURE_STATE_CACHE = state

        if changed or not SHOP_EXPOSURE_STATE_PATH.exists():
            state["updated_at_utc"] = (
                analytics_utc_now().isoformat()
            )
            atomic_write_json(
                SHOP_EXPOSURE_STATE_PATH,
                state
            )

        return state


def save_exposure_state(
    state: Dict[str, Any]
) -> None:
    global _EXPOSURE_STATE_CACHE

    with _ANALYTICS_LOCK:
        state["updated_at_utc"] = (
            analytics_utc_now().isoformat()
        )
        state["window_days"] = EXPOSURE_WINDOW_DAYS

        atomic_write_json(
            SHOP_EXPOSURE_STATE_PATH,
            state
        )

        _EXPOSURE_STATE_CACHE = state


def window_start_date(
    days: int
) -> str:
    safe_days = max(1, days)

    return (
        analytics_local_now().date()
        - timedelta(days=safe_days - 1)
    ).isoformat()


def exposure_window_count(
    record: Dict[str, Any],
    days: Optional[int] = None
) -> int:
    period_days = (
        days
        if days is not None
        else EXPOSURE_WINDOW_DAYS
    )

    cutoff = window_start_date(period_days)

    return sum(
        int(count)
        for date_key, count
        in record.get("daily_mentions", {}).items()
        if date_key >= cutoff
    )


def shop_exposure_metrics(
    entry: Dict[str, Any],
    days: Optional[int] = None
) -> Dict[str, Any]:
    if (
        not ENABLE_ANALYTICS
        or entry["type"] != "shop"
    ):
        return {
            "total_mentions": 0,
            "window_mentions": 0,
            "answer_mentions": 0,
            "featured_mentions": 0,
            "last_mentioned_utc": None,
            "days_since_last_mention": None,
            "never_mentioned": False
        }

    state = load_exposure_state()
    record = state.get(
        "shops",
        {}
    ).get(entry["name_key"])

    if not record:
        record = default_shop_exposure_record(
            entry
        )

    last_value = record.get(
        "last_mentioned_utc"
    )
    last_datetime = safe_parse_datetime(last_value)

    days_since: Optional[int] = None

    if last_datetime is not None:
        delta = (
            analytics_utc_now()
            - last_datetime
        )
        days_since = max(0, delta.days)

    total_mentions = int(
        record.get("total_mentions", 0)
    )

    return {
        "total_mentions": total_mentions,
        "window_mentions": exposure_window_count(
            record,
            days
        ),
        "answer_mentions": int(
            record.get("answer_mentions", 0)
        ),
        "featured_mentions": int(
            record.get("featured_mentions", 0)
        ),
        "last_mentioned_utc": last_value,
        "days_since_last_mention": days_since,
        "never_mentioned": total_mentions == 0
    }


def shop_fairness_boost(
    entry: Dict[str, Any]
) -> float:
    if (
        not ENABLE_ANALYTICS
        or not FAIRNESS_ENABLED
        or entry["type"] != "shop"
    ):
        return 0.0

    metrics = shop_exposure_metrics(entry)
    window_mentions = int(
        metrics["window_mentions"]
    )

    boost = (
        FAIRNESS_BASE_BOOST
        / (1.0 + window_mentions)
    )

    if metrics["never_mentioned"]:
        boost += FAIRNESS_NEVER_MENTIONED_BOOST

    days_since = metrics[
        "days_since_last_mention"
    ]

    if days_since is None:
        stale_boost = FAIRNESS_STALE_BOOST_MAX
    else:
        stale_boost = (
            min(
                days_since / FAIRNESS_STALE_DAYS,
                1.0
            )
            * FAIRNESS_STALE_BOOST_MAX
        )

    boost += stale_boost

    return min(
        boost,
        FAIRNESS_MAX_BOOST
    )


def append_jsonl(
    path: Path,
    payload: Dict[str, Any]
) -> None:
    path.parent.mkdir(
        parents=True,
        exist_ok=True
    )

    serialized = json.dumps(
        payload,
        ensure_ascii=False,
        separators=(",", ":")
    )

    with path.open(
        "a",
        encoding="utf-8"
    ) as file:
        file.write(serialized)
        file.write("\n")
        file.flush()


def update_shop_exposures(
    entries: Sequence[Dict[str, Any]],
    source: str,
    timestamp_utc: Optional[datetime] = None
) -> None:
    if not ENABLE_ANALYTICS:
        return

    unique_entries: Dict[str, Dict[str, Any]] = {
        entry["name_key"]: entry
        for entry in entries
        if entry["type"] == "shop"
    }

    if not unique_entries:
        return

    current_utc = (
        timestamp_utc
        or analytics_utc_now()
    )
    current_local = analytics_local_now()
    date_key = analytics_date_key(
        current_local
    )

    with _ANALYTICS_LOCK:
        state = load_exposure_state()
        shops = state.setdefault("shops", {})

        for key, entry in unique_entries.items():
            record = shops.setdefault(
                key,
                default_shop_exposure_record(
                    entry
                )
            )

            record["total_mentions"] = (
                int(
                    record.get(
                        "total_mentions",
                        0
                    )
                )
                + 1
            )

            if source == "assistant_answer":
                record["answer_mentions"] = (
                    int(
                        record.get(
                            "answer_mentions",
                            0
                        )
                    )
                    + 1
                )
            elif source == "featured_rotation":
                record["featured_mentions"] = (
                    int(
                        record.get(
                            "featured_mentions",
                            0
                        )
                    )
                    + 1
                )

            if not record.get(
                "first_mentioned_utc"
            ):
                record["first_mentioned_utc"] = (
                    current_utc.isoformat()
                )

            record["last_mentioned_utc"] = (
                current_utc.isoformat()
            )

            daily_mentions = record.setdefault(
                "daily_mentions",
                {}
            )

            daily_mentions[date_key] = (
                int(
                    daily_mentions.get(
                        date_key,
                        0
                    )
                )
                + 1
            )

        save_exposure_state(state)


def extract_answer_catalog_entries(
    answer: str,
    mall_data: Dict[str, Any]
) -> List[Dict[str, Any]]:
    catalog = build_catalog(mall_data)
    matches = official_names_in_text(
        answer,
        catalog
    )

    unique: Dict[str, Dict[str, Any]] = {}

    for entry in matches:
        unique[entry["name_key"]] = entry

    return list(unique.values())


def read_jsonl_records(
    path: Path
) -> List[Dict[str, Any]]:
    if not path.exists():
        return []

    records: List[Dict[str, Any]] = []

    with path.open(
        "r",
        encoding="utf-8"
    ) as file:
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


def filter_records_by_days(
    records: Sequence[Dict[str, Any]],
    days: int
) -> List[Dict[str, Any]]:
    cutoff = (
        analytics_utc_now()
        - timedelta(days=max(1, days))
    )

    filtered: List[Dict[str, Any]] = []

    for record in records:
        timestamp = safe_parse_datetime(
            record.get("timestamp_utc")
        )

        if timestamp is None or timestamp >= cutoff:
            filtered.append(record)

    return filtered


def analytics_response_mode(
    response: Dict[str, Any]
) -> str:
    debug_info = str(
        response.get("DebugInfo", "")
    ).lower()

    if "weather" in debug_info:
        return "weather"

    if "general knowledge" in debug_info:
        return "general_knowledge"

    if "small talk" in debug_info:
        return "small_talk"

    if "direct" in debug_info:
        return "direct"

    if "grounded" in debug_info:
        return "grounded"

    if "clarification" in debug_info:
        return "clarification"

    return "other"


def record_interaction(
    request: AssistantRequest,
    response: Dict[str, Any],
    intents: Sequence[str],
    response_ms: float
) -> None:
    if not ENABLE_ANALYTICS:
        return

    mall_data = get_mall_data()
    answer = str(
        response.get(
            "AnswerToSay",
            ""
        )
    )

    mentioned_entries = (
        extract_answer_catalog_entries(
            answer,
            mall_data
        )
    )

    mentioned_shops = [
        entry
        for entry in mentioned_entries
        if entry["type"] == "shop"
    ]

    mentioned_services = [
        entry
        for entry in mentioned_entries
        if entry["type"] == "service"
    ]

    mentioned_events = [
        entry
        for entry in mentioned_entries
        if entry["type"] == "event"
    ]

    current_utc = analytics_utc_now()
    current_local = analytics_local_now()

    session_id = (
        request.SessionId
        or request.sessionId
        or ""
    )

    language = (
        request.Language
        or request.language
        or "it"
    )

    interaction_source = (
        request.InteractionSource
        or request.interactionSource
        or "unknown"
    )

    mall_id = (
        request.MallId
        or request.mallId
        or ""
    )

    question = (
        request.UserText
        or request.userText
        or ""
    ).strip()

    suggested_shops = response.get(
        "SuggestedShops",
        []
    )

    record = {
        "record_id": str(uuid.uuid4()),
        "timestamp_utc": current_utc.isoformat(),
        "timestamp_local": current_local.isoformat(),
        "local_date": current_local.date().isoformat(),
        "session_id": analytics_session_id(
            session_id
        ),
        "mall_id": mall_id,
        "language": language,
        "interaction_source": interaction_source,
        "question": pii_safe_text(question),
        "question_normalized": normalize_text(
            pii_safe_text(question)
        ),
        "answer": pii_safe_text(answer),
        "intents": list(intents),
        "response_mode": analytics_response_mode(
            response
        ),
        "mentioned_items": [
            {
                "name": entry["item"].get(
                    "name",
                    ""
                ),
                "type": entry["type"],
                "category": entry["item"].get(
                    "category",
                    ""
                ),
                "area": entry["item"].get(
                    "area",
                    ""
                )
            }
            for entry in mentioned_entries
        ],
        "mentioned_shops": [
            entry["item"].get("name", "")
            for entry in mentioned_shops
        ],
        "mentioned_services": [
            entry["item"].get("name", "")
            for entry in mentioned_services
        ],
        "mentioned_events": [
            entry["item"].get("name", "")
            for entry in mentioned_events
        ],
        "suggested_item_names": [
            item.get("Name", "")
            for item in suggested_shops
            if isinstance(item, dict)
            and item.get("Name")
        ],
        "confidence": response.get(
            "Confidence",
            0
        ),
        "needs_clarification": bool(
            response.get(
                "bNeedsClarification",
                False
            )
        ),
        "is_out_of_scope": bool(
            response.get(
                "bIsOutOfScope",
                False
            )
        ),
        "answer_chars": len(answer),
        "response_ms": round(
            response_ms,
            2
        ),
        "openai_enabled": bool(
            USE_OPENAI and openai_client
        ),
        "openai_model": OPENAI_MODEL
    }

    if ANALYTICS_INCLUDE_DEBUG_INFO:
        record["debug_info"] = response.get(
            "DebugInfo",
            ""
        )

    with _ANALYTICS_LOCK:
        ensure_analytics_storage()

        append_jsonl(
            INTERACTION_LOG_PATH,
            record
        )

        update_shop_exposures(
            mentioned_shops,
            source="assistant_answer",
            timestamp_utc=current_utc
        )


def shop_coverage_rows(
    days: Optional[int] = None
) -> List[Dict[str, Any]]:
    period_days = (
        days
        if days is not None
        else EXPOSURE_WINDOW_DAYS
    )

    state = load_exposure_state()
    rows: List[Dict[str, Any]] = []

    for record in state.get(
        "shops",
        {}
    ).values():
        if not record.get("active", True):
            continue

        window_mentions = exposure_window_count(
            record,
            period_days
        )

        rows.append({
            "name": record.get("name", ""),
            "category": record.get("category", ""),
            "area": record.get("area", ""),
            "mentions_period": window_mentions,
            "total_mentions": int(
                record.get("total_mentions", 0)
            ),
            "answer_mentions": int(
                record.get("answer_mentions", 0)
            ),
            "featured_mentions": int(
                record.get("featured_mentions", 0)
            ),
            "first_mentioned_utc": record.get(
                "first_mentioned_utc"
            ),
            "last_mentioned_utc": record.get(
                "last_mentioned_utc"
            ),
            "never_mentioned": (
                int(
                    record.get(
                        "total_mentions",
                        0
                    )
                )
                == 0
            )
        })

    rows.sort(
        key=lambda row: (
            row["mentions_period"],
            row["last_mentioned_utc"] or "",
            row["total_mentions"],
            row["name"]
        )
    )

    return rows


def build_analytics_summary(
    days: int
) -> Dict[str, Any]:
    safe_days = max(1, min(days, 3650))

    interactions = filter_records_by_days(
        read_jsonl_records(
            INTERACTION_LOG_PATH
        ),
        safe_days
    )

    coverage_rows = shop_coverage_rows(
        safe_days
    )

    total_shops = len(coverage_rows)
    covered_shops = sum(
        1
        for row in coverage_rows
        if row["mentions_period"] > 0
    )

    intent_counter: Counter[str] = Counter()
    shop_counter: Counter[str] = Counter()
    question_counter: Counter[str] = Counter()
    mode_counter: Counter[str] = Counter()

    session_ids: set[str] = set()
    response_times: List[float] = []
    clarification_count = 0
    out_of_scope_count = 0

    for record in interactions:
        session_ids.add(
            str(record.get("session_id", ""))
        )

        response_times.append(
            float(
                record.get(
                    "response_ms",
                    0.0
                )
            )
        )

        clarification_count += int(
            bool(
                record.get(
                    "needs_clarification",
                    False
                )
            )
        )

        out_of_scope_count += int(
            bool(
                record.get(
                    "is_out_of_scope",
                    False
                )
            )
        )

        intent_counter.update(
            record.get("intents", [])
        )
        shop_counter.update(
            record.get("mentioned_shops", [])
        )

        normalized_question = str(
            record.get(
                "question_normalized",
                ""
            )
        )

        if normalized_question:
            question_counter[
                normalized_question
            ] += 1

        mode_counter[
            str(
                record.get(
                    "response_mode",
                    "other"
                )
            )
        ] += 1

    average_response_ms = (
        round(
            sum(response_times)
            / len(response_times),
            2
        )
        if response_times
        else 0.0
    )

    coverage_percent = (
        round(
            covered_shops
            / total_shops
            * 100.0,
            2
        )
        if total_shops
        else 0.0
    )

    return {
        "period_days": safe_days,
        "generated_at_utc": (
            analytics_utc_now().isoformat()
        ),
        "interactions": len(interactions),
        "unique_sessions": len(
            session_ids - {""}
        ),
        "average_response_ms": average_response_ms,
        "clarification_count": clarification_count,
        "out_of_scope_count": out_of_scope_count,
        "shop_coverage": {
            "total_shops": total_shops,
            "shops_mentioned": covered_shops,
            "shops_not_mentioned": (
                total_shops - covered_shops
            ),
            "coverage_percent": coverage_percent
        },
        "top_intents": [
            {
                "intent": name,
                "count": count
            }
            for name, count
            in intent_counter.most_common(15)
        ],
        "top_mentioned_shops": [
            {
                "name": name,
                "count": count
            }
            for name, count
            in shop_counter.most_common(15)
        ],
        "least_exposed_shops": (
            coverage_rows[:15]
        ),
        "top_questions": [
            {
                "question_normalized": name,
                "count": count
            }
            for name, count
            in question_counter.most_common(15)
        ],
        "response_modes": dict(
            mode_counter
        )
    }


def rows_to_csv_text(
    rows: Sequence[Dict[str, Any]],
    fieldnames: Sequence[str]
) -> str:
    output = io.StringIO()
    writer = csv.DictWriter(
        output,
        fieldnames=list(fieldnames),
        extrasaction="ignore"
    )
    writer.writeheader()

    for row in rows:
        writer.writerow(row)

    return output.getvalue()


def interactions_csv_rows(
    days: int
) -> List[Dict[str, Any]]:
    records = filter_records_by_days(
        read_jsonl_records(
            INTERACTION_LOG_PATH
        ),
        days
    )

    rows: List[Dict[str, Any]] = []

    for record in records:
        rows.append({
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
            "question": record.get(
                "question",
                ""
            ),
            "answer": record.get(
                "answer",
                ""
            ),
            "intents": "|".join(
                record.get(
                    "intents",
                    []
                )
            ),
            "mentioned_shops": "|".join(
                record.get(
                    "mentioned_shops",
                    []
                )
            ),
            "mentioned_services": "|".join(
                record.get(
                    "mentioned_services",
                    []
                )
            ),
            "response_mode": record.get(
                "response_mode",
                ""
            ),
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
            "response_ms": record.get(
                "response_ms",
                0
            )
        })

    return rows


def next_featured_shop_entry() -> Optional[Dict[str, Any]]:
    mall_data = get_mall_data()
    catalog = [
        entry
        for entry in build_catalog(
            mall_data
        )
        if entry["type"] == "shop"
    ]

    if not catalog:
        return None

    def sort_key(
        entry: Dict[str, Any]
    ) -> Tuple[int, int, str, str]:
        metrics = shop_exposure_metrics(
            entry
        )

        last_value = (
            metrics.get(
                "last_mentioned_utc"
            )
            or ""
        )

        return (
            int(
                metrics.get(
                    "window_mentions",
                    0
                )
            ),
            int(
                metrics.get(
                    "total_mentions",
                    0
                )
            ),
            last_value,
            entry["item"].get(
                "name",
                ""
            )
        )

    return sorted(
        catalog,
        key=sort_key
    )[0]


def featured_shop_payload(
    entry: Dict[str, Any]
) -> Dict[str, Any]:
    item = entry["item"]
    metrics = shop_exposure_metrics(
        entry
    )

    short_description = truncate_at_word_boundary(
        first_sentence(
            item.get("notes", "")
        ),
        150
    )

    answer = sanitize_answer(
        f"Oggi puoi scoprire "
        f"{item.get('name', '')}, "
        f"{get_area_phrase(item)}. "
        f"{short_description}"
    )

    return {
        "AnswerToSay": answer,
        "Name": item.get("name", ""),
        "Category": item.get(
            "category",
            ""
        ),
        "Area": item.get("area", ""),
        "Notes": item.get("notes", ""),
        "Exposure": metrics
    }


def record_featured_shop_exposure(
    entry: Dict[str, Any],
    channel: str
) -> Dict[str, Any]:
    if not ENABLE_ANALYTICS:
        return {
            "available": False,
            "reason": "analytics_disabled"
        }

    current_utc = analytics_utc_now()
    payload = featured_shop_payload(
        entry
    )

    event = {
        "record_id": str(uuid.uuid4()),
        "timestamp_utc": current_utc.isoformat(),
        "timestamp_local": (
            analytics_local_now().isoformat()
        ),
        "channel": channel,
        "shop_name": entry["item"].get(
            "name",
            ""
        ),
        "category": entry["item"].get(
            "category",
            ""
        ),
        "area": entry["item"].get(
            "area",
            ""
        ),
        "answer_to_say": payload[
            "AnswerToSay"
        ]
    }

    with _ANALYTICS_LOCK:
        append_jsonl(
            FEATURED_EXPOSURE_LOG_PATH,
            event
        )

        update_shop_exposures(
            [entry],
            source="featured_rotation",
            timestamp_utc=current_utc
        )

    return payload


# ---------------------------------------------------------------------
# REQUEST HELPERS
# ---------------------------------------------------------------------

def get_request_text(
    request: AssistantRequest
) -> str:
    return (
        request.UserText
        or request.userText
        or ""
    ).strip()


def get_request_history(
    request: AssistantRequest
) -> List[str]:
    return (
        request.ConversationHistory
        or request.conversationHistory
        or []
    )


def get_request_session_id(
    request: AssistantRequest
) -> str:
    return (
        request.SessionId
        or request.sessionId
        or ""
    ).strip()


def get_tts_text(
    request: TtsRequest
) -> str:
    return (
        request.Text
        or request.text
        or ""
    ).strip()


# ---------------------------------------------------------------------
# INTENTS
# ---------------------------------------------------------------------

INTENT_HINTS: Dict[str, List[str]] = {
    "restroom": [
        "bagno", "bagni", "wc", "toilette", "servizi igienici"
    ],
    "current_time": [
        "che ore sono",
        "che ora e",
        "che ora è",
        "ora esatta",
        "mi dici l'ora"
    ],
    "current_date": [
        "che giorno e",
        "che giorno è",
        "data di oggi",
        "oggi che giorno e",
        "oggi che giorno è"
    ],
    "weather": [
        "meteo",
        "che tempo fa",
        "come e il tempo",
        "come è il tempo",
        "piove",
        "piovera",
        "pioverà",
        "previsioni",
        "fa caldo",
        "fa freddo",
        "serve l'ombrello"
    ],
    "hours": [
        "orario",
        "orari",
        "aperto",
        "aperta",
        "apre",
        "chiude",
        "chiusura",
        "apertura"
    ],
    "contact": [
        "telefono",
        "whatsapp",
        "contatto",
        "contatti",
        "numero del centro"
    ],
    "directions": [
        "come arrivare",
        "come raggiungere",
        "dove si trova il centro",
        "autobus",
        "stazione",
        "uscita"
    ],
    "events": [
        "evento",
        "eventi",
        "iniziativa",
        "laboratorio",
        "cosa succede",
        "programma"
    ],
    "assistance": [
        "ho perso",
        "ho smarrito",
        "oggetto smarrito",
        "oggetti smarriti",
        "bambino perso",
        "mi sono perso",
        "assistenza",
        "informazioni del centro",
        "aiuto dal personale"
    ],
    "dental": [
        "mal di denti",
        "male ai denti",
        "dente",
        "denti",
        "dentista",
        "odontoiatra",
        "odontoiatria"
    ],
    "optical": [
        "ottico",
        "ottica",
        "occhiali",
        "lenti",
        "vista",
        "vedere meglio",
        "controllo della vista"
    ],
    "pharmacy": [
        "farmacia",
        "parafarmacia",
        "farmaco",
        "farmaci",
        "medicina",
        "medicine",
        "cerotto",
        "antidolorifico",
        "mal di testa"
    ],
    "hair": [
        "parrucchiere",
        "capelli",
        "piega",
        "taglio di capelli",
        "colorazione"
    ],
    "tailor": [
        "sartoria",
        "orlo",
        "ricamo",
        "aggiustare un vestito",
        "riparare un vestito"
    ],
    "laundry": [
        "lavanderia",
        "lavare",
        "lavaggio a secco",
        "sanificare"
    ],
    "travel": [
        "agenzia di viaggi",
        "vacanza",
        "viaggio",
        "prenotare un viaggio"
    ],
    "groceries": [
        "fare la spesa",
        "spesa alimentare",
        "supermercato",
        "ipermercato",
        "ingredienti",
        "farina",
        "uova",
        "latte",
        "zucchero",
        "lievito",
        "prodotti alimentari"
    ],
    "cooking_tools": [
        "teglia",
        "stampo per torta",
        "pentola",
        "padella",
        "utensili da cucina",
        "accessori da cucina"
    ],
    "dessert": [
        "dolce pronto",
        "torta pronta",
        "comprare una torta",
        "pasticceria",
        "dessert",
        "gelato",
        "cioccolatini"
    ],
    "books": [
        "libro",
        "libri",
        "libreria",
        "romanzo",
        "lettura",
        "manuale"
    ],
    "toys": [
        "giocattolo",
        "giocattoli",
        "puzzle",
        "gioco da tavolo",
        "giochi da tavolo"
    ],
    "phone_accessories": [
        "cover",
        "custodia telefono",
        "pellicola telefono",
        "caricatore",
        "cavo usb",
        "accessori smartphone",
        "accessori telefono"
    ],
    "generic_shopping": [
        "vorrei comprare",
        "devo comprare",
        "dove posso comprare",
        "dove posso trovare",
        "mi serve",
        "sto cercando",
        "cerco un",
        "cerco una"
    ],
    "gift": [
        "regalo",
        "regali",
        "idea regalo",
        "pensiero",
        "compleanno",
        "anniversario"
    ],
    "gaming": [
        "videogioco",
        "videogiochi",
        "console",
        "gaming",
        "gamer",
        "playstation",
        "xbox",
        "nintendo",
        "intrattenimento tecnologico"
    ],
    "telephony": [
        "sim",
        "ricarica telefonica",
        "operatore telefonico",
        "telefonia",
        "fibra",
        "internet casa",
        "abbonamento telefonico",
        "piano telefonico"
    ],
    "tech": [
        "tecnologia",
        "tecnologico",
        "elettronica",
        "smartphone",
        "telefono",
        "cuffie",
        "gadget",
        "computer",
        "notebook",
        "intrattenimento tecnologico"
    ],
    "beauty": [
        "profumo",
        "profumi",
        "profumeria",
        "makeup",
        "make up",
        "trucco",
        "cosmetici",
        "skincare",
        "bellezza"
    ],
    "jewelry": [
        "gioiello",
        "gioielli",
        "gioielleria",
        "anello",
        "collana",
        "bracciale",
        "orecchini",
        "orologio"
    ],
    "fashion": [
        "abbigliamento",
        "vestito",
        "vestiti",
        "moda",
        "outfit",
        "elegante",
        "scarpe",
        "sneaker",
        "borsa",
        "accessori"
    ],
    "home": [
        "casa",
        "casalinghi",
        "oggettistica",
        "decorazioni",
        "biancheria",
        "arredamento",
        "pentole",
        "tessili"
    ],
    "food": [
        "mangiare",
        "fame",
        "pranzo",
        "cena",
        "ristorante",
        "pizzeria",
        "bar",
        "caffe",
        "caffè",
        "hamburger",
        "pizza",
        "pollo",
        "poke"
    ],
    "children": [
        "bambino",
        "bambina",
        "bambini",
        "neonato",
        "neonata",
        "ragazzino",
        "ragazzina"
    ],
    "sport": [
        "sport",
        "sportivo",
        "sportiva",
        "calcio",
        "running",
        "fitness",
        "padel",
        "palestra"
    ],
    "joke": [
        "barzelletta",
        "battuta",
        "fammi ridere",
        "raccontami qualcosa di divertente"
    ],
    "greeting": [
        "ciao",
        "buongiorno",
        "buonasera",
        "salve"
    ],
    "thanks": [
        "grazie",
        "gentilissima",
        "molto utile",
        "perfetto"
    ]
}


def detect_intents(text: str) -> List[str]:
    normalized = normalize_text(text)
    intents: List[str] = []

    for intent, hints in INTENT_HINTS.items():
        if any(
            normalize_text(hint) in normalized
            for hint in hints
        ):
            intents.append(intent)

    # "Che ore sono?" non è una domanda sugli orari del centro.
    if "current_time" in intents and "hours" in intents:
        intents.remove("hours")

    # Comprensione contestuale della torta.
    cake_words = {
        "torta",
        "biscotti",
        "dolce"
    }

    is_cake_request = any(
        word in normalized
        for word in cake_words
    )

    wants_to_prepare = any(
        phrase in normalized
        for phrase in [
            "voglio fare",
            "devo fare",
            "preparare",
            "cucinare",
            "mi servono gli ingredienti",
            "ingredienti per"
        ]
    )

    wants_ready_product = any(
        phrase in normalized
        for phrase in [
            "comprare una torta",
            "torta pronta",
            "dolce pronto",
            "pasticceria",
            "gia pronta",
            "già pronta"
        ]
    )

    if is_cake_request and wants_to_prepare:
        if "groceries" not in intents:
            intents.append("groceries")

        if "dessert" in intents:
            intents.remove("dessert")

    if is_cake_request and wants_ready_product:
        if "dessert" not in intents:
            intents.append("dessert")

    # Una cover o un caricatore sono accessori, non una richiesta
    # generica a un operatore telefonico.
    if "phone_accessories" in intents:
        if "telephony" in intents:
            intents.remove("telephony")

        if "tech" in intents:
            intents.remove("tech")

        if "contact" in intents:
            intents.remove("contact")

    # La parola telefono indica i contatti del centro soltanto
    # quando è associata esplicitamente a numero, contatti o centro.
    if "contact" in intents:
        contact_phrases = [
            "numero del centro",
            "numero di telefono",
            "telefono del centro",
            "contatti del centro",
            "whatsapp del centro",
            "come posso contattare"
        ]

        if not any(
            phrase in normalized
            for phrase in contact_phrases
        ):
            intents.remove("contact")

    # Rimuove duplicati mantenendo l'ordine.
    return list(dict.fromkeys(intents))


# ---------------------------------------------------------------------
# EXPLICIT STORE/SERVICE REQUESTS
# ---------------------------------------------------------------------

EXPLICIT_QUERY_MARKERS = [
    "dove si trova",
    "dove trovo",
    "c'e",
    "ce ",
    "avete",
    "esiste",
    "e presente",
    "si trova",
    "orario di",
    "orari di",
    "cosa vende",
    "che cosa vende"
]

GENERIC_ENTITY_WORDS = {
    "qualche",
    "qualcuno",
    "qualcosa",
    "negozio",
    "servizio",
    "studio",
    "dentistico",
    "dentista",
    "odontoiatrico",
    "ottico",
    "ottica",
    "ristorante",
    "bar",
    "bagno",
    "bagni",
    "toilette",
    "regalo",
    "profumeria",
    "gioielleria",
    "parrucchiere",
    "lavanderia",
    "sartoria",
    "abbigliamento",
    "scarpe",
    "elettronica",
    "casa",
    "area",
    "giochi"
}


def is_explicit_entity_question(text: str) -> bool:
    normalized = normalize_text(text)

    patterns = [
        r"^\s*dove\s+(?:si\s+trova|trovo)\b",
        r"^\s*(?:c['’]?\s*e|ce|avete|esiste)\b",
        r"^\s*(?:e\s+presente|si\s+trova)\b",
        r"^\s*(?:orario|orari)\s+(?:di|del|della)\b",
        r"^\s*(?:cosa|che\s+cosa)\s+vende\b"
    ]

    return any(
        re.search(pattern, normalized)
        for pattern in patterns
    )


def clean_entity_candidate(candidate: str) -> str:
    candidate = candidate.strip(" \t\r\n?!.,;:")

    candidate = re.sub(
        r"\b(?:qui|nel centro|al centro|"
        r"in questo centro commerciale|"
        r"al centro commerciale)\b.*$",
        "",
        candidate,
        flags=re.IGNORECASE
    )

    candidate = re.sub(
        r"^(?:il|lo|la|i|gli|le|un|uno|una)\s+",
        "",
        candidate,
        flags=re.IGNORECASE
    )

    return candidate.strip(" \t\r\n?!.,;:")


def extract_explicit_entity_candidate(
    text: str
) -> Optional[str]:
    normalized_query = normalize_text(text)

    patterns = [
        r"dove\s+(?:si\s+trova|trovo)\s+(?:il\s+negozio\s+)?(?P<name>.+)$",
        r"(?:c['’]?\s*e|ce|avete|esiste)\s+(?:il\s+negozio\s+)?(?P<name>.+)$",
        r"(?:orario|orari)\s+(?:di|del|della)\s+(?P<name>.+)$",
        r"(?:cosa|che\s+cosa)\s+vende\s+(?P<name>.+)$"
    ]

    for pattern in patterns:
        match = re.search(
            pattern,
            normalized_query,
            flags=re.IGNORECASE
        )

        if match:
            candidate = clean_entity_candidate(
                match.group("name")
            )

            if candidate:
                return candidate

    return None


def is_likely_specific_unknown_name(
    candidate: str
) -> bool:
    tokens = tokenize(candidate)

    if not tokens or len(tokens) > 6:
        return False

    if any(
        token in GENERIC_ENTITY_WORDS
        for token in tokens
    ):
        return False

    if tokens[0] in {"un", "una", "qualche"}:
        return False

    return True


def find_exact_catalog_entry(
    requested_name: str,
    name_index: Dict[str, Dict[str, Any]]
) -> Optional[Dict[str, Any]]:
    return name_index.get(
        normalize_name_key(requested_name)
    )


# ---------------------------------------------------------------------
# HISTORY / ROTATION
# ---------------------------------------------------------------------

def get_previously_mentioned_name_keys(
    history: Sequence[str],
    catalog: Sequence[Dict[str, Any]]
) -> set[str]:
    mentioned: set[str] = set()

    history_text = " ".join(history)

    for entry in official_names_in_text(
        history_text,
        catalog
    ):
        mentioned.add(entry["name_key"])

    return mentioned


# ---------------------------------------------------------------------
# RETRIEVAL
# ---------------------------------------------------------------------

STOPWORDS = {
    "il", "lo", "la", "i", "gli", "le",
    "un", "uno", "una",
    "di", "a", "da", "in", "con", "su", "per",
    "tra", "fra", "e", "o", "ma", "anche",
    "che", "cosa", "dove", "come", "quando",
    "vorrei", "voglio", "cerco", "cercare",
    "trovare", "serve", "bisogno", "qualcosa",
    "mi", "me", "mio", "mia", "miei", "mie",
    "tu", "ti", "puoi", "potresti",
    "del", "dello", "della", "dei", "degli",
    "delle", "nel", "nello", "nella", "nei",
    "negli", "nelle", "al", "allo", "alla",
    "ai", "agli", "alle", "questo", "questa",
    "centro", "commerciale"
}


def meaningful_tokens(text: str) -> List[str]:
    return [
        token
        for token in tokenize(text)
        if token not in STOPWORDS
        and len(token) > 1
    ]


def recipient_profile_tokens(text: str) -> set[str]:
    normalized = normalize_text(text)
    profile: set[str] = set()

    profiles = {
        "older_woman": [
            "nonna",
            "nonnina",
            "signora anziana",
            "donna anziana",
            "donna adulta"
        ],
        "young_woman": [
            "ragazza",
            "fidanzata",
            "compagna",
            "giovane donna"
        ],
        "teen": [
            "adolescente",
            "ragazzo",
            "ragazza adolescente",
            "quattordicenne",
            "quindicenne",
            "sedicenne",
            "diciassettenne"
        ],
        "child": [
            "bambino",
            "bambina",
            "neonato",
            "neonata"
        ],
        "man": [
            "uomo",
            "marito",
            "fidanzato",
            "papà",
            "papa",
            "nonno"
        ]
    }

    for profile_name, hints in profiles.items():
        if any(
            normalize_text(hint) in normalized
            for hint in hints
        ):
            profile.add(profile_name)

    return profile


def category_or_notes_contains(
    item: Dict[str, Any],
    keywords: Sequence[str]
) -> bool:
    searchable = normalize_text(
        " ".join([
            item.get("name", ""),
            item.get("category", ""),
            item.get("notes", "")
        ])
    )

    return any(
        normalize_text(keyword) in searchable
        for keyword in keywords
    )


def entry_is_eligible_for_intents(
    entry: Dict[str, Any],
    user_text: str,
    intents: Sequence[str]
) -> bool:
    """
    Applica filtri rigidi prima del ranking.

    Evita che un intento molto specifico, per esempio odontoiatria,
    produca candidati genericamente appartenenti alla cura della persona.
    """
    item = entry["item"]
    entry_type = entry["type"]
    name_key = entry["name_key"]

    service_filters = {
        "dental": [
            "odontoiatria",
            "odontoiatrico",
            "dentale",
            "dental pro"
        ],
        "optical": [
            "ottica",
            "occhiali",
            "lenti a contatto",
            "cura della vista"
        ],
        "hair": [
            "parrucchiere",
            "capelli",
            "tagli",
            "pieghe"
        ],
        "tailor": [
            "sartoria",
            "riparazioni sartoriali",
            "orlo",
            "ricami"
        ],
        "laundry": [
            "lavanderia",
            "lavaggio a secco",
            "sanificazione"
        ],
        "travel": [
            "agenzia di viaggi",
            "vacanze su misura",
            "viaggio"
        ]
    }

    active_service_intents = [
        intent
        for intent in service_filters
        if intent in intents
    ]

    if active_service_intents:
        return any(
            category_or_notes_contains(
                item,
                service_filters[intent]
            )
            for intent in active_service_intents
        )

    if "restroom" in intents:
        return (
            entry_type == "service"
            and category_or_notes_contains(
                item,
                [
                    "bagni",
                    "wc",
                    "servizi igienici",
                    "toilette"
                ]
            )
        )

    if "gaming" in intents:
        return (
            entry_type == "shop"
            and (
                name_key in {
                    "gamelife",
                    "mediaworld"
                }
                or category_or_notes_contains(
                    item,
                    [
                        "videogiochi",
                        "console",
                        "piattaforme di gioco",
                        "articoli da collezione"
                    ]
                )
            )
        )

    telecom_names = {
        "tim",
        "vodafone",
        "wind3"
    }

    if "telephony" in intents:
        return (
            entry_type == "shop"
            and (
                name_key in telecom_names
                or category_or_notes_contains(
                    item,
                    [
                        "telefonia mobile",
                        "telefonia mobile e fissa",
                        "operatore di telecomunicazioni",
                        "internet e fibra"
                    ]
                )
            )
        )

    if "tech" in intents:
        if entry_type != "shop":
            return False

        if name_key in telecom_names:
            return False

        return category_or_notes_contains(
            item,
            [
                "elettronica",
                "smartphone",
                "notebook",
                "computer",
                "apple",
                "console",
                "videogiochi",
                "gadget tecnologici",
                "smart home"
            ]
        )

    if "beauty" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "profumeria",
                    "profumi",
                    "make-up",
                    "makeup",
                    "skincare",
                    "cosmetici",
                    "cura della persona"
                ]
            )
        )

    if "jewelry" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "gioielleria",
                    "gioielli",
                    "orologi",
                    "bijoux"
                ]
            )
        )

    if "home" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "casa",
                    "casalinghi",
                    "oggettistica",
                    "decorazioni",
                    "tessili",
                    "biancheria",
                    "arredamento"
                ]
            )
        )

    if "food" in intents:
        return (
            entry_type == "shop"
            and name_key != "coop"
            and category_or_notes_contains(
                item,
                [
                    "ristorante",
                    "fast food",
                    "bar",
                    "pizzeria",
                    "hamburger",
                    "pizza",
                    "poke"
                ]
            )
        )

    if "sport" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "sport",
                    "running",
                    "fitness",
                    "calcio",
                    "padel",
                    "sportswear"
                ]
            )
        )

    if "assistance" in intents:
        return (
            entry_type == "service"
            and name_key == "contatticentrocommerciale"
        )

    if "pharmacy" in intents:
        return (
            entry_type == "shop"
            and name_key == "coop"
            and category_or_notes_contains(
                item,
                ["parafarmacia"]
            )
        )

    if "groceries" in intents:
        return (
            entry_type == "shop"
            and name_key == "coop"
            and category_or_notes_contains(
                item,
                [
                    "prodotti alimentari",
                    "ipermercato",
                    "supermercato"
                ]
            )
        )

    if "cooking_tools" in intents:
        return (
            entry_type == "shop"
            and name_key in {"kasanova", "coop"}
        )

    if "dessert" in intents:
        return (
            entry_type == "shop"
            and (
                name_key in {
                    "chouse",
                    "chousebistrot",
                    "sanmarco",
                    "bonbon"
                }
                or category_or_notes_contains(
                    item,
                    [
                        "dolci artigianali",
                        "gelati artigianali",
                        "cioccolatini artigianali",
                        "confetteria"
                    ]
                )
            )
        )

    if "books" in intents:
        return (
            entry_type == "shop"
            and name_key == "giuntialpunto"
        )

    if "toys" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "giocattoli e giochi",
                    "puzzle",
                    "giochi da tavolo"
                ]
            )
        )

    if "phone_accessories" in intents:
        return (
            entry_type == "shop"
            and (
                name_key in {
                    "mediaworld",
                    "lacasadelascarcasas",
                    "ccconsultingapplereseller"
                }
                or category_or_notes_contains(
                    item,
                    [
                        "cover per smartphone",
                        "accessori tech",
                        "prodotti apple"
                    ]
                )
            )
        )

    if "gift" in intents:
        profiles = recipient_profile_tokens(user_text)

        if "older_woman" in profiles:
            return (
                (
                    entry_type == "shop"
                    or name_key == "giftcard"
                )
                and category_or_notes_contains(
                    item,
                    [
                        "gioielleria",
                        "gioielli",
                        "orologi",
                        "casalinghi",
                        "oggettistica",
                        "articoli per la casa",
                        "tessili",
                        "biancheria",
                        "decorazioni",
                        "idee regalo",
                        "articoli da regalo",
                        "bomboniere"
                    ]
                )
            )

        if "young_woman" in profiles:
            return (
                entry_type == "shop"
                and category_or_notes_contains(
                    item,
                    [
                        "gioielleria",
                        "gioielli",
                        "profumi",
                        "make-up",
                        "makeup",
                        "beauty",
                        "borse",
                        "accessori moda femminili",
                        "abbigliamento donna"
                    ]
                )
            )

        if "teen" in profiles:
            return (
                entry_type == "shop"
                and category_or_notes_contains(
                    item,
                    [
                        "videogiochi",
                        "console",
                        "gadget",
                        "streetwear",
                        "sneaker",
                        "pubblico giovane",
                        "ragazzi"
                    ]
                )
            )

        if "child" in profiles:
            return category_or_notes_contains(
                item,
                [
                    "bambino",
                    "bambini",
                    "neonati",
                    "ragazzi",
                    "giocattoli",
                    "libri per ragazzi"
                ]
            )

        if "man" in profiles:
            return (
                entry_type == "shop"
                and category_or_notes_contains(
                    item,
                    [
                        "abbigliamento uomo",
                        "orologi",
                        "elettronica",
                        "profumi",
                        "sport"
                    ]
                )
            )

        return (
            entry_type in {"shop", "service"}
            and category_or_notes_contains(
                item,
                [
                    "regalo",
                    "gioielleria",
                    "gioielli",
                    "profumi",
                    "casa",
                    "oggettistica",
                    "accessori",
                    "bomboniere",
                    "gift card",
                    "gadget",
                    "libri"
                ]
            )
        )

    if "fashion" in intents:
        return (
            entry_type == "shop"
            and category_or_notes_contains(
                item,
                [
                    "abbigliamento",
                    "calzature",
                    "accessori",
                    "moda",
                    "borse"
                ]
            )
        )

    if "children" in intents:
        return category_or_notes_contains(
            item,
            [
                "bambino",
                "bambini",
                "neonati",
                "ragazzi",
                "area bimbi",
                "giocattoli",
                "libri per ragazzi"
            ]
        )

    return True


def recommendation_bucket(
    entry: Dict[str, Any]
) -> str:
    item = entry["item"]

    if entry["type"] == "service":
        return "service"

    if entry["type"] == "event":
        return "event"

    category = normalize_text(
        item.get("category", "")
    )

    if "gioielleria" in category:
        return "jewelry"

    if any(
        keyword in category
        for keyword in [
            "casa",
            "casalinghi",
            "tempo libero"
        ]
    ) and category_or_notes_contains(
        item,
        [
            "casa",
            "casalinghi",
            "oggettistica",
            "decorazioni",
            "tessili",
            "biancheria",
            "arredamento"
        ]
    ):
        return "home"

    if category_or_notes_contains(
        item,
        ["gioielleria", "gioielli", "orologi", "bijoux"]
    ):
        return "jewelry"

    if category_or_notes_contains(
        item,
        [
            "casa",
            "casalinghi",
            "oggettistica",
            "decorazioni",
            "tessili",
            "biancheria",
            "arredamento"
        ]
    ):
        return "home"

    if category_or_notes_contains(
        item,
        [
            "elettronica",
            "smartphone",
            "notebook",
            "console",
            "videogiochi",
            "apple"
        ]
    ):
        return "tech"

    if category_or_notes_contains(
        item,
        [
            "profumeria",
            "profumi",
            "make-up",
            "makeup",
            "skincare",
            "cosmetici"
        ]
    ):
        return "beauty"

    if category_or_notes_contains(
        item,
        ["ristorante", "fast food", "bar", "pizzeria"]
    ):
        return "food"

    if category_or_notes_contains(
        item,
        ["abbigliamento", "calzature", "accessori", "moda"]
    ):
        return "fashion"

    return "other"


def score_entry(
    entry: Dict[str, Any],
    user_text: str,
    intents: Sequence[str],
    previous_names: set[str]
) -> float:
    item = entry["item"]
    search_text = entry["search_text"]
    entry_type = entry["type"]

    score = 0.0

    for token in meaningful_tokens(user_text):
        if token in search_text:
            score += 2.0

        if token in normalize_text(item.get("name", "")):
            score += 5.0

        if token in normalize_text(
            item.get("category", "")
        ):
            score += 4.0

    # Servizi personali.
    service_rules = {
        "dental": [
            "odontoiatria",
            "dentale",
            "dental pro"
        ],
        "optical": [
            "ottica",
            "occhiali",
            "lenti",
            "vista"
        ],
        "hair": [
            "parrucchiere",
            "capelli"
        ],
        "tailor": [
            "sartoria",
            "orlo",
            "ricami"
        ],
        "laundry": [
            "lavanderia",
            "lavaggio"
        ],
        "travel": [
            "agenzia di viaggi",
            "viaggi",
            "vacanze"
        ]
    }

    for intent, keywords in service_rules.items():
        if intent in intents:
            if category_or_notes_contains(
                item,
                keywords
            ):
                score += 40.0
            else:
                score -= 5.0

    if "restroom" in intents:
        if entry_type == "service" and (
            "bagni" in search_text
            or "wc" in search_text
            or "servizi igienici" in search_text
        ):
            score += 50.0
        else:
            score -= 10.0

    if "gaming" in intents:
        if entry["name_key"] == "gamelife":
            score += 80.0
        elif entry["name_key"] == "mediaworld":
            score += 55.0
        elif category_or_notes_contains(
            item,
            [
                "videogiochi",
                "console",
                "piattaforme di gioco",
                "articoli da collezione"
            ]
        ):
            score += 40.0

    if "telephony" in intents:
        if entry["name_key"] in {"tim", "vodafone", "wind3"}:
            score += 55.0

    if "tech" in intents:
        if entry["name_key"] == "mediaworld":
            score += 65.0
        elif entry["name_key"] == "gamelife":
            score += 48.0
        elif entry["name_key"] == "ccconsultingapplereseller":
            score += 42.0
        elif category_or_notes_contains(
            item,
            [
                "elettronica",
                "smartphone",
                "notebook",
                "computer",
                "apple",
                "console",
                "videogiochi",
                "gadget tecnologici"
            ]
        ):
            score += 30.0

    if "assistance" in intents:
        if entry["name_key"] == "contatticentrocommerciale":
            score += 90.0

    if "pharmacy" in intents:
        if entry["name_key"] == "coop":
            score += 90.0

    if "groceries" in intents:
        if entry["name_key"] == "coop":
            score += 100.0

    if "cooking_tools" in intents:
        if entry["name_key"] == "kasanova":
            score += 85.0
        elif entry["name_key"] == "coop":
            score += 45.0

    if "dessert" in intents:
        dessert_scores = {
            "chouse": 85.0,
            "chousebistrot": 75.0,
            "sanmarco": 70.0,
            "bonbon": 65.0
        }

        score += dessert_scores.get(
            entry["name_key"],
            0.0
        )

    if "books" in intents:
        if entry["name_key"] == "giuntialpunto":
            score += 100.0

    if "toys" in intents:
        if entry["name_key"] == "tiger":
            score += 90.0

    if "phone_accessories" in intents:
        accessory_scores = {
            "lacasadelascarcasas": 100.0,
            "mediaworld": 75.0,
            "ccconsultingapplereseller": 60.0
        }

        score += accessory_scores.get(
            entry["name_key"],
            0.0
        )

    if "beauty" in intents:
        if category_or_notes_contains(
            item,
            [
                "profumeria",
                "profumi",
                "make-up",
                "makeup",
                "skincare",
                "cosmetici",
                "cura della persona"
            ]
        ):
            score += 24.0

    if "jewelry" in intents:
        if category_or_notes_contains(
            item,
            [
                "gioielleria",
                "gioielli",
                "orologi",
                "bijoux"
            ]
        ):
            score += 24.0

    if "fashion" in intents:
        if category_or_notes_contains(
            item,
            [
                "abbigliamento",
                "calzature",
                "accessori",
                "moda",
                "borse"
            ]
        ):
            score += 18.0

    if "home" in intents:
        if category_or_notes_contains(
            item,
            [
                "casa",
                "casalinghi",
                "oggettistica",
                "decorazioni",
                "tessili",
                "biancheria",
                "arredamento"
            ]
        ):
            score += 22.0

    if "food" in intents:
        if entry["name_key"] == "coop":
            score -= 30.0
        elif entry_type == "shop" and category_or_notes_contains(
            item,
            [
                "ristorante",
                "fast food",
                "bar",
                "pizzeria",
                "hamburger",
                "pizza",
                "poke"
            ]
        ):
            score += 30.0

    if "sport" in intents:
        if category_or_notes_contains(
            item,
            [
                "sport",
                "running",
                "fitness",
                "calcio",
                "padel",
                "sportswear"
            ]
        ):
            score += 24.0

    if "children" in intents:
        if category_or_notes_contains(
            item,
            [
                "bambino",
                "bambini",
                "neonati",
                "ragazzi",
                "area bimbi",
                "giocattoli",
                "libri per ragazzi"
            ]
        ):
            score += 24.0

    # Regali e destinatario.
    if "gift" in intents and entry_type in {
        "shop",
        "service"
    }:
        if category_or_notes_contains(
            item,
            [
                "regalo",
                "gioielli",
                "gioielleria",
                "profumi",
                "casa",
                "oggettistica",
                "accessori",
                "bomboniere",
                "gift card",
                "gadget",
                "libri"
            ]
        ):
            score += 18.0

        profiles = recipient_profile_tokens(user_text)

        if "older_woman" in profiles:
            category = normalize_text(
                item.get("category", "")
            )

            if "gioielleria" in category:
                score += 48.0

            if category_or_notes_contains(
                item,
                [
                    "casalinghi",
                    "oggettistica",
                    "tessili",
                    "biancheria",
                    "decorazioni",
                    "articoli per la casa"
                ]
            ):
                score += 32.0

            if category_or_notes_contains(
                item,
                [
                    "regalo perfetto",
                    "idee regalo",
                    "articoli da regalo",
                    "raffinate",
                    "elegante"
                ]
            ):
                score += 16.0

            if category_or_notes_contains(
                item,
                [
                    "assortimento eclettico",
                    "giocattoli e giochi",
                    "gadget tecnologici",
                    "streetwear",
                    "gaming",
                    "videogiochi",
                    "pubblico giovane"
                ]
            ):
                score -= 24.0

        if "young_woman" in profiles:
            if category_or_notes_contains(
                item,
                [
                    "gioielli",
                    "profumi",
                    "make-up",
                    "borse",
                    "accessori",
                    "abbigliamento donna",
                    "beauty"
                ]
            ):
                score += 16.0

        if "teen" in profiles:
            if category_or_notes_contains(
                item,
                [
                    "videogiochi",
                    "console",
                    "streetwear",
                    "sneaker",
                    "giovane",
                    "gadget"
                ]
            ):
                score += 20.0

        if "child" in profiles:
            if category_or_notes_contains(
                item,
                [
                    "bambino",
                    "bambini",
                    "neonati",
                    "giocattoli",
                    "libri per ragazzi"
                ]
            ):
                score += 20.0

        if "man" in profiles:
            if category_or_notes_contains(
                item,
                [
                    "abbigliamento uomo",
                    "orologi",
                    "tecnologia",
                    "elettronica",
                    "profumi",
                    "sport"
                ]
            ):
                score += 15.0

    # Rotazione nella conversazione corrente.
    if entry["name_key"] in previous_names:
        score -= 12.0

    # Equità globale: premia soltanto i negozi già pertinenti
    # che sono stati nominati meno spesso nella finestra configurata.
    score += shop_fairness_boost(entry)

    return score


def retrieve_relevant_entries(
    mall_data: Dict[str, Any],
    user_text: str,
    intents: Sequence[str],
    history: Sequence[str],
    limit: int = 8
) -> List[Tuple[float, Dict[str, Any]]]:
    catalog = build_catalog(mall_data)

    previous_names = get_previously_mentioned_name_keys(
        history,
        catalog
    )

    scored: List[Tuple[float, Dict[str, Any]]] = []

    for entry in catalog:
        if not entry_is_eligible_for_intents(
            entry,
            user_text,
            intents
        ):
            continue

        score = score_entry(
            entry,
            user_text,
            intents,
            previous_names
        )

        if score > 0:
            scored.append((score, entry))

    scored.sort(
        key=lambda pair: (
            pair[0],
            pair[1]["item"].get("name", "")
        ),
        reverse=True
    )

    return scored[:limit]


# ---------------------------------------------------------------------
# RESPONSE SCHEMA
# ---------------------------------------------------------------------

def item_to_dual_schema(
    item: Dict[str, Any]
) -> Dict[str, str]:
    return {
        "Name": item.get("name", ""),
        "name": item.get("name", ""),

        "Category": item.get("category", ""),
        "category": item.get("category", ""),

        "Floor": item.get("floor", ""),
        "floor": item.get("floor", ""),

        "Area": item.get("area", ""),
        "area": item.get("area", ""),

        "Notes": item.get("notes", ""),
        "notes": item.get("notes", "")
    }


def make_response(
    answer: str,
    confidence: int = 1,
    used_sources: Optional[List[str]] = None,
    suggested_shops: Optional[List[Dict[str, Any]]] = None,
    needs_clarification: bool = False,
    clarification_question: str = "",
    is_out_of_scope: bool = False,
    debug_info: str = ""
) -> Dict[str, Any]:
    final_answer = sanitize_answer(answer)
    final_clarification = sanitize_answer(
        clarification_question
    )

    used_sources = used_sources or []
    suggested_shops = suggested_shops or []

    shops_dual = [
        item_to_dual_schema(item)
        for item in suggested_shops
    ]

    return {
        "AnswerToSay": final_answer,
        "Confidence": confidence,
        "UsedSources": used_sources,
        "SuggestedShops": shops_dual,
        "bNeedsClarification": needs_clarification,
        "ClarificationQuestion": final_clarification,
        "bIsOutOfScope": is_out_of_scope,
        "DebugInfo": debug_info,

        "answerToSay": final_answer,
        "confidence": confidence,
        "usedSources": used_sources,
        "suggestedShops": shops_dual,
        "needsClarification": needs_clarification,
        "clarificationQuestion": final_clarification,
        "isOutOfScope": is_out_of_scope,
        "debugInfo": debug_info
    }


# ---------------------------------------------------------------------
# DIRECT ANSWERS
# ---------------------------------------------------------------------

ABSENT_STORE_ANSWER = (
    "Mi dispiace, non ho informazioni a riguardo."
)


def direct_entry_answer(
    entry: Dict[str, Any],
    user_text: str
) -> Dict[str, Any]:
    item = entry["item"]

    name = item.get("name", "")
    area = get_area_phrase(item)
    opening_hours = item.get("opening_hours", "")
    notes = item.get("notes", "")
    category = item.get("category", "")

    normalized_question = normalize_text(user_text)

    if any(
        word in normalized_question
        for word in [
            "orario",
            "orari",
            "apre",
            "chiude",
            "aperto",
            "aperta"
        ]
    ):
        answer = (
            f"{name} si trova in {area}. "
            f"Gli orari indicati sono: {opening_hours}."
        )
    elif any(
        phrase in normalized_question
        for phrase in [
            "cosa vende",
            "che cosa vende",
            "cosa trovo",
            "che prodotti"
        ]
    ):
        short_notes = first_sentence(notes)

        answer = (
            f"{name}, {area}, è nella categoria "
            f"{category}. {short_notes}"
        )
    else:
        answer = (
            f"Sì, {name} è presente e si trova in {area}."
        )

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=[entry["source"]],
        suggested_shops=[item],
        debug_info=(
            f"Direct exact catalog match: {name}."
        )
    )


def restroom_answer(
    mall_data: Dict[str, Any]
) -> Dict[str, Any]:
    services = [
        item
        for item in mall_data.get("services", [])
        if "bagni" in normalize_text(
            item.get("name", "")
        )
    ]

    answer = (
        "I bagni si trovano di fronte all'ipermercato, "
        "tra TIM e Zagaria Sport, posizioni mappa dodici "
        "e tredici, oppure tra Pandora e Senso Unico, "
        "posizioni mappa sessantasette, sessantacinque "
        "e sessantasei."
    )

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=["mall_data.json:services"],
        suggested_shops=services,
        debug_info="Direct answer: restrooms."
    )


def opening_hours_answer(
    mall_data: Dict[str, Any],
    user_text: str
) -> Dict[str, Any]:
    opening_hours = mall_data.get(
        "opening_hours",
        {}
    )

    normalized = normalize_text(user_text)

    if any(
        word in normalized
        for word in [
            "coop",
            "ipermercato",
            "supermercato"
        ]
    ):
        answer = opening_hours.get(
            "supermarket",
            ""
        )
    elif any(
        word in normalized
        for word in [
            "ristorazione",
            "ristoranti",
            "food",
            "mangiare"
        ]
    ):
        answer = opening_hours.get("food", "")
    else:
        answer = opening_hours.get("mall", "")

    if not answer:
        answer = ABSENT_STORE_ANSWER

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=[
            "mall_data.json:opening_hours"
        ],
        debug_info="Direct answer: opening hours."
    )


def find_service_by_name(
    mall_data: Dict[str, Any],
    service_name: str
) -> Optional[Dict[str, Any]]:
    target_key = normalize_name_key(service_name)

    for service in mall_data.get("services", []):
        if normalize_name_key(
            service.get("name", "")
        ) == target_key:
            return service

    return None


def contact_answer(
    mall_data: Dict[str, Any]
) -> Dict[str, Any]:
    service = find_service_by_name(
        mall_data,
        "Contatti centro commerciale"
    )

    if not service:
        return make_response(
            answer=ABSENT_STORE_ANSWER,
            confidence=0,
            is_out_of_scope=True,
            debug_info="Contact service missing."
        )

    answer = (
        "Puoi contattare il Gran Shopping Mongolfiera "
        f"ai recapiti indicati: {service.get('notes', '')}"
    )

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=["mall_data.json:services"],
        suggested_shops=[service],
        debug_info="Direct answer: contacts."
    )


def directions_answer(
    mall_data: Dict[str, Any]
) -> Dict[str, Any]:
    service = find_service_by_name(
        mall_data,
        "Come raggiungere il centro"
    )

    if not service:
        return make_response(
            answer=ABSENT_STORE_ANSWER,
            confidence=0,
            is_out_of_scope=True,
            debug_info="Directions service missing."
        )

    answer = (
        f"Puoi raggiungere il centro da "
        f"{service.get('area', '')}. "
        f"{service.get('notes', '')}"
    )

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=["mall_data.json:services"],
        suggested_shops=[service],
        debug_info="Direct answer: directions."
    )


def events_answer(
    mall_data: Dict[str, Any]
) -> Dict[str, Any]:
    events = mall_data.get("events", [])

    if not events:
        return make_response(
            answer=ABSENT_STORE_ANSWER,
            confidence=0,
            is_out_of_scope=True,
            debug_info="No events in mall data."
        )

    selected_events = events[:2]

    parts = [
        (
            f"{event.get('name', '')}, "
            f"{event.get('date', '')}, "
            f"{event.get('area', '')}"
        )
        for event in selected_events
    ]

    answer = "Gli eventi indicati sono: " + "; ".join(parts) + "."

    return make_response(
        answer=answer,
        confidence=2,
        used_sources=["mall_data.json:events"],
        debug_info="Direct answer: events."
    )


# ---------------------------------------------------------------------
# SMALL TALK
# ---------------------------------------------------------------------

JOKES = [
    (
        "Perché il carrello era felice? "
        "Perché aveva finalmente trovato la sua corsia preferita."
    ),
    (
        "Sai perché le scarpe non litigano mai? "
        "Perché cercano sempre di restare un passo avanti."
    ),
    (
        "Perché il caffè è entrato nel centro commerciale? "
        "Per fare una pausa shopping."
    )
]


def deterministic_joke(
    session_id: str,
    user_text: str
) -> str:
    seed = f"{session_id}|{user_text}".encode("utf-8")
    digest = hashlib.sha256(seed).digest()
    index = digest[0] % len(JOKES)

    return JOKES[index]


def small_talk_answer(
    intents: Sequence[str],
    session_id: str,
    user_text: str
) -> Optional[Dict[str, Any]]:
    if "joke" in intents:
        return make_response(
            answer=deterministic_joke(
                session_id,
                user_text
            ),
            confidence=2,
            debug_info="Small talk: joke."
        )

    normalized = normalize_text(user_text)

    if (
        "thanks" in intents
        and len(tokenize(normalized)) <= 6
    ):
        return make_response(
            answer=(
                "È un piacere. Sono qui per aiutarti "
                "con negozi, servizi, orari e idee per "
                "il tuo shopping."
            ),
            confidence=2,
            debug_info="Small talk: thanks."
        )

    if (
        "greeting" in intents
        and len(tokenize(normalized)) <= 4
    ):
        return make_response(
            answer=(
                "Ciao, sono Nico. Posso aiutarti a "
                "trovare negozi, servizi, orari o "
                "un'idea su misura."
            ),
            confidence=2,
            debug_info="Small talk: greeting."
        )

    return None


# ---------------------------------------------------------------------
# RECOMMENDATION COMPOSITION
# ---------------------------------------------------------------------

def reason_for_item(
    item: Dict[str, Any],
    intents: Sequence[str],
    user_text: str
) -> str:
    profiles = recipient_profile_tokens(user_text)
    searchable = normalize_text(
        " ".join([
            item.get("category", ""),
            item.get("notes", "")
        ])
    )

    if "dental" in intents:
        return "per assistenza odontoiatrica"

    if "optical" in intents:
        return "per occhiali e servizi dedicati alla vista"

    if "hair" in intents:
        return "per la cura e lo styling dei capelli"

    if "tailor" in intents:
        return "per riparazioni e modifiche sartoriali"

    if "laundry" in intents:
        return "per lavaggio e cura dei tessuti"

    if "travel" in intents:
        return "per organizzare un viaggio su misura"

    if "assistance" in intents:
        return "per contattare direttamente il centro"

    if "pharmacy" in intents:
        return "per la parafarmacia indicata nei dati del punto vendita"

    if "groceries" in intents:
        return "per fare la spesa e acquistare prodotti alimentari"

    if "cooking_tools" in intents:
        return "per utensili e accessori da cucina"

    if "dessert" in intents:
        return "per dolci, gelati o proposte da gustare"

    if "books" in intents:
        return "per libri di generi e interessi diversi"

    if "toys" in intents:
        return "per giochi, giocattoli e idee divertenti"

    if "phone_accessories" in intents:
        return "per cover e accessori tecnologici"

    if "gaming" in intents:
        return "per videogiochi, console e gadget"

    if "tech" in intents:
        return "per tecnologia ed elettronica"

    if "beauty" in intents:
        return "per profumi, cosmetici e cura della persona"

    if "jewelry" in intents:
        return "per gioielli e orologi"

    if "home" in intents:
        return "per articoli per la casa e idee regalo"

    if "food" in intents:
        return "per una pausa o un pasto"

    if "sport" in intents:
        return "per abbigliamento e articoli sportivi"

    if "children" in intents:
        return "per proposte dedicate a bambini e ragazzi"

    if "gift" in intents:
        if "older_woman" in profiles:
            category = normalize_text(
                item.get("category", "")
            )

            if "gioielleria" in category:
                return "per un regalo elegante e classico"

            if any(
                keyword in searchable
                for keyword in [
                    "casa",
                    "casalinghi",
                    "oggettistica",
                    "tessili"
                ]
            ):
                return "per un regalo utile e classico per la casa"

            if "gioiell" in searchable:
                return "per un regalo elegante e classico"

        if "young_woman" in profiles:
            if "gioiell" in searchable:
                return "per un gioiello elegante e personale"

            if any(
                keyword in searchable
                for keyword in [
                    "profum",
                    "make",
                    "beauty"
                ]
            ):
                return "per un regalo beauty e personale"

            if any(
                keyword in searchable
                for keyword in [
                    "borse",
                    "accessori",
                    "abbigliamento donna"
                ]
            ):
                return "per moda e accessori femminili"

        if "teen" in profiles:
            if any(
                keyword in searchable
                for keyword in [
                    "videogiochi",
                    "console",
                    "gadget"
                ]
            ):
                return "per un regalo tecnologico e divertente"

            return "per uno stile giovane e contemporaneo"

        if "child" in profiles:
            return "per un regalo adatto a bambini e ragazzi"

        return "per una proposta regalo coerente con la richiesta"

    if "fashion" in intents:
        return "per abbigliamento e accessori"

    return "per una proposta coerente con la tua esigenza"


def compose_recommendation_answer(
    selected_entries: Sequence[Dict[str, Any]],
    intents: Sequence[str],
    user_text: str
) -> str:
    parts: List[str] = []

    for entry in selected_entries[:MAX_RECOMMENDATIONS]:
        item = entry["item"]

        name = item.get("name", "")
        area = get_area_phrase(item)
        reason = reason_for_item(
            item,
            intents,
            user_text
        )

        parts.append(
            f"{name}, {area}, {reason}"
        )

    if not parts:
        return (
            "Mi dispiace, non ho informazioni a riguardo."
        )

    if len(parts) == 1:
        return "Ti consiglio " + parts[0] + "."

    if len(parts) == 2:
        return (
            "Ti consiglio "
            + parts[0]
            + "; oppure "
            + parts[1]
            + "."
        )

    return (
        "Puoi valutare "
        + parts[0]
        + "; "
        + parts[1]
        + "; oppure "
        + parts[2]
        + "."
    )


def choose_diverse_entries(
    scored_entries: Sequence[
        Tuple[float, Dict[str, Any]]
    ],
    max_items: int = MAX_RECOMMENDATIONS
) -> List[Dict[str, Any]]:
    selected: List[Dict[str, Any]] = []
    seen_buckets: set[str] = set()

    for _, entry in scored_entries:
        bucket = recommendation_bucket(entry)

        if bucket in seen_buckets:
            continue

        selected.append(entry)
        seen_buckets.add(bucket)

        if len(selected) >= max_items:
            break

    if len(selected) < max_items:
        selected_keys = {
            entry["name_key"]
            for entry in selected
        }

        for _, entry in scored_entries:
            if entry["name_key"] in selected_keys:
                continue

            selected.append(entry)
            selected_keys.add(entry["name_key"])

            if len(selected) >= max_items:
                break

    return selected


# ---------------------------------------------------------------------
# OPENAI CANDIDATE SELECTION
# ---------------------------------------------------------------------

NICO_SYSTEM_PROMPT = """
Sei Nico, l'assistente virtuale donna del Gran Shopping Mongolfiera Molfetta.

Devi comprendere l'esigenza reale dell'utente e scegliere soltanto tra i candidati forniti dal sistema.

Regole obbligatorie:
- Usa esclusivamente i dati dei candidati forniti.
- Non inventare negozi, servizi, prodotti, posizioni, orari o dettagli.
- Seleziona da uno a tre candidati realmente pertinenti.
- Personalizza la scelta in base a destinatario, età, occasione, stile e interessi.
- Per problemi di salute non formulare diagnosi: scegli soltanto un servizio pertinente.
- Cerca varietà e non selezionare sempre gli stessi candidati quando esistono alternative valide.
- Tra candidati ugualmente pertinenti, preferisci quelli con mentions_in_window più basso, never_mentioned vero o più giorni dall'ultima menzione.
- Nel campo selected_item_names usa esclusivamente i nomi esatti forniti.
- Nel campo reason scrivi una motivazione molto breve, senza nomi, numeri, link o fonti.
- Non nominare file, JSON, database, fonti o processi interni.
- Se nessun candidato è adeguato, restituisci un elenco vuoto.
""".strip()


def candidates_for_model(
    scored_entries: Sequence[
        Tuple[float, Dict[str, Any]]
    ]
) -> List[Dict[str, Any]]:
    candidates: List[Dict[str, Any]] = []

    for score, entry in scored_entries[:8]:
        item = entry["item"]

        exposure = shop_exposure_metrics(
            entry
        )

        candidates.append({
            "name": item.get("name", ""),
            "type": entry["type"],
            "category": item.get("category", ""),
            "area": item.get("area", ""),
            "notes": item.get("notes", ""),
            "score": score,
            "mentions_in_window": exposure.get(
                "window_mentions",
                0
            ),
            "total_mentions": exposure.get(
                "total_mentions",
                0
            ),
            "never_mentioned": exposure.get(
                "never_mentioned",
                False
            ),
            "days_since_last_mention": exposure.get(
                "days_since_last_mention"
            )
        })

    return candidates


def select_entries_with_openai(
    user_text: str,
    intents: Sequence[str],
    scored_entries: Sequence[
        Tuple[float, Dict[str, Any]]
    ]
) -> Optional[List[Dict[str, Any]]]:
    if not USE_OPENAI or not openai_client:
        return None

    candidates = candidates_for_model(
        scored_entries
    )

    if not candidates:
        return None

    candidate_index = {
        normalize_name_key(candidate["name"]): entry
        for candidate, (_, entry) in zip(
            candidates,
            scored_entries[:len(candidates)]
        )
    }

    schema = {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "selected_items": {
                "type": "array",
                "maxItems": MAX_RECOMMENDATIONS,
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "properties": {
                        "name": {
                            "type": "string"
                        },
                        "reason": {
                            "type": "string"
                        }
                    },
                    "required": [
                        "name",
                        "reason"
                    ]
                }
            }
        },
        "required": ["selected_items"]
    }

    user_prompt = (
        "DOMANDA UTENTE:\n"
        f"{user_text}\n\n"
        "INTENT RILEVATI:\n"
        f"{', '.join(intents) if intents else 'nessuno'}\n\n"
        "CANDIDATI DISPONIBILI:\n"
        f"{json.dumps(candidates, ensure_ascii=False)}"
    )

    try:
        response = openai_client.responses.create(
            model=OPENAI_MODEL,
            input=[
                {
                    "role": "system",
                    "content": NICO_SYSTEM_PROMPT
                },
                {
                    "role": "user",
                    "content": user_prompt
                }
            ],
            text={
                "format": {
                    "type": "json_schema",
                    "name": "nico_candidate_selection",
                    "strict": True,
                    "schema": schema
                }
            }
        )

        parsed = json.loads(response.output_text)
        selected_items = parsed.get(
            "selected_items",
            []
        )

        selected_entries: List[Dict[str, Any]] = []
        selected_keys: set[str] = set()

        for selected_item in selected_items:
            name_key = normalize_name_key(
                selected_item.get("name", "")
            )

            entry = candidate_index.get(name_key)

            if not entry or name_key in selected_keys:
                continue

            selected_entries.append(entry)
            selected_keys.add(name_key)

        return selected_entries or None

    except Exception as error:
        print(f"[OpenAI Selection Error] {error}")
        return None


# ---------------------------------------------------------------------
# GENERIC / OUT-OF-SCOPE
# ---------------------------------------------------------------------

def is_mall_related_intent(
    intents: Sequence[str]
) -> bool:
    mall_intents = {
        "restroom",
        "current_time",
        "current_date",
        "weather",
        "hours",
        "contact",
        "directions",
        "events",
        "assistance",
        "dental",
        "optical",
        "pharmacy",
        "hair",
        "tailor",
        "laundry",
        "travel",
        "groceries",
        "cooking_tools",
        "dessert",
        "books",
        "toys",
        "phone_accessories",
        "generic_shopping",
        "gift",
        "gaming",
        "telephony",
        "tech",
        "beauty",
        "jewelry",
        "fashion",
        "home",
        "food",
        "children",
        "sport"
    }

    return any(
        intent in mall_intents
        for intent in intents
    )


def out_of_scope_answer() -> Dict[str, Any]:
    return make_response(
        answer=MALL_HELP_SUFFIX,
        confidence=1,
        needs_clarification=True,
        clarification_question=(
            "Cosa stai cercando nel centro commerciale?"
        ),
        is_out_of_scope=True,
        debug_info="Out of scope or no grounded candidate."
    )


GENERAL_KNOWLEDGE_SYSTEM_PROMPT = """
Sei Nico, assistente virtuale del Gran Shopping Mongolfiera Molfetta.

L'utente ha fatto una domanda semplice non collegata direttamente al centro commerciale.
Rispondi prima alla domanda in modo corretto, sintetico e naturale, poi il sistema aggiungerà automaticamente una frase che ricorda il tuo ruolo nel centro.

Regole obbligatorie:
- Rispondi in italiano.
- Fornisci soltanto una risposta breve e diretta.
- Non usare markdown, elenchi, link, citazioni o riferimenti alle fonti.
- Non parlare del centro commerciale nella tua parte della risposta.
- Non inventare dati aggiornati o in tempo reale.
- Se la domanda richiede informazioni correnti, notizie, quotazioni, risultati, traffico o dati che non puoi verificare, dichiaralo brevemente.
- Per salute, questioni legali o finanziarie, fornisci soltanto informazioni generali prudenti e invita a rivolgersi a un professionista quando necessario.
- Non superare il limite indicato dal sistema.
""".strip()


def compose_general_knowledge_answer(
    short_answer: str
) -> str:
    clean_answer = sanitize_answer(short_answer)

    if not clean_answer:
        return MALL_HELP_SUFFIX

    reserved_chars = len(MALL_HELP_SUFFIX) + 1
    available_chars = max(
        40,
        MAX_ANSWER_CHARS - reserved_chars
    )

    clean_answer = truncate_at_word_boundary(
        clean_answer,
        min(
            GENERAL_KNOWLEDGE_MAX_CHARS,
            available_chars
        )
    ).rstrip(" .")

    return f"{clean_answer}. {MALL_HELP_SUFFIX}"


def general_knowledge_answer(
    user_text: str,
    history: Sequence[str]
) -> Dict[str, Any]:
    if not USE_OPENAI or not openai_client:
        return out_of_scope_answer()

    schema = {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "answer": {
                "type": "string",
                "description": (
                    "Risposta breve e diretta alla domanda generale."
                )
            }
        },
        "required": ["answer"]
    }

    recent_history = list(history)[-6:]

    user_prompt = (
        "DOMANDA UTENTE:\n"
        f"{user_text}\n\n"
        "CRONOLOGIA RECENTE, SOLO SE UTILE:\n"
        f"{json.dumps(recent_history, ensure_ascii=False)}\n\n"
        "LIMITE MASSIMO DELLA TUA PARTE DI RISPOSTA:\n"
        f"{GENERAL_KNOWLEDGE_MAX_CHARS} caratteri"
    )

    try:
        response = openai_client.responses.create(
            model=OPENAI_MODEL,
            input=[
                {
                    "role": "system",
                    "content": GENERAL_KNOWLEDGE_SYSTEM_PROMPT
                },
                {
                    "role": "user",
                    "content": user_prompt
                }
            ],
            text={
                "format": {
                    "type": "json_schema",
                    "name": "nico_general_knowledge_answer",
                    "strict": True,
                    "schema": schema
                }
            }
        )

        parsed = json.loads(response.output_text)
        answer = compose_general_knowledge_answer(
            parsed.get("answer", "")
        )

        return make_response(
            answer=answer,
            confidence=2,
            needs_clarification=False,
            clarification_question="",
            is_out_of_scope=True,
            debug_info=(
                "General knowledge answer followed by mall reminder. "
                f"Model={OPENAI_MODEL}."
            )
        )

    except Exception as error:
        print(f"[General Knowledge Error] {error}")
        return out_of_scope_answer()



# ---------------------------------------------------------------------
# CURRENT TIME / DATE
# ---------------------------------------------------------------------

ITALIAN_WEEKDAYS = [
    "lunedì",
    "martedì",
    "mercoledì",
    "giovedì",
    "venerdì",
    "sabato",
    "domenica"
]

ITALIAN_MONTHS = [
    "",
    "gennaio",
    "febbraio",
    "marzo",
    "aprile",
    "maggio",
    "giugno",
    "luglio",
    "agosto",
    "settembre",
    "ottobre",
    "novembre",
    "dicembre"
]


def get_mall_now() -> datetime:
    """
    Restituisce l'ora locale del centro commerciale.

    Su Windows il database IANA può non essere installato.
    In quel caso usa il fuso locale configurato nel sistema operativo.
    """
    try:
        return datetime.now(ZoneInfo(MALL_TIMEZONE))
    except ZoneInfoNotFoundError:
        return datetime.now().astimezone()


def current_time_answer() -> Dict[str, Any]:
    now = get_mall_now()

    answer = (
        f"Sono le {now.hour}:{now.minute:02d} "
        f"a Molfetta."
    )

    return make_response(
        answer=answer,
        confidence=2,
        debug_info="Direct answer: current local time."
    )


def current_date_answer() -> Dict[str, Any]:
    now = get_mall_now()

    answer = (
        f"Oggi è {ITALIAN_WEEKDAYS[now.weekday()]} "
        f"{now.day} {ITALIAN_MONTHS[now.month]} "
        f"{now.year}."
    )

    return make_response(
        answer=answer,
        confidence=2,
        debug_info="Direct answer: current local date."
    )


# ---------------------------------------------------------------------
# WEATHER
# ---------------------------------------------------------------------

_WEATHER_COORDINATES_CACHE: Optional[Tuple[float, float]] = None
_WEATHER_RESPONSE_CACHE: Dict[str, Any] = {
    "timestamp": 0.0,
    "payload": None
}


def weather_code_description(code: int) -> str:
    descriptions = {
        0: "cielo sereno",
        1: "cielo prevalentemente sereno",
        2: "cielo parzialmente nuvoloso",
        3: "cielo coperto",
        45: "nebbia",
        48: "nebbia con brina",
        51: "pioviggine leggera",
        53: "pioviggine moderata",
        55: "pioviggine intensa",
        61: "pioggia leggera",
        63: "pioggia moderata",
        65: "pioggia intensa",
        71: "neve leggera",
        73: "neve moderata",
        75: "neve intensa",
        80: "rovesci leggeri",
        81: "rovesci moderati",
        82: "rovesci intensi",
        95: "temporali",
        96: "temporali con grandine",
        99: "temporali forti con grandine"
    }

    return descriptions.get(
        code,
        "condizioni variabili"
    )


def resolve_weather_coordinates() -> Tuple[float, float]:
    global _WEATHER_COORDINATES_CACHE

    if _WEATHER_COORDINATES_CACHE is not None:
        return _WEATHER_COORDINATES_CACHE

    if WEATHER_LATITUDE and WEATHER_LONGITUDE:
        _WEATHER_COORDINATES_CACHE = (
            float(WEATHER_LATITUDE),
            float(WEATHER_LONGITUDE)
        )

        return _WEATHER_COORDINATES_CACHE

    response = requests.get(
        WEATHER_GEOCODING_URL,
        params={
            "name": WEATHER_LOCATION_NAME,
            "count": 10,
            "language": "it",
            "format": "json"
        },
        timeout=8
    )

    response.raise_for_status()
    payload = response.json()

    results = payload.get("results", [])

    if not results:
        raise RuntimeError(
            "Località meteo non trovata."
        )

    selected = next(
        (
            result
            for result in results
            if normalize_text(
                str(result.get("name", ""))
            ) == normalize_text(WEATHER_LOCATION_NAME)
            and result.get("country_code") == WEATHER_COUNTRY_CODE
            and (
                not WEATHER_ADMIN1
                or normalize_text(
                    str(result.get("admin1", ""))
                ) == normalize_text(WEATHER_ADMIN1)
            )
        ),
        next(
            (
                result
                for result in results
                if normalize_text(
                    str(result.get("name", ""))
                ) == normalize_text(WEATHER_LOCATION_NAME)
                and result.get("country_code")
                == WEATHER_COUNTRY_CODE
            ),
            results[0]
        )
    )

    _WEATHER_COORDINATES_CACHE = (
        float(selected["latitude"]),
        float(selected["longitude"])
    )

    return _WEATHER_COORDINATES_CACHE


def get_weather_snapshot() -> Dict[str, Any]:
    import time

    now_timestamp = time.time()
    cached_payload = _WEATHER_RESPONSE_CACHE.get(
        "payload"
    )

    if (
        cached_payload is not None
        and now_timestamp
        - float(_WEATHER_RESPONSE_CACHE.get("timestamp", 0.0))
        < WEATHER_CACHE_SECONDS
    ):
        return cached_payload

    latitude, longitude = resolve_weather_coordinates()

    params: Dict[str, Any] = {
        "latitude": latitude,
        "longitude": longitude,
        "current": (
            "temperature_2m,"
            "apparent_temperature,"
            "precipitation,"
            "weather_code,"
            "wind_speed_10m"
        ),
        "daily": (
            "weather_code,"
            "temperature_2m_max,"
            "temperature_2m_min,"
            "precipitation_probability_max"
        ),
        "timezone": MALL_TIMEZONE,
        "forecast_days": 2
    }

    if WEATHER_API_KEY:
        params["apikey"] = WEATHER_API_KEY

    response = requests.get(
        WEATHER_API_BASE_URL,
        params=params,
        timeout=10
    )

    response.raise_for_status()
    payload = response.json()
    payload["_resolved_latitude"] = latitude
    payload["_resolved_longitude"] = longitude

    _WEATHER_RESPONSE_CACHE["timestamp"] = now_timestamp
    _WEATHER_RESPONSE_CACHE["payload"] = payload

    return payload


def weather_answer(
    user_text: str
) -> Dict[str, Any]:
    try:
        payload = get_weather_snapshot()
        normalized = normalize_text(user_text)

        daily = payload.get("daily", {})
        current = payload.get("current", {})

        daily_dates = daily.get("time", [])

        current_time_value = str(
            current.get("time", "")
        ).strip()

        try:
            local_today = datetime.fromisoformat(
                current_time_value
            ).date()
        except ValueError:
            local_today = get_mall_now().date()

        today_index = 0

        if daily_dates:
            for index, date_value in enumerate(daily_dates):
                try:
                    if datetime.fromisoformat(
                        str(date_value)
                    ).date() == local_today:
                        today_index = index
                        break
                except ValueError:
                    continue

        wants_tomorrow = "domani" in normalized

        if wants_tomorrow:
            tomorrow_index = today_index + 1

            weather_codes = daily.get(
                "weather_code",
                []
            )
            maximums = daily.get(
                "temperature_2m_max",
                []
            )
            minimums = daily.get(
                "temperature_2m_min",
                []
            )
            rain_probabilities = daily.get(
                "precipitation_probability_max",
                []
            )

            required_length = tomorrow_index + 1

            if not all(
                len(values) >= required_length
                for values in [
                    weather_codes,
                    maximums,
                    minimums,
                    rain_probabilities
                ]
            ):
                raise RuntimeError(
                    "Previsione di domani non disponibile."
                )

            answer = (
                f"Domani a Molfetta sono previste condizioni di "
                f"{weather_code_description(int(weather_codes[tomorrow_index]))}, "
                f"con massima di {round(float(maximums[tomorrow_index]))} gradi, "
                f"minima di {round(float(minimums[tomorrow_index]))} gradi "
                f"e probabilità massima di pioggia del "
                f"{round(float(rain_probabilities[tomorrow_index]))} per cento."
            )
        else:
            temperature = round(
                float(current.get("temperature_2m", 0))
            )
            apparent = round(
                float(
                    current.get(
                        "apparent_temperature",
                        temperature
                    )
                )
            )
            weather_code = int(
                current.get("weather_code", 0)
            )

            rain_probabilities = daily.get(
                "precipitation_probability_max",
                []
            )

            rain_probability = (
                round(
                    float(
                        rain_probabilities[today_index]
                    )
                )
                if len(rain_probabilities) > today_index
                else 0
            )

            current_precipitation = round(
                float(current.get("precipitation", 0)),
                1
            )

            precipitation_phrase = (
                "Al momento non sono indicate precipitazioni"
                if current_precipitation <= 0
                else (
                    "Al momento sono indicate precipitazioni "
                    f"per {current_precipitation} millimetri"
                )
            )

            answer = (
                f"A Molfetta ci sono "
                f"{weather_code_description(weather_code)}, "
                f"con {temperature} gradi percepiti come "
                f"{apparent}. {precipitation_phrase}; "
                f"la probabilità massima di pioggia oggi è del "
                f"{rain_probability} per cento."
            )

        return make_response(
            answer=answer,
            confidence=2,
            debug_info=(
                "Live weather answer. "
                f"Coordinates={payload.get('_resolved_latitude')},"
                f"{payload.get('_resolved_longitude')}. "
                f"CurrentTime={current.get('time', '')}."
            )
        )

    except Exception as error:
        print(f"[Weather Error] {error}")

        return make_response(
            answer=(
                "Al momento non riesco a recuperare il meteo "
                "di Molfetta. Posso comunque aiutarti con "
                "negozi, servizi e orari del centro."
            ),
            confidence=0,
            is_out_of_scope=False,
            debug_info=f"Weather error: {error}"
        )


# ---------------------------------------------------------------------
# GENERAL MALL NEED PLANNER
# ---------------------------------------------------------------------

GENERAL_PLANNER_SYSTEM_PROMPT = """
Sei il pianificatore di Nico, assistente del Gran Shopping Mongolfiera Molfetta.

Devi capire qual è il bisogno concreto di un cliente che si trova nel centro commerciale e scegliere soltanto elementi presenti nel catalogo fornito.

Puoi gestire esigenze espresse indirettamente, per esempio:
- preparare una torta e comprare ingredienti;
- comprare una cover o un caricatore;
- trovare qualcosa da mangiare;
- acquistare un regalo per una persona specifica;
- cercare prodotti per casa, bambini, sport, bellezza o tecnologia;
- trovare un servizio utile per un problema pratico.

Regole:
- Usa soltanto il catalogo fornito.
- Non inventare negozi, servizi, prodotti, posizioni o disponibilità.
- Un prodotto o servizio può essere consigliato soltanto quando la descrizione del catalogo lo supporta.
- Per preparare cibo o dolci, privilegia un punto vendita che dichiara prodotti alimentari.
- Non proporre una pasticceria se il catalogo non la contiene.
- Per salute e benessere non formulare diagnosi.
- Seleziona al massimo tre nomi esatti del catalogo.
- Evita i nomi già consigliati nella cronologia quando esistono alternative ugualmente pertinenti.
- Tra alternative ugualmente adatte, preferisci i negozi con mentions_in_window più basso o never_mentioned vero.
- Se la richiesta è ambigua, chiedi un solo chiarimento breve.
- Se il catalogo non contiene informazioni sufficienti, dichiara che non ci sono informazioni specifiche.
- Il campo lead_in non deve contenere nomi di negozi, cifre, link o riferimenti a file.
- Il campo reason deve essere breve e basato sulla descrizione dell'elemento scelto.
""".strip()


def build_compact_catalog_for_planner(
    mall_data: Dict[str, Any]
) -> List[Dict[str, str]]:
    compact: List[Dict[str, str]] = []

    for entry in build_catalog(mall_data):
        item = entry["item"]
        notes = re.sub(
            r"\s+",
            " ",
            item.get("notes", "")
        ).strip()

        exposure = shop_exposure_metrics(
            entry
        )

        compact.append({
            "name": item.get("name", ""),
            "type": entry["type"],
            "category": item.get("category", ""),
            "area": item.get("area", ""),
            "description": notes[:260],
            "mentions_in_window": exposure.get(
                "window_mentions",
                0
            ),
            "total_mentions": exposure.get(
                "total_mentions",
                0
            ),
            "never_mentioned": exposure.get(
                "never_mentioned",
                False
            )
        })

    return compact


def plan_general_mall_request(
    user_text: str,
    history: Sequence[str],
    mall_data: Dict[str, Any]
) -> Optional[Dict[str, Any]]:
    if not USE_OPENAI or not openai_client:
        return None

    catalog = build_catalog(mall_data)
    name_index = build_name_index(catalog)

    schema = {
        "type": "object",
        "additionalProperties": False,
        "properties": {
            "action": {
                "type": "string",
                "enum": [
                    "recommend",
                    "clarify",
                    "no_information"
                ]
            },
            "detected_need": {
                "type": "string"
            },
            "lead_in": {
                "type": "string"
            },
            "selected_items": {
                "type": "array",
                "maxItems": 2,
                "items": {
                    "type": "object",
                    "additionalProperties": False,
                    "properties": {
                        "name": {
                            "type": "string"
                        },
                        "reason": {
                            "type": "string"
                        }
                    },
                    "required": [
                        "name",
                        "reason"
                    ]
                }
            },
            "clarification_question": {
                "type": "string"
            }
        },
        "required": [
            "action",
            "detected_need",
            "lead_in",
            "selected_items",
            "clarification_question"
        ]
    }

    history_excerpt = list(history)[-8:]

    user_prompt = (
        "DOMANDA UTENTE:\n"
        f"{user_text}\n\n"
        "CRONOLOGIA RECENTE:\n"
        f"{json.dumps(history_excerpt, ensure_ascii=False)}\n\n"
        "CATALOGO UFFICIALE:\n"
        f"{json.dumps(build_compact_catalog_for_planner(mall_data), ensure_ascii=False)}"
    )

    try:
        response = openai_client.responses.create(
            model=OPENAI_MODEL,
            input=[
                {
                    "role": "system",
                    "content": GENERAL_PLANNER_SYSTEM_PROMPT
                },
                {
                    "role": "user",
                    "content": user_prompt
                }
            ],
            text={
                "format": {
                    "type": "json_schema",
                    "name": "nico_general_mall_plan",
                    "strict": True,
                    "schema": schema
                }
            }
        )

        parsed = json.loads(response.output_text)

        selected: List[Dict[str, Any]] = []
        selected_keys: set[str] = set()

        for selected_item in parsed.get(
            "selected_items",
            []
        ):
            entry = name_index.get(
                normalize_name_key(
                    selected_item.get("name", "")
                )
            )

            if not entry:
                continue

            if entry["name_key"] in selected_keys:
                continue

            reason = sanitize_answer(
                selected_item.get("reason", "")
            )

            selected.append({
                "entry": entry,
                "reason": truncate_at_word_boundary(
                    reason,
                    60
                )
            })

            selected_keys.add(entry["name_key"])

        return {
            "action": parsed.get(
                "action",
                "no_information"
            ),
            "detected_need": parsed.get(
                "detected_need",
                ""
            ),
            "lead_in": truncate_at_word_boundary(
                sanitize_answer(
                    parsed.get("lead_in", "")
                ),
                55
            ),
            "selected": selected,
            "clarification_question": (
                truncate_at_word_boundary(
                    sanitize_answer(
                        parsed.get(
                            "clarification_question",
                            ""
                        )
                    ),
                    180
                )
            )
        }

    except Exception as error:
        print(f"[General Planner Error] {error}")
        return None


def compose_general_plan_answer(
    plan: Dict[str, Any]
) -> str:
    selected = plan.get("selected", [])
    lead_in = plan.get("lead_in", "").strip()

    if not lead_in:
        lead_in = "Per questa esigenza puoi provare"

    parts: List[str] = []

    for selected_item in selected:
        entry = selected_item["entry"]
        item = entry["item"]
        reason = selected_item.get(
            "reason",
            ""
        ).strip()

        if not reason:
            reason = "per una proposta coerente con la tua richiesta"

        parts.append(
            f"{item.get('name', '')}, "
            f"{get_area_phrase(item)}, "
            f"{reason}"
        )

    if not parts:
        return (
            "Mi dispiace, non ho informazioni specifiche "
            "per questa richiesta."
        )

    if len(parts) == 1:
        return f"{lead_in} {parts[0]}."

    if len(parts) == 2:
        return (
            f"{lead_in} {parts[0]}; "
            f"oppure {parts[1]}."
        )

    return (
        f"{lead_in} {parts[0]}; "
        f"{parts[1]}; oppure {parts[2]}."
    )



# ---------------------------------------------------------------------
# ELEVENLABS TTS
# ---------------------------------------------------------------------

def synthesize_with_elevenlabs(text: str) -> bytes:
    if not ELEVENLABS_API_KEY:
        raise RuntimeError(
            "Missing ELEVENLABS_API_KEY in .env"
        )

    if not ELEVENLABS_VOICE_ID:
        raise RuntimeError(
            "Missing ELEVENLABS_VOICE_ID in .env"
        )

    url = (
        "https://api.elevenlabs.io/v1/"
        f"text-to-speech/{ELEVENLABS_VOICE_ID}"
    )

    headers = {
        "xi-api-key": ELEVENLABS_API_KEY,
        "Accept": "audio/mpeg",
        "Content-Type": "application/json"
    }

    payload = {
        "text": text,
        "model_id": ELEVENLABS_MODEL_ID,
        "voice_settings": {
            "stability": 0.45,
            "similarity_boost": 0.75,
            "style": 0.20,
            "use_speaker_boost": True
        }
    }

    print("[ElevenLabs] Sending TTS request...")
    print(
        "[ElevenLabs] VoiceId present: "
        f"{bool(ELEVENLABS_VOICE_ID)}"
    )
    print(
        f"[ElevenLabs] Model: {ELEVENLABS_MODEL_ID}"
    )
    print(
        "[ElevenLabs] OutputFormat: "
        f"{ELEVENLABS_OUTPUT_FORMAT}"
    )
    print(
        f"[ElevenLabs] Text length: {len(text)}"
    )

    response = requests.post(
        url,
        headers=headers,
        params={
            "output_format": ELEVENLABS_OUTPUT_FORMAT
        },
        json=payload,
        timeout=90
    )

    print(
        f"[ElevenLabs] StatusCode: "
        f"{response.status_code}"
    )
    print(
        "[ElevenLabs] Content-Type: "
        f"{response.headers.get('content-type', '')}"
    )
    print(
        "[ElevenLabs] Response bytes: "
        f"{len(response.content)}"
    )

    if (
        response.status_code < 200
        or response.status_code >= 300
    ):
        raise RuntimeError(
            "ElevenLabs error "
            f"{response.status_code}: {response.text}"
        )

    if not response.content:
        raise RuntimeError(
            "ElevenLabs returned empty audio content"
        )

    return response.content


# ---------------------------------------------------------------------
# ROUTES
# ---------------------------------------------------------------------

@app.on_event("startup")
def startup_log():
    print("------------------------------------------------------------")
    print("[AvatarZG Backend] Startup OK")
    print(f"[AvatarZG Backend] BASE_DIR: {BASE_DIR}")
    print(
        "[AvatarZG Backend] mall_data.json exists: "
        f"{MALL_DATA_PATH.exists()}"
    )
    print(
        f"[AvatarZG Backend] USE_OPENAI: {USE_OPENAI}"
    )
    print(
        f"[AvatarZG Backend] OPENAI_MODEL: {OPENAI_MODEL}"
    )
    print(
        "[AvatarZG Backend] OPENAI_API_KEY present: "
        f"{bool(OPENAI_API_KEY)}"
    )
    print(
        "[AvatarZG Backend] ELEVENLABS_API_KEY present: "
        f"{bool(ELEVENLABS_API_KEY)}"
    )
    print(
        "[AvatarZG Backend] ELEVENLABS_VOICE_ID present: "
        f"{bool(ELEVENLABS_VOICE_ID)}"
    )
    print(
        "[AvatarZG Backend] ELEVENLABS_MODEL_ID: "
        f"{ELEVENLABS_MODEL_ID}"
    )
    print(
        "[AvatarZG Backend] ELEVENLABS_OUTPUT_FORMAT: "
        f"{ELEVENLABS_OUTPUT_FORMAT}"
    )
    print(
        "[AvatarZG Backend] Analytics enabled: "
        f"{ENABLE_ANALYTICS}"
    )
    print(
        "[AvatarZG Backend] Analytics directory: "
        f"{ANALYTICS_DIR_PATH}"
    )
    print(
        "[AvatarZG Backend] Fairness enabled: "
        f"{FAIRNESS_ENABLED}"
    )

    if ENABLE_ANALYTICS:
        try:
            ensure_analytics_storage()
            load_exposure_state(
                force_reload=True
            )
        except Exception as analytics_error:
            print(
                "[AvatarZG Backend] Analytics startup error: "
                f"{analytics_error}"
            )

    print("------------------------------------------------------------")


@app.get("/health")
def health():
    return {
        "status": "ok",
        "service": "AvatarZG Nico Shopping Assistant Backend",
        "mall_data_file": str(MALL_DATA_PATH),
        "mall_data_exists": MALL_DATA_PATH.exists(),

        "use_openai": USE_OPENAI,
        "openai_model": OPENAI_MODEL,
        "openai_key_present": bool(OPENAI_API_KEY),

        "elevenlabs_key_present": bool(
            ELEVENLABS_API_KEY
        ),
        "elevenlabs_voice_id_present": bool(
            ELEVENLABS_VOICE_ID
        ),
        "elevenlabs_model_id": ELEVENLABS_MODEL_ID,
        "elevenlabs_output_format": (
            ELEVENLABS_OUTPUT_FORMAT
        ),

        "max_answer_chars": MAX_ANSWER_CHARS,
        "max_recommendations": MAX_RECOMMENDATIONS,
        "mall_timezone": MALL_TIMEZONE,
        "weather_location": WEATHER_LOCATION_NAME,
        "weather_admin1": WEATHER_ADMIN1,
        "weather_provider_configured": bool(
            WEATHER_API_BASE_URL
        ),
        "general_knowledge_enabled": bool(
            USE_OPENAI and openai_client
        ),
        "analytics_enabled": ENABLE_ANALYTICS,
        "analytics_directory": str(
            ANALYTICS_DIR_PATH
        ),
        "interaction_log_file": str(
            INTERACTION_LOG_PATH
        ),
        "shop_exposure_file": str(
            SHOP_EXPOSURE_STATE_PATH
        ),
        "fairness_enabled": FAIRNESS_ENABLED,
        "exposure_window_days": (
            EXPOSURE_WINDOW_DAYS
        )
    }


@app.get("/analytics/summary")
def analytics_summary(
    days: int = ANALYTICS_DEFAULT_DAYS
):
    return build_analytics_summary(days)


@app.get("/analytics/shops")
def analytics_shops(
    days: int = ANALYTICS_DEFAULT_DAYS
):
    safe_days = max(
        1,
        min(days, 3650)
    )

    return {
        "period_days": safe_days,
        "shops": shop_coverage_rows(
            safe_days
        )
    }


@app.get("/analytics/recent")
def analytics_recent(
    limit: int = 50
):
    safe_limit = max(
        1,
        min(limit, 500)
    )

    records = read_jsonl_records(
        INTERACTION_LOG_PATH
    )

    return {
        "count": min(
            len(records),
            safe_limit
        ),
        "records": records[-safe_limit:]
    }


@app.get("/analytics/export/interactions.csv")
def analytics_export_interactions(
    days: int = ANALYTICS_DEFAULT_DAYS
):
    safe_days = max(
        1,
        min(days, 3650)
    )

    rows = interactions_csv_rows(
        safe_days
    )

    fieldnames = [
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

    csv_text = rows_to_csv_text(
        rows,
        fieldnames
    )

    return Response(
        content=csv_text.encode("utf-8-sig"),
        media_type="text/csv",
        headers={
            "Content-Disposition": (
                "attachment; "
                "filename=nico_interactions.csv"
            )
        }
    )


@app.get("/analytics/export/shops.csv")
def analytics_export_shops(
    days: int = ANALYTICS_DEFAULT_DAYS
):
    safe_days = max(
        1,
        min(days, 3650)
    )

    rows = shop_coverage_rows(
        safe_days
    )

    fieldnames = [
        "name",
        "category",
        "area",
        "mentions_period",
        "total_mentions",
        "answer_mentions",
        "featured_mentions",
        "first_mentioned_utc",
        "last_mentioned_utc",
        "never_mentioned"
    ]

    csv_text = rows_to_csv_text(
        rows,
        fieldnames
    )

    return Response(
        content=csv_text.encode("utf-8-sig"),
        media_type="text/csv",
        headers={
            "Content-Disposition": (
                "attachment; "
                "filename=nico_shop_coverage.csv"
            )
        }
    )


@app.get("/analytics/featured-shop/preview")
def analytics_featured_shop_preview():
    entry = next_featured_shop_entry()

    if entry is None:
        return {
            "available": False
        }

    return {
        "available": True,
        **featured_shop_payload(entry)
    }


@app.post("/analytics/featured-shop/claim")
def analytics_featured_shop_claim(
    request: FeaturedShopRequest
):
    entry = next_featured_shop_entry()

    if entry is None:
        return {
            "available": False
        }

    channel = (
        request.Channel
        or request.channel
        or "idle_rotation"
    )

    payload = record_featured_shop_exposure(
        entry,
        channel=channel
    )

    if payload.get("available") is False:
        return payload

    return {
        "available": True,
        **payload
    }


@app.get("/mall-data")
def mall_data_preview():
    return get_mall_data()


@app.post("/assistant/retrieve")
def assistant_retrieve(
    request: AssistantRequest
):
    mall_data = get_mall_data()

    user_text = get_request_text(request)
    history = get_request_history(request)
    intents = detect_intents(user_text)

    retrieved = retrieve_relevant_entries(
        mall_data=mall_data,
        user_text=user_text,
        intents=intents,
        history=history,
        limit=8
    )

    return {
        "user_text": user_text,
        "intents": intents,
        "retrieved_items": [
            {
                "score": score,
                "type": entry["type"],
                "source": entry["source"],
                "item": entry["item"]
            }
            for score, entry in retrieved
        ]
    }


def assistant_query_core(
    request: AssistantRequest
):
    mall_data = get_mall_data()
    catalog = build_catalog(mall_data)
    name_index = build_name_index(catalog)

    user_text = get_request_text(request)
    history = get_request_history(request)
    session_id = get_request_session_id(request)

    if not user_text:
        return make_response(
            answer=(
                "Non ho ricevuto una domanda. "
                "Puoi ripetere?"
            ),
            confidence=0,
            needs_clarification=True,
            clarification_question=(
                "Puoi ripetere la domanda?"
            ),
            debug_info="Empty user text."
        )

    intents = detect_intents(user_text)

    # Conversazione leggera.
    small_talk = small_talk_answer(
        intents,
        session_id,
        user_text
    )

    if small_talk is not None:
        return small_talk

    # Informazioni dinamiche.
    if "current_time" in intents:
        return current_time_answer()

    if "current_date" in intents:
        return current_date_answer()

    if "weather" in intents:
        return weather_answer(user_text)

    # Bagni: risposta deterministica con entrambe le aree.
    if "restroom" in intents:
        return restroom_answer(mall_data)

    # Richiesta esplicita su un nome noto.
    known_mentions = official_names_in_text(
        user_text,
        catalog
    )

    if (
        known_mentions
        and is_explicit_entity_question(user_text)
    ):
        return direct_entry_answer(
            known_mentions[0],
            user_text
        )

    # Richiesta esplicita con estrazione del nome.
    explicit_candidate = None

    if is_explicit_entity_question(user_text):
        explicit_candidate = (
            extract_explicit_entity_candidate(
                user_text
            )
        )

    if explicit_candidate:
        exact_entry = find_exact_catalog_entry(
            explicit_candidate,
            name_index
        )

        if exact_entry:
            return direct_entry_answer(
                exact_entry,
                user_text
            )

        if is_likely_specific_unknown_name(
            explicit_candidate
        ):
            return make_response(
                answer=ABSENT_STORE_ANSWER,
                confidence=2,
                is_out_of_scope=False,
                debug_info=(
                    "Explicit catalog name absent: "
                    f"{explicit_candidate}"
                )
            )

    # Informazioni dirette sul centro.
    if "hours" in intents:
        return opening_hours_answer(
            mall_data,
            user_text
        )

    if "contact" in intents:
        return contact_answer(mall_data)

    if "directions" in intents:
        return directions_answer(mall_data)

    if "events" in intents:
        return events_answer(mall_data)

    specialized_retrieval_intents = {
        "assistance",
        "dental",
        "optical",
        "pharmacy",
        "hair",
        "tailor",
        "laundry",
        "travel",
        "groceries",
        "cooking_tools",
        "dessert",
        "books",
        "toys",
        "phone_accessories",
        "gift",
        "gaming",
        "telephony",
        "tech",
        "beauty",
        "jewelry",
        "fashion",
        "home",
        "food",
        "children",
        "sport"
    }

    has_specialized_intent = any(
        intent in specialized_retrieval_intents
        for intent in intents
    )

    # Per gli intenti riconosciuti, OpenAI può scegliere soltanto
    # tra candidati già filtrati e verificati dal backend.
    if has_specialized_intent:
        retrieved = retrieve_relevant_entries(
            mall_data=mall_data,
            user_text=user_text,
            intents=intents,
            history=history,
            limit=8
        )

        if retrieved:
            selected_entries = select_entries_with_openai(
                user_text=user_text,
                intents=intents,
                scored_entries=retrieved
            )

            if not selected_entries:
                selected_entries = choose_diverse_entries(
                    retrieved,
                    MAX_RECOMMENDATIONS
                )

            answer = compose_recommendation_answer(
                selected_entries,
                intents,
                user_text
            )

            used_sources = sorted({
                entry["source"]
                for entry in selected_entries
            })

            suggested_items = [
                entry["item"]
                for entry in selected_entries
            ]

            return make_response(
                answer=answer,
                confidence=2,
                used_sources=used_sources,
                suggested_shops=suggested_items,
                needs_clarification=False,
                clarification_question="",
                is_out_of_scope=False,
                debug_info=(
                    "Constrained grounded answer. "
                    f"Model={OPENAI_MODEL}. "
                    f"UseOpenAI={USE_OPENAI}. "
                    f"Intents={intents}. "
                    f"SelectedNames="
                    f"{[item.get('name', '') for item in suggested_items]}."
                )
            )

        return make_response(
            answer=(
                "Mi dispiace, non ho informazioni specifiche "
                "per questa richiesta. Posso aiutarti a cercare "
                "un'altra soluzione disponibile nel centro."
            ),
            confidence=0,
            needs_clarification=True,
            clarification_question=(
                "Vuoi descrivermi meglio cosa ti serve?"
            ),
            is_out_of_scope=False,
            debug_info=(
                f"No eligible candidates for intents {intents}."
            )
        )

    # Richieste di acquisto generiche e non classificate:
    # il planner può consultare l'intero catalogo, ma il backend
    # accetta soltanto nomi ufficiali.
    if "generic_shopping" in intents:
        general_plan = plan_general_mall_request(
            user_text=user_text,
            history=history,
            mall_data=mall_data
        )

        if general_plan is not None:
            action = general_plan.get("action")

            if (
                action == "recommend"
                and general_plan.get("selected")
            ):
                selected_entries = [
                    selected_item["entry"]
                    for selected_item
                    in general_plan["selected"]
                ]

                answer = compose_general_plan_answer(
                    general_plan
                )

                return make_response(
                    answer=answer,
                    confidence=2,
                    used_sources=sorted({
                        entry["source"]
                        for entry in selected_entries
                    }),
                    suggested_shops=[
                        entry["item"]
                        for entry in selected_entries
                    ],
                    needs_clarification=False,
                    clarification_question="",
                    is_out_of_scope=False,
                    debug_info=(
                        "General OpenAI mall plan. "
                        f"Need={general_plan.get('detected_need', '')}. "
                        f"Selected="
                        f"{[entry['item'].get('name', '') for entry in selected_entries]}."
                    )
                )

            if action == "clarify":
                question = general_plan.get(
                    "clarification_question",
                    ""
                ) or (
                    "Puoi dirmi qualcosa in più su ciò "
                    "che stai cercando?"
                )

                return make_response(
                    answer=question,
                    confidence=1,
                    needs_clarification=True,
                    clarification_question=question,
                    is_out_of_scope=False,
                    debug_info=(
                        "General OpenAI mall plan requested clarification."
                    )
                )

        return make_response(
            answer=(
                "Non ho trovato una soluzione precisa. "
                "Puoi dirmi meglio quale prodotto o servizio cerchi?"
            ),
            confidence=0,
            needs_clarification=True,
            clarification_question=(
                "Quale prodotto o servizio stai cercando?"
            ),
            is_out_of_scope=False,
            debug_info="Generic shopping request unresolved."
        )

    # Domande generali semplici: risposta breve e richiamo al centro.
    return general_knowledge_answer(
        user_text=user_text,
        history=history
    )


@app.post("/assistant/query")
def assistant_query(
    request: AssistantRequest
):
    started_at = time.perf_counter()

    user_text = get_request_text(request)
    intents = detect_intents(user_text)

    try:
        response = assistant_query_core(
            request
        )

    except Exception:
        elapsed_ms = (
            time.perf_counter()
            - started_at
        ) * 1000.0

        error_response = make_response(
            answer=(
                "Si è verificato un problema temporaneo. "
                "Puoi ripetere la domanda?"
            ),
            confidence=0,
            needs_clarification=True,
            clarification_question=(
                "Puoi ripetere la domanda?"
            ),
            debug_info="Unhandled assistant error."
        )

        try:
            record_interaction(
                request=request,
                response=error_response,
                intents=intents,
                response_ms=elapsed_ms
            )
        except Exception as analytics_error:
            print(
                "[Analytics Error] "
                f"{analytics_error}"
            )

        raise

    elapsed_ms = (
        time.perf_counter()
        - started_at
    ) * 1000.0

    try:
        record_interaction(
            request=request,
            response=response,
            intents=intents,
            response_ms=elapsed_ms
        )
    except Exception as analytics_error:
        print(
            "[Analytics Error] "
            f"{analytics_error}"
        )

    return response


@app.post("/assistant/tts")
def assistant_tts(
    request: TtsRequest
):
    text = get_tts_text(request)

    if not text:
        return Response(
            content=b"Missing text",
            media_type="text/plain",
            status_code=400
        )

    try:
        audio_bytes = synthesize_with_elevenlabs(
            text
        )

        return Response(
            content=audio_bytes,
            media_type="audio/mpeg",
            headers={
                "Content-Disposition": (
                    "inline; filename=assistant_tts.mp3"
                )
            }
        )

    except Exception as error:
        error_message = (
            f"[ElevenLabs Error] {error}"
        )

        print(error_message)

        return Response(
            content=error_message.encode("utf-8"),
            media_type="text/plain",
            status_code=500
        )

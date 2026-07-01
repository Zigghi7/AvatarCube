"""
Test end-to-end della funzione /assistant/query senza avviare FastAPI.

Modalità disponibili:

    python test_nico.py

Esegue test deterministici. Il meteo e la risposta di cultura generale
sono simulati: i valori stampati NON rappresentano il meteo reale.

    python test_nico.py --live

Usa il servizio meteo reale e OpenAI configurato nel file .env.
Richiede USE_OPENAI=true, OPENAI_API_KEY valida e connessione Internet.
"""

import argparse
from typing import Any, Dict

import main


# I test funzionali non devono alterare i dati reali di esposizione.
main.ENABLE_ANALYTICS = False
main.FAIRNESS_ENABLED = False


MALL_REMINDER = (
    "Posso aiutarti con negozi, servizi, orari, eventi, "
    "ristorazione e idee per lo shopping del Gran Shopping Molfetta."
)


def fake_weather_snapshot() -> Dict[str, Any]:
    return {
        "_resolved_latitude": 41.2,
        "_resolved_longitude": 16.6,
        "current": {
            "time": "2026-06-22T12:00",
            "temperature_2m": 24,
            "apparent_temperature": 25,
            "weather_code": 1,
            "precipitation": 0,
            "wind_speed_10m": 8,
        },
        "daily": {
            "time": ["2026-06-22", "2026-06-23"],
            "weather_code": [1, 61],
            "temperature_2m_max": [27, 22],
            "temperature_2m_min": [18, 17],
            "precipitation_probability_max": [10, 70],
        },
    }


def fake_general_knowledge_answer(
    user_text: str,
    history: list[str],
) -> Dict[str, Any]:
    del history

    normalized = main.normalize_text(user_text)

    if "capitale del giappone" in normalized:
        short_answer = "La capitale del Giappone è Tokyo"
    else:
        short_answer = "Posso rispondere brevemente alla domanda"

    return main.make_response(
        answer=main.compose_general_knowledge_answer(short_answer),
        confidence=2,
        is_out_of_scope=True,
        debug_info="Deterministic mocked general answer.",
    )


def build_tests(live_mode: bool) -> list[dict[str, Any]]:
    tests: list[dict[str, Any]] = [
        {
            "question": "Ho mal di denti, c'è qualche studio dentistico qui?",
            "must_contain": ["Dental Pro", "centotrenta"],
            "must_not_contain": ["Sephora", "Equivalenza"],
        },
        {
            "question": "Sto cercando un regalo per la mia nonnina",
            "must_contain_any": [
                "Blue Spirit",
                "Gioielli di valenza",
                "Kasanova",
                "Thun",
                "Sarni Oro",
            ],
            "must_not_contain": ["Oviesse"],
        },
        {
            "question": "Mi piace l'intrattenimento tecnologico. Consigli?",
            "must_contain": ["Gamelife", "Mediaworld"],
            "must_not_contain": ["Vodafone", "Wind tre", "Tim,"],
        },
        {
            "question": "Voglio fare una torta e mi servono gli ingredienti",
            "must_contain": ["COOP", "prodotti alimentari"],
            "must_not_contain": ["Vesuvya", "KFC"],
        },
        {
            "question": "Mi serve una teglia per fare una torta",
            "must_contain": ["Kasanova"],
        },
        {
            "question": "Vorrei comprare una cover per il telefono",
            "must_contain": ["La casa de las carcasas"],
        },
        {
            "question": "Vorrei comprare un libro",
            "must_contain": ["Giunti al punto"],
        },
        {
            "question": "C'è H&M in questo centro commerciale?",
            "exact": "Mi dispiace, non ho informazioni a riguardo.",
        },
        {
            "question": "Dove si trova MediaWorld?",
            "must_contain": ["Mediaworld", "cinquantaquattro"],
            "must_not_contain": ["Due Passi", "Pasqua"],
        },
        {
            "question": "Dove sono i bagni?",
            "must_contain": [
                "TIM",
                "Zagaria Sport",
                "Pandora",
                "Senso Unico",
            ],
        },
        {
            "question": "Raccontami una barzelletta",
            "must_contain_any": ["carrello", "scarpe", "caffè"],
        },
        {
            "question": "Qual è la capitale del Giappone?",
            "must_contain": ["Tokyo", MALL_REMINDER],
        },
    ]

    tests.append({
        "question": "Che ore sono?",
        "must_contain": ["Molfetta"],
    })

    if live_mode:
        tests.extend([
            {
                "question": "Che tempo fa oggi?",
                "must_contain": ["Molfetta"],
                "must_not_contain": ["non riesco a recuperare"],
                "label": "METEO REALE",
            },
            {
                "question": "Che tempo farà domani?",
                "must_contain": ["Domani", "Molfetta"],
                "must_not_contain": ["non riesco a recuperare"],
                "label": "METEO REALE",
            },
        ])
    else:
        tests.extend([
            {
                "question": "Che tempo fa oggi?",
                "must_contain": [
                    "Molfetta",
                    "ventiquattro",
                    "dieci per cento",
                ],
                "label": "METEO SIMULATO",
            },
            {
                "question": "Che tempo farà domani?",
                "must_contain": [
                    "Domani",
                    "pioggia",
                    "settanta per cento",
                ],
                "label": "METEO SIMULATO",
            },
        ])

    return tests


def run_test(test: dict[str, Any]) -> None:
    request = main.AssistantRequest(
        SessionId="nico-test-session",
        UserText=test["question"],
        ConversationHistory=[],
    )

    response = main.assistant_query(request)
    answer = response["AnswerToSay"]

    print()

    if test.get("label"):
        print(f"[{test['label']}]")

    print(test["question"])
    print(answer)

    if "exact" in test:
        assert answer == test["exact"], (
            f"Risposta diversa da quella attesa: {answer}"
        )

    for value in test.get("must_contain", []):
        assert value in answer, (
            f"Manca '{value}' nella risposta: {answer}"
        )

    if "must_contain_any" in test:
        assert any(
            value in answer
            for value in test["must_contain_any"]
        ), (
            "Nessuno dei valori attesi è presente: "
            f"{test['must_contain_any']} | {answer}"
        )

    for value in test.get("must_not_contain", []):
        assert value not in answer, (
            f"Valore non ammesso '{value}' nella risposta: {answer}"
        )

    assert len(answer) <= main.MAX_ANSWER_CHARS, (
        f"Risposta troppo lunga: {len(answer)} caratteri"
    )

    assert not any(character.isdigit() for character in answer), (
        f"La risposta contiene cifre: {answer}"
    )

    print("OK")


def main_test() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--live",
        action="store_true",
        help="Usa meteo reale e OpenAI reale.",
    )
    args = parser.parse_args()

    if args.live:
        print("MODALITÀ LIVE: meteo e OpenAI reali.")

        if not main.USE_OPENAI or not main.openai_client:
            raise RuntimeError(
                "Per --live imposta USE_OPENAI=true e una "
                "OPENAI_API_KEY valida nel file .env."
            )
    else:
        print(
            "MODALITÀ DETERMINISTICA: meteo e risposta generale "
            "sono simulati e non rappresentano dati reali."
        )

        main.USE_OPENAI = False
        main.get_weather_snapshot = fake_weather_snapshot
        main.general_knowledge_answer = fake_general_knowledge_answer

    for current_test in build_tests(args.live):
        run_test(current_test)

    print()
    print("Tutti i test di Nico sono stati superati.")


if __name__ == "__main__":
    main_test()

"""
Test isolato del logging analytics e della rotazione negozi.

Non modifica i dati reali: usa una cartella temporanea.
"""

import json
import tempfile
from pathlib import Path

import main


def run() -> None:
    with tempfile.TemporaryDirectory() as temporary:
        analytics_dir = Path(temporary)

        main.ENABLE_ANALYTICS = True
        main.FAIRNESS_ENABLED = True
        main.ANALYTICS_REDACT_PII = True
        main.ANALYTICS_DIR_PATH = analytics_dir
        main.INTERACTION_LOG_PATH = (
            analytics_dir / "interactions.jsonl"
        )
        main.FEATURED_EXPOSURE_LOG_PATH = (
            analytics_dir / "featured_exposures.jsonl"
        )
        main.SHOP_EXPOSURE_STATE_PATH = (
            analytics_dir / "shop_exposure.json"
        )
        main._EXPOSURE_STATE_CACHE = None
        main.USE_OPENAI = False

        requests_to_test = [
            main.AssistantRequest(
                SessionId="analytics-test-session",
                InteractionSource="test",
                UserText=(
                    "Mi piace l'intrattenimento "
                    "tecnologico. Consigli?"
                ),
                ConversationHistory=[]
            ),
            main.AssistantRequest(
                SessionId="analytics-test-session",
                InteractionSource="test",
                UserText=(
                    "Dove si trova MediaWorld?"
                ),
                ConversationHistory=[]
            )
        ]

        for request in requests_to_test:
            response = main.assistant_query(
                request
            )
            print(response["AnswerToSay"])

        assert main.INTERACTION_LOG_PATH.exists()
        assert main.SHOP_EXPOSURE_STATE_PATH.exists()

        records = main.read_jsonl_records(
            main.INTERACTION_LOG_PATH
        )

        assert len(records) == 2
        assert any(
            "Mediaworld"
            in record.get(
                "mentioned_shops",
                []
            )
            for record in records
        )

        with main.SHOP_EXPOSURE_STATE_PATH.open(
            "r",
            encoding="utf-8"
        ) as file:
            state = json.load(file)

        mediaworld = state["shops"]["mediaworld"]

        assert mediaworld["total_mentions"] >= 1
        assert mediaworld["answer_mentions"] >= 1

        preview = main.analytics_featured_shop_preview()
        assert preview["available"] is True

        claimed = main.analytics_featured_shop_claim(
            main.FeaturedShopRequest(
                Channel="test_rotation"
            )
        )

        assert claimed["available"] is True
        assert (
            main.FEATURED_EXPOSURE_LOG_PATH.exists()
        )

        summary = main.build_analytics_summary(30)

        assert summary["interactions"] == 2
        assert (
            summary["shop_coverage"]["total_shops"]
            > 0
        )

        print(
            "Test analytics superato. "
            f"Negozio featured: {claimed['Name']}"
        )


if __name__ == "__main__":
    run()

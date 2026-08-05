from __future__ import annotations

import unittest

from app import (
    annotate_attention,
    actor_display_name,
    build_tree,
    decorate_item_occurrences,
    optional_query_int,
    reason_labels,
    risk_condition,
    templates,
)


class QuarantineViewTests(unittest.TestCase):
    def test_reason_labels(self) -> None:
        self.assertEqual(reason_labels(1), ["Stackable em crash"])
        self.assertEqual(reason_labels(2), ["Identidade também encontrada com jogador"])
        self.assertEqual(
            reason_labels(3),
            ["Stackable em crash", "Identidade também encontrada com jogador"],
        )
        self.assertEqual(reason_labels(0), ["Contexto do container"])

    def test_manifest_tree_uses_source_indexes(self) -> None:
        items = [
            {"source_item_index": 2, "parent_source_item_index": -1, "item_name": "backpack"},
            {"source_item_index": 7, "parent_source_item_index": 2, "item_name": "bag"},
            {"source_item_index": 9, "parent_source_item_index": 7, "item_name": "gold coin"},
        ]
        tree = build_tree(items)
        self.assertEqual(len(tree), 1)
        self.assertEqual(tree[0].item["item_name"], "backpack")
        self.assertEqual(tree[0].children[0].item["item_name"], "bag")
        self.assertEqual(tree[0].children[0].children[0].item["item_name"], "gold coin")

    def test_all_templates_compile(self) -> None:
        for name in (
            "base.html",
            "dashboard.html",
            "item_detail.html",
            "quarantine_list.html",
            "quarantine_detail.html",
            "player_overview.html",
            "player_detail.html",
        ):
            templates.env.get_template(name)

    def test_actor_names_distinguish_attribution_from_ownership(self) -> None:
        self.assertEqual(actor_display_name(0), "Sem atribuição")
        self.assertEqual(actor_display_name(2, "GM Lancellin"), "GM Lancellin")
        self.assertEqual(actor_display_name(999), "Personagem #999")

    def test_risk_filter_accepts_only_known_bands(self) -> None:
        self.assertIsNone(risk_condition(""))
        self.assertIn("<= 80", risk_condition("high") or "")
        self.assertIn(">= 300", risk_condition("stable") or "")
        with self.assertRaises(Exception):
            risk_condition("invented")

    def test_optional_numeric_filters_accept_empty_form_fields(self) -> None:
        self.assertIsNone(optional_query_int("", "source", 1))
        self.assertIsNone(optional_query_int(None, "actor", 0))
        self.assertEqual(optional_query_int(" 84 ", "source", 1), 84)
        self.assertEqual(optional_query_int("0", "actor", 0), 0)
        with self.assertRaises(Exception):
            optional_query_int("not-a-number", "source", 1)

    def test_item_occurrences_include_container_and_tile_totals(self) -> None:
        manifest = [
            {"quarantine_id": 1, "source_item_index": 1, "parent_source_item_index": -1,
             "item_name": "backpack", "item_count": 1, "is_container": 1},
            {"quarantine_id": 1, "source_item_index": 2, "parent_source_item_index": 1,
             "item_name": "gold coin", "item_count": 70, "is_container": 0},
            {"quarantine_id": 1, "source_item_index": 3, "parent_source_item_index": 1,
             "item_name": "gold coin", "item_count": 20, "is_container": 0},
            {"quarantine_id": 1, "source_item_index": 4, "parent_source_item_index": -1,
             "item_name": "gold coin", "item_count": 10, "is_container": 0},
        ]
        decorated = decorate_item_occurrences(
            [manifest[1], manifest[2], manifest[3]], manifest
        )
        self.assertEqual(decorated[0]["container_item_total"], 90)
        self.assertEqual(decorated[1]["container_item_total"], 90)
        self.assertEqual(decorated[2]["container_item_total"], 10)
        self.assertEqual(decorated[0]["tile_item_total"], 100)
        self.assertEqual(decorated[0]["path"], "backpack › gold coin")

    def test_attention_bands_and_tiny_gold_context(self) -> None:
        high = {"stable_seconds": 80, "item_id": 2152, "item_count": 1}
        annotate_attention(high)
        self.assertEqual(high["attention_level"], "high")

        small_gold = {"stable_seconds": 10, "item_id": 2148, "item_count": 5}
        annotate_attention(small_gold)
        self.assertEqual(small_gold["attention_level"], "recent-small")

        review = {"stable_seconds": 81, "item_id": 2148, "item_count": 100}
        annotate_attention(review)
        self.assertEqual(review["attention_level"], "review")

        stable = {"stable_seconds": 300, "item_id": 2160, "item_count": 100}
        annotate_attention(stable)
        self.assertEqual(stable["attention_level"], "stable")


if __name__ == "__main__":
    unittest.main()

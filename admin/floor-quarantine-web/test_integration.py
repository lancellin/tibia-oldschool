from __future__ import annotations

import os
import unittest

from fastapi.testclient import TestClient

from app import app
from database import fetch_one


@unittest.skipUnless(os.getenv("FQ_TEST_INTEGRATION") == "1", "integration test disabled")
class QuarantineIntegrationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.client = TestClient(app)
        self.auth = (
            os.environ["FQ_ADMIN_USER"],
            os.environ["FQ_ADMIN_PASSWORD"],
        )

    def test_authentication_is_required(self) -> None:
        response = self.client.get("/")
        self.assertEqual(response.status_code, 401)

    def test_dashboard_and_list_render_from_database(self) -> None:
        dashboard = self.client.get("/", auth=self.auth)
        self.assertEqual(dashboard.status_code, 200)
        self.assertIn("Itens em quarentena", dashboard.text)

        listing = self.client.get("/quarantine", auth=self.auth)
        self.assertEqual(listing.status_code, 200)
        self.assertIn("Tiles em quarentena", listing.text)

        players = self.client.get("/players", auth=self.auth)
        self.assertEqual(players.status_code, 200)
        self.assertIn("Jogadores relacionados", players.text)
        self.assertIn("Sem atribui", players.text)

        blank_filters = self.client.get(
            "/?source=&actor=2", auth=self.auth
        )
        self.assertEqual(blank_filters.status_code, 200)
        self.assertIn("Itens em quarentena", blank_filters.text)

    def test_health_checks_database(self) -> None:
        response = self.client.get("/health", auth=self.auth)
        self.assertEqual(response.status_code, 200)
        self.assertEqual(response.json(), {"status": "ok"})

    def test_item_type_drilldown_renders(self) -> None:
        row = fetch_one(
            """
            SELECT item_id
              FROM floor_persistence_quarantine_items
             WHERE is_quarantined = 1
             ORDER BY item_id
             LIMIT 1
            """
        )
        if row is None:
            self.skipTest("no normalized quarantine items in database")
        response = self.client.get(f"/items/{row['item_id']}", auth=self.auth)
        self.assertEqual(response.status_code, 200)
        self.assertIn("Total de unidades", response.text)
        self.assertIn("No container", response.text)

    def test_existing_quarantine_detail_renders(self) -> None:
        row = fetch_one("SELECT id FROM floor_persistence_quarantine ORDER BY id LIMIT 1")
        if row is None:
            self.skipTest("no quarantine rows in database")
        response = self.client.get(f"/quarantine/{row['id']}", auth=self.auth)
        self.assertEqual(response.status_code, 200)
        self.assertIn("Evidência original", response.text)


    def test_existing_actor_detail_renders(self) -> None:
        row = fetch_one(
            """
            SELECT last_actor_guid
              FROM floor_persistence_quarantine_items
             WHERE is_quarantined = 1
             GROUP BY last_actor_guid
             ORDER BY (last_actor_guid = 0), last_actor_guid
             LIMIT 1
            """
        )
        if row is None:
            self.skipTest("no quarantine attribution available")
        response = self.client.get(
            f"/players/{row['last_actor_guid']}", auth=self.auth
        )
        self.assertEqual(response.status_code, 200)
        self.assertIn("Itens relacionados", response.text)
        self.assertIn("Ocorr", response.text)

        filtered = self.client.get(
            f"/players/{row['last_actor_guid']}?risk=high", auth=self.auth
        )
        self.assertEqual(filtered.status_code, 200)
        self.assertIn("0–80 segundos", filtered.text)

        unassigned = self.client.get("/players/0", auth=self.auth)
        self.assertEqual(unassigned.status_code, 200)
        self.assertIn("Sem atribui", unassigned.text)


if __name__ == "__main__":
    unittest.main()

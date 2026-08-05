from __future__ import annotations

import math
import os
import secrets
from dataclasses import dataclass, field
from pathlib import Path
from typing import Any

import pymysql
from fastapi import Depends, FastAPI, HTTPException, Query, Request, status
from fastapi.responses import HTMLResponse
from fastapi.security import HTTPBasic, HTTPBasicCredentials
from fastapi.staticfiles import StaticFiles
from fastapi.templating import Jinja2Templates

from database import ConfigurationError, fetch_all, fetch_one


BASE_DIR = Path(__file__).resolve().parent
PAGE_SIZE = 50
ITEM_PAGE_SIZE = 100
HIGH_ATTENTION_SECONDS = 80
STABLE_ATTENTION_SECONDS = 300

CHECKPOINT_JOIN = """
LEFT JOIN (
    SELECT save_session_id, MAX(created_at) AS last_checkpoint_at
      FROM floor_persistence_checkpoints
     WHERE state = 'COMMITTED'
     GROUP BY save_session_id
) cp ON cp.save_session_id = q.recovery_source_session_id
"""

STABILITY_SECONDS_SQL = """
CASE
    WHEN q.source_snapshot_updated_at IS NULL OR cp.last_checkpoint_at IS NULL THEN NULL
    ELSE GREATEST(
        TIMESTAMPDIFF(SECOND, q.source_snapshot_updated_at, cp.last_checkpoint_at),
        0
    )
END
"""

RISK_FILTERS = {
    "high": f"({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}",
    "review": (
        f"({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS} "
        f"AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}"
    ),
    "stable": f"({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS}",
    "unknown": f"({STABILITY_SECONDS_SQL}) IS NULL",
}

app = FastAPI(
    title="Floor Quarantine Review",
    docs_url=None,
    redoc_url=None,
    openapi_url=None,
)
app.mount("/static", StaticFiles(directory=BASE_DIR / "static"), name="static")
templates = Jinja2Templates(directory=BASE_DIR / "templates")
security = HTTPBasic()


@dataclass
class ItemNode:
    item: dict[str, Any]
    children: list["ItemNode"] = field(default_factory=list)


def require_staff(credentials: HTTPBasicCredentials = Depends(security)) -> str:
    expected_user = os.getenv("FQ_ADMIN_USER", "")
    expected_password = os.getenv("FQ_ADMIN_PASSWORD", "")
    if not expected_user or not expected_password:
        raise HTTPException(
            status_code=status.HTTP_503_SERVICE_UNAVAILABLE,
            detail="Staff authentication is not configured.",
        )
    valid_user = secrets.compare_digest(
        credentials.username.encode("utf-8"), expected_user.encode("utf-8")
    )
    valid_password = secrets.compare_digest(
        credentials.password.encode("utf-8"), expected_password.encode("utf-8")
    )
    if not (valid_user and valid_password):
        raise HTTPException(
            status_code=status.HTTP_401_UNAUTHORIZED,
            detail="Invalid credentials.",
            headers={"WWW-Authenticate": "Basic"},
        )
    return credentials.username


def reason_labels(mask: int) -> list[str]:
    labels: list[str] = []
    if mask & 1:
        labels.append("Stackable em crash")
    if mask & 2:
        labels.append("Identidade também encontrada com jogador")
    if not labels:
        labels.append("Contexto do container")
    return labels


def annotate_attention(row: dict[str, Any]) -> None:
    stable_seconds = row.get("stable_seconds")
    if stable_seconds is None:
        row["attention_level"] = "unknown"
        row["attention_label"] = "Tempo não disponível"
        return

    seconds = int(stable_seconds)
    row["stable_seconds"] = seconds
    if seconds <= HIGH_ATTENTION_SECONDS:
        if int(row.get("item_id", 0)) == 2148 and int(row.get("item_count", 0)) <= 5:
            row["attention_level"] = "recent-small"
            row["attention_label"] = "Recente, quantidade mínima"
        else:
            row["attention_level"] = "high"
            row["attention_label"] = "Atenção: muito próximo do crash"
    elif seconds < STABLE_ATTENTION_SECONDS:
        row["attention_level"] = "review"
        row["attention_label"] = "Revisar: menos de 5 minutos"
    else:
        row["attention_level"] = "stable"
        row["attention_label"] = "5+ minutos antes do último checkpoint"


def build_tree(items: list[dict[str, Any]]) -> list[ItemNode]:
    nodes = {int(item["source_item_index"]): ItemNode(item=item) for item in items}
    roots: list[ItemNode] = []
    for item in items:
        index = int(item["source_item_index"])
        parent_index = int(item["parent_source_item_index"])
        node = nodes[index]
        if parent_index < 0 or parent_index not in nodes:
            roots.append(node)
        else:
            nodes[parent_index].children.append(node)
    return roots


def decorate_item_occurrences(
    occurrences: list[dict[str, Any]], manifest_items: list[dict[str, Any]]
) -> list[dict[str, Any]]:
    by_quarantine: dict[int, dict[int, dict[str, Any]]] = {}
    for item in manifest_items:
        quarantine_id = int(item["quarantine_id"])
        by_quarantine.setdefault(quarantine_id, {})[
            int(item["source_item_index"])
        ] = item

    tile_totals: dict[int, int] = {}
    container_totals: dict[tuple[int, int], int] = {}
    decorated: list[dict[str, Any]] = []
    for occurrence in occurrences:
        quarantine_id = int(occurrence["quarantine_id"])
        manifest = by_quarantine.get(quarantine_id, {})
        parent_index = int(occurrence["parent_source_item_index"])
        ancestors: list[dict[str, Any]] = []
        visited: set[int] = set()
        while parent_index >= 0 and parent_index not in visited:
            visited.add(parent_index)
            parent = manifest.get(parent_index)
            if parent is None:
                break
            ancestors.append(parent)
            parent_index = int(parent["parent_source_item_index"])
        ancestors.reverse()

        nearest_container = next(
            (item for item in reversed(ancestors) if int(item["is_container"]) == 1),
            None,
        )
        container_index = (
            int(nearest_container["source_item_index"])
            if nearest_container is not None
            else -1
        )
        count = int(occurrence["item_count"])
        tile_totals[quarantine_id] = tile_totals.get(quarantine_id, 0) + count
        container_key = (quarantine_id, container_index)
        container_totals[container_key] = container_totals.get(container_key, 0) + count

        row = dict(occurrence)
        row["container_index"] = container_index
        row["container_name"] = (
            nearest_container["item_name"]
            if nearest_container is not None
            else "Solto no tile"
        )
        row["path"] = " › ".join(
            [str(item["item_name"]) for item in ancestors]
            + [str(occurrence["item_name"])]
        )
        decorated.append(row)

    for row in decorated:
        quarantine_id = int(row["quarantine_id"])
        row["tile_item_total"] = tile_totals[quarantine_id]
        row["container_item_total"] = container_totals[
            (quarantine_id, int(row["container_index"]))
        ]
    return decorated


def risk_condition(risk: str) -> str | None:
    if not risk:
        return None
    condition = RISK_FILTERS.get(risk)
    if condition is None:
        raise HTTPException(status_code=400, detail="Invalid risk filter.")
    return condition


def actor_display_name(actor_guid: int, player_name: str | None = None) -> str:
    if actor_guid == 0:
        return "Sem atribuição"
    return player_name or f"Personagem #{actor_guid}"


def optional_query_int(
    value: str | int | None,
    field_name: str,
    minimum: int,
) -> int | None:
    if value is None or str(value).strip() == "":
        return None
    try:
        parsed = int(str(value).strip())
    except ValueError as exc:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid {field_name} filter.",
        ) from exc
    if parsed < minimum:
        raise HTTPException(
            status_code=400,
            detail=f"Invalid {field_name} filter.",
        )
    return parsed


def attach_actor_breakdowns(
    item_types: list[dict[str, Any]],
    source: int | None,
    actor: int | None,
) -> None:
    if not item_types:
        return

    where = ["qi.is_quarantined = 1"]
    params: list[Any] = []
    if source is not None:
        where.append("q.recovery_source_session_id = %s")
        params.append(source)
    if actor is not None:
        where.append("qi.last_actor_guid = %s")
        params.append(actor)

    rows = fetch_all(
        f"""
        SELECT qi.item_id, qi.last_actor_guid,
               MAX(p.name) AS player_name,
               COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE {" AND ".join(where)}
         GROUP BY qi.item_id, qi.last_actor_guid
         ORDER BY qi.item_id, total_units DESC, stack_count DESC
        """,
        tuple(params),
    )
    grouped: dict[int, list[dict[str, Any]]] = {}
    for row in rows:
        actor_guid = int(row["last_actor_guid"])
        row["actor_name"] = actor_display_name(actor_guid, row.get("player_name"))
        grouped.setdefault(int(row["item_id"]), []).append(row)

    for item in item_types:
        breakdown = grouped.get(int(item["item_id"]), [])
        item["actor_breakdown"] = breakdown[:3]
        item["actor_more_count"] = max(0, len(breakdown) - 3)


@app.get("/health")
def health(_: str = Depends(require_staff)) -> dict[str, str]:
    try:
        fetch_one("SELECT 1 AS ok")
    except (ConfigurationError, pymysql.MySQLError) as exc:  # type: ignore[name-defined]
        raise HTTPException(status_code=503, detail=str(exc)) from exc
    return {"status": "ok"}


@app.get("/", response_class=HTMLResponse)
@app.get("/items", response_class=HTMLResponse)
def dashboard(
    request: Request,
    source: str = Query(default=""),
    actor: str = Query(default=""),
    _: str = Depends(require_staff),
) -> HTMLResponse:
    source_value = optional_query_int(source, "source", 1)
    actor_value = optional_query_int(actor, "actor", 0)
    source = source_value
    actor = actor_value
    item_where = ["qi.is_quarantined = 1"]
    item_params: list[Any] = []
    if source is not None:
        item_where.append("q.recovery_source_session_id = %s")
        item_params.append(source)
    if actor is not None:
        item_where.append("qi.last_actor_guid = %s")
        item_params.append(actor)
    item_where_sql = " AND ".join(item_where)
    item_types = fetch_all(
        f"""
        SELECT qi.item_id,
               MAX(qi.item_name) AS item_name,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COUNT(*) AS stack_count,
               COUNT(DISTINCT qi.quarantine_id) AS tile_count,
               COALESCE(SUM(CASE WHEN qi.parent_source_item_index >= 0 THEN 1 ELSE 0 END), 0)
                   AS stacks_in_containers,
               COUNT(DISTINCT q.recovery_source_session_id) AS source_count,
               COUNT(DISTINCT NULLIF(qi.last_actor_guid, 0)) AS actor_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS high_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS}
                                      AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS review_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS stable_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) IS NULL
                                 THEN 1 ELSE 0 END), 0) AS unknown_time_count,
               MIN(q.source_snapshot_updated_at) AS oldest_update,
               MAX(q.source_snapshot_updated_at) AS newest_update
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
         WHERE {item_where_sql}
         GROUP BY qi.item_id
         ORDER BY high_attention_count DESC, review_attention_count DESC,
                  total_units DESC, item_name, qi.item_id
        """,
        tuple(item_params),
    )
    attach_actor_breakdowns(item_types, source, actor)
    actors = fetch_all(
        """
        SELECT qi.last_actor_guid,
               COALESCE(MAX(p.name), CONCAT('Personagem #', qi.last_actor_guid)) AS actor_name,
               COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units
          FROM floor_persistence_quarantine_items qi
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE qi.is_quarantined = 1 AND qi.last_actor_guid > 0
         GROUP BY qi.last_actor_guid
         ORDER BY actor_name
        """
    )
    selected_actor_name = None
    if actor is not None:
        selected_actor_name = (
            "Sem atribuição"
            if actor == 0
            else next(
                (
                    row["actor_name"]
                    for row in actors
                    if int(row["last_actor_guid"]) == actor
                ),
                f"Personagem #{actor}",
            )
        )
    sessions = fetch_all(
        """
        SELECT recovery_source_session_id,
               COUNT(*) AS quarantine_rows,
               COALESCE(SUM(quarantine_item_count), 0) AS stackable_items,
               COALESCE(SUM(player_match_item_count), 0) AS player_matches,
               COALESCE(SUM(CASE WHEN active = 1 AND state = 'PENDING' THEN 1 ELSE 0 END), 0)
                   AS pending_rows,
               MIN(created_at) AS first_created_at,
               MAX(updated_at) AS last_updated_at
          FROM floor_persistence_quarantine
         GROUP BY recovery_source_session_id
         ORDER BY recovery_source_session_id DESC
        """
    )
    totals = fetch_one(
        """
        SELECT COUNT(*) AS quarantine_rows,
               COALESCE(SUM(quarantine_item_count), 0) AS stackable_items,
               COALESCE(SUM(player_match_item_count), 0) AS player_matches,
               COALESCE(SUM(CASE WHEN active = 1 AND state = 'PENDING' THEN 1 ELSE 0 END), 0)
                   AS pending_rows
          FROM floor_persistence_quarantine
        """
    ) or {}
    return templates.TemplateResponse(
        request=request,
        name="dashboard.html",
        context={
            "item_types": item_types,
            "sessions": sessions,
            "totals": totals,
            "source": source,
            "actor": actor,
            "actors": actors,
            "selected_actor_name": selected_actor_name,
        },
    )


@app.get("/players", response_class=HTMLResponse)
def player_overview(
    request: Request,
    source: str = Query(default=""),
    risk: str = Query(default=""),
    query: str = Query(default="", max_length=100),
    _: str = Depends(require_staff),
) -> HTMLResponse:
    source_value = optional_query_int(source, "source", 1)
    source = source_value
    where = ["qi.is_quarantined = 1"]
    params: list[Any] = []
    if source is not None:
        where.append("q.recovery_source_session_id = %s")
        params.append(source)
    condition = risk_condition(risk)
    if condition:
        where.append(condition)
    if query:
        term = f"%{query}%"
        where.append(
            """(
                p.name LIKE %s OR CAST(qi.last_actor_guid AS CHAR) LIKE %s
                OR qi.item_name LIKE %s OR CAST(qi.item_id AS CHAR) LIKE %s
            )"""
        )
        params.extend([term, term, term, term])
    where_sql = " AND ".join(where)

    players = fetch_all(
        f"""
        SELECT qi.last_actor_guid, MAX(p.name) AS player_name,
               COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COUNT(DISTINCT qi.item_id) AS item_type_count,
               COUNT(DISTINCT qi.quarantine_id) AS tile_count,
               COUNT(DISTINCT q.recovery_source_session_id) AS source_count,
               COALESCE(SUM(CASE WHEN qi.parent_source_item_index >= 0
                                 THEN 1 ELSE 0 END), 0) AS stacks_in_containers,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS high_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS}
                                      AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS review_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS stable_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) IS NULL
                                 THEN 1 ELSE 0 END), 0) AS unknown_time_count,
               MAX(q.source_snapshot_updated_at) AS newest_update
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE {where_sql}
         GROUP BY qi.last_actor_guid
         ORDER BY (qi.last_actor_guid = 0), high_attention_count DESC,
                  review_attention_count DESC, total_units DESC, player_name
        """,
        tuple(params),
    )
    for row in players:
        actor_guid = int(row["last_actor_guid"])
        row["actor_name"] = actor_display_name(actor_guid, row.get("player_name"))
        row["is_unassigned"] = actor_guid == 0

    coverage = fetch_one(
        f"""
        SELECT COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COALESCE(SUM(qi.last_actor_guid > 0), 0) AS attributed_stacks,
               COALESCE(SUM(qi.last_actor_guid = 0), 0) AS unassigned_stacks,
               COUNT(DISTINCT NULLIF(qi.last_actor_guid, 0)) AS player_count
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE {where_sql}
        """,
        tuple(params),
    ) or {}
    stack_count = int(coverage.get("stack_count", 0) or 0)
    attributed = int(coverage.get("attributed_stacks", 0) or 0)
    coverage["coverage_percent"] = round((attributed / stack_count) * 100) if stack_count else 0

    return templates.TemplateResponse(
        request=request,
        name="player_overview.html",
        context={
            "players": players,
            "coverage": coverage,
            "source": source,
            "risk": risk,
            "query": query,
        },
    )


@app.get("/players/{actor_guid}", response_class=HTMLResponse)
def player_detail(
    actor_guid: int,
    request: Request,
    source: str = Query(default=""),
    risk: str = Query(default=""),
    query: str = Query(default="", max_length=100),
    page: int = Query(default=1, ge=1),
    _: str = Depends(require_staff),
) -> HTMLResponse:
    if actor_guid < 0:
        raise HTTPException(status_code=404, detail="Player attribution not found.")
    source_value = optional_query_int(source, "source", 1)
    source = source_value

    player_name: str | None = None
    if actor_guid > 0:
        player = fetch_one("SELECT name FROM players WHERE id = %s", (actor_guid,))
        player_name = str(player["name"]) if player else None
    actor_name = actor_display_name(actor_guid, player_name)

    where = ["qi.is_quarantined = 1", "qi.last_actor_guid = %s"]
    params: list[Any] = [actor_guid]
    if source is not None:
        where.append("q.recovery_source_session_id = %s")
        params.append(source)
    if query:
        term = f"%{query}%"
        where.append("(qi.item_name LIKE %s OR CAST(qi.item_id AS CHAR) LIKE %s)")
        params.extend([term, term])
    risk_base_where_sql = " AND ".join(where)
    risk_base_params = tuple(params)
    condition = risk_condition(risk)
    if condition:
        where.append(condition)
    where_sql = " AND ".join(where)

    summary = fetch_one(
        f"""
        SELECT COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COUNT(DISTINCT qi.item_id) AS item_type_count,
               COUNT(DISTINCT qi.quarantine_id) AS tile_count,
               COUNT(DISTINCT q.recovery_source_session_id) AS source_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS high_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS}
                                      AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS review_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS stable_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) IS NULL
                                 THEN 1 ELSE 0 END), 0) AS unknown_time_count
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
         WHERE {where_sql}
        """,
        tuple(params),
    ) or {}
    risk_totals = fetch_one(
        f"""
        SELECT COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS high_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS}
                                      AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS review_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS stable_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) IS NULL
                                 THEN 1 ELSE 0 END), 0) AS unknown_time_count
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
         WHERE {risk_base_where_sql}
        """,
        risk_base_params,
    ) or {}
    total = int(summary.get("stack_count", 0) or 0)
    if total == 0 and not query and not risk and source is None:
        raise HTTPException(status_code=404, detail="Player attribution not found.")

    item_types = fetch_all(
        f"""
        SELECT qi.item_id, MAX(qi.item_name) AS item_name,
               COUNT(*) AS stack_count,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COUNT(DISTINCT qi.quarantine_id) AS tile_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS high_attention_count,
               COALESCE(SUM(CASE WHEN ({STABILITY_SECONDS_SQL}) > {HIGH_ATTENTION_SECONDS}
                                      AND ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS}
                                 THEN 1 ELSE 0 END), 0) AS review_attention_count
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
         WHERE {where_sql}
         GROUP BY qi.item_id
         ORDER BY high_attention_count DESC, review_attention_count DESC,
                  total_units DESC, item_name
        """,
        tuple(params),
    )

    pages = max(1, math.ceil(total / ITEM_PAGE_SIZE))
    page = min(page, pages)
    offset = (page - 1) * ITEM_PAGE_SIZE
    occurrences = fetch_all(
        f"""
        SELECT qi.*, q.recovery_source_session_id, q.tile_x, q.tile_y, q.tile_z,
               q.state, q.source_snapshot_updated_at, q.source_checkpoint_group_id,
               cp.last_checkpoint_at, ({STABILITY_SECONDS_SQL}) AS stable_seconds
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
         WHERE {where_sql}
         ORDER BY CASE
                    WHEN ({STABILITY_SECONDS_SQL}) <= {HIGH_ATTENTION_SECONDS} THEN 0
                    WHEN ({STABILITY_SECONDS_SQL}) < {STABLE_ATTENTION_SECONDS} THEN 1
                    WHEN ({STABILITY_SECONDS_SQL}) >= {STABLE_ATTENTION_SECONDS} THEN 2
                    ELSE 3
                  END,
                  q.recovery_source_session_id DESC, q.tile_z, q.tile_y, q.tile_x,
                  qi.source_item_index
         LIMIT %s OFFSET %s
        """,
        tuple(params + [ITEM_PAGE_SIZE, offset]),
    )
    for row in occurrences:
        annotate_attention(row)
    quarantine_ids = sorted({int(row["quarantine_id"]) for row in occurrences})
    if quarantine_ids:
        placeholders = ",".join(["%s"] * len(quarantine_ids))
        manifest_items = fetch_all(
            f"""
            SELECT quarantine_id, source_item_index, parent_source_item_index,
                   item_name, item_count, is_container
              FROM floor_persistence_quarantine_items
             WHERE quarantine_id IN ({placeholders})
             ORDER BY quarantine_id, source_item_index
            """,
            tuple(quarantine_ids),
        )
        occurrences = decorate_item_occurrences(occurrences, manifest_items)

    return templates.TemplateResponse(
        request=request,
        name="player_detail.html",
        context={
            "actor_guid": actor_guid,
            "actor_name": actor_name,
            "summary": summary,
            "risk_totals": risk_totals,
            "item_types": item_types,
            "occurrences": occurrences,
            "source": source,
            "risk": risk,
            "query": query,
            "page": page,
            "pages": pages,
        },
    )


@app.get("/items/{item_id}", response_class=HTMLResponse)
def item_detail(
    item_id: int,
    request: Request,
    source: str = Query(default=""),
    actor: str = Query(default=""),
    page: int = Query(default=1, ge=1),
    _: str = Depends(require_staff),
) -> HTMLResponse:
    source_value = optional_query_int(source, "source", 1)
    actor_value = optional_query_int(actor, "actor", 0)
    source = source_value
    actor = actor_value
    where = ["qi.is_quarantined = 1", "qi.item_id = %s"]
    params: list[Any] = [item_id]
    if source is not None:
        where.append("q.recovery_source_session_id = %s")
        params.append(source)
    if actor is not None:
        where.append("qi.last_actor_guid = %s")
        params.append(actor)
    where_sql = " AND ".join(where)
    summary = fetch_one(
        f"""
        SELECT MAX(qi.item_name) AS item_name,
               COALESCE(SUM(qi.item_count), 0) AS total_units,
               COUNT(*) AS stack_count,
               COUNT(DISTINCT qi.quarantine_id) AS tile_count,
               COUNT(DISTINCT q.recovery_source_session_id) AS source_count,
               COUNT(DISTINCT NULLIF(qi.last_actor_guid, 0)) AS actor_count
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
         WHERE {where_sql}
        """,
        tuple(params),
    ) or {}
    total = int(summary.get("stack_count", 0))
    if total == 0:
        raise HTTPException(status_code=404, detail="Quarantined item type not found.")
    pages = max(1, math.ceil(total / ITEM_PAGE_SIZE))
    page = min(page, pages)
    offset = (page - 1) * ITEM_PAGE_SIZE
    occurrences = fetch_all(
        f"""
        SELECT qi.*, q.recovery_source_session_id, q.tile_x, q.tile_y, q.tile_z,
               q.state, q.source_snapshot_updated_at, q.source_checkpoint_group_id,
               cp.last_checkpoint_at, ({STABILITY_SECONDS_SQL}) AS stable_seconds,
               COALESCE(p.name, CASE WHEN qi.last_actor_guid > 0
                    THEN CONCAT('Personagem #', qi.last_actor_guid)
                    ELSE 'Sem atribuição' END) AS last_actor_name
          FROM floor_persistence_quarantine_items qi
          JOIN floor_persistence_quarantine q ON q.id = qi.quarantine_id
          {CHECKPOINT_JOIN}
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE {where_sql}
         ORDER BY q.recovery_source_session_id DESC, q.tile_z, q.tile_y, q.tile_x,
                  qi.source_item_index
         LIMIT %s OFFSET %s
        """,
        tuple(params + [ITEM_PAGE_SIZE, offset]),
    )
    for row in occurrences:
        annotate_attention(row)
    quarantine_ids = sorted({int(row["quarantine_id"]) for row in occurrences})
    placeholders = ",".join(["%s"] * len(quarantine_ids))
    manifest_items = fetch_all(
        f"""
        SELECT quarantine_id, source_item_index, parent_source_item_index,
               item_name, item_count, is_container
          FROM floor_persistence_quarantine_items
         WHERE quarantine_id IN ({placeholders})
         ORDER BY quarantine_id, source_item_index
        """,
        tuple(quarantine_ids),
    )
    occurrences = decorate_item_occurrences(occurrences, manifest_items)
    return templates.TemplateResponse(
        request=request,
        name="item_detail.html",
        context={
            "item_id": item_id,
            "summary": summary,
            "occurrences": occurrences,
            "source": source,
            "actor": actor,
            "page": page,
            "pages": pages,
        },
    )


@app.get("/quarantine", response_class=HTMLResponse)
def quarantine_list(
    request: Request,
    source: str = Query(default=""),
    actor: str = Query(default=""),
    state_filter: str = Query(default=""),
    query: str = Query(default="", max_length=100),
    page: int = Query(default=1, ge=1),
    _: str = Depends(require_staff),
) -> HTMLResponse:
    source_value = optional_query_int(source, "source", 1)
    actor_value = optional_query_int(actor, "actor", 0)
    source = source_value
    actor = actor_value
    where = ["1=1"]
    params: list[Any] = []
    if source is not None:
        where.append("q.recovery_source_session_id = %s")
        params.append(source)
    if actor is not None:
        where.append(
            """EXISTS (
                SELECT 1 FROM floor_persistence_quarantine_items actor_item
                 WHERE actor_item.quarantine_id = q.id
                   AND actor_item.is_quarantined = 1
                   AND actor_item.last_actor_guid = %s
            )"""
        )
        params.append(actor)
    if state_filter:
        where.append("q.state = %s")
        params.append(state_filter)
    if query:
        term = f"%{query}%"
        where.append(
            """(
                CAST(q.tile_x AS CHAR) LIKE %s OR CAST(q.tile_y AS CHAR) LIKE %s
                OR EXISTS (
                    SELECT 1 FROM floor_persistence_quarantine_items qi
                    LEFT JOIN players search_player ON search_player.id = qi.last_actor_guid
                     WHERE qi.quarantine_id = q.id
                       AND (qi.item_name LIKE %s OR CAST(qi.item_id AS CHAR) LIKE %s
                            OR qi.instance_id LIKE %s OR search_player.name LIKE %s)
                )
            )"""
        )
        params.extend([term, term, term, term, term, term])

    where_sql = " AND ".join(where)
    count = fetch_one(
        f"SELECT COUNT(*) AS total FROM floor_persistence_quarantine q WHERE {where_sql}",
        tuple(params),
    )
    total = int((count or {}).get("total", 0))
    pages = max(1, math.ceil(total / PAGE_SIZE))
    page = min(page, pages)
    offset = (page - 1) * PAGE_SIZE
    rows = fetch_all(
        f"""
        SELECT q.*,
               cp.last_checkpoint_at,
               ({STABILITY_SECONDS_SQL}) AS stable_seconds,
               (SELECT COUNT(*) FROM floor_persistence_quarantine_items qi
                 WHERE qi.quarantine_id = q.id) AS manifest_items,
               (SELECT COUNT(*) FROM floor_persistence_quarantine_items qi
                 WHERE qi.quarantine_id = q.id AND qi.is_quarantined = 1)
                   AS manifest_quarantined,
               (SELECT GROUP_CONCAT(DISTINCT COALESCE(p.name, CONCAT('#', qi.last_actor_guid))
                                    ORDER BY COALESCE(p.name, CONCAT('#', qi.last_actor_guid))
                                    SEPARATOR ', ')
                  FROM floor_persistence_quarantine_items qi
                  LEFT JOIN players p ON p.id = qi.last_actor_guid
                 WHERE qi.quarantine_id = q.id AND qi.is_quarantined = 1
                   AND qi.last_actor_guid > 0) AS actor_names
          FROM floor_persistence_quarantine q
          {CHECKPOINT_JOIN}
         WHERE {where_sql}
         ORDER BY q.recovery_source_session_id DESC, q.tile_z, q.tile_y, q.tile_x
         LIMIT %s OFFSET %s
        """,
        tuple(params + [PAGE_SIZE, offset]),
    )
    for row in rows:
        row["reason_labels"] = reason_labels(int(row["reason_mask"]))
        annotate_attention(row)
    actors = fetch_all(
        """
        SELECT qi.last_actor_guid,
               COALESCE(MAX(p.name), CONCAT('Personagem #', qi.last_actor_guid)) AS actor_name
          FROM floor_persistence_quarantine_items qi
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE qi.is_quarantined = 1 AND qi.last_actor_guid > 0
         GROUP BY qi.last_actor_guid
         ORDER BY actor_name
        """
    )
    return templates.TemplateResponse(
        request=request,
        name="quarantine_list.html",
        context={
            "rows": rows,
            "source": source,
            "actor": actor,
            "actors": actors,
            "state_filter": state_filter,
            "query": query,
            "page": page,
            "pages": pages,
            "total": total,
        },
    )


@app.get("/quarantine/{quarantine_id}", response_class=HTMLResponse)
def quarantine_detail(
    quarantine_id: int, request: Request, _: str = Depends(require_staff)
) -> HTMLResponse:
    quarantine = fetch_one(
        f"""
        SELECT q.*, cp.last_checkpoint_at,
               ({STABILITY_SECONDS_SQL}) AS stable_seconds
          FROM floor_persistence_quarantine q
          {CHECKPOINT_JOIN}
         WHERE q.id = %s
        """,
        (quarantine_id,),
    )
    if quarantine is None:
        raise HTTPException(status_code=404, detail="Quarantine record not found.")
    items = fetch_all(
        """
        SELECT qi.*,
               COALESCE(p.name, CASE WHEN qi.last_actor_guid > 0
                    THEN CONCAT('Personagem #', qi.last_actor_guid)
                    ELSE 'Sem atribuição' END) AS last_actor_name
          FROM floor_persistence_quarantine_items qi
          LEFT JOIN players p ON p.id = qi.last_actor_guid
         WHERE qi.quarantine_id = %s
         ORDER BY qi.source_item_index
        """,
        (quarantine_id,),
    )
    for item in items:
        item["reason_labels"] = reason_labels(int(item["reason_mask"]))
    quarantine["reason_labels"] = reason_labels(int(quarantine["reason_mask"]))
    annotate_attention(quarantine)
    return templates.TemplateResponse(
        request=request,
        name="quarantine_detail.html",
        context={
            "quarantine": quarantine,
            "items": items,
            "tree": build_tree(items),
        },
    )

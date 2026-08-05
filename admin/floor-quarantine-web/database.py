from __future__ import annotations

import os
from contextlib import contextmanager
from typing import Any, Iterator

import pymysql
from pymysql.cursors import DictCursor


class ConfigurationError(RuntimeError):
    pass


def _required(name: str) -> str:
    value = os.getenv(name, "").strip()
    if not value:
        raise ConfigurationError(f"Missing required environment variable: {name}")
    return value


def connection_settings() -> dict[str, Any]:
    return {
        "host": _required("FQ_DB_HOST"),
        "port": int(os.getenv("FQ_DB_PORT", "3306")),
        "user": _required("FQ_DB_USER"),
        "password": _required("FQ_DB_PASSWORD"),
        "database": _required("FQ_DB_NAME"),
        "charset": "utf8mb4",
        "cursorclass": DictCursor,
        "autocommit": True,
        "connect_timeout": 5,
        "read_timeout": 15,
        "write_timeout": 15,
    }


@contextmanager
def connection() -> Iterator[pymysql.Connection]:
    db = pymysql.connect(**connection_settings())
    try:
        yield db
    finally:
        db.close()


def fetch_all(sql: str, params: tuple[Any, ...] = ()) -> list[dict[str, Any]]:
    with connection() as db, db.cursor() as cursor:
        cursor.execute(sql, params)
        return list(cursor.fetchall())


def fetch_one(sql: str, params: tuple[Any, ...] = ()) -> dict[str, Any] | None:
    with connection() as db, db.cursor() as cursor:
        cursor.execute(sql, params)
        return cursor.fetchone()

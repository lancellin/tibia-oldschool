from __future__ import annotations

import os

import uvicorn
from dotenv import load_dotenv


if __name__ == "__main__":
    load_dotenv()
    uvicorn.run(
        "app:app",
        host=os.getenv("FQ_BIND_HOST", "127.0.0.1"),
        port=int(os.getenv("FQ_BIND_PORT", "8787")),
        access_log=True,
    )

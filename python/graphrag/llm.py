from __future__ import annotations

import os
from pathlib import Path
from typing import Any

_ENV_FILE = Path(__file__).resolve().parents[1] / ".env"


def _load_env_file() -> None:
    if not _ENV_FILE.is_file():
        return
    for line in _ENV_FILE.read_text(encoding="utf-8").splitlines():
        key, separator, value = line.strip().partition("=")
        if separator and key and not key.startswith("#"):
            os.environ.setdefault(key.strip(), value.strip().strip('"').strip("'"))


def settings() -> dict[str, str]:
    _load_env_file()
    return {
        "base_url": os.environ.get("NETMIND_LLM_BASE_URL", "").strip(),
        "api_key": os.environ.get("NETMIND_LLM_API_KEY", "").strip(),
        "model": os.environ.get("NETMIND_LLM_MODEL", "").strip(),
        "timeout": os.environ.get("NETMIND_LLM_TIMEOUT_S", "30").strip(),
    }


def is_configured() -> bool:
    config = settings()
    return bool(config["base_url"] and config["api_key"] and config["model"])


def describe() -> dict[str, Any]:
    config = settings()
    host = config["base_url"].split("//")[-1].split("/")[0]
    return {
        "engine": "agent" if is_configured() else "rules",
        "model": config["model"] or None,
        "provider": host or None,
    }


def chat_model():
    if not is_configured():
        return None
    from langchain_openai import ChatOpenAI

    config = settings()
    return ChatOpenAI(model=config["model"], base_url=config["base_url"], api_key=config["api_key"],
                      temperature=0, timeout=float(config["timeout"] or 30), max_retries=1)

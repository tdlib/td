import asyncio
import ctypes
import json
import logging
from datetime import UTC, datetime
from pathlib import Path

import httpx

from .config import get_settings
from .observability import init_sentry

log = logging.getLogger("open-tgate-worker")
settings = get_settings()


def load_tdlib() -> ctypes.CDLL:
    library = Path(settings.tdlib_library_path)
    if not library.is_file():
        raise RuntimeError(f"TDLib library not found: {library}")
    return ctypes.CDLL(str(library))


async def publish_heartbeat() -> None:
    if not settings.supabase_url or not settings.supabase_secret_key:
        raise RuntimeError("Supabase server configuration is missing")
    endpoint = f"{settings.supabase_url.rstrip('/')}/rest/v1/open_tgate_worker_heartbeats"
    payload = {
        "worker_id": settings.worker_id,
        "service": "tdlib-sync",
        "status": "running",
        "last_seen_at": datetime.now(UTC).isoformat(),
        "metadata": {"send_enabled": settings.external_send_enabled},
    }
    headers = {
        "apikey": settings.supabase_secret_key,
        "authorization": f"Bearer {settings.supabase_secret_key}",
        "content-type": "application/json",
        "prefer": "resolution=merge-duplicates",
    }
    async with httpx.AsyncClient(timeout=15) as client:
        response = await client.post(endpoint, headers=headers, content=json.dumps(payload))
        response.raise_for_status()


async def main() -> None:
    logging.basicConfig(level=settings.log_level)
    init_sentry(settings, component="worker")
    Path(settings.tdlib_database_directory).mkdir(parents=True, exist_ok=True)
    Path(settings.tdlib_files_directory).mkdir(parents=True, exist_ok=True)
    load_tdlib()
    log.info("TDLib loaded; outbound sending=%s", settings.external_send_enabled)
    while True:
        try:
            await publish_heartbeat()
        except Exception:
            log.exception("Heartbeat failed")
        await asyncio.sleep(settings.heartbeat_interval_seconds)


if __name__ == "__main__":
    asyncio.run(main())


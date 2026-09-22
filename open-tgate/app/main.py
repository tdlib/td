from pathlib import Path

from fastapi import Depends, FastAPI
from fastapi.middleware.cors import CORSMiddleware

from .api_accounts import router as telegram_router
from .config import get_settings
from .observability import init_sentry
from .security import require_admin

settings = get_settings()
init_sentry(settings, component="api")
app = FastAPI(title="Open-TGate API", version="0.1.0", docs_url=None if settings.app_env == "production" else "/docs")
app.add_middleware(
    CORSMiddleware,
    allow_origins=[settings.dashboard_origin],
    allow_credentials=True,
    allow_methods=["GET", "POST"],
    allow_headers=["Authorization", "Content-Type"],
)

app.include_router(telegram_router)


@app.get("/healthz")
def health() -> dict[str, str]:
    return {"status": "ok", "service": "open-tgate-api"}


@app.get("/readyz")
def readiness() -> dict[str, object]:
    state_exists = Path(settings.tdlib_database_directory).parent.exists()
    return {
        "ready": settings.production_ready,
        "environment": settings.app_env,
        "tdlib_state_mount": state_exists,
        "external_send_enabled": settings.external_send_enabled,
    }


@app.get("/api/v1/system", dependencies=[Depends(require_admin)])
def system_status() -> dict[str, object]:
    return {
        "worker_id": settings.worker_id,
        "configured": settings.production_ready,
        "external_send_enabled": settings.external_send_enabled,
        "safety": "human approval required; inbound content is untrusted",
    }


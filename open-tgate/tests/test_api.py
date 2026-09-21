from fastapi.testclient import TestClient

from app.config import Settings
from app.main import app
from app.observability import init_sentry


client = TestClient(app)


def test_health() -> None:
    response = client.get("/healthz")
    assert response.status_code == 200
    assert response.json()["status"] == "ok"


def test_system_requires_admin_token() -> None:
    assert client.get("/api/v1/system").status_code == 401


def test_sending_is_disabled_by_default() -> None:
    response = client.get("/readyz")
    assert response.status_code == 200
    assert response.json()["external_send_enabled"] is False


def test_quoted_false_is_normalized() -> None:
    assert Settings(EXTERNAL_SEND_ENABLED='"false').external_send_enabled is False
    assert Settings(EXTERNAL_SEND_ENABLED="'false'").external_send_enabled is False


def test_sentry_is_disabled_without_dsn() -> None:
    settings = Settings(sentry_dsn="")
    assert settings.sentry_enabled is False
    assert init_sentry(settings, component="api") is False


def test_sentry_placeholder_dsn_is_not_enabled() -> None:
    assert Settings(sentry_dsn="REPLACE_WITH_SENTRY_DSN").sentry_enabled is False


def test_sentry_enabled_with_real_dsn() -> None:
    assert Settings(sentry_dsn="https://key@o0.ingest.sentry.io/1").sentry_enabled is True


def test_placeholders_are_not_production_ready() -> None:
    settings = Settings(
        API_ADMIN_TOKEN="REPLACE_WITH_API_ADMIN_TOKEN",
        SUPABASE_URL="https://example.supabase.co",
        SUPABASE_SECRET_KEY="REPLACE_WITH_SUPABASE_SECRET_KEY",
        TELEGRAM_API_ID=12345,
        TELEGRAM_API_HASH="REPLACE_WITH_TELEGRAM_API_HASH",
    )
    assert settings.production_ready is False

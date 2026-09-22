from functools import lru_cache

from pydantic import Field, field_validator
from pydantic_settings import BaseSettings, SettingsConfigDict


def _is_configured(value: object) -> bool:
    if value is None:
        return False
    normalized = str(value).strip().strip("\"'")
    return bool(normalized) and not normalized.upper().startswith("REPLACE_WITH_")


class Settings(BaseSettings):
    model_config = SettingsConfigDict(env_file=".env", extra="ignore")

    app_env: str = "development"
    log_level: str = "INFO"
    port: int = 8000
    api_admin_token: str = Field(default="", repr=False)
    dashboard_origin: str = "http://localhost:3000"
    supabase_url: str = ""
    supabase_secret_key: str = Field(default="", repr=False)
    telegram_api_id: int | None = None
    telegram_api_hash: str = Field(default="", repr=False)
    tdlib_library_path: str = "/usr/local/lib/libtdjson.so"
    tdlib_database_directory: str = "/data/tdlib/db"
    tdlib_files_directory: str = "/data/tdlib/files"
    worker_id: str = "open-tgate-worker-1"
    heartbeat_interval_seconds: int = 30
    external_send_enabled: bool = False

    # Telegram account runtime (multi-account login + read-only auto-sync).
    command_poll_seconds: int = 3
    sync_pacing_seconds: float = 0.4
    max_login_accounts: int = 25

    # Observability (Sentry). Disabled unless a DSN is configured.
    sentry_dsn: str = Field(default="", repr=False)
    sentry_environment: str = ""
    sentry_release: str = ""
    sentry_traces_sample_rate: float = 0.0

    @field_validator("external_send_enabled", mode="before")
    @classmethod
    def normalize_external_send_enabled(cls, value: object) -> object:
        if isinstance(value, str):
            return value.strip().strip("\"'")
        return value

    @property
    def sentry_enabled(self) -> bool:
        return _is_configured(self.sentry_dsn)

    @property
    def telegram_enabled(self) -> bool:
        """True when the worker has enough config to drive TDLib account login.

        Requires Telegram API credentials plus Supabase (the command bus). When
        false the worker still runs its heartbeat loop but the account manager
        stays dormant — this keeps CI/local runs green without credentials.
        """

        return all(
            (
                _is_configured(self.telegram_api_id),
                _is_configured(self.telegram_api_hash),
                _is_configured(self.supabase_url),
                _is_configured(self.supabase_secret_key),
            )
        )

    @property
    def production_ready(self) -> bool:
        return all(
            (
                _is_configured(self.api_admin_token),
                _is_configured(self.supabase_url),
                _is_configured(self.supabase_secret_key),
                _is_configured(self.telegram_api_id),
                _is_configured(self.telegram_api_hash),
            )
        )


@lru_cache
def get_settings() -> Settings:
    return Settings()

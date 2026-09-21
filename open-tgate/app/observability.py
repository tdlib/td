"""Optional Sentry error/performance reporting.

Sentry is opt-in: when ``SENTRY_DSN`` is not configured the initializer is a
no-op, so local development and CI run without any external dependency and
without emitting events. PII is never sent by default.
"""

import logging

from .config import Settings

log = logging.getLogger("open-tgate.observability")


def init_sentry(settings: Settings, component: str) -> bool:
    """Initialize Sentry for ``component`` ("api" or "worker").

    Returns ``True`` when Sentry was initialized, ``False`` otherwise (DSN not
    set, or the SDK is not installed). Never raises: observability must not be
    able to crash the service it observes.
    """
    if not settings.sentry_enabled:
        return False
    try:
        import sentry_sdk
    except ImportError:
        log.warning("SENTRY_DSN is set but sentry-sdk is not installed; skipping Sentry init")
        return False

    try:
        sentry_sdk.init(
            dsn=settings.sentry_dsn,
            environment=settings.sentry_environment or settings.app_env,
            release=settings.sentry_release or None,
            traces_sample_rate=settings.sentry_traces_sample_rate,
            send_default_pii=False,
        )
        sentry_sdk.set_tag("component", component)
        sentry_sdk.set_tag("worker_id", settings.worker_id)
    except Exception:  # pragma: no cover - defensive; never break the service
        log.exception("Sentry initialization failed; continuing without it")
        return False

    log.info("Sentry initialized for component=%s environment=%s", component, settings.sentry_environment or settings.app_env)
    return True

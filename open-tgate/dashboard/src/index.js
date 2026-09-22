import { page } from "./site.js";
import { appHtml } from "./app.js";

// Base security headers shared by all responses.
const BASE_HEADERS = {
  "x-content-type-options": "nosniff",
  "referrer-policy": "no-referrer",
  "x-frame-options": "DENY",
};

// CSP for the public landing page: self + inline style/script only.
const CSP_LANDING = [
  "default-src 'self'",
  "base-uri 'self'",
  "img-src 'self' data:",
  "style-src 'self' 'unsafe-inline'",
  "script-src 'self' 'unsafe-inline'",
  "connect-src 'self'",
  "form-action 'none'",
  "frame-ancestors 'none'",
].join("; ");

// CSP for the operator console: also allows the Supabase JS SDK (jsDelivr) and
// XHR/fetch to the Supabase project origin for Auth + PostgREST.
function cspApp(supabaseUrl) {
  const origin = supabaseUrl ? new URL(supabaseUrl).origin : "";
  return [
    "default-src 'self'",
    "base-uri 'self'",
    "img-src 'self' data:",
    "style-src 'self' 'unsafe-inline'",
    "script-src 'self' 'unsafe-inline' https://cdn.jsdelivr.net",
    `connect-src 'self'${origin ? " " + origin : ""}`,
    "font-src 'self'",
    "form-action 'none'",
    "frame-ancestors 'none'",
  ].join("; ");
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Public health proxy to the Zeabur API.
    if (url.pathname === "/healthz") {
      const base = env.API_BASE_URL;
      if (!base) {
        return Response.json({ status: "unconfigured", detail: "API_BASE_URL is not set" }, { status: 503 });
      }
      try {
        const upstream = await fetch(`${base.replace(/\/$/, "")}/healthz`, { headers: { accept: "application/json" } });
        return new Response(upstream.body, {
          status: upstream.status,
          headers: { "content-type": "application/json; charset=utf-8" },
        });
      } catch {
        return Response.json({ status: "unavailable", detail: "upstream health check failed" }, { status: 502 });
      }
    }

    // Never expose data API paths from the edge dashboard.
    if (url.pathname.startsWith("/api/")) {
      return new Response("Direct API access is disabled", { status: 403 });
    }

    // Operator console (login-gated in the browser via Supabase Auth).
    if (url.pathname === "/app" || url.pathname.startsWith("/app/")) {
      const supabaseUrl = env.SUPABASE_URL || "";
      const supabaseKey = env.SUPABASE_PUBLISHABLE_KEY || "";
      const html = appHtml
        .replaceAll("%SUPABASE_URL%", supabaseUrl)
        .replaceAll("%SUPABASE_KEY%", supabaseKey);
      return new Response(html, {
        headers: {
          "content-type": "text/html; charset=utf-8",
          "cache-control": "no-store",
          "content-security-policy": cspApp(supabaseUrl),
          ...BASE_HEADERS,
        },
      });
    }

    // Public landing page.
    return new Response(page, {
      headers: {
        "content-type": "text/html; charset=utf-8",
        "cache-control": "public, max-age=300",
        "content-security-policy": CSP_LANDING,
        ...BASE_HEADERS,
      },
    });
  },
};

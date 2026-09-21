import { page } from "./site.js";

// Content-Security-Policy for the operations landing page.
// The page uses one inline <style> and one inline <script> (theme toggle +
// /healthz status probe) and loads no third-party resources, so we allow
// inline style/script from self only and restrict connect-src to self.
const CSP = [
  "default-src 'self'",
  "base-uri 'self'",
  "img-src 'self' data:",
  "style-src 'self' 'unsafe-inline'",
  "script-src 'self' 'unsafe-inline'",
  "connect-src 'self'",
  "form-action 'none'",
  "frame-ancestors 'none'",
].join("; ");

const SECURITY_HEADERS = {
  "content-security-policy": CSP,
  "x-content-type-options": "nosniff",
  "referrer-policy": "no-referrer",
  "x-frame-options": "DENY",
};

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Proxy only the public health endpoint through to the Zeabur API.
    if (url.pathname === "/healthz") {
      const base = env.API_BASE_URL;
      if (!base) {
        return Response.json(
          { status: "unconfigured", detail: "API_BASE_URL is not set" },
          { status: 503 },
        );
      }
      try {
        const upstream = await fetch(`${base.replace(/\/$/, "")}/healthz`, {
          headers: { accept: "application/json" },
        });
        return new Response(upstream.body, {
          status: upstream.status,
          headers: { "content-type": "application/json; charset=utf-8" },
        });
      } catch {
        return Response.json(
          { status: "unavailable", detail: "upstream health check failed" },
          { status: 502 },
        );
      }
    }

    // Never expose data API paths from the edge dashboard.
    if (url.pathname.startsWith("/api/")) {
      return new Response("Direct API access is disabled", { status: 403 });
    }

    // Everything else renders the landing page.
    return new Response(page, {
      headers: {
        "content-type": "text/html; charset=utf-8",
        "cache-control": "public, max-age=300",
        ...SECURITY_HEADERS,
      },
    });
  },
};

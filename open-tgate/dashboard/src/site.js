// Open-TGate marketing/operations landing page.
//
// Served by the Cloudflare Worker in src/index.js. Kept as a single exported
// string so the Worker stays a zero-build single bundle (no HTML loader rule).
// Styles and the tiny status/theme script are inline; the Worker sets a strict
// CSP that permits only self resources plus these inline blocks.

export const page = `<!doctype html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<meta name="color-scheme" content="dark light" />
<title>Open-TGate — Authorized Telegram Operations Backend</title>
<meta name="description" content="Open-TGate is an authorized, private Telegram operations backend built on TDLib: persistent session sync, Supabase as system of record, Sentry observability, and safety-first controls. Deployed on Zeabur behind Cloudflare." />
<style>
  :root{
    --bg:#080a11; --bg-soft:#0e1220; --card:#121727; --card-2:#161c2f;
    --line:#242c44; --line-soft:#1b2236;
    --fg:#eef2ff; --muted:#9aa6c6; --faint:#6c789c;
    --brand:#b69cff; --brand-2:#7c74ff; --accent:#5ad1ff;
    --ok:#5fe3a1; --warn:#ffd166; --bad:#ff7a8a;
    --radius:16px; --maxw:1120px;
    --shadow:0 20px 50px -24px rgba(0,0,0,.75);
    --grad:radial-gradient(120% 120% at 15% 0%, rgba(124,116,255,.22), transparent 55%),
           radial-gradient(90% 90% at 100% 0%, rgba(90,209,255,.14), transparent 50%);
  }
  html[data-theme="light"]{
    --bg:#f6f8ff; --bg-soft:#eef1fb; --card:#ffffff; --card-2:#f4f6ff;
    --line:#e2e7f5; --line-soft:#edf0fb;
    --fg:#101528; --muted:#4d5981; --faint:#7683a8;
    --brand:#6b4df6; --brand-2:#7c74ff; --accent:#0aa5da;
    --shadow:0 20px 50px -30px rgba(30,40,90,.35);
    --grad:radial-gradient(120% 120% at 15% 0%, rgba(124,116,255,.16), transparent 55%),
           radial-gradient(90% 90% at 100% 0%, rgba(10,165,218,.10), transparent 50%);
  }
  *{box-sizing:border-box}
  html,body{margin:0;padding:0}
  body{
    font:16px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,"Apple Color Emoji","Segoe UI Emoji",sans-serif;
    color:var(--fg); background:var(--bg); background-image:var(--grad);
    background-repeat:no-repeat; -webkit-font-smoothing:antialiased;
  }
  a{color:inherit}
  .wrap{max-width:var(--maxw);margin:0 auto;padding:0 20px}
  .btn{
    display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line);
    background:var(--card);color:var(--fg);text-decoration:none;font-weight:600;
    padding:11px 18px;border-radius:12px;transition:.18s ease;white-space:nowrap;
  }
  .btn:hover{transform:translateY(-1px);border-color:var(--brand)}
  .btn.primary{
    background:linear-gradient(135deg,var(--brand-2),var(--brand));border-color:transparent;color:#0a0c16;
  }
  html[data-theme="light"] .btn.primary{color:#fff}
  .btn.ghost{background:transparent}

  /* Nav */
  header.nav{position:sticky;top:0;z-index:20;backdrop-filter:blur(10px);
    background:color-mix(in srgb,var(--bg) 82%,transparent);border-bottom:1px solid var(--line-soft)}
  .nav-in{display:flex;align-items:center;gap:18px;height:64px}
  .brand{display:flex;align-items:center;gap:11px;font-weight:800;letter-spacing:.2px;text-decoration:none}
  .logo{width:30px;height:30px;border-radius:9px;
    background:linear-gradient(135deg,var(--brand-2),var(--accent));display:grid;place-items:center;
    color:#0a0c16;font-weight:900;box-shadow:0 6px 18px -6px var(--brand-2)}
  .nav-links{display:flex;gap:22px;margin-left:14px}
  .nav-links a{color:var(--muted);text-decoration:none;font-weight:600;font-size:14.5px}
  .nav-links a:hover{color:var(--fg)}
  .nav-right{margin-left:auto;display:flex;align-items:center;gap:10px}
  .icon-btn{border:1px solid var(--line);background:var(--card);color:var(--fg);width:40px;height:40px;
    border-radius:11px;cursor:pointer;font-size:16px;display:grid;place-items:center}
  .icon-btn:hover{border-color:var(--brand)}

  /* Hero */
  .hero{padding:76px 0 40px}
  .pill{display:inline-flex;align-items:center;gap:8px;font-size:13px;font-weight:600;color:var(--muted);
    border:1px solid var(--line);background:var(--card);padding:6px 12px;border-radius:999px}
  .dot{width:8px;height:8px;border-radius:50%;background:var(--faint)}
  .dot.ok{background:var(--ok);box-shadow:0 0 0 4px color-mix(in srgb,var(--ok) 22%,transparent)}
  .dot.warn{background:var(--warn);box-shadow:0 0 0 4px color-mix(in srgb,var(--warn) 22%,transparent)}
  h1.title{font-size:clamp(34px,6vw,60px);line-height:1.03;letter-spacing:-1.2px;margin:20px 0 0;font-weight:850}
  .title .grad{background:linear-gradient(120deg,var(--brand),var(--accent));-webkit-background-clip:text;background-clip:text;color:transparent}
  .lead{color:var(--muted);font-size:clamp(16px,2.2vw,20px);max-width:680px;margin:18px 0 0}
  .cta{display:flex;flex-wrap:wrap;gap:12px;margin-top:28px}
  .hero-meta{display:flex;flex-wrap:wrap;gap:22px;margin-top:34px;color:var(--faint);font-size:13.5px}
  .hero-meta b{color:var(--fg)}

  /* Sections */
  section{padding:52px 0;border-top:1px solid var(--line-soft)}
  .eyebrow{color:var(--brand);font-weight:700;letter-spacing:.14em;text-transform:uppercase;font-size:12.5px}
  h2{font-size:clamp(24px,3.4vw,34px);letter-spacing:-.6px;margin:10px 0 0;font-weight:800}
  .sub{color:var(--muted);max-width:640px;margin:12px 0 0}

  .grid{display:grid;gap:16px;margin-top:30px}
  .grid.c3{grid-template-columns:repeat(3,1fr)}
  .grid.c2{grid-template-columns:repeat(2,1fr)}
  .card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:22px;box-shadow:var(--shadow)}
  .card h3{margin:14px 0 6px;font-size:18px}
  .card p{color:var(--muted);margin:0;font-size:14.5px}
  .ic{width:42px;height:42px;border-radius:11px;display:grid;place-items:center;font-size:20px;
    background:color-mix(in srgb,var(--brand) 16%,transparent);border:1px solid var(--line)}

  /* Stack flow */
  .flow{display:flex;flex-wrap:wrap;align-items:stretch;gap:10px;margin-top:28px}
  .node{flex:1 1 150px;min-width:140px;background:var(--card);border:1px solid var(--line);border-radius:14px;padding:16px}
  .node .k{font-size:12px;color:var(--faint);text-transform:uppercase;letter-spacing:.1em}
  .node .v{font-weight:700;margin-top:4px}
  .node .d{color:var(--muted);font-size:13px;margin-top:6px}
  .arrow{align-self:center;color:var(--faint);font-size:20px}

  /* Security list */
  .checks{display:grid;grid-template-columns:repeat(2,1fr);gap:12px 26px;margin-top:26px}
  .chk{display:flex;gap:12px;align-items:flex-start}
  .chk .mk{color:var(--ok);font-weight:800;margin-top:1px}
  .chk b{display:block}
  .chk span{color:var(--muted);font-size:14px}

  /* Status panel */
  .status-panel{display:flex;flex-wrap:wrap;gap:16px;align-items:center;justify-content:space-between;
    background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:22px 24px;margin-top:26px;box-shadow:var(--shadow)}
  .status-left{display:flex;align-items:center;gap:14px}
  .status-txt b{font-size:17px}
  .status-txt span{display:block;color:var(--muted);font-size:13.5px}
  code.mono{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;background:var(--bg-soft);
    border:1px solid var(--line-soft);padding:3px 8px;border-radius:8px;font-size:13px;color:var(--accent)}

  footer{border-top:1px solid var(--line-soft);padding:34px 0 60px;color:var(--faint)}
  .foot-in{display:flex;flex-wrap:wrap;gap:16px;align-items:center;justify-content:space-between}
  .foot-links{display:flex;gap:20px;flex-wrap:wrap}
  .foot-links a{color:var(--muted);text-decoration:none;font-size:14px}
  .foot-links a:hover{color:var(--fg)}
  .note{color:var(--faint);font-size:12.5px;margin-top:10px;max-width:720px}

  @media (max-width:860px){
    .grid.c3{grid-template-columns:1fr 1fr}
    .nav-links{display:none}
    .checks{grid-template-columns:1fr}
  }
  @media (max-width:560px){
    .grid.c3,.grid.c2{grid-template-columns:1fr}
    .hero{padding:52px 0 30px}
  }
</style>
</head>
<body>
<header class="nav">
  <div class="wrap nav-in">
    <a class="brand" href="/"><span class="logo">t</span>Open-TGate</a>
    <nav class="nav-links">
      <a href="#overview">Overview</a>
      <a href="#features">Capabilities</a>
      <a href="#stack">Stack</a>
      <a href="#security">Security</a>
      <a href="#status">Status</a>
    </nav>
    <div class="nav-right">
      <button class="icon-btn" id="theme" aria-label="Toggle color theme" title="Toggle theme">◐</button>
      <a class="btn ghost" href="https://github.com/hillstreet-ph/open-tgate" rel="noopener">GitHub</a>
      <a class="btn primary" href="/app">Operator login</a>
    </div>
  </div>
</header>

<main>
  <!-- HERO -->
  <div class="wrap hero">
    <span class="pill"><span class="dot" id="hero-dot"></span><span id="hero-status">Checking runtime…</span></span>
    <h1 class="title">Authorized Telegram<br /><span class="grad">operations backend</span></h1>
    <p class="lead">
      Open-TGate is a private, safety-first backend built on <b>TDLib</b>. It keeps authorized
      Telegram accounts synchronized to a durable system of record — with persistent sessions,
      idempotent ingestion, and human-approval controls before anything is ever sent.
    </p>
    <div class="cta">
      <a class="btn primary" href="#stack">See the architecture</a>
      <a class="btn" href="#status">Live status</a>
      <a class="btn ghost" href="https://github.com/hillstreet-ph/open-tgate" rel="noopener">Source &amp; docs</a>
    </div>
    <div class="hero-meta">
      <div><b>TDLib</b> native sync engine</div>
      <div><b>Supabase</b> system of record</div>
      <div><b>Zeabur</b> always-on runtime</div>
      <div><b>Cloudflare</b> edge &amp; WAF</div>
    </div>
  </div>

  <!-- OVERVIEW -->
  <section id="overview">
    <div class="wrap">
      <div class="eyebrow">What it is</div>
      <h2>A durable bridge between Telegram and your data platform</h2>
      <p class="sub">
        A persistent worker completes an authorized Telegram session through TDLib and continuously
        normalizes accounts, chats and messages into Supabase. State survives redeploys, ingestion is
        idempotent, and outbound sending stays disabled until approval controls pass verification.
      </p>
      <div class="grid c3">
        <div class="card">
          <div class="ic">🔒</div>
          <h3>Private by design</h3>
          <p>No public API surface for data. The dashboard proxies only health; direct API paths are refused at the edge.</p>
        </div>
        <div class="card">
          <div class="ic">♻️</div>
          <h3>Restart-safe</h3>
          <p>TDLib session and database state live on a persistent volume, so a redeploy resumes instead of re-importing.</p>
        </div>
        <div class="card">
          <div class="ic">🧾</div>
          <h3>System of record</h3>
          <p>Normalized entities and an outbox event stream land in Supabase — the durable source downstream systems read.</p>
        </div>
      </div>
    </div>
  </section>

  <!-- FEATURES -->
  <section id="features">
    <div class="wrap">
      <div class="eyebrow">Capabilities</div>
      <h2>Built for reliable, authorized synchronization</h2>
      <p class="sub">Every capability is scoped to authorized operation and observable in production.</p>
      <div class="grid c3">
        <div class="card"><div class="ic">📡</div><h3>Persistent TDLib worker</h3><p>Loads native <code class="mono">libtdjson</code>, holds the session, and heartbeats liveness continuously.</p></div>
        <div class="card"><div class="ic">🗂️</div><h3>Entity sync</h3><p>Accounts and chats normalized to durable records; per-chat backfill with resumable checkpoints.</p></div>
        <div class="card"><div class="ic">⚡</div><h3>Idempotent ingestion</h3><p>Live updates UPSERT safely — no duplicates on retry, restart, or replay.</p></div>
        <div class="card"><div class="ic">📤</div><h3>Transactional outbox</h3><p>Downstream events emit only after the database transaction commits.</p></div>
        <div class="card"><div class="ic">🛰️</div><h3>Sentry observability</h3><p>DSN-gated error and performance reporting across api and worker; a no-op when unset.</p></div>
        <div class="card"><div class="ic">🩺</div><h3>Health &amp; readiness</h3><p><code class="mono">/healthz</code> and <code class="mono">/readyz</code> gate deploys and rollbacks with real signals.</p></div>
      </div>
    </div>
  </section>

  <!-- STACK -->
  <section id="stack">
    <div class="wrap">
      <div class="eyebrow">Delivery stack</div>
      <h2>Source to production, verified end to end</h2>
      <p class="sub">One canonical path — no drift, no untracked runtimes. Cloudflare fronts the public edge; Zeabur runs the always-on services.</p>
      <div class="flow">
        <div class="node"><div class="k">Source</div><div class="v">GitHub</div><div class="d">Branches, PRs, required checks</div></div>
        <div class="arrow">→</div>
        <div class="node"><div class="k">Registry</div><div class="v">Docker Hub</div><div class="d">Immutable, versioned images</div></div>
        <div class="arrow">→</div>
        <div class="node"><div class="k">Runtime</div><div class="v">Zeabur</div><div class="d">api + worker, volume, health checks</div></div>
        <div class="arrow">→</div>
        <div class="node"><div class="k">Data</div><div class="v">Supabase</div><div class="d">Postgres, RLS, migrations</div></div>
      </div>
      <div class="grid c2" style="margin-top:16px">
        <div class="card"><div class="ic">🌐</div><h3>Cloudflare edge</h3><p>DNS, TLS, proxy and WAF in front of the Zeabur API domain. This dashboard is a Cloudflare Worker that exposes only health.</p></div>
        <div class="card"><div class="ic">📦</div><h3>Immutable releases</h3><p>Semver tags publish to Docker Hub; production pins an exact tag or <code class="mono">@sha256</code> digest so rollback is one redeploy.</p></div>
      </div>
    </div>
  </section>

  <!-- SECURITY -->
  <section id="security">
    <div class="wrap">
      <div class="eyebrow">Safety posture</div>
      <h2>Least privilege, enforced by default</h2>
      <p class="sub">Safety is a property of the build, not an afterthought. These controls ship enabled.</p>
      <div class="checks">
        <div class="chk"><span class="mk">✓</span><div><b>Outbound sending disabled</b><span><code class="mono">EXTERNAL_SEND_ENABLED=false</code> until approval controls pass verification.</span></div></div>
        <div class="chk"><span class="mk">✓</span><div><b>Sessions never leave the volume</b><span>Telegram session material stays on the worker's encrypted volume — never in secrets or the database.</span></div></div>
        <div class="chk"><span class="mk">✓</span><div><b>Admin-scoped API</b><span>System endpoints require an admin token; the dashboard exposes no data paths.</span></div></div>
        <div class="chk"><span class="mk">✓</span><div><b>Untrusted inbound content</b><span>All inbound Telegram content is treated as untrusted throughout the pipeline.</span></div></div>
        <div class="chk"><span class="mk">✓</span><div><b>No secrets in source</b><span>Only variable names and safe placeholders are committed; real secrets live in platform stores.</span></div></div>
        <div class="chk"><span class="mk">✓</span><div><b>Strict edge CSP</b><span>The dashboard serves a locked-down Content-Security-Policy and refuses direct API access.</span></div></div>
      </div>
    </div>
  </section>

  <!-- STATUS -->
  <section id="status">
    <div class="wrap">
      <div class="eyebrow">Live</div>
      <h2>Runtime status</h2>
      <p class="sub">Checked from your browser against the API's public health endpoint.</p>
      <div class="status-panel">
        <div class="status-left">
          <span class="dot" id="s-dot" style="width:14px;height:14px"></span>
          <div class="status-txt">
            <b id="s-title">Checking API…</b>
            <span id="s-detail">Contacting <code class="mono">/healthz</code></span>
          </div>
        </div>
        <a class="btn" href="/healthz" target="_blank" rel="noopener">Open /healthz</a>
      </div>
      <p class="note">
        Outbound Telegram sending remains disabled until operator approval controls pass verification.
        This page performs no authenticated operations and stores no data.
      </p>
    </div>
  </section>
</main>

<footer>
  <div class="wrap foot-in">
    <div>
      <div class="brand" style="font-size:15px"><span class="logo" style="width:24px;height:24px;font-size:13px">t</span>Open-TGate</div>
      <div class="note" style="margin-top:8px">Authorized private Telegram backend · TDLib sync worker + API</div>
    </div>
    <div class="foot-links">
      <a href="https://github.com/hillstreet-ph/open-tgate" rel="noopener">GitHub</a>
      <a href="#stack">Architecture</a>
      <a href="#security">Security</a>
      <a href="/healthz">Health</a>
    </div>
  </div>
</footer>

<script>
(function(){
  var root=document.documentElement, key="otg-theme";
  try{var saved=localStorage.getItem(key); if(saved){root.setAttribute("data-theme",saved);}}catch(e){}
  var tbtn=document.getElementById("theme");
  if(tbtn){tbtn.addEventListener("click",function(){
    var next=root.getAttribute("data-theme")==="dark"?"light":"dark";
    root.setAttribute("data-theme",next);
    try{localStorage.setItem(key,next);}catch(e){}
  });}

  function set(sel,txt){var el=document.getElementById(sel); if(el){el.textContent=txt;}}
  function cls(sel,c){var el=document.getElementById(sel); if(el){el.className=c;}}

  fetch("/healthz",{headers:{accept:"application/json"}})
    .then(function(r){ if(!r.ok) throw new Error("bad"); return r.json(); })
    .then(function(d){
      var svc=(d&&d.service)?d.service:"open-tgate-api";
      cls("hero-dot","dot ok"); set("hero-status","Runtime reachable");
      cls("s-dot","dot ok"); set("s-title","API reachable");
      set("s-detail","Service: "+svc+" · status ok");
    })
    .catch(function(){
      cls("hero-dot","dot warn"); set("hero-status","Runtime unavailable");
      cls("s-dot","dot warn"); set("s-title","API unavailable");
      set("s-detail","Health check did not respond");
    });
})();
</script>
</body>
</html>`;

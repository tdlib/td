// Open-TGate operator console (/app).
//
// A real login-gated console served by the Cloudflare Worker. Auth is Supabase
// Auth (email magic link) using the project's PUBLISHABLE key (safe in the
// browser). After sign-in, access is gated by the `open_tgate_operators`
// allowlist and RLS: a signed-in user who is not an active operator can read
// nothing. Live worker status is read directly from Supabase (PostgREST),
// RLS-restricted to operators — so the console works without the API deployed.
//
// %SUPABASE_URL% and %SUPABASE_KEY% are replaced by the Worker from its vars.

export const appHtml = `<!doctype html>
<html lang="en" data-theme="dark">
<head>
<meta charset="utf-8" />
<meta name="viewport" content="width=device-width, initial-scale=1" />
<meta name="color-scheme" content="dark light" />
<title>Open-TGate — Operator Console</title>
<meta name="robots" content="noindex, nofollow" />
<style>
  :root{
    --bg:#080a11; --card:#121727; --card2:#161c2f; --line:#242c44; --line2:#1b2236;
    --fg:#eef2ff; --muted:#9aa6c6; --faint:#6c789c;
    --brand:#b69cff; --brand2:#7c74ff; --accent:#5ad1ff;
    --ok:#5fe3a1; --warn:#ffd166; --bad:#ff7a8a; --radius:16px;
    --shadow:0 20px 50px -24px rgba(0,0,0,.75);
  }
  html[data-theme="light"]{
    --bg:#f6f8ff; --card:#fff; --card2:#f4f6ff; --line:#e2e7f5; --line2:#edf0fb;
    --fg:#101528; --muted:#4d5981; --faint:#7683a8; --brand:#6b4df6; --accent:#0aa5da;
    --shadow:0 20px 50px -30px rgba(30,40,90,.35);
  }
  *{box-sizing:border-box}
  body{margin:0;font:16px/1.6 -apple-system,BlinkMacSystemFont,"Segoe UI",Roboto,Helvetica,Arial,sans-serif;
    color:var(--fg);background:var(--bg);
    background-image:radial-gradient(120% 120% at 15% 0%,rgba(124,116,255,.18),transparent 55%);
    background-repeat:no-repeat;-webkit-font-smoothing:antialiased}
  a{color:var(--accent)}
  .wrap{max-width:1020px;margin:0 auto;padding:0 20px}
  header.nav{position:sticky;top:0;z-index:10;backdrop-filter:blur(10px);
    background:color-mix(in srgb,var(--bg) 82%,transparent);border-bottom:1px solid var(--line2)}
  .nav-in{display:flex;align-items:center;gap:12px;height:60px}
  .brand{display:flex;align-items:center;gap:10px;font-weight:800;text-decoration:none;color:inherit}
  .logo{width:28px;height:28px;border-radius:8px;background:linear-gradient(135deg,var(--brand2),var(--accent));
    display:grid;place-items:center;color:#0a0c16;font-weight:900}
  .nav-right{margin-left:auto;display:flex;align-items:center;gap:10px}
  .btn{display:inline-flex;align-items:center;gap:8px;border:1px solid var(--line);background:var(--card);
    color:var(--fg);text-decoration:none;font-weight:600;padding:10px 16px;border-radius:11px;cursor:pointer;
    font-size:14.5px;transition:.15s ease}
  .btn:hover{border-color:var(--brand);transform:translateY(-1px)}
  .btn.primary{background:linear-gradient(135deg,var(--brand2),var(--brand));border-color:transparent;color:#0a0c16}
  html[data-theme="light"] .btn.primary{color:#fff}
  .btn[disabled]{opacity:.55;cursor:default;transform:none}
  main{padding:40px 0 80px}
  .card{background:var(--card);border:1px solid var(--line);border-radius:var(--radius);padding:26px;box-shadow:var(--shadow)}
  h1{font-size:26px;letter-spacing:-.5px;margin:0 0 4px}
  h2{font-size:18px;margin:0 0 2px}
  .sub{color:var(--muted);margin:0 0 20px}
  label{display:block;font-size:13px;color:var(--muted);margin:0 0 6px;font-weight:600}
  input[type=email],input[type=password],input[type=text],input[type=tel]{width:100%;padding:12px 14px;border-radius:11px;
    border:1px solid var(--line);background:var(--bg);color:var(--fg);font-size:15px}
  input:focus{outline:none;border-color:var(--brand)}
  .row{display:flex;gap:10px;margin-top:14px;flex-wrap:wrap}
  .msg{margin-top:14px;font-size:14px;padding:10px 12px;border-radius:10px;border:1px solid var(--line2);display:none}
  .msg.show{display:block}
  .msg.ok{color:var(--ok);border-color:color-mix(in srgb,var(--ok) 40%,transparent)}
  .msg.err{color:var(--bad);border-color:color-mix(in srgb,var(--bad) 40%,transparent)}
  .msg.info{color:var(--muted)}
  .hidden{display:none}
  .pill{display:inline-flex;align-items:center;gap:8px;font-size:12.5px;font-weight:600;color:var(--muted);
    border:1px solid var(--line);background:var(--card2);padding:5px 11px;border-radius:999px}
  .dot{width:8px;height:8px;border-radius:50%;background:var(--faint)}
  .dot.ok{background:var(--ok)} .dot.warn{background:var(--warn)} .dot.bad{background:var(--bad)}
  .who{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:20px}
  .who .email{font-weight:700}
  .grid{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin:6px 0 22px}
  .tile{background:var(--card2);border:1px solid var(--line2);border-radius:13px;padding:16px}
  .tile .k{font-size:12px;color:var(--faint);text-transform:uppercase;letter-spacing:.09em}
  .tile .v{font-size:22px;font-weight:800;margin-top:4px}
  table{width:100%;border-collapse:collapse;font-size:14px;margin-top:8px}
  th,td{text-align:left;padding:10px 12px;border-bottom:1px solid var(--line2)}
  th{color:var(--faint);font-size:12px;text-transform:uppercase;letter-spacing:.06em}
  td.mono{font-family:ui-monospace,Menlo,Consolas,monospace;font-size:13px}
  .empty{color:var(--muted);padding:18px 12px;text-align:center}
  footer{color:var(--faint);font-size:12.5px;margin-top:18px}
  .acct{border:1px solid var(--line2);border-radius:13px;padding:18px;margin-top:12px;background:var(--card2)}
  .acct-head{display:flex;align-items:center;gap:10px;flex-wrap:wrap}
  .acct-title{font-weight:700;font-size:15px}
  .status{display:inline-flex;align-items:center;gap:7px;font-size:12.5px;font-weight:600;border:1px solid var(--line);
    background:var(--bg);padding:4px 10px;border-radius:999px}

  /* Profile strip */
  .profile{display:flex;align-items:center;gap:14px;margin:12px 0 0;padding:12px 14px;
    background:var(--bg);border:1px solid var(--line2);border-radius:11px}
  .profile-avatar{width:40px;height:40px;border-radius:50%;background:linear-gradient(135deg,var(--brand2),var(--accent));
    display:grid;place-items:center;color:#fff;font-weight:800;font-size:16px;flex-shrink:0}
  .profile-info{flex:1;min-width:0}
  .profile-name{font-weight:700;font-size:14px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .profile-user{font-size:13px;color:var(--accent)}
  .profile-phone{font-size:12.5px;color:var(--faint);font-family:ui-monospace,Menlo,Consolas,monospace}

  /* Entity counts chips */
  .entity-counts{display:flex;gap:8px;flex-wrap:wrap;margin:12px 0 0}
  .entity-chip{display:inline-flex;align-items:center;gap:6px;font-size:12.5px;font-weight:600;
    border:1px solid var(--line2);background:var(--bg);padding:5px 12px;border-radius:999px;color:var(--muted)}
  .entity-chip b{color:var(--fg)}
  .entity-chip .icon{font-size:14px;line-height:1}

  /* Sync progress stepper */
  .sync-stepper{display:flex;align-items:center;gap:0;margin:14px 0 6px;overflow-x:auto}
  .sync-step{display:flex;align-items:center;gap:6px;font-size:12px;font-weight:600;color:var(--faint);
    padding:6px 12px;border-radius:8px;white-space:nowrap;transition:.2s}
  .sync-step.active{color:var(--accent);background:color-mix(in srgb,var(--accent) 12%,transparent)}
  .sync-step.done{color:var(--ok)}
  .sync-step .check{font-size:13px}
  .sync-arrow{color:var(--line);font-size:11px;margin:0 2px}

  /* Entity browser */
  .entity-browser{margin:14px 0 0;border:1px solid var(--line2);border-radius:11px;overflow:hidden}
  .entity-tabs{display:flex;gap:0;border-bottom:1px solid var(--line2);overflow-x:auto;background:var(--bg)}
  .entity-tab{padding:8px 14px;font-size:12.5px;font-weight:600;color:var(--faint);cursor:pointer;
    border:none;background:transparent;border-bottom:2px solid transparent;transition:.15s;white-space:nowrap}
  .entity-tab:hover{color:var(--fg)}
  .entity-tab.active{color:var(--accent);border-bottom-color:var(--accent)}
  .entity-list{max-height:320px;overflow-y:auto;padding:0}
  .entity-row{display:flex;align-items:center;gap:10px;padding:8px 14px;border-bottom:1px solid var(--line2);font-size:13.5px}
  .entity-row:last-child{border-bottom:none}
  .entity-row .ename{font-weight:600;flex:1;min-width:0;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
  .entity-row .euser{color:var(--accent);font-size:12.5px;flex-shrink:0}
  .entity-row .emeta{color:var(--faint);font-size:12px;flex-shrink:0}

  .step{margin-top:12px;border-top:1px solid var(--line2);padding-top:12px}
  .qr{display:inline-block;background:#fff;padding:12px;border-radius:12px;margin-top:10px}
  .qr img{display:block;width:200px;height:200px;image-rendering:pixelated}
  .hint{font-size:13px;color:var(--muted);margin:6px 0 0}
  .mini{font-size:13px;padding:8px 12px}
  @media(max-width:640px){.grid{grid-template-columns:1fr} .sync-stepper{gap:0}}
</style>
</head>
<body>
<header class="nav"><div class="wrap nav-in">
  <a class="brand" href="/"><span class="logo">t</span>Open-TGate</a>
  <span class="pill" style="margin-left:6px">Operator Console</span>
  <div class="nav-right">
    <button class="btn" id="theme" title="Toggle theme" aria-label="Toggle theme">◐</button>
    <button class="btn hidden" id="signout">Sign out</button>
  </div>
</div></header>

<main class="wrap">
  <!-- Loading -->
  <section id="view-loading" class="card"><p class="sub" style="margin:0">Loading console…</p></section>

  <!-- Login -->
  <section id="view-login" class="card hidden" style="max-width:460px;margin:6vh auto 0">
    <h1 id="login-title">Operator sign in</h1>
    <p class="sub" id="login-sub">Sign in with your authorized email. Password, magic link, Google, and GitHub are all supported.</p>

    <div class="row" style="margin:0 0 16px;gap:10px">
      <button class="btn" id="oauth-google" style="flex:1;justify-content:center">Continue with Google</button>
      <button class="btn" id="oauth-github" style="flex:1;justify-content:center">Continue with GitHub</button>
    </div>
    <div style="display:flex;align-items:center;gap:10px;color:var(--faint);font-size:12px;margin:0 0 14px">
      <span style="flex:1;height:1px;background:var(--line2)"></span>OR<span style="flex:1;height:1px;background:var(--line2)"></span>
    </div>

    <form id="login-form" autocomplete="on">
      <label for="email">Email address</label>
      <input id="email" type="email" autocomplete="email" placeholder="you@example.com" required />
      <label for="password" style="margin-top:12px">Password</label>
      <input id="password" type="password" autocomplete="current-password" placeholder="Your password" />
      <div class="row">
        <button class="btn primary" id="btn-password" type="submit" style="flex:1;justify-content:center">Sign in</button>
        <button class="btn" id="btn-magic" type="button" style="flex:1;justify-content:center">Email me a link</button>
      </div>
    </form>
    <div class="row" style="margin-top:10px">
      <button class="btn" id="btn-reset" type="button" style="flex:1;justify-content:center;font-size:13.5px">Set / reset password</button>
    </div>
    <div class="msg" id="login-msg"></div>
    <footer>Access is restricted to allowlisted operators. Sessions are handled by Supabase Auth.</footer>
  </section>

  <!-- Set new password (email-verified recovery) -->
  <section id="view-recovery" class="card hidden" style="max-width:440px;margin:6vh auto 0">
    <h1>Set your password</h1>
    <p class="sub">Choose a password for your operator account. You reached this screen from a verified email link.</p>
    <form id="recovery-form">
      <label for="new-password">New password</label>
      <input id="new-password" type="password" autocomplete="new-password" placeholder="At least 8 characters" required />
      <div class="row"><button class="btn primary" id="btn-setpw" type="submit" style="flex:1;justify-content:center">Save password</button></div>
    </form>
    <div class="msg" id="recovery-msg"></div>
  </section>

  <!-- Not authorized -->
  <section id="view-denied" class="card hidden" style="max-width:480px;margin:6vh auto 0">
    <h1>Not authorized</h1>
    <p class="sub" id="denied-sub">This account is signed in but is not an Open-TGate operator.</p>
    <button class="btn" id="denied-signout">Sign out</button>
  </section>

  <!-- Console -->
  <section id="view-console" class="hidden">
    <div class="who">
      <span class="pill"><span class="dot ok"></span><span id="who-role">operator</span></span>
      <span class="email" id="who-email"></span>
    </div>
    <div class="grid">
      <div class="tile"><div class="k">Workers</div><div class="v" id="stat-workers">—</div></div>
      <div class="tile"><div class="k">Live (≤2 min)</div><div class="v" id="stat-live">—</div></div>
      <div class="tile"><div class="k">Outbound send</div><div class="v" id="stat-send">disabled</div></div>
    </div>
    <div class="card" id="tg-card" style="margin-bottom:16px">
      <div style="display:flex;align-items:center;gap:12px;flex-wrap:wrap">
        <div><h2>Telegram accounts</h2><p class="sub" style="margin:0">Connect accounts by phone number or QR code. Sync is read-only and ban-safe.</p></div>
        <button class="btn primary" id="tg-add" style="margin-left:auto">+ Connect account</button>
      </div>
      <div id="tg-add-form" class="hidden" style="margin-top:14px;border:1px solid var(--line2);border-radius:12px;padding:16px">
        <label for="tg-label">Account label</label>
        <input id="tg-label" type="text" placeholder="e.g. Support line" maxlength="80" autocomplete="off" />
        <div class="row">
          <button class="btn primary" id="tg-create" type="button" style="flex:1;justify-content:center">Create</button>
          <button class="btn" id="tg-cancel" type="button" style="flex:1;justify-content:center">Cancel</button>
        </div>
      </div>
      <div id="tg-list"><div class="empty" id="tg-empty">No Telegram accounts connected yet. Use "Connect account" to begin.</div></div>
      <div class="msg" id="tg-msg"></div>
    </div>
    <div class="card">
      <div style="display:flex;align-items:center;gap:12px;flex-wrap:wrap">
        <div><h2>Worker heartbeats</h2><p class="sub" style="margin:0">Live from Supabase (RLS-restricted to operators).</p></div>
        <button class="btn" id="refresh" style="margin-left:auto">Refresh</button>
      </div>
      <div id="hb-wrap">
        <table id="hb-table" class="hidden"><thead><tr>
          <th>Worker</th><th>Service</th><th>Status</th><th>Last seen</th>
        </tr></thead><tbody id="hb-body"></tbody></table>
        <div class="empty" id="hb-empty">No heartbeats yet — the worker has not reported in. This is expected until the worker is deployed and running.</div>
      </div>
      <div class="msg" id="console-msg"></div>
    </div>
    <footer>Outbound Telegram sending stays disabled until operator approval controls pass verification. This console performs read-only operations.</footer>
  </section>
</main>

<script src="https://cdn.jsdelivr.net/npm/@supabase/supabase-js@2.58.0/dist/umd/supabase.min.js"></script>
<script src="https://cdn.jsdelivr.net/npm/qrcode-generator@1.4.4/qrcode.js"></script>
<script>
(function(){
  var CFG = { url: "%SUPABASE_URL%", key: "%SUPABASE_KEY%" };
  var root = document.documentElement, TKEY = "otg-theme";
  try{ var t = localStorage.getItem(TKEY); if(t) root.setAttribute("data-theme", t); }catch(e){}
  document.getElementById("theme").addEventListener("click", function(){
    var n = root.getAttribute("data-theme")==="dark" ? "light":"dark";
    root.setAttribute("data-theme", n); try{ localStorage.setItem(TKEY, n); }catch(e){}
  });

  function show(id){ ["loading","login","recovery","denied","console"].forEach(function(v){
    document.getElementById("view-"+v).classList.toggle("hidden", v!==id); }); }
  function msg(id, text, kind){ var el=document.getElementById(id);
    el.textContent=text; el.className="msg show "+(kind||"info"); }
  function clearMsg(id){ var el=document.getElementById(id); el.className="msg"; el.textContent=""; }

  if(!window.supabase || CFG.url.indexOf("%SUPABASE")===0){
    show("login"); msg("login-msg","Console is not configured (SUPABASE_URL / key missing).","err");
    return;
  }
  var sb = window.supabase.createClient(CFG.url, CFG.key, { auth:{ persistSession:true, autoRefreshToken:true, detectSessionInUrl:true } });

  var REDIRECT = window.location.origin + "/app";
  var recovering = false;
  function em(){ return document.getElementById("email").value.trim(); }
  function pw(){ return document.getElementById("password").value; }

  document.getElementById("btn-reset").addEventListener("click", async function(){
    var email = em();
    if(!email){ msg("login-msg","Enter your email first.","err"); return; }
    clearMsg("login-msg"); msg("login-msg","Sending a password-set link…","info");
    var r = await sb.auth.resetPasswordForEmail(email, { redirectTo: REDIRECT });
    if(r.error){ msg("login-msg", r.error.message || "Could not send the link.","err"); }
    else{ msg("login-msg","If an account already exists for this email, a set-password link has been sent. Never signed in before? Use \\"Email me a link\\" or Google/GitHub first to create the account, then set a password.","ok"); }
  });

  document.getElementById("login-form").addEventListener("submit", async function(e){
    e.preventDefault();
    var email = em(), password = pw();
    if(!email){ msg("login-msg","Enter your email.","err"); return; }
    if(!password){ msg("login-msg","Enter your password, or use \\"Email me a link\\" / \\"Set / reset password\\".","err"); return; }
    var btn = document.getElementById("btn-password"); btn.disabled=true; clearMsg("login-msg");
    msg("login-msg","Signing in…","info");
    var r = await sb.auth.signInWithPassword({ email: email, password: password });
    btn.disabled=false;
    if(r.error){ msg("login-msg", (r.error.message||"Sign-in failed")+". First time here? Sign in with \\"Email me a link\\" or Google/GitHub to create your account, then set a password.","err"); return; }
  });

  document.getElementById("recovery-form").addEventListener("submit", async function(e){
    e.preventDefault();
    var np = document.getElementById("new-password").value;
    if(!np || np.length < 8){ msg("recovery-msg","Password must be at least 8 characters.","err"); return; }
    var btn = document.getElementById("btn-setpw"); btn.disabled=true; clearMsg("recovery-msg");
    msg("recovery-msg","Saving your password…","info");
    var r = await sb.auth.updateUser({ password: np });
    btn.disabled=false;
    if(r.error){ msg("recovery-msg", r.error.message || "Could not set password.","err"); return; }
    recovering = false;
    var s = await sb.auth.getSession();
    renderFor(s.data.session);
  });

  document.getElementById("btn-magic").addEventListener("click", async function(){
    var email = em();
    if(!email){ msg("login-msg","Enter your email first.","err"); return; }
    clearMsg("login-msg"); msg("login-msg","Sending magic link…","info");
    var r = await sb.auth.signInWithOtp({ email: email, options:{ emailRedirectTo: REDIRECT } });
    if(r.error){ msg("login-msg", r.error.message || "Could not send link.","err"); }
    else{ msg("login-msg","Check your inbox for a secure sign-in link, then return here.","ok"); }
  });

  async function oauth(provider){
    clearMsg("login-msg"); msg("login-msg","Redirecting to "+provider+"…","info");
    var r = await sb.auth.signInWithOAuth({ provider: provider, options:{ redirectTo: REDIRECT } });
    if(r.error){ msg("login-msg", (r.error.message||"OAuth failed")+" — this provider may not be enabled in Supabase yet.","err"); }
  }
  document.getElementById("oauth-google").addEventListener("click", function(){ oauth("google"); });
  document.getElementById("oauth-github").addEventListener("click", function(){ oauth("github"); });

  document.getElementById("signout").addEventListener("click", doSignOut);
  document.getElementById("denied-signout").addEventListener("click", doSignOut);
  async function doSignOut(){ if(tgTimer){ clearInterval(tgTimer); tgTimer=null; } await sb.auth.signOut(); location.replace("/app"); }

  async function renderFor(session){
    if(recovering){ show("recovery"); return; }
    if(!session){ show("login"); return; }
    var email = (session.user && session.user.email) || "";
    var op = await sb.from("open_tgate_operators").select("email,role,is_active").limit(1);
    if(op.error){ show("denied"); document.getElementById("denied-sub").textContent =
      "Signed in as "+email+", but the operator check failed: "+op.error.message; return; }
    var row = (op.data && op.data[0]) || null;
    if(!row || !row.is_active){
      show("denied");
      document.getElementById("denied-sub").textContent =
        "Signed in as "+email+", but this account is not an active Open-TGate operator.";
      return;
    }
    document.getElementById("who-email").textContent = email;
    document.getElementById("who-role").textContent = row.role || "operator";
    document.getElementById("signout").classList.remove("hidden");
    show("console");
    await loadHeartbeats();
    startAccounts();
  }

  document.getElementById("refresh").addEventListener("click", loadHeartbeats);
  async function loadHeartbeats(){
    clearMsg("console-msg");
    var r = await sb.from("open_tgate_worker_heartbeats")
      .select("worker_id,service,status,last_seen_at,metadata")
      .order("last_seen_at",{ascending:false}).limit(200);
    if(r.error){ msg("console-msg","Could not load heartbeats: "+r.error.message,"err"); return; }
    var rows = r.data || [];
    var body = document.getElementById("hb-body"); body.innerHTML="";
    var now = Date.now(), live = 0;
    rows.forEach(function(h){
      var seen = h.last_seen_at ? new Date(h.last_seen_at) : null;
      var isLive = seen && (now - seen.getTime() < 120000); if(isLive) live++;
      var tr = document.createElement("tr");
      tr.innerHTML =
        '<td class="mono">'+esc(h.worker_id)+'</td>'+
        '<td>'+esc(h.service||"")+'</td>'+
        '<td><span class="dot '+(isLive?"ok":"warn")+'"></span> '+esc(h.status||"")+'</td>'+
        '<td>'+(seen?seen.toLocaleString():"—")+'</td>';
      body.appendChild(tr);
    });
    document.getElementById("stat-workers").textContent = rows.length;
    document.getElementById("stat-live").textContent = live;
    document.getElementById("hb-table").classList.toggle("hidden", rows.length===0);
    document.getElementById("hb-empty").classList.toggle("hidden", rows.length!==0);
  }
  function esc(s){ return String(s==null?"":s).replace(/[&<>"']/g,function(c){
    return ({"&":"&amp;","<":"&lt;",">":"&gt;",'"':"&quot;","'":"&#39;"})[c]; }); }

  // ---- Telegram account login + sync + entity browser ---------------------
  var tgTimer = null, tgSig = {};
  var SYNC_STEPS = ["profile","chats","archived","contacts","complete"];
  var SYNC_LABELS = { profile:"Profile", chats:"Chats", archived:"Archive", contacts:"Contacts", complete:"Done" };
  var STATUS_LABEL = {
    pending:"Not connected", initializing:"Starting…", awaiting_phone:"Enter phone number",
    awaiting_qr_scan:"Scan QR code", awaiting_code:"Enter login code",
    awaiting_password:"Enter 2FA password", authorized:"Connected", logged_out:"Signed out", error:"Needs attention"
  };
  var ENTITY_ICONS = { contact:"👤", user:"👤", group:"👥", channel:"📢", bot:"🤖", file:"📎" };
  var ENTITY_LABELS = { contact:"Contacts", user:"Users", group:"Groups", channel:"Channels", bot:"Bots", file:"Files" };
  // Track which entity tab is open per account.
  var openTabs = {};

  function tgMsg(t,k){ msg("tg-msg",t,k); } function tgClear(){ clearMsg("tg-msg"); }

  document.getElementById("tg-add").addEventListener("click", function(){
    document.getElementById("tg-add-form").classList.remove("hidden");
    document.getElementById("tg-label").focus();
  });
  document.getElementById("tg-cancel").addEventListener("click", function(){
    document.getElementById("tg-add-form").classList.add("hidden");
    document.getElementById("tg-label").value="";
  });
  document.getElementById("tg-create").addEventListener("click", async function(){
    var label = document.getElementById("tg-label").value.trim();
    if(!label){ tgMsg("Enter a label for the account.","err"); return; }
    tgClear();
    var r = await sb.from("open_tgate_tg_accounts").insert({ label: label, status:"pending", created_via:"app" });
    if(r.error){ tgMsg("Could not create account: "+r.error.message,"err"); return; }
    document.getElementById("tg-add-form").classList.add("hidden");
    document.getElementById("tg-label").value="";
    refreshAccounts();
  });

  async function tgCommand(accountId, action, payload){
    var row = { account_id: accountId, action: action, status:"pending" };
    if(payload) row.payload = payload;
    var r = await sb.from("open_tgate_login_commands").insert(row);
    if(r.error){ tgMsg("Command failed: "+r.error.message,"err"); return false; }
    tgClear(); return true;
  }

  function renderQr(link){
    try{
      var qr = window.qrcode(0, "M"); qr.addData(link); qr.make();
      return '<div class="qr">'+qr.createImgTag(4,0)+'</div>';
    }catch(e){ return '<p class="hint">QR ready — open Telegram → Settings → Devices → Link Desktop Device and scan the code.</p>'; }
  }

  function renderSyncStepper(currentStep){
    if(!currentStep) return "";
    var idx = SYNC_STEPS.indexOf(currentStep);
    if(idx === -1) return "";
    var html = '<div class="sync-stepper">';
    for(var i=0;i<SYNC_STEPS.length;i++){
      var step = SYNC_STEPS[i];
      var cls = i < idx ? "done" : (i === idx ? (step==="complete"?"done":"active") : "");
      var check = i < idx || (i===idx && step==="complete") ? '<span class="check">✓</span>' : "";
      var spinner = (i===idx && step!=="complete") ? '<span class="check">◌</span>' : "";
      if(i>0) html += '<span class="sync-arrow">→</span>';
      html += '<span class="sync-step '+cls+'">'+spinner+check+esc(SYNC_LABELS[step]||step)+'</span>';
    }
    html += '</div>';
    return html;
  }

  function renderProfile(acc){
    if(!acc.tg_first_name && !acc.tg_username) return "";
    var name = ((acc.tg_first_name||"")+" "+(acc.tg_last_name||"")).trim();
    var initials = (acc.tg_first_name||"?")[0].toUpperCase();
    return '<div class="profile">'+
      '<div class="profile-avatar">'+esc(initials)+'</div>'+
      '<div class="profile-info">'+
        (name ? '<div class="profile-name">'+esc(name)+'</div>' : '')+
        (acc.tg_username ? '<div class="profile-user">@'+esc(acc.tg_username)+'</div>' : '')+
        (acc.phone_masked ? '<div class="profile-phone">'+esc(acc.phone_masked)+'</div>' : '')+
      '</div></div>';
  }

  function renderEntityCounts(counts){
    if(!counts || typeof counts !== "object") return "";
    var kinds = ["contact","user","group","channel","bot","file"];
    var html = '<div class="entity-counts">';
    var any = false;
    for(var i=0;i<kinds.length;i++){
      var k = kinds[i], v = counts[k] || 0;
      if(v > 0){
        any = true;
        html += '<span class="entity-chip"><span class="icon">'+(ENTITY_ICONS[k]||"")+'</span>'+
          '<b>'+v+'</b> '+(ENTITY_LABELS[k]||k)+'</span>';
      }
    }
    html += '</div>';
    return any ? html : "";
  }

  async function loadEntities(accountId, kind){
    var r = await sb.from("open_tgate_tg_entities")
      .select("tg_id,kind,title,username,meta,last_message_date,is_archived")
      .eq("account_id", accountId)
      .eq("kind", kind)
      .order("title",{ascending:true})
      .limit(200);
    if(r.error) return [];
    return r.data || [];
  }

  function renderEntityList(entities, kind){
    if(!entities || entities.length===0) return '<div class="empty">No '+esc(ENTITY_LABELS[kind]||kind)+' synced yet.</div>';
    var html = '';
    for(var i=0;i<entities.length;i++){
      var e = entities[i];
      var meta = e.meta || {};
      var badge = "";
      if(meta.is_verified) badge = ' <span title="Verified" style="color:var(--accent)">✓</span>';
      if(meta.is_premium) badge += ' <span title="Premium" style="color:var(--warn)">★</span>';
      if(meta.is_bot) badge = ' <span title="Bot" style="color:var(--faint)">🤖</span>';
      var username = e.username ? '@'+esc(e.username) : '';
      var metaStr = "";
      if(kind==="group" || kind==="channel"){
        if(meta.member_count) metaStr = meta.member_count + ' members';
        if(meta.unread_count) metaStr += (metaStr?' · ':'')+meta.unread_count+' unread';
      }
      if(kind==="contact" || kind==="user"){
        if(meta.phone_masked) metaStr = meta.phone_masked;
      }
      if(kind==="file"){
        if(meta.mime_type) metaStr = meta.mime_type;
        if(meta.size) metaStr += (metaStr?' · ':'')+formatSize(meta.size);
      }
      html += '<div class="entity-row">'+
        '<span class="ename">'+esc(e.title||"(no name)")+badge+'</span>'+
        (username ? '<span class="euser">'+username+'</span>' : '')+
        (metaStr ? '<span class="emeta">'+esc(metaStr)+'</span>' : '')+
        '</div>';
    }
    return html;
  }

  function formatSize(b){
    if(!b || b<=0) return "";
    if(b<1024) return b+" B";
    if(b<1048576) return (b/1024).toFixed(1)+" KB";
    return (b/1048576).toFixed(1)+" MB";
  }

  function renderEntityBrowser(acc){
    if(acc.status !== "authorized") return "";
    var counts = acc.entity_counts || {};
    var kinds = ["contact","user","group","channel","bot","file"];
    var available = kinds.filter(function(k){ return (counts[k]||0) > 0; });
    if(available.length === 0) return "";
    var activeTab = openTabs[acc.id] || available[0];
    if(available.indexOf(activeTab)===-1) activeTab = available[0];

    var html = '<div class="entity-browser" id="browser-'+acc.id+'">';
    html += '<div class="entity-tabs">';
    for(var i=0;i<available.length;i++){
      var k = available[i];
      var cls = k===activeTab ? "active" : "";
      html += '<button class="entity-tab '+cls+'" data-browser="'+acc.id+'" data-kind="'+k+'">'
        +(ENTITY_ICONS[k]||"")+" "+(ENTITY_LABELS[k]||k)+' ('+counts[k]+')</button>';
    }
    html += '</div>';
    html += '<div class="entity-list" id="elist-'+acc.id+'"><div class="empty" style="padding:20px">Loading…</div></div>';
    html += '</div>';
    return html;
  }

  function actionsFor(acc){
    var s = acc.status;
    if(s==="authorized"){
      return '<div class="step"><button class="btn mini" data-act="logout" data-id="'+acc.id+'">Disconnect account</button></div>';
    }
    if(s==="awaiting_qr_scan"){
      var body = acc.qr_link ? renderQr(acc.qr_link) : '<p class="hint">Generating QR code…</p>';
      return '<div class="step">'+body+'<p class="hint">In Telegram: Settings → Devices → Link Desktop Device, then scan.</p></div>';
    }
    if(s==="awaiting_code"){
      return '<div class="step"><label>Login code (sent in Telegram)</label>'+
        '<input type="tel" inputmode="numeric" id="code-'+acc.id+'" placeholder="12345" autocomplete="off" />'+
        '<div class="row"><button class="btn primary mini" data-act="code" data-id="'+acc.id+'">Submit code</button></div></div>';
    }
    if(s==="awaiting_password"){
      return '<div class="step"><label>Two-step verification password</label>'+
        '<input type="password" id="pw-'+acc.id+'" autocomplete="off" />'+
        '<div class="row"><button class="btn primary mini" data-act="password" data-id="'+acc.id+'">Submit password</button></div></div>';
    }
    return '<div class="step" id="login-'+acc.id+'">'+
      '<div class="row" style="margin-top:0">'+
      '<button class="btn mini" data-act="phone-open" data-id="'+acc.id+'">Login by phone</button>'+
      '<button class="btn mini" data-act="qr" data-id="'+acc.id+'">Login by QR code</button></div></div>';
  }

  function bindAccountActions(){
    document.querySelectorAll("#tg-list [data-act]").forEach(function(btn){
      if(btn._bound) return; btn._bound = true;
      btn.addEventListener("click", async function(){
        var id = btn.getAttribute("data-id"), act = btn.getAttribute("data-act");
        if(act==="qr"){ if(await tgCommand(id,"start_qr",null)) tgMsg("Requesting QR code…","info"); }
        else if(act==="logout"){ if(await tgCommand(id,"logout",null)) tgMsg("Disconnecting…","info"); }
        else if(act==="phone-open"){
          var host = document.getElementById("login-"+id);
          host.innerHTML = '<label>Phone number (international format)</label>'+
            '<input type="tel" id="phone-'+id+'" placeholder="+15551234567" autocomplete="off" />'+
            '<div class="row"><button class="btn primary mini" data-act="phone-send" data-id="'+id+'">Send code</button></div>';
          bindAccountActions(); document.getElementById("phone-"+id).focus();
        }
        else if(act==="phone-send"){
          var phone=(document.getElementById("phone-"+id).value||"").trim();
          if(!/^\\+\\d{7,15}$/.test(phone)){ tgMsg("Enter a valid number like +15551234567.","err"); return; }
          if(await tgCommand(id,"start_phone",{ phone_number: phone })) tgMsg("Requesting login code…","info");
        }
        else if(act==="code"){
          var code=(document.getElementById("code-"+id).value||"").trim();
          if(!/^\\d{3,8}$/.test(code)){ tgMsg("Enter the numeric login code.","err"); return; }
          if(await tgCommand(id,"submit_code",{ code: code })) tgMsg("Verifying code…","info");
        }
        else if(act==="password"){
          var pw=document.getElementById("pw-"+id).value;
          if(!pw){ tgMsg("Enter your two-step password.","err"); return; }
          if(await tgCommand(id,"submit_password",{ password: pw })) tgMsg("Verifying password…","info");
        }
      });
    });
    // Bind entity browser tabs.
    document.querySelectorAll("#tg-list [data-browser]").forEach(function(tab){
      if(tab._bound) return; tab._bound = true;
      tab.addEventListener("click", async function(){
        var accId = tab.getAttribute("data-browser");
        var kind = tab.getAttribute("data-kind");
        openTabs[accId] = kind;
        // Highlight active tab.
        var browser = document.getElementById("browser-"+accId);
        if(browser){
          browser.querySelectorAll(".entity-tab").forEach(function(t){
            t.classList.toggle("active", t.getAttribute("data-kind")===kind);
          });
        }
        // Load and render entities.
        var listEl = document.getElementById("elist-"+accId);
        if(listEl){ listEl.innerHTML = '<div class="empty" style="padding:20px">Loading…</div>'; }
        var entities = await loadEntities(accId, kind);
        if(listEl){ listEl.innerHTML = renderEntityList(entities, kind); }
      });
    });
  }

  async function refreshAccounts(){
    var r = await sb.from("open_tgate_tg_accounts")
      .select("id,label,status,needs,qr_link,last_error,phone_masked,tg_first_name,tg_last_name,tg_username,sync_step,entity_counts,updated_at")
      .order("created_at",{ascending:true});
    if(r.error){ tgMsg("Could not load accounts: "+r.error.message,"err"); return; }
    var accounts = r.data || [];
    var list = document.getElementById("tg-list");
    if(accounts.length===0){
      list.innerHTML = '<div class="empty">No Telegram accounts connected yet. Use "Connect account" to begin.</div>';
      tgSig = {}; return;
    }
    for(var i=0;i<accounts.length;i++){
      var acc = accounts[i];
      var counts = acc.entity_counts || {};
      var totalEntities = 0;
      for(var ck in counts){ totalEntities += (counts[ck]||0); }
      var sig = acc.status+"|"+(acc.needs||"")+"|"+(acc.qr_link?"q":"")+"|"+totalEntities+"|"+(acc.last_error||"")+"|"+(acc.sync_step||"")+"|"+(acc.tg_username||"");
      var card = document.getElementById("acct-"+acc.id);
      if(card && tgSig[acc.id]===sig) continue;
      tgSig[acc.id]=sig;
      if(!card){ card=document.createElement("div"); card.className="acct"; card.id="acct-"+acc.id; list.appendChild(card); }
      var dot = acc.status==="authorized"?"ok":(acc.status==="error"?"bad":"warn");
      card.innerHTML =
        '<div class="acct-head"><span class="acct-title">'+esc(acc.label)+'</span>'+
        '<span class="status"><span class="dot '+dot+'"></span>'+esc(STATUS_LABEL[acc.status]||acc.status)+'</span>'+
        (acc.phone_masked?'<span class="pill">'+esc(acc.phone_masked)+'</span>':'')+'</div>'+
        renderProfile(acc)+
        renderSyncStepper(acc.sync_step)+
        renderEntityCounts(counts)+
        renderEntityBrowser(acc)+
        (acc.last_error ? '<p class="hint" style="color:var(--bad)">'+esc(acc.last_error)+'</p>' : "")+
        actionsFor(acc);
    }
    // Remove cards for deleted accounts.
    var ids = accounts.map(function(a){return "acct-"+a.id;});
    Array.prototype.slice.call(list.querySelectorAll(".acct")).forEach(function(el){
      if(ids.indexOf(el.id)===-1){ el.remove(); delete tgSig[el.id.slice(5)]; }
    });
    bindAccountActions();
    // Auto-load active entity browser tabs.
    for(var j=0;j<accounts.length;j++){
      var a = accounts[j];
      if(a.status==="authorized" && a.entity_counts){
        var tabKind = openTabs[a.id];
        var listEl = document.getElementById("elist-"+a.id);
        if(listEl && tabKind){
          var ents = await loadEntities(a.id, tabKind);
          listEl.innerHTML = renderEntityList(ents, tabKind);
        } else if(listEl){
          // Auto-select first available tab.
          var firstKind = null;
          var ekinds = ["contact","user","group","channel","bot","file"];
          for(var ki=0;ki<ekinds.length;ki++){
            if((a.entity_counts[ekinds[ki]]||0)>0){ firstKind=ekinds[ki]; break; }
          }
          if(firstKind){
            openTabs[a.id] = firstKind;
            var ents2 = await loadEntities(a.id, firstKind);
            listEl.innerHTML = renderEntityList(ents2, firstKind);
          }
        }
      }
    }
  }

  function startAccounts(){
    refreshAccounts();
    if(tgTimer) clearInterval(tgTimer);
    tgTimer = setInterval(refreshAccounts, 3000);
  }

  if(/(?:^|[#&?])type=recovery(?:&|$)/.test(window.location.hash || "")){
    recovering = true; show("recovery");
  }
  sb.auth.getSession().then(function(res){ if(!recovering) renderFor(res.data.session); });
  sb.auth.onAuthStateChange(function(evt, session){
    if(evt === "PASSWORD_RECOVERY"){ recovering = true; show("recovery"); return; }
    if(recovering) return;
    renderFor(session);
  });
})();
</script>
</body>
</html>`;

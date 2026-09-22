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
  .wrap{max-width:960px;margin:0 auto;padding:0 20px}
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
  input[type=email],input[type=password]{width:100%;padding:12px 14px;border-radius:11px;border:1px solid var(--line);
    background:var(--bg);color:var(--fg);font-size:15px}
  input[type=email]:focus,input[type=password]:focus{outline:none;border-color:var(--brand)}
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
  @media(max-width:640px){.grid{grid-template-columns:1fr}}
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

  // Email-proven "set / reset password": a recovery link is emailed to the
  // address, so a password can only be set by whoever controls that inbox.
  // This closes the self-signup hole (an allowlisted email cannot be claimed
  // by a stranger) and also works for operators who already exist from a
  // magic-link login.
  document.getElementById("btn-reset").addEventListener("click", async function(){
    var email = em();
    if(!email){ msg("login-msg","Enter your email first.","err"); return; }
    clearMsg("login-msg"); msg("login-msg","Sending a password-set link…","info");
    var r = await sb.auth.resetPasswordForEmail(email, { redirectTo: REDIRECT });
    if(r.error){ msg("login-msg", r.error.message || "Could not send the link.","err"); }
    else{ msg("login-msg","If an account already exists for this email, a set-password link has been sent. Never signed in before? Use \\"Email me a link\\" or Google/GitHub first to create the account, then set a password.","ok"); }
  });

  // Password sign-in only — no client-side account creation.
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
    // onAuthStateChange drives the transition to the console.
  });

  // Recovery: set a new password on the email-verified recovery session.
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
  async function doSignOut(){ await sb.auth.signOut(); location.replace("/app"); }

  async function renderFor(session){
    if(recovering){ show("recovery"); return; }
    if(!session){ show("login"); return; }
    var email = (session.user && session.user.email) || "";
    // Operator gate: RLS returns our own allowlist row iff we are an operator.
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
      var send = h.metadata && (h.metadata.send_enabled===true);
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

  // If this load is a password-recovery redirect, lock to the set-password
  // view synchronously — before the async initial render can replace it.
  if(/(?:^|[#&?])type=recovery(?:&|$)/.test(window.location.hash || "")){
    recovering = true; show("recovery");
  }
  sb.auth.getSession().then(function(res){ if(!recovering) renderFor(res.data.session); });
  sb.auth.onAuthStateChange(function(evt, session){
    if(evt === "PASSWORD_RECOVERY"){ recovering = true; show("recovery"); return; }
    if(recovering) return; // stay on the set-password screen until it completes
    renderFor(session);
  });
})();
</script>
</body>
</html>`;

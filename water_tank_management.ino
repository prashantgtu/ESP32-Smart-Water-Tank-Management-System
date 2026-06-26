/*
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║          ESP32  —  SMART WATER TANK MANAGEMENT SYSTEM               ║
 * ╠══════════════════════════════════════════════════════════════════════╣
 * ║  Hardware:                                                           ║
 * ║    • ESP32 Dev Board                                                 ║
 * ║    • HC-SR04  Ultrasonic sensor  (water level)                       ║
 * ║    • Relay module                (controls water pump)               ║
 * ║    • Buzzer                      (alerts)                            ║
 * ╠══════════════════════════════════════════════════════════════════════╣
 * ║  Features:                                                           ║
 * ║    ✔ Real-time water level %  &  cm display                         ║
 * ║    ✔ Tank volume in litres                                           ║
 * ║    ✔ Auto pump ON/OFF by threshold                                   ║
 * ║    ✔ Manual pump override from web dashboard                         ║
 * ║    ✔ Buzzer: fast beep = critical low, slow = overflow               ║
 * ║    ✔ Wi-Fi web dashboard (AP mode or home Wi-Fi)                     ║
 * ║    ✔ Animated SVG tank visualisation                                 ║
 * ║    ✔ JSON API  /data  for custom integrations                        ║
 * ║    ✔ Serial monitor commands  (p / m / s / ?)                        ║
 * ╠══════════════════════════════════════════════════════════════════════╣
 * ║  Wiring:                                                             ║
 * ║    HC-SR04  TRIG  →  GPIO 5                                          ║
 * ║    HC-SR04  ECHO  →  GPIO 18                                         ║
 * ║    Relay    IN    →  GPIO 26   (active-LOW relay board)              ║
 * ║    Buzzer   +     →  GPIO 27                                         ║
 * ║    GND / 3.3V / 5V as needed                                         ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 *
 *  Board manager : esp32 by Espressif  (≥ 2.0)
 *  No extra libraries needed — only built-in ESP32 Arduino core.
 */

// ═══════════════════════════════════════════════════════════════════════════
//  LIBRARIES
// ═══════════════════════════════════════════════════════════════════════════
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>

// ═══════════════════════════════════════════════════════════════════════════
//  ★  USER CONFIGURATION  —  Edit these to match your hardware & tank
// ═══════════════════════════════════════════════════════════════════════════

// ── Pin assignments ──────────────────────────────────────────────────────
#define PIN_TRIG      5    // HC-SR04 Trigger
#define PIN_ECHO     18    // HC-SR04 Echo
#define PIN_RELAY    26    // Relay IN  (LOW = pump ON for active-LOW boards)
#define PIN_BUZZER   27    // Buzzer positive leg

// ── Tank physical dimensions ─────────────────────────────────────────────
// Place sensor at the very top, facing straight down into the tank.
#define TANK_TOTAL_CM    100.0f   // Total usable depth of tank (cm)
                                  //   = distance from sensor face to tank floor
#define SENSOR_OFFSET_CM   5.0f  // Gap from sensor face to full-water surface (cm)
#define TANK_LENGTH_CM    60.0f  // Tank length — for volume calculation (cm)
#define TANK_WIDTH_CM     40.0f  // Tank width  — for volume calculation (cm)
//  Volume at 100% = LENGTH × WIDTH × (TANK_TOTAL_CM - SENSOR_OFFSET_CM) / 1000 litres

// ── Auto-control thresholds ──────────────────────────────────────────────
#define PUMP_AUTO_ON_PCT   20    // Pump turns ON  when level falls below this %
#define PUMP_AUTO_OFF_PCT  90    // Pump turns OFF when level rises above this %
#define ALERT_CRITICAL_PCT 10    // Fast buzzer alert below this %
#define ALERT_OVERFLOW_PCT 95    // Slow buzzer alert above this %

// ── Wi-Fi ────────────────────────────────────────────────────────────────
//  WIFI_MODE_AP  → ESP32 creates its own hotspot (no router needed)
//  WIFI_MODE_STA → ESP32 joins your home Wi-Fi (phone & ESP32 on same network)
#define WIFI_MODE_AP   1
#define WIFI_MODE_STA  2

#define WIFI_MODE       WIFI_MODE_AP       // ← change to WIFI_MODE_STA if needed

const char* AP_SSID      = "WaterTank_ESP32";
const char* AP_PASSWORD  = "watertank123";   // min 8 chars
const char* STA_SSID     = "YourHomeWiFi";   // used only when WIFI_MODE_STA
const char* STA_PASSWORD = "YourPassword";   // used only when WIFI_MODE_STA
#define     STA_TIMEOUT_MS  12000            // ms to wait before giving up on STA

// ── Sampling ─────────────────────────────────────────────────────────────
#define SENSOR_INTERVAL_MS  500   // How often to poll the sensor (ms)
#define SENSOR_SAMPLES        5   // Readings averaged per poll


// ═══════════════════════════════════════════════════════════════════════════
//  GLOBAL STATE
// ═══════════════════════════════════════════════════════════════════════════
WebServer server(80);

struct TankData {
    float distanceCm    = 0.0f;
    float waterHeightCm = 0.0f;
    int   levelPct      = 0;
    float volumeL       = 0.0f;
    bool  pumpOn        = false;
    bool  autoMode      = true;
    bool  sensorFault   = false;
    bool  critLow       = false;
    bool  overflow      = false;
    unsigned long lastSampleMs = 0;
} tank;

// Non-blocking buzzer
unsigned long beepLastMs = 0;
bool          beepState  = false;


// ═══════════════════════════════════════════════════════════════════════════
//  HTML DASHBOARD  (stored in flash with PROGMEM)
// ═══════════════════════════════════════════════════════════════════════════
const char HTML_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Water Tank Monitor</title>
<style>
/* ── Design tokens ── */
:root{
  --ink:  #0d1117;
  --panel:#161b22;
  --rim:  #21262d;
  --line: #30363d;
  --mute: #8b949e;
  --text: #e6edf3;
  --blue: #3b82f6;
  --sky:  #60a5fa;
  --teal: #0ea5e9;
  --green:#22c55e;
  --amber:#f59e0b;
  --rose: #ef4444;
  --r8:   8px;
  --r12:  12px;
  --r16:  16px;
}
*{margin:0;padding:0;box-sizing:border-box}
body{background:var(--ink);color:var(--text);font-family:'Segoe UI',system-ui,sans-serif;min-height:100vh;font-size:15px}

/* ── Header ── */
header{
  background:linear-gradient(110deg,#0c1a3a 0%,#1a3a6e 60%,#1e40af 100%);
  padding:14px 22px;display:flex;align-items:center;justify-content:space-between;
  border-bottom:1px solid #1e3a8a;
  box-shadow:0 4px 24px #00000055;
}
.hdr-left h1{font-size:1.2rem;font-weight:700;letter-spacing:-.3px}
.hdr-left p{font-size:.72rem;color:#93c5fd;margin-top:2px}
.uptime{font-size:.72rem;background:#ffffff14;border:1px solid #ffffff22;
  padding:5px 12px;border-radius:20px;color:#bfdbfe;white-space:nowrap}

/* ── Alerts ── */
.alert{display:none;align-items:center;gap:10px;padding:12px 18px;
  border-radius:var(--r12);margin-bottom:14px;font-weight:600;font-size:.87rem}
.alert.show{display:flex}
.ac{background:#ef444418;border:1px solid var(--rose);color:#fca5a5;
  animation:blink .75s ease-in-out infinite}
.ao{background:#f59e0b18;border:1px solid var(--amber);color:#fcd34d}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.5}}

/* ── Layout ── */
.wrap{max-width:880px;margin:0 auto;padding:20px 14px}
.top-grid{display:grid;grid-template-columns:210px 1fr;gap:14px;margin-bottom:14px}
@media(max-width:540px){.top-grid{grid-template-columns:1fr}}

/* ── Tank card ── */
.tank-card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r16);
  padding:18px;display:flex;flex-direction:column;align-items:center;gap:8px}
.card-tag{font-size:.68rem;color:var(--mute);text-transform:uppercase;letter-spacing:1.2px}
.big-num{font-size:2.4rem;font-weight:800;color:var(--sky);line-height:1;margin-top:4px}
.sub-txt{font-size:.76rem;color:var(--mute)}

/* ── Stats ── */
.stats{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.stat{background:var(--panel);border:1px solid var(--line);border-radius:var(--r12);padding:14px}
.stat-lbl{font-size:.67rem;color:var(--mute);text-transform:uppercase;letter-spacing:.6px;margin-bottom:5px}
.stat-val{font-size:1.4rem;font-weight:700}
.unit{font-size:.74rem;color:var(--mute);margin-left:2px}
.sky{color:var(--sky)} .green{color:var(--green)} .rose{color:var(--rose)} .amber{color:var(--amber)}

/* ── Controls ── */
.ctrl-card{background:var(--panel);border:1px solid var(--line);border-radius:var(--r16);padding:18px;margin-bottom:14px}
.ctrl-card h2{font-size:.9rem;font-weight:600;margin-bottom:14px;color:var(--mute);
  text-transform:uppercase;letter-spacing:.8px}
.row{display:flex;align-items:center;gap:10px;margin-bottom:10px;flex-wrap:wrap}
.row-lbl{font-size:.78rem;color:var(--mute);width:66px;flex-shrink:0}
.btn{flex:1;min-width:100px;padding:10px 6px;border:none;border-radius:var(--r8);
  font-size:.85rem;font-weight:600;cursor:pointer;transition:.15s all;color:#fff;letter-spacing:.2px}
.btn:active{transform:scale(.95)}
.btn:disabled{opacity:.3;cursor:not-allowed;transform:none}
.b-blue {background:#1d4ed8}.b-blue:hover:not(:disabled) {background:#2563eb}
.b-slate{background:#374151;color:#9ca3af}.b-slate:hover:not(:disabled){background:#4b5563}
.b-green{background:#15803d}.b-green:hover:not(:disabled){background:#16a34a}
.b-red  {background:#991b1b}.b-red:hover:not(:disabled)  {background:#b91c1c}

/* ── Progress bar ── */
.prog-wrap{margin-top:6px}
.prog-row{display:flex;justify-content:space-between;font-size:.67rem;color:var(--mute);margin-bottom:4px}
.prog-track{height:10px;background:#0f172a;border-radius:5px;overflow:hidden;border:1px solid var(--line)}
.prog-fill{height:100%;border-radius:5px;transition:width .6s ease, background .5s}

/* ── Rules info ── */
.info{background:var(--panel);border:1px solid var(--line);border-radius:var(--r16);
  padding:16px;font-size:.78rem;color:var(--mute);margin-bottom:14px}
.info h3{color:var(--text);font-size:.85rem;font-weight:600;margin-bottom:8px}
.rule{display:flex;gap:8px;align-items:baseline;margin-bottom:5px}
.dot{width:5px;height:5px;border-radius:50%;background:var(--blue);flex-shrink:0;margin-top:4px}

/* ── Sensor fault ── */
.fault{display:none;background:#ff000012;border:1px solid #7f1d1d;color:#fca5a5;
  padding:10px 16px;border-radius:var(--r8);font-size:.8rem;margin-bottom:14px}
.fault.show{display:block}

footer{text-align:center;padding:18px;font-size:.68rem;color:#374151}
</style>
</head>
<body>

<header>
  <div class="hdr-left">
    <h1>💧 Water Tank Monitor</h1>
    <p>ESP32 Smart Management System</p>
  </div>
  <div class="uptime" id="up">⏱ --</div>
</header>

<div class="wrap">
  <div id="faultBanner" class="fault">⛔ Sensor fault — check HC-SR04 wiring and power</div>
  <div id="ac" class="alert ac">⚠️ CRITICAL LOW — Tank nearly empty! Pump activated automatically.</div>
  <div id="ao" class="alert ao">🔔 HIGH WATER — Tank almost full. Pump will stop soon.</div>

  <div class="top-grid">

    <!-- Animated tank SVG -->
    <div class="tank-card">
      <div class="card-tag">🪣 Tank Level</div>
      <svg viewBox="0 0 120 220" width="125" xmlns="http://www.w3.org/2000/svg">
        <defs>
          <clipPath id="cp">
            <rect x="12" y="22" width="96" height="178" rx="6"/>
          </clipPath>
          <linearGradient id="wg" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stop-color="#60a5fa"/>
            <stop offset="100%" stop-color="#1d4ed8"/>
          </linearGradient>
          <linearGradient id="tg" x1="0" y1="0" x2="0" y2="1">
            <stop offset="0%"   stop-color="#1e3a5f"/>
            <stop offset="100%" stop-color="#0c1a3a"/>
          </linearGradient>
        </defs>
        <!-- Tank shell -->
        <rect x="12" y="22" width="96" height="178" rx="6" fill="url(#tg)" stroke="#30363d" stroke-width="2"/>
        <!-- Water + wave clipped to tank -->
        <g clip-path="url(#cp)">
          <rect id="wf" x="12" y="200" width="96" height="0" fill="url(#wg)" opacity=".9"/>
          <path id="wv" d="" fill="#93c5fd" opacity=".3"/>
        </g>
        <!-- Tick marks -->
        <line x1="10" y1="66"  x2="18" y2="66"  stroke="#334155" stroke-width="1.5"/>
        <line x1="10" y1="111" x2="18" y2="111" stroke="#334155" stroke-width="1.5"/>
        <line x1="10" y1="155" x2="18" y2="155" stroke="#334155" stroke-width="1.5"/>
        <text x="8" y="69"  font-size="7" fill="#475569" text-anchor="end">75%</text>
        <text x="8" y="114" font-size="7" fill="#475569" text-anchor="end">50%</text>
        <text x="8" y="158" font-size="7" fill="#475569" text-anchor="end">25%</text>
        <!-- Sensor block -->
        <rect x="38" y="10" width="44" height="14" rx="3" fill="#1e293b" stroke="#334155" stroke-width="1"/>
        <text x="60" y="20" font-size="7" fill="#64748b" text-anchor="middle">HC-SR04</text>
        <!-- Dashed beam -->
        <line id="beam" x1="60" y1="24" x2="60" y2="200" stroke="#1d4ed8" stroke-width="1"
              stroke-dasharray="3,4" opacity=".4"/>
        <!-- Level text inside SVG -->
        <text id="svgPct" x="60" y="120" font-size="20" fill="#bfdbfe"
              text-anchor="middle" font-weight="800">0%</text>
      </svg>
      <div class="big-num" id="pct">0%</div>
      <div class="sub-txt" id="vol">0.0 L</div>
    </div>

    <!-- Stats grid -->
    <div class="stats">
      <div class="stat">
        <div class="stat-lbl">📡 Sensor Dist.</div>
        <div class="stat-val sky" id="sDist">--<span class="unit">cm</span></div>
      </div>
      <div class="stat">
        <div class="stat-lbl">📏 Water Height</div>
        <div class="stat-val sky" id="sWH">--<span class="unit">cm</span></div>
      </div>
      <div class="stat">
        <div class="stat-lbl">⚙️ Pump</div>
        <div class="stat-val" id="sPump">--</div>
      </div>
      <div class="stat">
        <div class="stat-lbl">🔄 Mode</div>
        <div class="stat-val" id="sMode">--</div>
      </div>
      <div class="stat">
        <div class="stat-lbl">💧 Volume</div>
        <div class="stat-val sky" id="sVol">--<span class="unit">L</span></div>
      </div>
      <div class="stat">
        <div class="stat-lbl">🚨 Alert</div>
        <div class="stat-val" id="sAlert">--</div>
      </div>
    </div>

  </div><!-- .top-grid -->

  <!-- Controls -->
  <div class="ctrl-card">
    <h2>🎛️ Controls</h2>

    <div class="row">
      <span class="row-lbl">Mode</span>
      <button class="btn b-blue"  onclick="setMode(1)">🤖 Auto</button>
      <button class="btn b-slate" onclick="setMode(0)">🖐 Manual</button>
    </div>

    <div class="row">
      <span class="row-lbl">Pump</span>
      <button class="btn b-green" id="bOn"  onclick="ctrlPump(1)">▶ Pump ON</button>
      <button class="btn b-red"   id="bOff" onclick="ctrlPump(0)">⏹ Pump OFF</button>
    </div>

    <div class="prog-wrap">
      <div class="prog-row"><span>Empty</span><span id="pPct">0%</span><span>Full</span></div>
      <div class="prog-track">
        <div class="prog-fill" id="pb" style="width:0%"></div>
      </div>
    </div>
  </div>

  <!-- Auto-mode rules -->
  <div class="info">
    <h3>ℹ️ How Auto-Mode Works</h3>
    <div class="rule"><div class="dot"></div><span>Pump turns <b>ON</b> automatically when water drops below the <b>PUMP_AUTO_ON_PCT</b> threshold.</span></div>
    <div class="rule"><div class="dot"></div><span>Pump turns <b>OFF</b> when water rises above the <b>PUMP_AUTO_OFF_PCT</b> threshold.</span></div>
    <div class="rule"><div class="dot"></div><span><b>Fast beep</b> = critical low water (below ALERT_CRITICAL_PCT).</span></div>
    <div class="rule"><div class="dot"></div><span><b>Slow beep</b> = overflow warning (above ALERT_OVERFLOW_PCT).</span></div>
    <div class="rule"><div class="dot"></div><span>Switch to <b>Manual</b> to override pump with the buttons above.</span></div>
  </div>

</div><!-- .wrap -->
<footer>ESP32 Water Tank Management · refreshes every 2 s</footer>

<script>
let isAuto = true;

/* ─── Fetch & render ─── */
async function fetchData() {
  try {
    const r = await fetch('/data');
    if (!r.ok) return;
    render(await r.json());
  } catch(e) {}
}

function u(id, html) { document.getElementById(id).innerHTML = html; }
function cls(id, c)  { document.getElementById(id).className = c; }

function render(d) {
  isAuto = d.autoMode;

  /* Alerts & fault */
  document.getElementById('faultBanner').classList.toggle('show', d.sensorFault);
  document.getElementById('ac').classList.toggle('show', d.critLow);
  document.getElementById('ao').classList.toggle('show', d.overflow);

  /* Main numbers */
  document.getElementById('pct').textContent    = d.levelPct + '%';
  document.getElementById('svgPct').textContent = d.levelPct + '%';
  document.getElementById('vol').textContent    = d.volumeL.toFixed(1) + ' L';
  document.getElementById('pPct').textContent   = d.levelPct + '%';

  /* Stats */
  u('sDist', d.distanceCm    + '<span class="unit">cm</span>');
  u('sWH',   d.waterHeightCm + '<span class="unit">cm</span>');
  u('sVol',  d.volumeL.toFixed(1) + '<span class="unit">L</span>');

  const pEl = document.getElementById('sPump');
  pEl.textContent = d.pumpOn ? 'ON 🟢' : 'OFF 🔴';
  pEl.className   = 'stat-val ' + (d.pumpOn ? 'green' : 'rose');

  const mEl = document.getElementById('sMode');
  mEl.textContent = d.autoMode ? 'AUTO 🤖' : 'MANUAL 🖐';
  mEl.className   = 'stat-val ' + (d.autoMode ? 'sky' : 'amber');

  const aEl = document.getElementById('sAlert');
  if (d.critLow)   { aEl.textContent = 'LOW ⚠️';  aEl.className = 'stat-val rose'; }
  else if (d.overflow) { aEl.textContent = 'HIGH 🔔'; aEl.className = 'stat-val amber'; }
  else             { aEl.textContent = 'OK ✅';   aEl.className = 'stat-val green'; }

  /* Progress bar colour */
  const pb = document.getElementById('pb');
  const lv = d.levelPct;
  pb.style.width = lv + '%';
  pb.style.background = lv <= 10 ? '#ef4444'
                       : lv <= 20 ? '#f59e0b'
                       : lv >= 90 ? '#22c55e' : '#3b82f6';

  /* Pump buttons disabled in auto */
  document.getElementById('bOn').disabled  = d.autoMode;
  document.getElementById('bOff').disabled = d.autoMode;

  /* Uptime */
  const s = d.uptimeSec;
  const h = Math.floor(s/3600), m = Math.floor(s%3600/60), sc = s%60;
  document.getElementById('up').textContent = `⏱ ${h}h ${m}m ${sc}s`;

  /* SVG tank */
  drawTank(d.levelPct);
}

/* ─── SVG animated tank ─── */
function drawTank(pct) {
  const maxH = 178, topY = 22;
  const wh   = (pct / 100) * maxH;
  const wy   = topY + maxH - wh;

  const wf = document.getElementById('wf');
  wf.setAttribute('y', wy);
  wf.setAttribute('height', wh);

  // Dashed distance beam
  document.getElementById('beam').setAttribute('y2', Math.max(24, wy));

  if (wh < 2) { document.getElementById('wv').setAttribute('d', ''); return; }
  const amp = Math.min(3.5, wh / 12);
  const t   = Date.now() / 1000;
  let p = `M 12 ${wy}`;
  for (let x = 12; x <= 108; x += 4) {
    const y = wy - amp * Math.sin((x / 13) + t * 2.1);
    p += ` L ${x} ${y}`;
  }
  p += ` L 108 ${wy} Z`;
  document.getElementById('wv').setAttribute('d', p);
}

/* Continuous wave animation */
(function loop() { drawTank(parseInt(document.getElementById('pct').textContent) || 0); requestAnimationFrame(loop); })();

/* ─── Controls ─── */
async function setMode(a) {
  const fd = new FormData(); fd.append('auto', a);
  try { const r = await fetch('/mode', { method:'POST', body:fd }); render(await r.json()); } catch(e){}
}

async function ctrlPump(s) {
  if (isAuto) { alert('Switch to Manual mode first!'); return; }
  const fd = new FormData(); fd.append('state', s);
  try { const r = await fetch('/pump', { method:'POST', body:fd }); render(await r.json()); } catch(e){}
}

/* Poll every 2 s */
fetchData();
setInterval(fetchData, 2000);
</script>
</body>
</html>
)rawliteral";


// ═══════════════════════════════════════════════════════════════════════════
//  FUNCTION PROTOTYPES
// ═══════════════════════════════════════════════════════════════════════════
float  measureDistanceCm();
void   updateTankState();
void   setPump(bool on);
void   handleBuzzer();
String buildJSON();
void   serveRoot();
void   serveData();
void   servePumpPost();
void   serveModePost();
void   handleSerial();
void   startWifi();
void   beep(int freq, int durationMs);


// ═══════════════════════════════════════════════════════════════════════════
//  SETUP
// ═══════════════════════════════════════════════════════════════════════════
void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println(F("\n╔══════════════════════════════════════╗"));
    Serial.println(F("║  Water Tank Management System  BOOT  ║"));
    Serial.println(F("╚══════════════════════════════════════╝"));

    // ── Hardware init ─────────────────────────────────────────────────────
    pinMode(PIN_TRIG,   OUTPUT);
    pinMode(PIN_ECHO,   INPUT);
    pinMode(PIN_RELAY,  OUTPUT);
    pinMode(PIN_BUZZER, OUTPUT);

    digitalWrite(PIN_RELAY,  HIGH);  // Pump OFF (active-LOW relay)
    digitalWrite(PIN_BUZZER, LOW);

    Serial.println(F("[PIN]  Trig/Echo/Relay/Buzzer configured"));

    // ── Wi-Fi ─────────────────────────────────────────────────────────────
    startWifi();

    // ── Web routes ────────────────────────────────────────────────────────
    server.on("/",     HTTP_GET,  serveRoot);
    server.on("/data", HTTP_GET,  serveData);
    server.on("/pump", HTTP_POST, servePumpPost);
    server.on("/mode", HTTP_POST, serveModePost);
    server.begin();
    Serial.println(F("[HTTP] Server started on port 80"));

    // ── Boot tone ─────────────────────────────────────────────────────────
    beep(800,  120); delay(150);
    beep(1200, 120); delay(150);
    beep(1800, 200);

    Serial.println(F("\n[READY]  Serial commands:  p=pump  m=mode  s=status  ?=help\n"));
}


// ═══════════════════════════════════════════════════════════════════════════
//  LOOP
// ═══════════════════════════════════════════════════════════════════════════
void loop() {
    server.handleClient();

    // ── Sensor poll ───────────────────────────────────────────────────────
    if (millis() - tank.lastSampleMs >= SENSOR_INTERVAL_MS) {
        updateTankState();

        // Auto pump logic
        if (tank.autoMode && !tank.sensorFault) {
            if (!tank.pumpOn && tank.levelPct <= PUMP_AUTO_ON_PCT)  setPump(true);
            if ( tank.pumpOn && tank.levelPct >= PUMP_AUTO_OFF_PCT) setPump(false);
        }

        tank.lastSampleMs = millis();
    }

    // ── Non-blocking buzzer ───────────────────────────────────────────────
    handleBuzzer();

    // ── Serial commands ───────────────────────────────────────────────────
    if (Serial.available()) handleSerial();
}


// ═══════════════════════════════════════════════════════════════════════════
//  ULTRASONIC MEASUREMENT
//  Returns averaged distance in cm, or -1.0 on fault.
// ═══════════════════════════════════════════════════════════════════════════
float measureDistanceCm() {
    float sum   = 0.0f;
    int   valid = 0;

    for (int i = 0; i < SENSOR_SAMPLES; i++) {
        digitalWrite(PIN_TRIG, LOW);
        delayMicroseconds(2);
        digitalWrite(PIN_TRIG, HIGH);
        delayMicroseconds(10);
        digitalWrite(PIN_TRIG, LOW);

        // Timeout ≈ 30 ms  →  max range ≈ 5.1 m (well beyond any realistic tank)
        long us = pulseIn(PIN_ECHO, HIGH, 30000UL);
        float d = us * 0.01715f;          // cm  (speed-of-sound / 2)

        if (d > 1.0f && d < 400.0f) {
            sum += d;
            valid++;
        }
        delay(8);
    }

    return valid > 0 ? (sum / valid) : -1.0f;
}


// ═══════════════════════════════════════════════════════════════════════════
//  UPDATE TANK STATE
// ═══════════════════════════════════════════════════════════════════════════
void updateTankState() {
    float d = measureDistanceCm();

    if (d < 0.0f) {
        tank.sensorFault = true;
        Serial.println(F("[SENSOR] ⚠ Read fault — check wiring"));
        return;
    }
    tank.sensorFault = false;

    // Water height = total tank depth  –  (measured distance – sensor offset)
    float wh = TANK_TOTAL_CM - (d - SENSOR_OFFSET_CM);
    wh = constrain(wh, 0.0f, TANK_TOTAL_CM);

    tank.distanceCm    = d;
    tank.waterHeightCm = wh;
    tank.levelPct      = constrain((int)((wh / TANK_TOTAL_CM) * 100.0f), 0, 100);

    // Volume in litres   (L×W×H  /  1000  to convert cm³ → L)
    tank.volumeL = (TANK_LENGTH_CM * TANK_WIDTH_CM * wh) / 1000.0f;

    // Alert flags
    tank.critLow  = (tank.levelPct <= ALERT_CRITICAL_PCT);
    tank.overflow = (tank.levelPct >= ALERT_OVERFLOW_PCT);

    // Uptime
    tank.uptimeSec = millis() / 1000UL;

    Serial.printf("[TANK]  Dist: %5.1f cm | Water: %5.1f cm | %3d%% | %.1f L | Pump: %s | Mode: %s\n",
        tank.distanceCm, tank.waterHeightCm, tank.levelPct, tank.volumeL,
        tank.pumpOn  ? "ON"   : "OFF",
        tank.autoMode? "AUTO" : "MANUAL");
}


// ═══════════════════════════════════════════════════════════════════════════
//  PUMP CONTROL
// ═══════════════════════════════════════════════════════════════════════════
void setPump(bool on) {
    tank.pumpOn = on;
    digitalWrite(PIN_RELAY, on ? LOW : HIGH);   // Active-LOW relay
    Serial.printf("[PUMP]  %s\n", on ? "ON" : "OFF");
    beep(on ? 900 : 450, 100);
}


// ═══════════════════════════════════════════════════════════════════════════
//  NON-BLOCKING BUZZER ALERTS
// ═══════════════════════════════════════════════════════════════════════════
void handleBuzzer() {
    if (!tank.critLow && !tank.overflow) return;   // No alert — stay silent

    unsigned long interval = tank.critLow ? 400UL : 1100UL;  // fast vs slow

    if (millis() - beepLastMs >= interval) {
        beepState = !beepState;
        if (beepState) {
            int freq = tank.critLow ? 2400 : 1300;
            tone(PIN_BUZZER, freq, 160);
        }
        beepLastMs = millis();
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  BLOCKING BEEP HELPER
// ═══════════════════════════════════════════════════════════════════════════
void beep(int freq, int durationMs) {
    tone(PIN_BUZZER, freq, durationMs);
    delay(durationMs + 10);
}


// ═══════════════════════════════════════════════════════════════════════════
//  JSON DATA STRING
// ═══════════════════════════════════════════════════════════════════════════
String buildJSON() {
    String j;
    j.reserve(220);
    j  = "{";
    j += "\"distanceCm\":"    + String(tank.distanceCm, 1)    + ",";
    j += "\"waterHeightCm\":" + String(tank.waterHeightCm, 1) + ",";
    j += "\"levelPct\":"      + String(tank.levelPct)          + ",";
    j += "\"volumeL\":"       + String(tank.volumeL, 2)        + ",";
    j += "\"pumpOn\":"        + (tank.pumpOn     ? "true":"false") + ",";
    j += "\"autoMode\":"      + (tank.autoMode   ? "true":"false") + ",";
    j += "\"sensorFault\":"   + (tank.sensorFault? "true":"false") + ",";
    j += "\"critLow\":"       + (tank.critLow    ? "true":"false") + ",";
    j += "\"overflow\":"      + (tank.overflow   ? "true":"false") + ",";
    j += "\"uptimeSec\":"     + String(millis() / 1000UL);
    j += "}";
    return j;
}


// ═══════════════════════════════════════════════════════════════════════════
//  HTTP HANDLERS
// ═══════════════════════════════════════════════════════════════════════════

/* GET  /  → serve dashboard HTML */
void serveRoot() {
    server.send_P(200, "text/html", HTML_PAGE);
}

/* GET  /data  → JSON state (for AJAX & custom integrations) */
void serveData() {
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildJSON());
}

/* POST /pump  body: state=1|0  — manual pump override */
void servePumpPost() {
    if (!tank.autoMode && server.hasArg("state")) {
        bool on = (server.arg("state") == "1");
        setPump(on);
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildJSON());
}

/* POST /mode  body: auto=1|0  — switch control mode */
void serveModePost() {
    if (server.hasArg("auto")) {
        tank.autoMode = (server.arg("auto") == "1");
        Serial.printf("[MODE]  %s\n", tank.autoMode ? "AUTO" : "MANUAL");
    }
    server.sendHeader("Access-Control-Allow-Origin", "*");
    server.send(200, "application/json", buildJSON());
}


// ═══════════════════════════════════════════════════════════════════════════
//  SERIAL MONITOR COMMANDS
// ═══════════════════════════════════════════════════════════════════════════
void handleSerial() {
    char c = Serial.read();
    switch (c) {
        case 'p': case 'P':
            if (tank.autoMode) {
                Serial.println(F("[CMD]  Switch to MANUAL mode first (send 'm')"));
            } else {
                setPump(!tank.pumpOn);
            }
            break;

        case 'm': case 'M':
            tank.autoMode = !tank.autoMode;
            Serial.printf("[CMD]  Mode → %s\n", tank.autoMode ? "AUTO" : "MANUAL");
            break;

        case 's': case 'S':
            Serial.printf("[STATUS]\n"
                "  Distance    : %.1f cm\n"
                "  Water height: %.1f cm\n"
                "  Level       : %d%%\n"
                "  Volume      : %.1f L\n"
                "  Pump        : %s\n"
                "  Mode        : %s\n"
                "  Crit-low    : %s\n"
                "  Overflow    : %s\n"
                "  Uptime      : %lu s\n",
                tank.distanceCm, tank.waterHeightCm,
                tank.levelPct, tank.volumeL,
                tank.pumpOn   ? "ON"   : "OFF",
                tank.autoMode ? "AUTO" : "MANUAL",
                tank.critLow  ? "YES"  : "no",
                tank.overflow ? "YES"  : "no",
                millis() / 1000UL);
            break;

        case '?':
            Serial.println(F("\n[HELP]  Serial commands:"));
            Serial.println(F("  p  — toggle pump (manual mode only)"));
            Serial.println(F("  m  — toggle auto / manual mode"));
            Serial.println(F("  s  — print current status"));
            Serial.println(F("  ?  — show this help\n"));
            break;
    }
}


// ═══════════════════════════════════════════════════════════════════════════
//  WI-FI STARTUP  (AP or STA)
// ═══════════════════════════════════════════════════════════════════════════
void startWifi() {
#if WIFI_MODE == WIFI_MODE_STA
    Serial.printf("[WiFi] Connecting to  \"%s\" ...", STA_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(STA_SSID, STA_PASSWORD);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < STA_TIMEOUT_MS) {
        delay(300); Serial.print('.');
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println(F(" connected!"));
        Serial.printf("[WiFi] Dashboard → http://%s\n", WiFi.localIP().toString().c_str());
        return;
    }

    Serial.println(F("\n[WiFi] STA failed — falling back to AP mode"));
#endif

    // Access Point mode
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASSWORD);
    Serial.printf("[WiFi] AP  SSID     : %s\n", AP_SSID);
    Serial.printf("[WiFi] AP  Password : %s\n", AP_PASSWORD);
    Serial.printf("[WiFi] Dashboard    : http://%s\n", WiFi.softAPIP().toString().c_str());
}


// ═══════════════════════════════════════════════════════════════════════════
//  END OF FILE
// ═══════════════════════════════════════════════════════════════════════════

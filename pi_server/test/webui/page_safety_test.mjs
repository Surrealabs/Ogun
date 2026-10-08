// ============================================================
//  Ogun control page: drive-safety behaviour test (Node, no deps).
//
//  Run from the repo root:
//    node pi_server/test/webui/page_safety_test.mjs
//  or against another copy of the page (e.g. a git-show of HEAD):
//    node pi_server/test/webui/page_safety_test.mjs path/to/index.html
//  Exit code 0 = every scenario passed.
//
//  Lives OUTSIDE pi_server/webui/ on purpose: that directory is packaged to
//  the rover and served under /static/, a test file has no business there.
//
//  What it does: extracts the page's single inline <script> from the REAL
//  index.html and runs it in a fresh node:vm context per scenario, with a
//  catch-all DOM stub, captured document/window/button listeners, a
//  controllable clock and gamepad, and a small Pi + Teensy model:
//    ignition_start -> Pi started (unless estopped or pi.ackStart=false) -> Teensy ARMED
//    estop          -> Pi estop, not started -> Teensy disarmed
//    estop_clear    -> Pi not estop, not started
//    drive          -> forwarded only if Pi started and not estop; recorded if Teensy armed
//    trip()         -> Teensy disarmed + a safety line to the page; Pi keeps started
//                      (Pi-side P1 not done yet)
//  The core property: every drive frame an ARMED Teensy receives after the
//  last arm and before fresh operator input is neutral.
//
//  It models page logic only: no layout, no real browser, no real socket.
// ============================================================
import fs from 'node:fs';
import vm from 'node:vm';
import { fileURLToPath } from 'node:url';

const htmlPath = process.argv[2] || fileURLToPath(new URL('../../webui/index.html', import.meta.url));
const html = fs.readFileSync(htmlPath, 'utf8');
const blocks = [...html.matchAll(/<script(?![^>]*\bsrc=)[^>]*>([\s\S]*?)<\/script>/g)].map(m => m[1]);
if (blocks.length !== 1) throw new Error(`expected exactly one inline <script> in ${htmlPath}, found ${blocks.length}`);
const src = blocks[0];

// ---- DOM stub -------------------------------------------------------------
const stubHandler = {
  get(t, k) {
    if (k === Symbol.toPrimitive) return () => 0;
    if (k === 'then') return undefined;
    if (k === 'length') return 0;
    if (k === Symbol.iterator) return function* () {};
    return stub;
  },
  set() { return true; }, apply() { return stub; }, construct() { return stub; }, has() { return true; },
};
const stub = new Proxy(function () {}, stubHandler);
const recording = (base) => new Proxy(base, {
  get(t, k) { return k in t ? t[k] : stub; },
  set(t, k, v) { t[k] = v; return true; },
});
function makeClassList() {
  const s = new Set();
  return {
    add: (...c) => c.forEach(x => s.add(x)), remove: (...c) => c.forEach(x => s.delete(x)),
    toggle: (c, f) => { const on = f === undefined ? !s.has(c) : !!f; if (on) s.add(c); else s.delete(c); return on; },
    contains: c => s.has(c),
  };
}

// ---- One page instance + Pi/Teensy model ------------------------------------
function loadPage() {
  const intervals = [], sent = [], logs = [];
  const listeners = { doc: {}, win: {}, btn: {} };
  const add = (bag) => (type, fn) => { (bag[type] = bag[type] || []).push(fn); };
  const btn = recording({ textContent: '', title: '', classList: makeClassList(), addEventListener: add(listeners.btn) });
  const document = recording({
    hidden: false,
    addEventListener: add(listeners.doc),
    getElementById: (id) => (id === 'ignition-btn' ? btn : stub),
    createElement: () => stub, querySelectorAll: () => [], querySelector: () => stub,
  });
  const window = recording({ addEventListener: add(listeners.win) });
  const clock = { now: 1_000_000 };
  const pads = [];
  const ctx = {
    console, JSON, Math, URLSearchParams,
    Date: class extends Date { static now() { return clock.now; } },
    document, window, navigator: { getGamepads: () => pads },
    localStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    sessionStorage: { getItem: () => null, setItem() {}, removeItem() {} },
    location: { port: '8080', hostname: 'rover', protocol: 'http:', host: 'rover:8080' },
    requestAnimationFrame: () => 0, setTimeout: () => 0, clearTimeout() {},
    setInterval: (fn, ms) => { intervals.push({ fn, ms }); return intervals.length; }, clearInterval() {},
    WebSocket: Object.assign(function () {}, { OPEN: 1 }),
    alert() {}, confirm: () => true, fetch: () => new Promise(() => {}),
    Image: function () { return stub; }, Event: function () {}, CustomEvent: function () {}, KeyboardEvent: function () {},
  };
  ctx.globalThis = ctx;
  vm.createContext(ctx);
  vm.runInContext(src, ctx, { filename: 'index.html#script' });
  const run = (code) => vm.runInContext(code, ctx);
  ctx.addLog = (m) => logs.push(String(m));     // capture the page log
  ctx.__sent = sent;
  run('ws = { readyState: 1, send: (s) => globalThis.__sent.push(s), close(){} };');
  const driveTick = intervals.find(i => i.ms === 33);
  if (!driveTick) throw new Error('33 ms drive interval not found');

  const pi = { started: false, estop: false, ackStart: true };
  const teensy = { armed: false, rx: [] };      // {arm:true} markers + drive frames received while armed
  let processed = 0;
  const deliver = (o) => run(`handleMessage(${JSON.stringify(JSON.stringify(o))})`);
  const typesSent = (t) => sent.filter(s => JSON.parse(s).type === t).length;
  function pump() {
    while (processed < sent.length) {
      const m = JSON.parse(sent[processed++]);
      if (m.type === 'ignition_start') {
        if (!pi.estop && pi.ackStart) { pi.started = true; teensy.armed = true; teensy.rx.push({ arm: true }); }
      } else if (m.type === 'estop') { pi.estop = true; pi.started = false; teensy.armed = false; }
      else if (m.type === 'estop_clear') { pi.estop = false; pi.started = false; }
      else if (m.type === 'drive') { if (pi.started && !pi.estop && teensy.armed) teensy.rx.push(m); }
    }
    deliver({ type: 'telemetry', started: pi.started, estop: pi.estop, precheck_ok: true, teensy_connected: true });
  }
  const key = (type, k, repeat) => {
    (listeners.doc[type] || []).forEach(f => f({ key: k, repeat: !!repeat, target: { tagName: 'BODY' }, preventDefault() {} }));
    pump();
  };
  const p = {
    run, logs, sent, pi, teensy, pads, btn, clock, typesSent,
    login() { run('onLoginSuccess(); precheckOk = true;'); pump(); },
    tick(n = 1) { for (let i = 0; i < n; i++) { clock.now += 33; driveTick.fn(); pump(); } },
    press() { (listeners.btn.click || []).forEach(f => f({ preventDefault() {} })); pump(); },
    keydown(k, repeat = false) { key('keydown', k, repeat); },
    keyup(k) { key('keyup', k, false); },
    hide() { document.hidden = true; (listeners.doc.visibilitychange || []).forEach(f => f({})); pump(); },
    show() { document.hidden = false; (listeners.doc.visibilitychange || []).forEach(f => f({})); pump(); },
    blur() { (listeners.win.blur || []).forEach(f => f({})); pump(); },
    holdStick(y) { run(`joyL.activeId = 1; joyL.y = ${y}; joyL.onMove(0, ${y});`); pump(); },
    releaseStick() { run('joyL.activeId = null; joyL._recenter();'); pump(); },
    pad(y) {
      pads.length = 0;
      pads.push({ connected: true, index: 0, id: 'pad', axes: [0, -y, 0, 0], buttons: [{ pressed: false }, { pressed: false }] });
      run('pollGamepad()'); pump();
    },
    trip() { teensy.armed = false; deliver({ type: 'safety', event: 'watchdog', action: 'disarm', trips: 1, watchdog_ms: 500 }); pump(); },
    // Press until the page sends ignition_start (at most n presses); `between` runs before each press.
    pressUntilStart(n, between = () => {}) {
      for (let i = 0; i < n; i++) {
        between();
        const before = typesSent('ignition_start');
        p.press(); p.tick(2);
        if (typesSent('ignition_start') > before) return true;
      }
      return false;
    },
    sinceLastArm() { const i = teensy.rx.map(x => !!x.arm).lastIndexOf(true); return teensy.rx.slice(i + 1).filter(x => !x.arm); },
    mark() { return teensy.rx.length; },
    framesSince(m) { return teensy.rx.slice(m).filter(x => !x.arm); },
    sentMark() { return sent.length; },
    drivesSentSince(m) { return sent.slice(m).map(s => JSON.parse(s)).filter(x => x.type === 'drive'); },
  };
  return p;
}

// ---- Scenarios ------------------------------------------------------------
const neutral = (f) => f.y === 0 && f.rot === 0;
const firstBad = (frames) => JSON.stringify(frames.find(f => !neutral(f)) || 'none');
const results = [];
function scenario(name, fn) {
  const checks = [];
  const check = (cond, msg) => checks.push({ ok: !!cond, msg });
  try { fn(loadPage(), check); } catch (e) { checks.push({ ok: false, msg: 'threw: ' + e.message }); }
  results.push({ name, failed: checks.filter(c => !c.ok), n: checks.length });
}

// -- Baseline: the neutral stream and the trip latch (first fix) --
scenario('S1 no drive frames before START', (p, check) => {
  p.login();
  const m = p.sentMark(); p.tick(30);
  check(p.drivesSentSince(m).length === 0, 'no frames while not started');
});
scenario('S2 neutral stream from the START press, before the Pi acks', (p, check) => {
  p.login(); p.pi.ackStart = false;
  const m = p.sentMark();
  p.press();
  check(p.typesSent('ignition_start') === 1, 'START sends ignition_start');
  p.tick(10);
  const d = p.drivesSentSince(m);
  check(d.length === 10 && d.every(neutral), `a neutral frame every tick before the ack (${d.length})`);
  p.tick(Math.ceil(2000 / 33));
  const total = p.drivesSentSince(m).length;
  check(total > 0 && total <= Math.ceil(2000 / 33) + 1, `an unacked START streams only inside the 2 s grace (${total})`);
  const m2 = p.sentMark(); p.tick(10);
  check(p.drivesSentSince(m2).length === 0, 'stream stops after the grace');
});
scenario('S3 idle started page streams neutral every tick', (p, check) => {
  p.login(); p.press(); p.tick(300);
  const f = p.sinceLastArm();
  check(f.length >= 295 && f.every(neutral), `neutral every tick for ~10 s (${f.length})`);
});
scenario('S4 trip: stream stops, button re-arms instead of E-STOP', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.trip();
  const m = p.sentMark(); p.tick(30);
  check(p.drivesSentSince(m).length === 0, 'no frames while tripped, though the Pi still reports started');
  check(p.btn.textContent === 'START', `button offers START after a trip (shows ${p.btn.textContent})`);
  const estops = p.typesSent('estop');
  p.press();
  check(p.typesSent('ignition_start') === 2 && p.typesSent('estop') === estops, 'button sends ignition_start, not estop');
  p.tick(5);
  check(p.sinceLastArm().length === 5, 'stream resumes after re-arm');
});
scenario('S5 E-BRK latches and stops the stream', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.press();
  check(p.typesSent('estop') === 1, 'started + not tripped: the button is E-BRK');
  const m = p.sentMark(); p.tick(10);
  check(p.drivesSentSince(m).length === 0, 'no frames while the estop is latched');
});
scenario('S6 no frames when disconnected', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.run('connected = false');
  const m = p.sentMark(); p.tick(10);
  check(p.drivesSentSince(m).length === 0, 'no frames when not connected');
});

// -- Finding 1: stale input after re-arm --
scenario('A held key across a tab hide (keyup lost)', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.keydown('w'); p.tick(5);
  check(p.sinceLastArm().some(f => f.y === 1), 'driving on W before the hide');
  p.hide(); p.tick(3); p.show();
  check(p.pressUntilStart(3), 'START reachable after the hide');
  p.tick(30);
  const after = p.sinceLastArm();
  check(after.length > 0 && after.every(neutral), `only neutral frames after re-arm (got ${firstBad(after)})`);
  const m = p.mark(); p.keydown('s'); p.tick(5);
  check(p.framesSince(m).some(f => f.y === -1), 'fresh input after re-arm still drives');
});
scenario('B touch stick held through a trip and the re-arm presses', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.holdStick(0.3); p.tick(5);
  check(p.sinceLastArm().some(f => f.y === 0.3), 'driving at 0.3 before the trip');
  p.trip(); p.tick(3);
  const m = p.mark();
  p.pressUntilStart(3);
  p.tick(10);
  check(p.framesSince(m).every(neutral), `no non-neutral frame to an armed Teensy while held (got ${firstBad(p.framesSince(m))})`);
  check(p.btn.textContent === 'RELEASE', `button shows RELEASE while held (shows ${p.btn.textContent})`);
  check(p.logs.some(l => /release the controls to start/i.test(l)), 'log: "release the controls to start"');
  check(p.logs.some(l => /held: left stick/.test(l)), 'R2: the refusal names the left stick');
  p.releaseStick();
  check(p.pressUntilStart(3), 'START accepted once released');
  p.tick(20);
  check(p.sinceLastArm().length > 0 && p.sinceLastArm().every(neutral), 'neutral after re-arm');
  const m2 = p.mark(); p.holdStick(0.5); p.tick(5);
  check(p.framesSince(m2).some(f => f.y === 0.5), 'fresh stick input drives');
});
scenario('C E-BRK then START, key still held (no auto-repeat)', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.keydown('w'); p.tick(5);
  p.press(); p.tick(3);
  check(p.pi.estop, 'E-BRK latched');
  p.pressUntilStart(3);
  p.tick(20);
  check(p.sinceLastArm().every(neutral), `only neutral frames after START (got ${firstBad(p.sinceLastArm())})`);
  const m = p.mark(); p.keyup('w'); p.keydown('w'); p.tick(5);
  check(p.framesSince(m).some(f => f.y === 1), 'a real release + press drives');
});
scenario('C2 E-BRK then START, key auto-repeating throughout', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.keydown('w'); p.tick(5);
  p.press(); p.tick(3);                                   // E-BRK
  const m = p.mark();
  p.pressUntilStart(3, () => p.keydown('w', true));       // repeats keep arriving
  for (let i = 0; i < 10; i++) { p.keydown('w', true); p.tick(1); }
  check(p.framesSince(m).every(neutral), `no non-neutral frame while W is held and repeating (got ${firstBad(p.framesSince(m))})`);
  const m2 = p.mark(); p.keyup('w'); p.keydown('w'); p.tick(5);
  check(p.framesSince(m2).some(f => f.y === 1), 'a real release + press drives');
});
scenario('D gamepad stick held through a trip and the re-arm presses', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.pad(0.5); p.tick(5);
  check(p.sinceLastArm().some(f => f.y === 0.5), 'driving at 0.5 on the pad');
  p.trip(); p.tick(3);
  const m = p.mark();
  p.pressUntilStart(3, () => p.pad(0.5));
  p.tick(10);
  check(p.framesSince(m).every(neutral), `no non-neutral frame while the pad is held (got ${firstBad(p.framesSince(m))})`);
  check(p.logs.some(l => /held: gamepad axis 1/.test(l)), 'R2: the refusal names gamepad axis 1');
  p.pad(0);
  check(p.pressUntilStart(3, () => p.pad(0)), 'START accepted once centred');
  p.tick(10);
  check(p.sinceLastArm().every(neutral), 'neutral after START');
});
scenario('E window blur with a key held', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.keydown('w'); p.tick(5);
  const m = p.mark(), s = p.sentMark();
  p.blur();
  const immediate = p.drivesSentSince(s);
  check(immediate.length >= 1 && neutral(immediate[0]), 'one neutral frame sent at once on blur');
  p.tick(10);
  const after = p.framesSince(m);
  check(after.length > 0 && after.every(neutral), `frames after blur are neutral (got ${firstBad(after)})`);
});

// -- R1: auto-repeat after a neutralize, START inside the OS repeat delay --
scenario('G1 R1: W held, blur, START inside the repeat delay, then repeats', (p, check) => {
  p.login();
  p.keydown('w');                                         // real press, not started yet
  p.blur();                                               // alt-tab away and back: keys dropped
  check(p.pressUntilStart(1), 'START accepted (no key tracked after the blur)');
  for (let i = 0; i < 15; i++) { p.keydown('w', true); p.tick(1); }   // OS repeats begin
  const after = p.sinceLastArm();
  check(after.length > 0 && after.every(neutral), `repeats of a dropped key never reach an armed Teensy (got ${firstBad(after)})`);
  const m = p.mark(); p.keyup('w'); p.keydown('w'); p.tick(5);
  check(p.framesSince(m).some(f => f.y === 1), 'a real release + press drives');
});
scenario('G2 R1: W held, trip, re-arm inside the repeat delay, then repeats', (p, check) => {
  p.login(); p.press(); p.tick(5);
  p.keydown('w'); p.tick(5);
  p.trip();                                               // page neutralizes on the safety line
  check(p.pressUntilStart(1), 'START (re-arm) accepted before the first repeat');
  for (let i = 0; i < 15; i++) { p.keydown('w', true); p.tick(1); }
  const after = p.sinceLastArm();
  check(after.length > 0 && after.every(neutral), `repeats after re-arm stay neutral (got ${firstBad(after)})`);
});
scenario('K R2: START refused while a drive key is held, named', (p, check) => {
  p.login();
  p.keydown('w');
  p.press();
  check(p.typesSent('ignition_start') === 0, 'START refused while W is held');
  check(p.btn.textContent === 'RELEASE', `button shows RELEASE (shows ${p.btn.textContent})`);
  check(p.logs.some(l => /held: keys/.test(l)), 'the refusal names the keys');
  p.keyup('w');
  check(p.pressUntilStart(1), 'START accepted after keyup');
});
scenario('L R3: input made while disconnected does not survive login', (p, check) => {
  p.run('connected = false');
  p.holdStick(0.4);                                       // stick moved while there is no session
  p.pi.started = true;                                    // Pi still says started (P1 not done)
  const m = p.sentMark();
  p.login(); p.tick(10);
  const d = p.drivesSentSince(m);
  check(d.length > 0 && d.every(neutral), `frames after login are neutral (got ${firstBad(d)})`);
});
scenario('M R4: tune-dialog fallback matches the firmware turn defaults', (p, check) => {
  const t = JSON.parse(p.run('JSON.stringify(driveTuneDefault)'));
  check(t.turn_max_pwm === 90 && t.turn_slowdown === 0.85 && t.turn_ramp_sec === 0.35 && t.invert_turn === false,
        `driveTuneDefault turn = ${t.turn_max_pwm} / ${t.turn_slowdown} / ${t.turn_ramp_sec}`);
});

// ---- Report ----------------------------------------------------------------
let failedScenarios = 0;
for (const r of results) {
  const ok = r.failed.length === 0;
  if (!ok) failedScenarios++;
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${r.name}  (${r.n - r.failed.length}/${r.n})`);
  for (const f of r.failed) console.log('        - ' + f.msg);
}
const checks = results.reduce((a, r) => a + r.n, 0), bad = results.reduce((a, r) => a + r.failed.length, 0);
console.log(`page_safety_test (${htmlPath}): ${results.length - failedScenarios}/${results.length} scenarios, ${checks - bad}/${checks} checks passed`);
process.exit(failedScenarios ? 1 : 0);

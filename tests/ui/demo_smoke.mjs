// Checks the static Vercel demo (demo-dist/): serves it locally, drives it in headless Edge/Chrome
// through the DevTools protocol, and verifies both what works (recorded C++ results) and that anything
// needing the live backend is refused with a clear message. Optional: Node >= 22 and Edge/Chrome.
//
//   node scripts/build_demo.mjs && node tests/ui/demo_smoke.mjs [base-url-or-empty] [screenshot-dir]
// With a base URL (e.g. the deployed Vercel address) it tests that instead of the local copy.
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
const siteDir = path.join(repo, 'demo-dist');
const remote = process.argv[2] && process.argv[2].startsWith('http') ? process.argv[2].replace(/\/$/, '') : '';
const shotDir = path.resolve(process.argv[3] || path.join(os.tmpdir(), 'wb-demo-shots'));
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const MIME = { '.html': 'text/html', '.js': 'text/javascript', '.css': 'text/css', '.json': 'application/json', '.pdf': 'application/pdf', '.svg': 'image/svg+xml' };

let passes = 0, failures = 0;
const consoleErrors = [];
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${ok || !detail ? '' : `  -> ${String(detail).slice(0, 200)}`}`);
  if (ok) passes++; else failures++;
}

function findBrowser() {
  const c = [process.env.BROWSER_PATH,
    path.join(process.env['ProgramFiles(x86)'] || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Google', 'Chrome', 'Application', 'chrome.exe')];
  const f = c.find((x) => x && fs.existsSync(x));
  if (!f) throw new Error('No Edge/Chrome found. Set BROWSER_PATH.');
  return f;
}

async function main() {
  fs.mkdirSync(shotDir, { recursive: true });
  let server, baseUrl = remote;
  if (!remote) {
    if (!fs.existsSync(siteDir)) throw new Error('demo-dist/ missing: run node scripts/build_demo.mjs first');
    server = http.createServer((req, res) => {
      const rel = decodeURIComponent(new URL(req.url, 'http://x').pathname).replace(/^\/+/, '') || 'index.html';
      const file = path.join(siteDir, rel);
      if (!file.startsWith(siteDir) || !fs.existsSync(file) || fs.statSync(file).isDirectory()) { res.writeHead(404); res.end('not found'); return; }
      res.writeHead(200, { 'Content-Type': MIME[path.extname(file)] || 'application/octet-stream' });
      fs.createReadStream(file).pipe(res);
    });
    await new Promise((r) => server.listen(0, '127.0.0.1', r));
    baseUrl = `http://127.0.0.1:${server.address().port}`;
  }

  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'wb-demo-'));
  const dp = 9300 + Math.floor(Math.random() * 500);
  const browser = spawn(findBrowser(), ['--headless=new', `--remote-debugging-port=${dp}`, `--user-data-dir=${profile}`, '--window-size=1280,900',
    '--no-first-run', '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist', 'about:blank'], { stdio: 'ignore' });
  try {
    let target;
    for (let i = 0; i < 100 && !target; i++) { await sleep(100); try { target = (await (await fetch(`http://127.0.0.1:${dp}/json/list`)).json()).find((t) => t.type === 'page'); } catch { /* wait */ } }
    if (!target) throw new Error('browser did not start');
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((r) => (ws.onopen = r));
    let id = 1; const pend = new Map();
    ws.onmessage = (e) => {
      const m = JSON.parse(e.data);
      if (m.id && pend.has(m.id)) { pend.get(m.id)(m); pend.delete(m.id); return; }
      if (m.method === 'Runtime.exceptionThrown') consoleErrors.push(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
      if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') consoleErrors.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    };
    const cdp = (method, params = {}) => new Promise((res, rej) => { const i = id++; pend.set(i, (m) => (m.error ? rej(new Error(`${method}: ${m.error.message}`)) : res(m.result))); ws.send(JSON.stringify({ id: i, method, params })); });
    const ev = async (expr) => { const r = await cdp('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true }); if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description); return r.result.value; };
    const text = (i) => ev(`document.getElementById(${JSON.stringify(i)}).textContent`);
    const click = (i) => ev(`document.getElementById(${JSON.stringify(i)}).click()`);
    const waitFor = async (what, expr, ms = 15000) => { const end = Date.now() + ms; while (Date.now() < end) { if (await ev(expr).catch(() => false)) return true; await sleep(100); } check(`wait: ${what}`, false, 'timed out'); return false; };
    const tab = (t) => ev(`document.querySelector('#tabs button[data-tab=${t}]').click()`);
    const shot = async (name) => {
      const { contentSize } = await cdp('Page.getLayoutMetrics');
      await cdp('Emulation.setDeviceMetricsOverride', { width: 1280, height: Math.min(Math.ceil(contentSize.height), 3200), deviceScaleFactor: 1, mobile: false });
      await sleep(400);
      const r = await cdp('Page.captureScreenshot', { format: 'png' });
      await cdp('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
      fs.writeFileSync(path.join(shotDir, `${name}.png`), Buffer.from(r.data, 'base64'));
    };

    await cdp('Page.enable'); await cdp('Runtime.enable'); await cdp('DOM.enable');
    await cdp('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
    await cdp('Page.navigate', { url: `${baseUrl}/` });
    await waitFor('demo status', `document.getElementById('status-text').textContent.includes('Static demo')`);
    check('status bar says it is a static demo of recorded C++ results', (await text('status-text')).includes('Static demo') && (await text('status-text')).includes('recorded'), await text('status-text'));
    check('page title says static demo', (await ev('document.title')).includes('static demo'));
    await waitFor('banner meta', `document.getElementById('demo-meta').textContent.includes('commit')`);
    check('banner explains the demo and lists the recording (CPU, compiler, commit)', (await text('demo-meta')).includes('Core') && (await text('demo-meta')).includes('g++'), await text('demo-meta'));
    check('Quit button is hidden (nothing to stop)', await ev(`document.getElementById('quit').hidden`));
    check('report PDF is served', await ev(`fetch('report.pdf').then(r => r.ok && r.headers.get('content-type').includes('pdf'))`));

    await click('stl-sample');
    await waitFor('stl', `document.getElementById('stl-stats').textContent.includes('Triangles')`);
    check('STL sample: 12 triangles, 8 vertices (recorded)', /Triangles12/.test((await text('stl-stats')).replace(/\s/g, '')) && /Uniquevertices8/.test((await text('stl-stats')).replace(/\s/g, '')));
    check('STL drawn as mesh3d', await ev(`document.getElementById('stl-plot').data?.[0]?.type === 'mesh3d'`));
    await click('csv-sample');
    await waitFor('csv', `document.getElementById('csv-stats').textContent.includes('Nodes')`);
    check('CSV sample: 1331 nodes, scatter3d with "Temperature (°C)" colourbar', (await text('csv-stats')).includes('1331') && await ev(`JSON.stringify(document.getElementById('csv-plot').data[0].marker.colorbar.title).includes('Temperature (°C)')`));
    await ev(`document.getElementById('node-id').value = '1231'`);
    await click('node-lookup');
    await waitFor('node', `document.getElementById('node-info').textContent.includes('Node 1231')`);
    check('node lookup answers from recorded backend data (node 1231: 0.9, 0.1, 1.0 m, 20.001 °C)', /x = 0\.9 m, y = 0\.1 m, z = 1 m, T = 20\.001 °C/.test(await text('node-info')), await text('node-info'));
    await ev(`document.getElementById('node-id').value = '99999'`);
    await click('node-lookup');
    await waitFor('node err', `document.getElementById('node-info').textContent.includes('not in the recorded sample')`);
    check('unknown node id gives a clear demo message', true);
    const doc = await cdp('DOM.getDocument');
    const input = await cdp('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '#stl-file' });
    await cdp('DOM.setFileInputFiles', { nodeId: input.nodeId, files: [path.join(repo, 'tests', 'data', 'binary.stl')] });
    await waitFor('upload msg', `document.getElementById('stl-msg').textContent.length > 0`);
    check('uploading a file is refused with a "needs the real app" message', (await text('stl-msg')).includes('Static demo') && (await text('stl-msg')).includes('Launch.exe'), await text('stl-msg'));
    await sleep(500);
    await shot('demo_geometry');

    await tab('thermal');
    await click('fea-solve');
    await waitFor('fea table', `document.querySelectorAll('#fea-table tr').length === 5`);
    const feaTable = (await text('fea-table')).replace(/\s/g, '');
    check('default case replays the recorded convergence table (1.25 / 0.3125 / 0.078125 / 0.01953125, order 2.000)',
      ['1.2500000', '0.31250000', '0.078125000', '0.019531250', '2.000'].every((v) => feaTable.includes(v)), feaTable);
    check('three FEA plots drawn', await ev(`['fea-plot','fea-err-plot','fea-conv-plot'].every(i => document.getElementById(i).data?.length > 0)`));
    await ev(`document.querySelector('#fea-form [name=k_W_mK]').value = '20'`);
    await click('fea-solve');
    await waitFor('fea refuse', `document.getElementById('fea-msg').textContent.includes('Static demo')`);
    check('a custom parameter is refused with an explanation (no fake result)', (await text('fea-msg')).includes('custom parameters'), await text('fea-msg'));
    await click('bp-run');
    await waitFor('bp', `document.querySelectorAll('#bp-table tr').length === 5`);
    const bp = await text('bp-table');
    check('backprop: 4 gradients pass, dJ/dw = -1.2094645 (recorded)', (bp.match(/yes/g) || []).length === 4 && bp.includes('-1.2094645'), bp);
    await sleep(700);
    await shot('demo_thermal');

    await tab('performance');
    await click('perf-run');
    await waitFor('perf', `document.querySelectorAll('#perf-table tr').length >= 4`);
    check('performance table replays the recorded run with PASS agreement', (await text('perf-table')).includes('parallel') && (await text('perf-msg')).includes('PASS'), await text('perf-msg'));
    await ev(`document.getElementById('perf-form').elements.cases.value = '100'`);
    await click('perf-run');
    await waitFor('perf refuse', `document.getElementById('perf-msg').textContent.includes('Static demo')`);
    check('other benchmark settings are refused with an explanation', true);
    await sleep(500);
    await shot('demo_performance');

    await tab('hardware');
    await waitFor('devices', `document.querySelectorAll('#hw-table tr').length === 4`);
    check('3 SIMULATED devices listed from the recording', (await ev(`document.querySelectorAll('#hw-table .badge').length`)) === 3);
    await ev(`document.querySelector('#hw-table button[data-hw=caps][data-id=jtag0]').click()`);
    await waitFor('caps', `document.getElementById('hw-caps').textContent.includes('compute')`);
    check('JTAG capabilities (recorded) say compute: false', (await text('hw-caps')).includes('"compute": false'));
    await ev(`document.querySelector('#hw-table button[data-hw=connect][data-id=mcu0]').click()`);
    await waitFor('hw refuse', `document.getElementById('hw-msg').textContent.includes('Static demo')`);
    check('live connect is refused and points to the recorded session', (await text('hw-msg')).includes('recorded session'), await text('hw-msg'));
    const rec = await text('hw-recorded');
    check('recorded session shows two timeouts then success with backoff', rec.includes('SIMULATED timeout') && rec.includes('"attempt": 3') && rec.includes('"backoff_ms_after": 200'), rec.slice(0, 300));
    await sleep(500);
    await shot('demo_hardware');

    check('no JavaScript errors in the console', consoleErrors.length === 0, consoleErrors.join(' | '));
    ws.close();
  } finally {
    browser.kill();
    if (server) server.close();
    await sleep(400);
    try { fs.rmSync(profile, { recursive: true, force: true }); } catch { /* still releasing */ }
  }
  console.log(failures === 0 ? `\nDemo test: ALL ${passes} CHECKS PASSED (${remote || 'local copy'})` : `\nDemo test: ${failures} FAILED, ${passes} passed`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(2); });

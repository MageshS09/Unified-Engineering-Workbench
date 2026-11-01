// End-to-end UI smoke test: starts Launch.exe (--no-browser) and a headless Edge/Chrome, drives the
// real page through the Chrome DevTools Protocol, asserts on what the user would see, and saves
// screenshots. Optional test: needs Node >= 22 and Edge or Chrome (set BROWSER_PATH to override).
//
//   node tests/ui/ui_smoke.mjs <path-to-Launch.exe> [screenshot-dir]
import { spawn } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const repo = path.resolve(here, '..', '..');
const launchExe = path.resolve(process.argv[2] || path.join(repo, 'build', 'Launch.exe'));
const shotDir = path.resolve(process.argv[3] || path.join(repo, 'docs', 'screenshots'));
const badSamples = path.join(repo, 'tests', 'data');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function findBrowser() {
  const candidates = [process.env.BROWSER_PATH,
    path.join(process.env['ProgramFiles(x86)'] || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Google', 'Chrome', 'Application', 'chrome.exe')];
  const found = candidates.find((c) => c && fs.existsSync(c));
  if (!found) throw new Error('No Edge/Chrome found. Set BROWSER_PATH.');
  return found;
}

let failures = 0;
const consoleErrors = [];
function check(name, ok, detail = '') {
  console.log(`${ok ? 'PASS' : 'FAIL'}  ${name}${ok || !detail ? '' : `  -> ${detail}`}`);
  if (!ok) failures++;
}

async function main() {
  fs.mkdirSync(shotDir, { recursive: true });
  const logFile = path.join(path.dirname(launchExe), 'logs', 'workbench.log');
  fs.rmSync(logFile, { force: true });
  const app = spawn(launchExe, ['--no-browser'], { stdio: 'ignore' });
  let port = 0;
  for (let i = 0; i < 100 && !port; i++) {
    await sleep(100);
    if (fs.existsSync(logFile)) port = Number((/127\.0\.0\.1:(\d+)/.exec(fs.readFileSync(logFile, 'utf8')) || [])[1]) || 0;
  }
  if (!port) throw new Error('Launch.exe did not report a port in logs/workbench.log');

  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'wb-ui-'));
  const debugPort = 9300 + Math.floor(Math.random() * 500);
  const browser = spawn(findBrowser(), ['--headless=new', `--remote-debugging-port=${debugPort}`, `--user-data-dir=${profile}`,
    '--window-size=1280,900', '--no-first-run', '--enable-unsafe-swiftshader', '--use-angle=swiftshader', '--ignore-gpu-blocklist',
    'about:blank'], { stdio: 'ignore' });

  try {
    let target;
    for (let i = 0; i < 100 && !target; i++) {
      await sleep(100);
      try { target = (await (await fetch(`http://127.0.0.1:${debugPort}/json/list`)).json()).find((t) => t.type === 'page'); } catch { /* not up yet */ }
    }
    if (!target) throw new Error('Browser did not expose a DevTools page');
    const ws = new WebSocket(target.webSocketDebuggerUrl);
    await new Promise((r) => (ws.onopen = r));
    let nextId = 1;
    const pending = new Map();
    ws.onmessage = (ev) => {
      const m = JSON.parse(ev.data);
      if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); return; }
      if (m.method === 'Runtime.exceptionThrown') consoleErrors.push(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
      if (m.method === 'Runtime.consoleAPICalled' && m.params.type === 'error') consoleErrors.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    };
    const cdp = (method, params = {}) => new Promise((res, rej) => {
      const id = nextId++;
      pending.set(id, (m) => (m.error ? rej(new Error(`${method}: ${m.error.message}`)) : res(m.result)));
      ws.send(JSON.stringify({ id, method, params }));
    });
    const evalJs = async (expr) => {
      const r = await cdp('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true });
      if (r.exceptionDetails) throw new Error(`eval failed: ${expr}\n${r.exceptionDetails.exception?.description}`);
      return r.result.value;
    };
    const text = (id) => evalJs(`document.getElementById(${JSON.stringify(id)}).textContent`);
    const click = (id) => evalJs(`document.getElementById(${JSON.stringify(id)}).click()`);
    const waitFor = async (what, expr, timeoutMs = 15000) => {
      const end = Date.now() + timeoutMs;
      while (Date.now() < end) {
        if (await evalJs(expr).catch(() => false)) return true;
        await sleep(100);
      }
      check(`wait: ${what}`, false, 'timed out');
      return false;
    };
    const showTab = (tab) => evalJs(`document.querySelector('#tabs button[data-tab=${tab}]').click()`);
    const shot = async (name) => {
      // Resize the viewport to the page height so the fixed status bar sits at the bottom of the image.
      const { contentSize } = await cdp('Page.getLayoutMetrics');
      const height = Math.min(Math.ceil(contentSize.height), 3200);
      await cdp('Emulation.setDeviceMetricsOverride', { width: 1280, height, deviceScaleFactor: 1, mobile: false });
      await sleep(400);  // let Plotly re-layout
      const r = await cdp('Page.captureScreenshot', { format: 'png' });
      await cdp('Emulation.setDeviceMetricsOverride', { width: 1280, height: 900, deviceScaleFactor: 1, mobile: false });
      fs.writeFileSync(path.join(shotDir, `${name}.png`), Buffer.from(r.data, 'base64'));
    };

    await cdp('Page.enable'); await cdp('Runtime.enable'); await cdp('DOM.enable');
    await cdp('Page.navigate', { url: `http://127.0.0.1:${port}/` });
    await waitFor('backend ready', `document.getElementById('status-text').textContent.includes('Backend ready')`);
    check('status bar shows "Backend ready"', (await text('status-text')).includes('Backend ready'));
    check('four tabs present', (await evalJs(`[...document.querySelectorAll('#tabs button')].map(b => b.textContent).join('|')`)) === 'Geometry|Thermal Analysis|Performance|Hardware');

    // ---- Geometry ----
    await click('stl-sample');
    await waitFor('STL stats', `document.getElementById('stl-stats').textContent.includes('Triangles')`);
    const stl = await text('stl-stats');
    check('STL sample: 12 triangles, 8 vertices', /Triangles12/.test(stl.replace(/\s/g, '')) && /Uniquevertices8/.test(stl.replace(/\s/g, '')), stl);
    check('STL mesh3d trace drawn', await evalJs(`document.getElementById('stl-plot').data?.[0]?.type === 'mesh3d'`));
    check('Fit to view enabled after load', await evalJs(`!document.getElementById('stl-fit').disabled`));
    await click('stl-fit');
    await sleep(500);
    await shot('01_geometry_stl');

    await click('csv-sample');
    await waitFor('CSV stats', `document.getElementById('csv-stats').textContent.includes('Nodes')`);
    check('CSV sample: 1331 nodes', (await text('csv-stats')).includes('1331'));
    check('point cloud is scatter3d with colourbar "Temperature (°C)"',
      await evalJs(`(() => { const t = document.getElementById('csv-plot').data?.[0]; return t?.type === 'scatter3d' && JSON.stringify(t.marker.colorbar.title).includes('Temperature (°C)'); })()`));
    check('hover template shows node_id, x, y, z and T', await evalJs(`(() => { const h = document.getElementById('csv-plot').data[0].hovertemplate; return ['node_id','x ','y ','z ','T '].every(s => h.includes(s)); })()`));
    await evalJs(`document.getElementById('node-id').value = '1'`);
    await click('node-lookup');
    await waitFor('node info', `document.getElementById('node-info').textContent.includes('Node 1')`);
    check('node inspection returns node 1', (await text('node-info')).includes('T = 20.0937'));
    await sleep(500);
    await shot('02_geometry_csv');

    // negative upload: binary STL through the real file input
    const doc = await cdp('DOM.getDocument');
    const input = await cdp('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '#stl-file' });
    await cdp('DOM.setFileInputFiles', { nodeId: input.nodeId, files: [path.join(badSamples, 'binary.stl')] });
    await waitFor('binary STL error', `document.getElementById('stl-msg').textContent.length > 0`);
    check('binary STL is rejected with an actionable message', (await text('stl-msg')).includes('binary STL') && (await text('stl-msg')).includes('re-export'), await text('stl-msg'));
    const input2 = await cdp('DOM.querySelector', { nodeId: doc.root.nodeId, selector: '#csv-file' });
    await cdp('DOM.setFileInputFiles', { nodeId: input2.nodeId, files: [path.join(badSamples, 'duplicate_id.csv')] });
    await waitFor('duplicate id error', `document.getElementById('csv-msg').textContent.includes('duplicate')`);
    check('CSV with duplicate node_id is rejected with its line number', (await text('csv-msg')).includes('line 4'), await text('csv-msg'));

    // ---- Thermal ----
    await showTab('thermal');
    await evalJs(`document.querySelector('#fea-form [name=k_W_mK]').value = '-1'`);
    await click('fea-solve');
    check('invalid k is explained before any request', (await text('fea-msg')).includes('Conductivity k') && (await text('fea-msg')).includes('greater than 0'), await text('fea-msg'));
    await evalJs(`document.querySelector('#fea-form [name=k_W_mK]').value = '10'`);
    await click('fea-solve');
    await waitFor('FEA table', `document.querySelectorAll('#fea-table tr').length === 5`);
    const feaTable = (await text('fea-table')).replace(/\s/g, '');
    check('convergence table: midpoint errors 1.25, 0.3125, 0.078125, 0.01953125',
      ['1.2500000', '0.31250000', '0.078125000', '0.019531250'].every((v) => feaTable.includes(v)), feaTable);
    check('observed order 2.000', (feaTable.match(/2\.000/g) || []).length >= 3);
    check('three FEA plots drawn', await evalJs(`['fea-plot','fea-err-plot','fea-conv-plot'].every(id => document.getElementById(id).data?.length > 0)`));
    check('temperature plot has FE N=10..80 and exact', await evalJs(`document.getElementById('fea-plot').data.length === 5`));
    check('export buttons enabled', await evalJs(`['fea-export-conv','fea-export-json','fea-export-sol'].every(id => !document.getElementById(id).disabled)`));
    // backprop panel
    await click('bp-run');
    await waitFor('backprop table', `document.querySelectorAll('#bp-table tr').length === 5`);
    const bp = await text('bp-table');
    check('backprop: 4 gradients pass the finite-difference check', (bp.match(/yes/g) || []).length === 4, bp);
    check('backprop: dJ/dw = -1.2094645', bp.includes('-1.2094645'));
    await sleep(800);
    await shot('03_thermal');

    // ---- Performance ----
    await showTab('performance');
    await click('perf-run');
    await waitFor('perf table', `document.querySelectorAll('#perf-table tr').length >= 4`, 30000);
    const perf = await text('perf-table');
    check('performance table lists serial and parallel rows', perf.includes('serial') && perf.includes('parallel'), perf);
    check('serial/parallel agreement reported as PASS', (await text('perf-msg')).includes('PASS'), await text('perf-msg'));
    check('speedup plot drawn', await evalJs(`document.getElementById('perf-plot').data?.length === 2`));
    await sleep(500);
    await shot('04_performance');

    // cancel a long run: buttons must be locked while it runs, and the run must end "cancelled"
    await evalJs(`(() => { const f = document.getElementById('perf-form'); f.elements.cases.value = '3000'; f.elements.mesh.value = '20000'; })()`);
    await click('perf-run');
    await waitFor('job running', `document.getElementById('status-text').textContent.includes('%')`);
    check('run buttons are disabled while a job runs', await evalJs(`document.getElementById('perf-run').disabled && document.getElementById('fea-solve').disabled`));
    check('Cancel button is visible during the job', await evalJs(`!document.getElementById('status-cancel').hidden`));
    await click('status-cancel');
    await waitFor('cancelled', `document.getElementById('status-text').textContent.includes('cancelled')`, 30000);
    check('cancelled run keeps no results and says so', (await text('perf-msg')).includes('no partial results'), await text('perf-msg'));
    check('buttons re-enabled after cancel', await evalJs(`!document.getElementById('perf-run').disabled && !document.getElementById('fea-solve').disabled`));
    await evalJs(`(() => { const f = document.getElementById('perf-form'); f.elements.cases.value = '400'; f.elements.mesh.value = '2000'; })()`);

    // ---- Hardware ----
    await showTab('hardware');
    await waitFor('device table', `document.querySelectorAll('#hw-table tr').length === 4`);
    check('3 devices listed, all badged SIMULATED', (await evalJs(`document.querySelectorAll('#hw-table .badge').length`)) === 3);
    await click('hw-fault');
    await waitFor('fault on', `document.getElementById('hw-log').textContent.includes('fault injection ON')`);
    await evalJs(`document.querySelector('#hw-table button[data-hw=retry][data-id=jtag0]').click()`);
    await waitFor('retry result', `document.getElementById('hw-log').textContent.includes('attempt 3 OK')`);
    const log = await text('hw-log');
    check('timeout fault shown, then recovery by retry with backoff', log.includes('attempt 1 FAILED') && log.includes('attempt 2 FAILED') && log.includes('attempt 3 OK') && log.includes('SIMULATED timeout'), log);
    await evalJs(`document.querySelector('#hw-table button[data-hw=telemetry][data-id=jtag0]').click()`);
    await waitFor('telemetry', `document.getElementById('hw-telemetry').textContent.includes('SIMULATED')`);
    check('telemetry message is labelled SIMULATED', (await text('hw-telemetry')).includes('"label": "SIMULATED"'));
    await evalJs(`document.querySelector('#hw-table button[data-hw=caps][data-id=jtag0]').click()`);
    await waitFor('caps', `document.getElementById('hw-caps').textContent.includes('compute')`);
    check('JTAG capabilities say it is not a compute board', (await text('hw-caps')).includes('"compute": false'));
    await sleep(500);
    await shot('05_hardware');

    check('no JavaScript errors in the console', consoleErrors.length === 0, consoleErrors.join(' | '));

    // ---- Quit ----
    await click('quit');
    const exited = await Promise.race([new Promise((r) => app.once('exit', () => r(true))), sleep(5000).then(() => false)]);
    check('Quit button stops Launch.exe', exited);
    ws.close();
  } finally {
    browser.kill();
    app.kill();
    await sleep(300);
    fs.rmSync(profile, { recursive: true, force: true });
  }
  console.log(failures === 0 ? '\nUI smoke test: ALL PASSED' : `\nUI smoke test: ${failures} FAILED`);
  process.exit(failures === 0 ? 0 : 1);
}

main().catch((e) => { console.error(e); process.exit(2); });

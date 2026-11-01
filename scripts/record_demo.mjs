// Records real responses of the C++ backend (Launch.exe) for the static Vercel demo.
// Nothing is computed here: every number in demo/data/*.json is exactly what the C++ server returned.
//
//   node scripts/record_demo.mjs [path-to-Launch.exe]     (default: build/Launch.exe)
import { spawn, execSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const launchExe = path.resolve(process.argv[2] || path.join(repo, 'build', 'Launch.exe'));
const outDir = path.join(repo, 'demo', 'data');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
fs.mkdirSync(outDir, { recursive: true });

const logFile = path.join(path.dirname(launchExe), 'logs', 'workbench.log');
fs.rmSync(logFile, { force: true });
const app = spawn(launchExe, ['--no-browser'], { stdio: 'ignore' });
let port = 0;
for (let i = 0; i < 100 && !port; i++) {
  await sleep(100);
  if (fs.existsSync(logFile)) port = Number((/127\.0\.0\.1:(\d+)/.exec(fs.readFileSync(logFile, 'utf8')) || [])[1]) || 0;
}
if (!port) throw new Error('Launch.exe did not start');
const base = `http://127.0.0.1:${port}`;

async function get(p) {
  const r = await fetch(base + p);
  if (!r.ok) throw new Error(`${p}: HTTP ${r.status}`);
  return r.json();
}
async function post(p, body) {
  const r = await fetch(base + p, { method: 'POST', body: JSON.stringify(body) });
  if (!r.ok) throw new Error(`${p}: HTTP ${r.status}`);
  return r.json();
}
async function runJob(p, body) {
  const { job_id } = await post(p, body);
  for (let i = 0; i < 600; i++) {
    const j = await get(`/api/jobs/${job_id}`);
    if (j.state === 'done') return j.result;
    if (j.state !== 'running') throw new Error(`job ended as ${j.state}`);
    await sleep(100);
  }
  throw new Error('job timed out');
}
const save = (name, data) => fs.writeFileSync(path.join(outDir, name), JSON.stringify(data));

try {
  const health = await get('/api/health');
  save('health.json', health);
  save('sysinfo.json', await get('/api/sysinfo'));
  save('fea.json', await runJob('/api/fea/solve', { meshes: [10, 20, 40, 80] }));
  save('backprop.json', await get('/api/backprop'));
  save('perf.json', await runJob('/api/perf/run', {}));
  save('stl_sample.json', await get('/api/geometry/sample/stl'));
  const csv = await get('/api/geometry/sample/csv');
  save('csv_sample.json', csv);
  // Node lookup table: one real backend answer per node (the page's "Look up" box uses it).
  const nodes = {};
  for (let id = 1; id <= csv.node_count; id++) nodes[id] = await get(`/api/geometry/node/${id}`);
  save('nodes.json', nodes);
  const devices = await get('/api/devices');
  save('devices.json', devices);
  const caps = {};
  for (const d of devices.devices) caps[d.id] = await get(`/api/devices/${d.id}/capabilities`);
  save('capabilities.json', caps);

  // A recorded hardware session (all calls answered by the C++ simulated adapters).
  const session = [];
  const step = async (label, fn) => {
    try { session.push({ step: label, ok: true, response: await fn() }); } catch (e) { session.push({ step: label, ok: false, error: e.message }); }
  };
  await step('inject timeout fault', () => post('/api/devices/fault', { simulate_timeout: true }));
  await step('connect jtag0 with retry/backoff', () => post('/api/devices/jtag0/connect', { retry: true }));
  await step('read telemetry jtag0', () => get('/api/devices/jtag0/telemetry'));
  await step('disconnect jtag0', () => post('/api/devices/jtag0/disconnect', {}));
  save('hardware_session.json', session);

  let commit = 'unknown';
  try { commit = execSync('git rev-parse --short HEAD', { cwd: repo }).toString().trim(); } catch { /* not a git checkout */ }
  save('meta.json', {
    recorded_at: new Date().toISOString(),
    git_commit: commit,
    compiler: health.compiler,
    flags: health.flags,
    cpu: JSON.parse(fs.readFileSync(path.join(outDir, 'sysinfo.json'), 'utf8')).cpu,
    note: 'Every file in this folder is a recorded response of the C++ backend; nothing was computed in JavaScript.',
  });
  console.log(`recorded ${fs.readdirSync(outDir).length} files into demo/data`);
} finally {
  await fetch(`${base}/api/shutdown`, { method: 'POST' }).catch(() => {});
  await sleep(500);
  app.kill();
}

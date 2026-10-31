// Presentation only: all computation happens in the C++ backend (see src/server, src/core).
'use strict';

const $ = (id) => document.getElementById(id);

// ---------- status bar + job polling ----------
const status = {
  set(text, cls) {
    $('status-text').textContent = text;
    $('statusbar').className = cls || 'ok';
  },
};

async function api(path, options) {
  const r = await fetch(path, options);
  const body = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(body.error || `Request failed (${r.status}).`);
  return body;
}

// Runs a backend job, polling until done; resolves with the result, rejects on failure/cancel.
function pollJob(jobId, label, onBusy) {
  let finished = false;
  const cancelBtn = $('status-cancel');
  cancelBtn.hidden = false;
  cancelBtn.onclick = () => api(`/api/jobs/${jobId}/cancel`, { method: 'POST' }).catch(() => {});
  $('status-progress').hidden = false;
  onBusy(true);
  const finish = () => {
    finished = true;
    cancelBtn.hidden = true;
    $('status-progress').hidden = true;
    onBusy(false);
  };
  return new Promise((resolve, reject) => {
    const tick = async () => {
      if (finished) return;
      try {
        const j = await api(`/api/jobs/${jobId}`);
        $('status-progress').value = j.progress;
        status.set(`${label}: ${Math.round(j.progress * 100)}%`, 'busy');
        if (j.state === 'running') return setTimeout(tick, 100);
        finish();
        if (j.state === 'done') { status.set(`${label}: done`, 'ok'); resolve(j.result); }
        else if (j.state === 'cancelled') { status.set(`${label}: cancelled, no results were kept`, 'err'); reject(new Error('Cancelled. Press Solve to run again.')); }
        else { status.set(`${label}: failed`, 'err'); reject(new Error(j.error || 'The job failed.')); }
      } catch (e) {
        finish();
        status.set('Lost contact with the backend. Restart Launch.exe.', 'err');
        reject(e);
      }
    };
    tick();
  });
}

function download(filename, text, mime) {
  const a = document.createElement('a');
  a.href = URL.createObjectURL(new Blob([text], { type: mime || 'text/csv' }));
  a.download = filename;
  a.click();
  URL.revokeObjectURL(a.href);
}

// ---------- tabs, health, heartbeat, quit ----------
document.querySelectorAll('#tabs button').forEach((b) => {
  b.onclick = () => {
    document.querySelectorAll('#tabs button').forEach((x) => x.classList.toggle('active', x === b));
    document.querySelectorAll('.tab').forEach((t) => t.classList.toggle('active', t.id === `tab-${b.dataset.tab}`));
    window.dispatchEvent(new Event('resize'));  // let Plotly fit its container
  };
});

async function checkHealth() {
  try {
    const h = await api('/api/health');
    status.set(`Backend ready (launch-to-ready ${h.launch_to_ready_ms.toFixed(0)} ms)`, 'ok');
  } catch (e) {
    status.set('Backend not reachable. Close this tab and start Launch.exe again.', 'err');
  }
}
setInterval(() => fetch('/api/heartbeat', { method: 'POST' }).catch(() => {}), 10000);
$('quit').onclick = async () => {
  await fetch('/api/shutdown', { method: 'POST' }).catch(() => {});
  status.set('Workbench stopped. You can close this tab.', 'err');
};

// ---------- Thermal Analysis ----------
const fea = { result: null };
const feaFields = ['length_m', 'area_m2', 'k_W_mK', 'q_W_m3', 't_left_C', 't_right_C'];

// Light client-side check for fast feedback; the C++ backend is the authority.
function readFeaForm() {
  const form = $('fea-form');
  const body = {};
  let ok = true;
  for (const name of feaFields) {
    const input = form.elements[name];
    const v = Number(input.value);
    const positive = ['length_m', 'area_m2', 'k_W_mK'].includes(name);
    const bad = input.value.trim() === '' || !Number.isFinite(v) || (positive && v <= 0);
    input.classList.toggle('invalid', bad);
    if (bad) ok = false;
    body[name] = v;
  }
  const meshInput = form.elements.meshes;
  const meshes = meshInput.value.split(',').map((s) => s.trim()).filter(Boolean).map(Number);
  const badMesh = meshes.length === 0 || meshes.some((n) => !Number.isInteger(n) || n < 1);
  meshInput.classList.toggle('invalid', badMesh);
  body.meshes = meshes;
  return ok && !badMesh ? body : null;
}

function setFeaBusy(busy) {
  ['fea-solve'].forEach((id) => ($(id).disabled = busy));
}

function showFeaMessage(text, isError) {
  $('fea-msg').textContent = text;
  $('fea-msg').className = 'msg' + (isError ? ' err' : '');
}

function renderFea(res) {
  const sols = res.solutions;
  const traces = [];
  const first = sols[sols.length - 1];
  traces.push({ x: first.x_m, y: first.T_exact_C, name: 'exact', mode: 'lines', line: { color: 'black', dash: 'dash' } });
  sols.forEach((s) => traces.push({ x: s.x_m, y: s.T_fe_C, name: `FE N=${s.N}`, mode: s.N <= 20 ? 'lines+markers' : 'lines' }));
  Plotly.newPlot('fea-plot', traces, {
    title: 'Temperature along the rod', xaxis: { title: 'x (m)' }, yaxis: { title: 'T (°C)' }, margin: { t: 40 },
  }, { responsive: true });

  // Error at nodes is round-off; the meaningful error is the FE interpolant between nodes.
  Plotly.newPlot('fea-err-plot', sols.map((s) => ({ x: s.x_m, y: s.abs_err_C, name: `N=${s.N}`, mode: 'lines' })), {
    title: 'Absolute error at nodes (°C)', xaxis: { title: 'x (m)' }, yaxis: { title: '|T_FE − T_exact| at nodes (°C)', type: 'log' }, margin: { t: 40 },
  }, { responsive: true });

  const conv = res.convergence;
  Plotly.newPlot('fea-conv-plot', [{
    x: conv.map((r) => r.h_m), y: conv.map((r) => r.max_midpoint_err_C), mode: 'lines+markers', name: 'max midpoint error',
  }], {
    title: 'Mesh convergence', xaxis: { title: 'h (m)', type: 'log' }, yaxis: { title: 'max midpoint error (°C)', type: 'log' }, margin: { t: 40 },
  }, { responsive: true });

  const fmt = (v, p) => (v === null ? '–' : Number(v).toPrecision(p));
  $('fea-table').innerHTML = '<tr><th>N</th><th>h (m)</th><th>max nodal error (°C)</th><th>max midpoint error (°C)</th><th>observed order</th></tr>' +
    conv.map((r) => `<tr><td>${r.N}</td><td>${r.h_m}</td><td>${r.max_nodal_err_C.toExponential(2)}</td><td>${fmt(r.max_midpoint_err_C, 8)}</td><td>${fmt(r.observed_order, 4)}</td></tr>`).join('');

  const sel = $('fea-export-n');
  sel.innerHTML = sols.map((s) => `<option value="${s.N}">${s.N}</option>`).join('');
  ['fea-export-conv', 'fea-export-n', 'fea-export-sol'].forEach((id) => ($(id).disabled = false));
}

$('fea-solve').onclick = async () => {
  const body = readFeaForm();
  if (!body) return showFeaMessage('Some inputs are invalid (highlighted). L, A, k must be > 0; meshes must be whole numbers >= 1.', true);
  showFeaMessage('');
  try {
    const { job_id } = await api('/api/fea/solve', { method: 'POST', body: JSON.stringify(body) });
    fea.result = await pollJob(job_id, 'FEA solve', setFeaBusy);
    renderFea(fea.result);
    showFeaMessage(`Solved ${fea.result.solutions.length} meshes.`);
  } catch (e) {
    showFeaMessage(e.message, true);
  }
};
$('fea-export-conv').onclick = () => download('fea_convergence.csv', fea.result.csv.convergence);
$('fea-export-sol').onclick = () => {
  const n = $('fea-export-n').value;
  download(`fea_N${n}.csv`, fea.result.csv[`N${n}`]);
};

// ---------- Performance ----------
const perf = { result: null };

function readPerfForm() {
  const form = $('perf-form');
  const body = {};
  let ok = true;
  for (const name of ['seed', 'cases', 'mesh', 'max_workers']) {
    const input = form.elements[name];
    const v = Number(input.value);
    const bad = input.value.trim() === '' || !Number.isInteger(v) || v < (name === 'seed' ? 0 : 1);
    input.classList.toggle('invalid', bad);
    if (bad) ok = false;
    body[name] = v;
  }
  return ok ? body : null;
}

function renderPerf(r) {
  const par = r.rows.filter((x) => x.mode === 'parallel');
  $('perf-table').innerHTML = '<tr><th>mode</th><th>workers</th><th>median (ms)</th><th>speedup T1/Tp</th><th>efficiency</th><th>speedup vs serial</th><th>max |ΔT| vs serial (°C)</th></tr>' +
    r.rows.map((x) => `<tr><td>${x.mode}</td><td>${x.workers}</td><td>${(x.median_s * 1000).toFixed(2)}</td>` +
      (x.mode === 'parallel'
        ? `<td>${x.speedup.toFixed(3)}</td><td>${x.efficiency.toFixed(3)}</td><td>${x.speedup_vs_serial.toFixed(3)}</td>`
        : '<td>–</td><td>–</td><td>–</td>') + `<td>${x.max_abs_diff_C.toExponential(2)}</td></tr>`).join('');
  Plotly.newPlot('perf-plot', [
    { x: par.map((x) => x.workers), y: par.map((x) => x.workers), mode: 'lines', name: 'ideal (= p)', line: { dash: 'dash', color: 'black' } },
    { x: par.map((x) => x.workers), y: par.map((x) => x.speedup), mode: 'lines+markers', name: 'measured' },
  ], { title: 'Speedup T1/Tp', xaxis: { title: 'workers p (threads)', dtick: 1 }, yaxis: { title: 'speedup T1/Tp (dimensionless)' }, margin: { t: 40 } }, { responsive: true });
  const s = r.system;
  let msg = `${s.cpu}: ${s.physical_cores} physical / ${s.logical_cores} logical cores, ${(s.ram_bytes / 2 ** 30).toFixed(2)} GiB RAM. ` +
    `${r.build.compiler}, flags ${r.build.flags}. Serial/parallel agreement: max |ΔT| = ${r.max_abs_diff_C.toExponential(2)} °C (${r.agreement_ok ? 'PASS' : 'FAIL'}, limit 1e-8).`;
  if (r.note) msg += ' ' + r.note;
  $('perf-msg').textContent = msg;
  $('perf-msg').className = 'msg' + (r.agreement_ok ? '' : ' err');
  $('perf-export-csv').disabled = $('perf-export-sum').disabled = false;
}

$('perf-run').onclick = async () => {
  const body = readPerfForm();
  if (!body) return ($('perf-msg').textContent = 'Seed must be a whole number >= 0; cases, mesh and max workers must be whole numbers >= 1.', $('perf-msg').className = 'msg err');
  $('perf-msg').textContent = '';
  try {
    const { job_id } = await api('/api/perf/run', { method: 'POST', body: JSON.stringify(body) });
    perf.result = await pollJob(job_id, 'Benchmark', (busy) => ($('perf-run').disabled = busy));
    renderPerf(perf.result);
  } catch (e) {
    $('perf-msg').textContent = e.message;
    $('perf-msg').className = 'msg err';
  }
};
$('perf-export-csv').onclick = () => download('perf.csv', perf.result.csv.perf);
$('perf-export-sum').onclick = () => download('sysinfo.txt', perf.result.csv.summary, 'text/plain');
api('/api/sysinfo').then((s) => {
  $('perf-sys').textContent = `This machine: ${s.cpu}, ${s.physical_cores} physical / ${s.logical_cores} logical cores, ${(s.ram_bytes / 2 ** 30).toFixed(2)} GiB RAM.`;
}).catch(() => {});

// ---------- Backpropagation panel ----------
const bp = { result: null };
$('bp-run').onclick = async () => {
  try {
    bp.result = await api('/api/backprop');
    const r = bp.result;
    const f = (v) => Number(v).toFixed(7);
    $('bp-table').innerHTML = '<tr><th>parameter</th><th>analytic dJ/dp</th><th>numeric (central diff)</th><th>|error|</th><th>pass (≤ 1e-5)</th><th>after one update</th></tr>' +
      r.gradcheck.map((c) => `<tr><td>${c.parameter}</td><td>${f(c.analytic)}</td><td>${f(c.numeric)}</td><td>${c.abs_error.toExponential(2)}</td><td>${c.pass ? 'yes' : 'NO'}</td><td>${f(r.updated[c.parameter])}</td></tr>`).join('');
    Plotly.newPlot('bp-plot', [{ x: r.loss.map((_, i) => i), y: r.loss, mode: 'lines', name: 'J' }], {
      title: `Loss vs training step (lr = 0.1); J(initial) = ${f(r.forward.loss)}`,
      xaxis: { title: 'training step (step 1 = first update)' }, yaxis: { title: 'loss J (dimensionless)' }, margin: { t: 40 },
    }, { responsive: true });
    $('bp-msg').textContent = '';
    $('bp-msg').className = 'msg';
    $('bp-export-loss').disabled = $('bp-export-check').disabled = false;
  } catch (e) {
    $('bp-msg').textContent = e.message;
    $('bp-msg').className = 'msg err';
  }
};
$('bp-export-loss').onclick = () => download('backprop_loss.csv', bp.result.csv.loss);
$('bp-export-check').onclick = () => download('backprop_gradcheck.csv', bp.result.csv.gradcheck);

checkHealth();

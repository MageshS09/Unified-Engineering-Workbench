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

// ---------- Geometry: STL mesh and CSV point cloud ----------
const MAX_FILE_BYTES = 200 * 1024 * 1024;
const geo = { stl: null, csv: null };
const DEFAULT_CAMERA = { eye: { x: 1.25, y: 1.25, z: 1.25 }, center: { x: 0, y: 0, z: 0 }, up: { x: 0, y: 0, z: 1 } };

function sceneLayout(title) {
  const axis = (t) => ({ title: t, autorange: true });
  return {
    title, margin: { t: 40, l: 0, r: 0, b: 0 },
    scene: { aspectmode: 'data', camera: DEFAULT_CAMERA, xaxis: axis('x (m)'), yaxis: axis('y (m)'), zaxis: axis('z (m)') },
  };
}

// Reset camera and axis ranges so the whole model is visible.
function fitToView(div) {
  Plotly.relayout(div, {
    'scene.camera': DEFAULT_CAMERA, 'scene.xaxis.autorange': true, 'scene.yaxis.autorange': true, 'scene.zaxis.autorange': true,
  });
}

function showGeoMessage(id, text, isError) {
  $(id).textContent = text;
  $(id).className = 'msg' + (isError ? ' err' : '');
}

function statsTable(rows) {
  return rows.map(([k, v]) => `<tr><th style="text-align:left">${k}</th><td>${v}</td></tr>`).join('');
}

const fmt3 = (a) => a.map((v) => Number(v).toPrecision(5)).join(', ');

// Sends a file (or asks for a bundled sample) to the C++ parser; returns the parsed JSON.
async function loadGeometry(kind, file) {
  showGeoMessage(`${kind}-msg`, '');
  if (file && file.size > MAX_FILE_BYTES) {
    throw new Error(`The file is ${(file.size / 1048576).toFixed(1)} MB, above the 200 MB limit. Split or decimate the data and load a smaller file.`);
  }
  status.set(`Parsing ${file ? file.name : 'sample'} in the C++ backend...`, 'busy');
  const url = file ? `/api/geometry/${kind}` : `/api/geometry/sample/${kind}`;
  const opts = file ? { method: 'POST', body: file, headers: { 'Content-Type': 'application/octet-stream', 'X-Filename': encodeURIComponent(file.name) } } : undefined;
  const r = await api(url, opts);
  status.set('Backend ready', 'ok');
  return r;
}

function renderStl(r) {
  geo.stl = r;
  const traces = [{ type: 'mesh3d', x: r.x, y: r.y, z: r.z, i: r.i, j: r.j, k: r.k, color: '#8fb4d9', flatshading: true, name: 'surface' }];
  if (r.triangle_count <= 5000) {  // wireframe edges only for small models (keeps big meshes fast)
    const ex = [], ey = [], ez = [];
    for (let t = 0; t < r.i.length; t++) {
      for (const v of [r.i[t], r.j[t], r.k[t], r.i[t], null]) {
        ex.push(v === null ? null : r.x[v]); ey.push(v === null ? null : r.y[v]); ez.push(v === null ? null : r.z[v]);
      }
    }
    traces.push({ type: 'scatter3d', mode: 'lines', x: ex, y: ey, z: ez, line: { color: '#1f3a5f', width: 2 }, hoverinfo: 'skip', name: 'edges' });
  }
  Plotly.newPlot('stl-plot', traces, sceneLayout(`${r.name || 'STL'} (${r.triangle_count} triangles)`), { responsive: true });
  $('stl-stats').innerHTML = statsTable([
    ['Source', r.source], ['Solid name', r.name || '(unnamed)'], ['Triangles', r.triangle_count], ['Unique vertices', r.vertex_count],
    ['Bounding box min (m)', fmt3(r.bbox.min)], ['Bounding box max (m)', fmt3(r.bbox.max)], ['Units', r.units],
  ]);
  $('stl-fit').disabled = $('stl-export').disabled = false;
}

function renderCsv(r) {
  geo.csv = r;
  const trace = {
    type: 'scatter3d', mode: 'markers', x: r.x, y: r.y, z: r.z, customdata: r.node_id.map((id, n) => [id, r.T[n]]),
    marker: {
      size: 3, color: r.T, colorscale: 'Jet', cmin: r.t_min_C, cmax: r.t_max_C,
      colorbar: { title: 'Temperature (°C)' },
    },
    hovertemplate: 'node_id %{customdata[0]}<br>x %{x:.5g} m<br>y %{y:.5g} m<br>z %{z:.5g} m<br>T %{customdata[1]:.4g} °C<extra></extra>',
  };
  Plotly.newPlot('csv-plot', [trace], sceneLayout('Nodal temperature (°C)'), { responsive: true });
  $('csv-plot').on('plotly_click', (ev) => {
    const p = ev.points[0];
    showGeoMessage('node-info', `Selected node ${p.customdata[0]}: x = ${p.x} m, y = ${p.y} m, z = ${p.z} m, T = ${Number(geo.csv.T[p.pointNumber]).toFixed(4)} °C`);
  });
  const shown = r.display_stride > 1 ? ` (showing every ${r.display_stride}th node: ${r.displayed_count} of ${r.node_count})` : '';
  $('csv-stats').innerHTML = statsTable([
    ['Source', r.source], ['Nodes', `${r.node_count}${shown}`], ['Bounding box min (m)', fmt3(r.bbox.min)],
    ['Bounding box max (m)', fmt3(r.bbox.max)], ['Temperature range (°C)', `${r.t_min_C.toPrecision(5)} to ${r.t_max_C.toPrecision(5)}`],
  ]);
  $('csv-fit').disabled = $('csv-export').disabled = $('node-lookup').disabled = false;
}

function wireGeometry(kind, render) {
  const handle = async (file) => {
    try { render(await loadGeometry(kind, file)); } catch (e) {
      status.set('Could not load the file', 'err');
      showGeoMessage(`${kind}-msg`, e.message, true);
    }
  };
  $(`${kind}-file`).onchange = (ev) => ev.target.files[0] && handle(ev.target.files[0]);
  $(`${kind}-sample`).onclick = () => handle(null);
  $(`${kind}-fit`).onclick = () => fitToView(`${kind}-plot`);
  $(`${kind}-export`).onclick = () => {
    const { x, y, z, i, j, k, node_id, T, ...summary } = geo[kind];  // drop the bulk arrays
    download(`${kind}_summary.json`, JSON.stringify(summary, null, 2), 'application/json');
  };
}
wireGeometry('stl', renderStl);
wireGeometry('csv', renderCsv);

$('node-lookup').onclick = async () => {
  try {
    const n = await api(`/api/geometry/node/${encodeURIComponent($('node-id').value.trim())}`);
    showGeoMessage('node-info', `Node ${n.node_id}: x = ${n.x_m} m, y = ${n.y_m} m, z = ${n.z_m} m, T = ${n.temperature_C} °C`);
  } catch (e) { showGeoMessage('node-info', e.message, true); }
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

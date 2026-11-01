// Presentation only: all computation happens in the C++ backend (see src/server, src/core).
'use strict';

const $ = (id) => document.getElementById(id);

// ---------- status bar + job polling ----------
const status = {
  timer: null,
  idleText: 'Backend ready',  // replaced by checkHealth in the static demo
  set(text, cls) {
    clearTimeout(this.timer);
    $('status-text').textContent = text;
    $('statusbar').className = cls || 'ok';
  },
  // Show a finished-job message, then fall back to the idle text so stale errors do not linger.
  setThenIdle(text, cls, idleMs = 8000) {
    this.set(text, cls);
    this.timer = setTimeout(() => this.set(this.idleText, 'ok'), idleMs);
  },
};

const BACKEND_GONE = 'Cannot reach the workbench backend. Launch.exe may have been closed: start it again and reload this page.';

async function api(path, options) {
  let r;
  try {
    r = await fetch(path, options);
  } catch (e) {
    throw new Error(BACKEND_GONE);
  }
  const body = await r.json().catch(() => ({}));
  if (!r.ok) throw new Error(body.error || (r.status === 413 ? 'The upload is too large for the backend. Use a file under 200 MB.' : `Request failed (HTTP ${r.status}). Reload the page and try again.`));
  return body;
}

// Only one backend job runs at a time: every button marked .jobbtn is disabled while it runs.
function setJobButtonsDisabled(disabled) {
  document.querySelectorAll('.jobbtn').forEach((b) => { b.disabled = disabled; });
}

// Runs a backend job, polling until done; resolves with the result, rejects on failure/cancel.
function pollJob(jobId, label, onBusy) {
  let finished = false;
  const cancelBtn = $('status-cancel');
  cancelBtn.hidden = false;
  cancelBtn.disabled = false;
  cancelBtn.onclick = () => {
    cancelBtn.disabled = true;  // one request is enough; the job stops between solves
    status.set(`${label}: cancelling...`, 'busy');
    api(`/api/jobs/${jobId}/cancel`, { method: 'POST' }).catch(() => {});
  };
  $('status-progress').hidden = false;
  setJobButtonsDisabled(true);
  onBusy(true);
  const finish = () => {
    finished = true;
    cancelBtn.hidden = true;
    $('status-progress').hidden = true;
    setJobButtonsDisabled(false);
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
        if (j.state === 'done') { status.setThenIdle(`${label}: done`, 'ok'); resolve(j.result); }
        else if (j.state === 'cancelled') { status.setThenIdle(`${label}: cancelled, no results were kept`, 'err'); reject(new Error('Cancelled: no partial results were kept. Start the run again when ready.')); }
        else { status.setThenIdle(`${label}: failed`, 'err', 15000); reject(new Error(j.error || 'The job failed.')); }
      } catch (e) {
        finish();
        status.set(BACKEND_GONE, 'err');
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
    if (h.demo) {
      status.idleText = h.status_text;
      status.set(h.status_text, 'ok');
    } else {
      status.set(`Backend ready (launch-to-ready ${h.launch_to_ready_ms.toFixed(0)} ms)`, 'ok');
    }
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

const feaFieldLabels = {
  length_m: 'Length L (m)', area_m2: 'Area A (m²)', k_W_mK: 'Conductivity k (W/(m·K))',
  q_W_m3: 'Heat generation q (W/m³)', t_left_C: 'T(0) (°C)', t_right_C: 'T(L) (°C)',
};

// Light client-side check for fast feedback; the C++ backend stays the authority.
// Returns { body } when valid, or { problems: [...] } with one actionable sentence per bad field.
function readFeaForm() {
  const form = $('fea-form');
  const body = {};
  const problems = [];
  for (const name of feaFields) {
    const input = form.elements[name];
    const v = Number(input.value);
    const positive = ['length_m', 'area_m2', 'k_W_mK'].includes(name);
    let bad = false;
    if (input.value.trim() === '' || !Number.isFinite(v)) {
      bad = true;
      problems.push(`${feaFieldLabels[name]} must be a number (for example 1 or 1e-4).`);
    } else if (positive && v <= 0) {
      bad = true;
      problems.push(`${feaFieldLabels[name]} must be greater than 0.`);
    }
    input.classList.toggle('invalid', bad);
    body[name] = v;
  }
  const meshInput = form.elements.meshes;
  const meshes = meshInput.value.split(',').map((s) => s.trim()).filter(Boolean).map(Number);
  const badMesh = meshes.length === 0 || meshes.some((n) => !Number.isInteger(n) || n < 1);
  if (badMesh) problems.push('Meshes must be a comma-separated list of whole numbers >= 1, for example 10,20,40,80.');
  meshInput.classList.toggle('invalid', badMesh);
  body.meshes = meshes;
  return problems.length ? { problems } : { body };
}

function setFeaBusy() {}  // buttons are handled globally by pollJob (.jobbtn)

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

  // Error at the nodes is only round-off (see table); the meaningful error is that of the FE
  // interpolant between nodes, so plot it at the element midpoints.
  Plotly.newPlot('fea-err-plot', sols.map((s) => ({ x: s.x_mid_m, y: s.midpoint_err_C, name: `N=${s.N}`, mode: 'lines+markers' })), {
    title: 'Absolute error of the FE interpolant at element midpoints',
    xaxis: { title: 'x (m)' }, yaxis: { title: '|T_FE − T_exact| at midpoints (°C)' }, margin: { t: 40 },
  }, { responsive: true });

  const conv = res.convergence;
  // Dashed reference line with slope 2 (error proportional to h^2), anchored at the coarsest mesh.
  const hs = conv.map((r) => r.h_m);
  const ref = conv.map((r) => conv[0].max_midpoint_err_C * (r.h_m / conv[0].h_m) ** 2);
  Plotly.newPlot('fea-conv-plot', [
    { x: hs, y: conv.map((r) => r.max_midpoint_err_C), mode: 'lines+markers', name: 'max midpoint error' },
    { x: hs, y: ref, mode: 'lines', name: 'slope 2 reference (∝ h²)', line: { dash: 'dash', color: 'gray' } },
  ], {
    title: 'Mesh convergence (log-log)',
    xaxis: { title: 'h (m)', type: 'log', tickvals: hs, ticktext: hs.map((h) => String(h)) },
    yaxis: { title: 'max midpoint error (°C)', type: 'log', dtick: 1, exponentformat: 'power' }, margin: { t: 40 },
  }, { responsive: true });

  const fmt = (v, p) => (v === null ? '–' : Number(v).toPrecision(p));
  $('fea-table').innerHTML = '<tr><th>N</th><th>h (m)</th><th>max nodal error (°C)</th><th>max midpoint error (°C)</th><th>observed order</th></tr>' +
    conv.map((r) => `<tr><td>${r.N}</td><td>${r.h_m}</td><td>${r.max_nodal_err_C.toExponential(2)}</td><td>${fmt(r.max_midpoint_err_C, 8)}</td><td>${fmt(r.observed_order, 4)}</td></tr>`).join('');

  const sel = $('fea-export-n');
  sel.innerHTML = sols.map((s) => `<option value="${s.N}">${s.N}</option>`).join('');
  ['fea-export-conv', 'fea-export-json', 'fea-export-n', 'fea-export-sol'].forEach((id) => ($(id).disabled = false));
}

$('fea-solve').onclick = async () => {
  const { body, problems } = readFeaForm();
  if (problems) return showFeaMessage(`Cannot solve yet:\n${problems.map((p) => `- ${p}`).join('\n')}`, true);
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
$('fea-export-json').onclick = () => {
  const { csv, ...data } = fea.result;  // CSV text is exported separately
  download('fea_results.json', JSON.stringify(data, null, 2), 'application/json');
};
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
  status.set(status.idleText, 'ok');
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
      colorbar: { title: { text: 'Temperature (°C)', side: 'right' } },
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

// ---------- Hardware (all devices are SIMULATED by the backend) ----------
const hw = { log: [] };

function hwLog(text) {
  hw.log.push({ time: new Date().toISOString(), event: text });
  $('hw-log').textContent = hw.log.map((e) => `${e.time}  ${e.event}`).join('\n');
  $('hw-export-log').disabled = false;
}

function hwMessage(text, isError) {
  $('hw-msg').textContent = text;
  $('hw-msg').className = 'msg' + (isError ? ' err' : '');
}

async function hwRefresh() {
  const r = await api('/api/devices');
  $('hw-fault').checked = r.pending_timeouts > 0;
  $('hw-table').innerHTML = '<tr><th>Device</th><th>Represents (assumed)</th><th>State</th><th>Actions</th></tr>' +
    r.devices.map((d) => `<tr><td><span class="badge">SIMULATED</span>${d.name}<br><small>${d.id} / ${d.kind}</small></td>` +
      `<td>${d.board_class}</td><td class="state-${d.state}">${d.state}</td><td>` +
      ['connect', 'retry', 'disconnect', 'telemetry', 'caps'].map((a) => `<button data-hw="${a}" data-id="${d.id}">${{
        connect: 'Connect', retry: 'Connect with retry', disconnect: 'Disconnect', telemetry: 'Read telemetry', caps: 'Capabilities',
      }[a]}</button>`).join(' ') + '</td></tr>').join('');
  document.querySelectorAll('#hw-table button').forEach((b) => (b.onclick = () => hwAction(b.dataset.hw, b.dataset.id)));
}

async function hwAction(action, id) {
  try {
    if (action === 'caps') {
      $('hw-caps').textContent = JSON.stringify(await api(`/api/devices/${id}/capabilities`), null, 2);
    } else if (action === 'telemetry') {
      const t = await api(`/api/devices/${id}/telemetry`);
      $('hw-telemetry').textContent = JSON.stringify(t, null, 2);
      hw.lastTelemetry = t;
      $('hw-export-telemetry').disabled = false;
      hwLog(`${id}: telemetry seq ${t.seq} (SIMULATED)`);
    } else if (action === 'disconnect') {
      await api(`/api/devices/${id}/disconnect`, { method: 'POST' });
      hwLog(`${id}: disconnected`);
    } else {
      const r = await api(`/api/devices/${id}/connect`, { method: 'POST', body: JSON.stringify({ retry: action === 'retry' }) });
      if (r.attempts.length === 0) hwLog(`${id}: connected`);
      r.attempts.forEach((a) => hwLog(`${id}: attempt ${a.attempt} ${a.ok ? 'OK' : 'FAILED'} - ${a.message}` + (a.backoff_ms_after ? ` (waiting ${a.backoff_ms_after} ms before retry)` : '')));
      if (!r.connected) hwMessage(`${id} could not connect after ${r.attempts.length} attempts. Clear the fault or retry later.`, true);
    }
    if (action !== 'caps' && action !== 'telemetry') hwMessage('');
  } catch (e) {
    hwLog(`${id}: ${action} failed - ${e.message}`);
    hwMessage(e.message, true);
  }
  await hwRefresh().catch(() => {});
}

$('hw-refresh').onclick = () => hwRefresh().then(() => hwLog('discovery: 3 simulated devices')).catch((e) => hwMessage(e.message, true));
$('hw-fault').onchange = async (ev) => {
  try {
    await api('/api/devices/fault', { method: 'POST', body: JSON.stringify({ simulate_timeout: ev.target.checked }) });
    hwLog(ev.target.checked ? 'fault injection ON: next 2 connect attempts will time out (SIMULATED)' : 'fault injection OFF');
    await hwRefresh();
  } catch (e) { hwMessage(e.message, true); }
};
$('hw-export-telemetry').onclick = () => download('hardware_telemetry.json', JSON.stringify(hw.lastTelemetry, null, 2), 'application/json');
$('hw-export-log').onclick = () => download('hardware_event_log.json', JSON.stringify({ label: 'SIMULATED', events: hw.log }, null, 2), 'application/json');
hwRefresh().catch(() => {});

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

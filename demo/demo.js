// Static-demo shim (used only on the Vercel deployment, never by Launch.exe).
// The page normally calls the C++ backend on 127.0.0.1. Here /api/* is answered from responses that were
// RECORDED from that backend (demo-data/*.json, see scripts/record_demo.mjs). Nothing is computed in
// JavaScript: requests that would need the live backend get an explicit "needs the real app" message.
(() => {
  'use strict';
  const realFetch = window.fetch.bind(window);
  const cache = {};
  const load = async (name) => (cache[name] ??= await (await realFetch(`demo-data/${name}`)).json());
  const reply = (body, status = 200) =>
    new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json' } });
  const needsRealApp = (what) =>
    reply({ error: `Static demo: ${what} needs the live C++ backend. Download the project and double-click Launch.exe to use it.` }, 409);

  // Only the default cases were recorded, so only identical requests can be replayed.
  const sameNumbers = (body, expected) => Object.entries(expected).every(([k, v]) =>
    Array.isArray(v) ? JSON.stringify(body[k]) === JSON.stringify(v) : Number(body[k]) === v);
  const FEA_DEFAULT = { length_m: 1, area_m2: 1e-4, k_W_mK: 10, q_W_m3: 10000, t_left_C: 100, t_right_C: 20, meshes: [10, 20, 40, 80] };
  const PERF_DEFAULT = { seed: 42, cases: 400, mesh: 2000, max_workers: 4 };

  async function route(method, path, init) {
    const body = () => { try { return JSON.parse((init && init.body) || '{}'); } catch { return {}; } };
    if (method === 'GET' && path === '/api/health') {
      const h = await load('health.json');
      const m = await load('meta.json');
      return reply({ ...h, demo: true, status_text: `Static demo: replaying results recorded from the C++ backend (${m.compiler}, ${m.flags})` });
    }
    if (path === '/api/heartbeat') return reply({});
    if (path === '/api/shutdown') return needsRealApp('stopping the backend');
    if (method === 'GET' && path === '/api/sysinfo') return reply(await load('sysinfo.json'));
    if (method === 'GET' && path === '/api/backprop') return reply(await load('backprop.json'));

    if (method === 'POST' && path === '/api/fea/solve')
      return sameNumbers(body(), FEA_DEFAULT) ? reply({ job_id: 'demo-fea' }) : needsRealApp('solving with custom parameters or meshes (only the default case is recorded)');
    if (method === 'POST' && path === '/api/perf/run')
      return sameNumbers(body(), PERF_DEFAULT) ? reply({ job_id: 'demo-perf' }) : needsRealApp('running the benchmark with other settings (only the default run is recorded)');
    if (method === 'GET' && path === '/api/jobs/demo-fea') return reply({ state: 'done', progress: 1, result: await load('fea.json') });
    if (method === 'GET' && path === '/api/jobs/demo-perf') return reply({ state: 'done', progress: 1, result: await load('perf.json') });
    if (path.startsWith('/api/jobs/') && path.endsWith('/cancel')) return reply({});

    if (method === 'GET' && path === '/api/geometry/sample/stl') return reply(await load('stl_sample.json'));
    if (method === 'GET' && path === '/api/geometry/sample/csv') return reply(await load('csv_sample.json'));
    if (method === 'POST' && path.startsWith('/api/geometry/')) return needsRealApp('parsing an uploaded file (the demo only has the two bundled samples)');
    if (method === 'GET' && path.startsWith('/api/geometry/node/')) {
      const nodes = await load('nodes.json');
      const id = decodeURIComponent(path.split('/').pop());
      return nodes[id] ? reply(nodes[id]) : reply({ error: `Static demo: node_id "${id}" is not in the recorded sample (1 to 1331).` }, 404);
    }

    if (method === 'GET' && path === '/api/devices') return reply(await load('devices.json'));
    const caps = /^\/api\/devices\/([^/]+)\/capabilities$/.exec(path);
    if (method === 'GET' && caps) {
      const all = await load('capabilities.json');
      return all[caps[1]] ? reply(all[caps[1]]) : reply({ error: `Unknown device id '${caps[1]}'.` }, 404);
    }
    if (path.startsWith('/api/devices')) return needsRealApp('connecting the simulated devices (see the recorded session below)');
    return reply({ error: `Static demo: ${path} is not available.` }, 404);
  }

  window.fetch = (input, init) => {
    const url = typeof input === 'string' ? input : input.url;
    const path = new URL(url, window.location.href).pathname;
    if (!path.startsWith('/api/')) return realFetch(input, init);
    return route(((init && init.method) || 'GET').toUpperCase(), path, init);
  };

  document.addEventListener('DOMContentLoaded', async () => {
    const quit = document.getElementById('quit');
    if (quit) quit.hidden = true;  // nothing to stop in the demo
    const meta = await load('meta.json').catch(() => null);
    const metaSpan = document.getElementById('demo-meta');
    if (meta && metaSpan) metaSpan.textContent = `recorded ${meta.recorded_at.slice(0, 10)} on ${meta.cpu}, ${meta.compiler}, flags ${meta.flags}, commit ${meta.git_commit}`;
    // Show the recorded hardware session under the Hardware tab, since live connect/telemetry is not possible here.
    const hw = document.getElementById('tab-hardware');
    const session = await load('hardware_session.json').catch(() => null);
    if (hw && session) {
      hw.insertAdjacentHTML('beforeend', '<h4>Recorded session (real responses of the C++ simulated adapters)</h4>' +
        '<p class="note">Steps: inject a timeout fault, connect the JTAG adapter with retry and backoff (attempts 1 and 2 time out, attempt 3 succeeds), read telemetry, disconnect.</p><pre id="hw-recorded" class="out"></pre>');
      document.getElementById('hw-recorded').textContent = JSON.stringify(session, null, 2);
    }
  });
})();

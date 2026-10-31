// Presentation only: all computation happens in the C++ backend.
async function checkHealth() {
  const bar = document.getElementById('statusbar');
  try {
    const r = await fetch('/api/health');
    const h = await r.json();
    bar.textContent = `Backend ready (launch-to-ready ${h.launch_to_ready_ms.toFixed(0)} ms)`;
    bar.className = 'ok';
  } catch (e) {
    bar.textContent = 'Backend not reachable. Close this tab and start Launch.exe again.';
    bar.className = 'err';
  }
}
setInterval(() => fetch('/api/heartbeat', { method: 'POST' }).catch(() => {}), 10000);
checkHealth();

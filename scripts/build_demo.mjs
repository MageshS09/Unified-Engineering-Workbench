// Assembles the static demo site into demo-dist/ (run by Vercel as the build command, or by hand).
// It is a copy-and-patch step only: the page (app/web) is the same one Launch.exe serves, plus the
// recorded backend responses (demo/data) and the replay shim (demo/demo.js).
//
//   node scripts/build_demo.mjs
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const out = path.join(repo, 'demo-dist');

fs.rmSync(out, { recursive: true, force: true });
fs.cpSync(path.join(repo, 'app', 'web'), out, { recursive: true });
fs.cpSync(path.join(repo, 'demo', 'data'), path.join(out, 'demo-data'), { recursive: true });
fs.copyFileSync(path.join(repo, 'demo', 'demo.js'), path.join(out, 'demo.js'));
for (const doc of ['report.pdf']) {
  const from = path.join(repo, 'docs', doc);
  if (fs.existsSync(from)) fs.copyFileSync(from, path.join(out, doc));
}

const banner = `<div class="banner" style="margin:0;border-radius:0;border-width:0 0 1px 0">
  <strong>Static demo.</strong> This page replays results <em>recorded from the real C++ backend</em> (<span id="demo-meta">loading...</span>).
  Custom inputs, file uploads and live device simulation need the real app: download the project from the
  <a href="https://github.com/MageshS09/Unified-Engineering-Workbench">repository</a> and double-click <code>Launch.exe</code> (Windows 11 x64).
  <a href="report.pdf">Technical report (PDF)</a>.
</div>`;

const indexPath = path.join(out, 'index.html');
let html = fs.readFileSync(indexPath, 'utf8');
if (!html.includes('<script src="app.js"></script>') || !html.includes('</header>')) throw new Error('index.html changed: patch points not found');
html = html.replace('</header>', `</header>\n${banner}`)
  .replace('<script src="app.js"></script>', '<script src="demo.js"></script>\n<script src="app.js"></script>')
  .replace('<title>Unified Engineering Workbench</title>', '<title>Unified Engineering Workbench (static demo)</title>');
fs.writeFileSync(indexPath, html);
console.log(`demo-dist ready: ${fs.readdirSync(out).length} entries`);

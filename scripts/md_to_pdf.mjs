// Documentation helper: converts docs/report.md to docs/report.pdf with headless Edge/Chrome.
// Supports the small Markdown subset used by the report (headings, paragraphs, lists, tables,
// fenced code, images, **bold**, *italic*, `code`). Needs Node >= 18 and Edge or Chrome (BROWSER_PATH overrides).
//
//   node scripts/md_to_pdf.mjs [input.md] [output.pdf]
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repo = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const input = path.resolve(process.argv[2] || path.join(repo, 'docs', 'report.md'));
const output = path.resolve(process.argv[3] || input.replace(/\.md$/, '.pdf'));

const esc = (s) => s.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

function inline(s) {
  return esc(s)
    .replace(/`([^`]+)`/g, '<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>')
    .replace(/(^|[^*])\*([^*\s][^*]*)\*/g, '$1<em>$2</em>')
    .replace(/\[([^\]]+)\]\(([^)]+)\)/g, '<span class="link">$1</span>');
}

function imageUrl(src) {
  return pathToFileURL(path.resolve(path.dirname(input), src)).href;
}

function convert(md) {
  const lines = md.replace(/\r/g, '').split('\n');
  const out = [];
  let i = 0;
  while (i < lines.length) {
    const line = lines[i];
    if (/^```/.test(line)) {  // fenced code
      const code = [];
      i++;
      while (i < lines.length && !/^```/.test(lines[i])) code.push(lines[i++]);
      i++;
      out.push(`<pre>${esc(code.join('\n'))}</pre>`);
    } else if (/^#{1,3}\s/.test(line)) {
      const level = line.match(/^#+/)[0].length;
      out.push(`<h${level}>${inline(line.replace(/^#+\s*/, ''))}</h${level}>`);
      i++;
    } else if (/^\|/.test(line) && /^\|[\s:|-]+\|?\s*$/.test(lines[i + 1] || '')) {  // table
      const row = (l) => l.trim().replace(/^\||\|$/g, '').split('|').map((c) => c.trim());
      const head = row(line);
      i += 2;
      const body = [];
      while (i < lines.length && /^\|/.test(lines[i])) body.push(row(lines[i++]));
      out.push('<table><thead><tr>' + head.map((c) => `<th>${inline(c)}</th>`).join('') + '</tr></thead><tbody>' +
        body.map((r) => '<tr>' + r.map((c) => `<td>${inline(c)}</td>`).join('') + '</tr>').join('') + '</tbody></table>');
    } else if (/^\s*([-*]|\d+\.)\s/.test(line)) {  // list
      const ordered = /^\s*\d+\./.test(line);
      const items = [];
      while (i < lines.length && /^\s*([-*]|\d+\.)\s/.test(lines[i])) {
        let text = lines[i++].replace(/^\s*([-*]|\d+\.)\s+/, '');
        while (i < lines.length && /^\s{2,}\S/.test(lines[i]) && !/^\s*([-*]|\d+\.)\s/.test(lines[i])) text += ' ' + lines[i++].trim();
        items.push(`<li>${inline(text)}</li>`);
      }
      out.push(`<${ordered ? 'ol' : 'ul'}>${items.join('')}</${ordered ? 'ol' : 'ul'}>`);
    } else if (/^!\[/.test(line)) {  // one or more images on a line, shown side by side
      const imgs = [...line.matchAll(/!\[([^\]]*)\]\(([^)]+)\)/g)];
      out.push('<div class="figs">' + imgs.map((m) => `<figure><img src="${imageUrl(m[2])}"><figcaption>${inline(m[1])}</figcaption></figure>`).join('') + '</div>');
      i++;
    } else if (line.trim() === '') {
      i++;
    } else {  // paragraph
      const para = [];
      while (i < lines.length && lines[i].trim() !== '' && !/^(#{1,3}\s|```|\||!\[|\s*([-*]|\d+\.)\s)/.test(lines[i])) para.push(lines[i++].trim());
      out.push(`<p>${inline(para.join(' '))}</p>`);
    }
  }
  return out.join('\n');
}

const css = `
@page { size: A4; margin: 14mm 14mm 14mm 14mm; }
body { font: 10.2pt/1.38 "Segoe UI", Arial, sans-serif; color: #111; }
h1 { font-size: 16pt; margin: 0 0 4px; color: #1f3a5f; }
h2 { font-size: 11.5pt; margin: 10px 0 3px; color: #1f3a5f; border-bottom: 1px solid #b8c2cc; padding-bottom: 1px; }
h3 { font-size: 10pt; margin: 7px 0 2px; }
p { margin: 3px 0; } ul, ol { margin: 3px 0 3px 18px; padding: 0; } li { margin: 1px 0; }
table { border-collapse: collapse; margin: 4px 0; font-size: 9pt; width: 100%; break-inside: avoid; }
th, td { border: 1px solid #9aa7b4; padding: 2px 5px; text-align: left; vertical-align: top; } th { background: #e6ecf2; }
code { font: 8.4pt Consolas, monospace; background: #eef1f4; padding: 0 2px; }
pre { font: 7.8pt/1.25 Consolas, monospace; background: #f3f5f7; border: 1px solid #d0d7de; padding: 4px 6px; margin: 4px 0; white-space: pre-wrap; }
.figs { display: flex; gap: 8px; margin: 4px 0; } figure { margin: 0; flex: 1; max-width: 50%; text-align: center; } figure img { width: 100%; }
figcaption { font-size: 8pt; color: #333; }
`;

function findBrowser() {
  const candidates = [process.env.BROWSER_PATH,
    path.join(process.env['ProgramFiles(x86)'] || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Microsoft', 'Edge', 'Application', 'msedge.exe'),
    path.join(process.env.ProgramFiles || '', 'Google', 'Chrome', 'Application', 'chrome.exe')];
  const found = candidates.find((c) => c && fs.existsSync(c));
  if (!found) throw new Error('No Edge/Chrome found. Set BROWSER_PATH.');
  return found;
}

const html = `<!doctype html><html><head><meta charset="utf-8"><style>${css}</style></head><body>${convert(fs.readFileSync(input, 'utf8'))}</body></html>`;
const tmpHtml = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'wb-pdf-')), 'report.html');
fs.writeFileSync(tmpHtml, html);
if (process.argv[4]) fs.copyFileSync(tmpHtml, path.resolve(process.argv[4]));  // optional: keep the HTML for inspection
const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'wb-pdf-profile-'));
const r = spawnSync(findBrowser(), ['--headless=new', '--disable-gpu', `--user-data-dir=${profile}`, '--no-pdf-header-footer',
  `--print-to-pdf=${output}`, pathToFileURL(tmpHtml).href], { stdio: 'ignore', timeout: 60000 });
if (!fs.existsSync(output)) throw new Error(`PDF was not created (exit ${r.status})`);
const pages = (fs.readFileSync(output, 'latin1').match(/\/Type\s*\/Page[^s]/g) || []).length;
console.log(`${path.basename(output)}: ${pages} page(s), ${Math.round(fs.statSync(output).size / 1024)} KB`);

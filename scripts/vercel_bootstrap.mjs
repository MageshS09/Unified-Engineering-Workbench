// Build step for a Vercel project that was created from FILES (not linked to a GitHub repository).
// It downloads a pinned commit of the public repository from GitHub, runs the repository's own
// scripts/build_demo.mjs on it, and leaves the finished static site in ./demo-dist.
// Pinning the commit makes the deployment reproducible: it always serves exactly that commit's demo.
//
//   node vercel_bootstrap.mjs            (WB_REF=<commit or branch> overrides the pinned commit)
import { execFileSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const REPO = 'MageshS09/Unified-Engineering-Workbench';
const REF = process.env.WB_REF || 'bf1f1c4c8314a291ac4d7ca16b111117285fdf61';

const work = fs.mkdtempSync(path.join(os.tmpdir(), 'wb-src-'));
const archive = path.join(work, 'src.tar.gz');
const url = `https://codeload.github.com/${REPO}/tar.gz/${REF}`;

console.log(`downloading ${REPO} @ ${REF}`);
const res = await fetch(url);
if (!res.ok) throw new Error(`download failed: HTTP ${res.status} for ${url}`);
fs.writeFileSync(archive, Buffer.from(await res.arrayBuffer()));
// Relative names + cwd: GNU tar on Windows would read a drive letter (C:) as a remote host name.
execFileSync('tar', ['-xzf', 'src.tar.gz'], { cwd: work, stdio: 'inherit' });

const srcDir = fs.readdirSync(work).map((d) => path.join(work, d)).find((d) => fs.statSync(d).isDirectory());
if (!srcDir) throw new Error('archive did not contain a folder');
execFileSync(process.execPath, ['scripts/build_demo.mjs'], { cwd: srcDir, stdio: 'inherit' });

fs.rmSync('demo-dist', { recursive: true, force: true });
fs.cpSync(path.join(srcDir, 'demo-dist'), 'demo-dist', { recursive: true });
console.log(`demo-dist ready: ${fs.readdirSync('demo-dist').join(', ')}`);

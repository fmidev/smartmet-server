// Captures the stills and animations of the data explorer documentation
// (docs/data-explorer.md) from a running SmartMet Server with the test data.
//
// Usage: node capture.js [--server URL] [--out DIR] [scene ...]
//        node capture.js --probe 'HASH' [--out DIR]
//
// Each scene writes DIR/<name>.png (a still) or DIR/<name>/ (frames and their times, made
// into DIR/<name>.webp by assemble.py). --probe opens the explorer with the given state
// (the part after # in its address) and saves probe.png with a list of the controls shown.

'use strict';

const fs = require('node:fs');
const path = require('node:path');
const {startBrowser, stopBrowser, sleep} = require('./cdp.js');
const scenes = require('./scenes.js');

const args = process.argv.slice(2);
const option = (name, fallback) => {
  const i = args.indexOf(name);
  if (i < 0)
    return fallback;
  const v = args[i + 1];
  args.splice(i, 2);
  return v;
};
const server = option('--server', 'http://localhost:18181').replace(/\/$/, '');
const out = path.resolve(option('--out', path.join(__dirname, 'out')));
const probe = option('--probe', null);
// Buttons (by their text, separated by |) clicked before the probe screenshot, and a scroll
const probeClicks = option('--click', '');
const probeScroll = option('--scroll', '0');
fs.mkdirSync(out, {recursive: true});

function writeFrames(name, frames, crop)
{
  const dir = path.join(out, name);
  fs.rmSync(dir, {recursive: true, force: true});
  fs.mkdirSync(dir, {recursive: true});
  const times = [];
  frames.forEach((f, i) => {
    const file = `${String(i).padStart(4, '0')}.png`;
    fs.writeFileSync(path.join(dir, file), f.png);
    times.push({file, t: f.t});
  });
  fs.writeFileSync(path.join(dir, 'frames.json'), JSON.stringify(crop ? {crop, frames: times} : times));
  console.log(`${name}: ${frames.length} frames, ${(frames.at(-1).t / 1000).toFixed(1)} s`);
}

async function main()
{
  const b = await startBrowser();
  const ctx = {
    b,
    server,
    explorer: `${server}/api/explorer`,
    sleep,
    // Opens the explorer with a state: an object of the hash parameters
    open: async (state) => {
      const hash = new URLSearchParams(state || {}).toString();
      await b.open(`${server}/api/explorer#${hash}`);
      await b.waitFor(`!document.querySelector('main') ||
          !/Loading/.test(document.querySelector('main').textContent.slice(0, 200))`, 'the module');
      await sleep(400);
    },
    still: async (name, clip) => {
      await sleep(300);
      await b.evaluate(`document.getElementById('doc-pointer').style.display = 'none'`);
      fs.writeFileSync(path.join(out, `${name}.png`), await b.screenshot(clip));
      await b.evaluate(`document.getElementById('doc-pointer').style.display = ''`);
      console.log(`${name}: still`);
    },
    // An animation; crop {x, y, width, height} keeps a part of the window
    anim: async (name, fn, opts = {}) => writeFrames(name, await b.record(fn), opts.crop),
  };

  try
  {
    if (probe !== null)
    {
      await b.open(`${server}/api/explorer#${probe.replace(/^#/, '')}`);
      await sleep(2500);
      for (const text of probeClicks.split('|').filter(Boolean))
      {
        await b.evaluate(`(() => {
          const e = [...document.querySelectorAll('button, summary, [role=tab]')].filter((x) => x.getClientRects().length)
              .find((x) => x.textContent.trim() === ${JSON.stringify(text)});
          if (!e) throw new Error('no button ${text.replace(/'/g, '')}');
          e.click();
        })()`);
        await sleep(text === 'Run' || /Skew|Tephi|Stüve|Meteogram/.test(text) ? 6000 : 1500);
      }
      await b.evaluate(`window.scrollTo(0, ${Number(probeScroll)})`);
      await sleep(500);
      fs.writeFileSync(path.join(out, 'probe.png'), await b.screenshot());
      const controls = await b.evaluate(`[...document.querySelectorAll('button, select, input, summary, [role=tab], a')]
          .filter((e) => e.getClientRects().length)
          .map((e) => e.tagName.toLowerCase() + (e.className ? '.' + String(e.className).replace(/ /g, '.') : '') +
               (e.getAttribute('aria-label') ? '[' + e.getAttribute('aria-label') + ']' : '') + ' ' +
               (e.tagName === 'SELECT' ? '{' + [...e.options].map((o) => o.textContent).join('|').slice(0, 200) + '}' :
                (e.value || e.textContent || '').trim().slice(0, 60)))`);
      const seen = new Map();
      for (const c of controls)
        seen.set(c, (seen.get(c) || 0) + 1);
      console.log([...seen].map(([c, n]) => (n > 1 ? `${c}  (x${n})` : c)).join('\n'));
      console.log(await b.evaluate(`'height ' + document.documentElement.scrollHeight + ' hash ' + location.hash`));
      if (b.errors.length)
        console.log('ERRORS', b.errors);
      return;
    }
    const names = args.length ? args : Object.keys(scenes);
    for (const name of names)
    {
      if (!scenes[name])
        throw new Error(`no scene ${name}: ${Object.keys(scenes).join(' ')}`);
      const started = Date.now();
      await scenes[name](ctx);
      if (b.errors.length)
        console.log(`${name}: page errors`, b.errors.splice(0));
      console.log(`${name}: ${((Date.now() - started) / 1000).toFixed(0)} s`);
    }
  }
  finally
  {
    await stopBrowser(b);
  }
}

main().catch((e) => {
  console.error(e);
  process.exit(1);
});

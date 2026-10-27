// A headless Chrome driven over the DevTools protocol, with a drawn mouse pointer, for the
// captures of the data explorer documentation.

'use strict';

const childProcess = require('node:child_process');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function findBrowser()
{
  for (const name of ['chromium-browser', 'chromium', 'google-chrome', 'google-chrome-stable'])
    if (childProcess.spawnSync('sh', ['-c', `command -v ${name}`]).status === 0)
      return name;
  throw new Error('Chromium or Google Chrome is needed');
}

// The pointer drawn on the page: headless browsers draw none
const POINTER_SCRIPT = `(function add() {
  if (!document.body) { document.addEventListener('DOMContentLoaded', add); return; }
  if (document.getElementById('doc-pointer')) return;
  const p = document.createElement('div');
  p.id = 'doc-pointer';
  p.innerHTML = '<svg width="22" height="26" viewBox="0 0 22 26"><path d="M2 2 L2 21 L7 16.5 L10.5 24 L14 22.5 L10.5 15 L17 15 Z" fill="#fff" stroke="#111" stroke-width="1.6" stroke-linejoin="round"/></svg>';
  p.style.cssText = 'position:fixed;left:-50px;top:-50px;z-index:2147483647;pointer-events:none;width:22px;height:26px;margin:-2px 0 0 -2px;filter:drop-shadow(0 1px 2px rgba(0,0,0,.35))';
  document.documentElement.append(p);
  window.__docRipple = (x, y) => {
    const r = document.createElement('div');
    r.style.cssText = 'position:fixed;z-index:2147483646;pointer-events:none;border-radius:50%;border:3px solid rgba(232,90,30,.85);width:10px;height:10px;transform:translate(-50%,-50%);transition:width .35s ease-out,height .35s ease-out,opacity .35s ease-out';
    r.style.left = x + 'px'; r.style.top = y + 'px';
    document.documentElement.append(r);
    requestAnimationFrame(() => { r.style.width = '44px'; r.style.height = '44px'; r.style.opacity = '0'; });
    setTimeout(() => r.remove(), 450);
  };
})()`;

async function startBrowser({width = 1240, height = 900} = {})
{
  const profile = fs.mkdtempSync(path.join(os.tmpdir(), 'explorer-capture-'));
  const child = childProcess.spawn(
      findBrowser(),
      [
        '--headless=new', '--disable-gpu', '--no-sandbox', '--hide-scrollbars',
        '--force-color-profile=srgb', '--font-render-hinting=none', '--lang=en-GB',
        `--user-data-dir=${profile}`, '--remote-debugging-port=0', `--window-size=${width},${height}`,
        'about:blank'
      ],
      {stdio: 'ignore'});
  const portFile = path.join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 100 && !fs.existsSync(portFile); i++)
    await sleep(100);
  const port = fs.readFileSync(portFile, 'utf8').split('\n')[0];
  const targets = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
  const ws = new WebSocket(targets.find((t) => t.type === 'page').webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0;
  const pending = new Map();
  const errors = [];
  ws.onmessage = (event) => {
    const m = JSON.parse(event.data);
    if (m.id && pending.has(m.id))
    {
      const {resolve, reject, method} = pending.get(m.id);
      pending.delete(m.id);
      if (m.error)
        reject(new Error(`${method}: ${m.error.message}`));
      else
        resolve(m.result);
    }
    else if (m.method === 'Runtime.exceptionThrown')
      errors.push(m.params.exceptionDetails.exception?.description || m.params.exceptionDetails.text);
  };
  // A call not answered in time (a minute by default) fails, for example an evaluation or a
  // screenshot whose page was unloaded
  const send = (method, params = {}, timeout = 60000) => new Promise((resolve, reject) => {
    const i = ++id;
    const timer = setTimeout(() => {
      if (pending.delete(i))
        reject(new Error(`${method}: no answer`));
    }, timeout);
    pending.set(i, {
      resolve: (v) => {
        clearTimeout(timer);
        resolve(v);
      },
      reject: (e) => {
        clearTimeout(timer);
        reject(e);
      },
      method
    });
    ws.send(JSON.stringify({id: i, method, params}));
  });
  await send('Runtime.enable');
  await send('Page.enable');
  await send('Emulation.setDeviceMetricsOverride',
             {width, height, deviceScaleFactor: 1, mobile: false});
  await send('Emulation.setEmulatedMedia',
             {features: [{name: 'prefers-color-scheme', value: 'light'}]});
  await send('Page.addScriptToEvaluateOnNewDocument', {source: POINTER_SCRIPT});

  const b = {child, profile, send, errors, width, height, pointer: {x: width * 0.6, y: height * 0.6}};

  b.evaluate = async (expression) => {
    const r = await send('Runtime.evaluate', {expression, awaitPromise: true, returnByValue: true});
    if (r.exceptionDetails)
      throw new Error(`${expression.slice(0, 120)}: ${r.exceptionDetails.exception?.description || r.exceptionDetails.text}`);
    return r.result.value;
  };

  b.waitFor = async (expression, what, timeout = 20000) => {
    const end = Date.now() + timeout;
    while (Date.now() < end)
    {
      if (await b.evaluate(expression))
        return;
      await sleep(100);
    }
    throw new Error('timed out waiting for ' + (what || expression));
  };

  // Opens a page, the state of the explorer in the hash, with a clean local storage
  b.open = async (url) => {
    await send('Page.navigate', {url: 'about:blank'});
    await sleep(100);
    await send('Page.navigate', {url});
    await b.waitFor(`document.readyState === 'complete' && !!document.querySelector('nav')`, 'the page');
    await b.evaluate(POINTER_SCRIPT);
    await b.placePointer(b.pointer.x, b.pointer.y);
  };

  b.placePointer = async (x, y) => {
    b.pointer = {x, y};
    await b.evaluate(`(() => { const p = document.getElementById('doc-pointer');
      if (p) { p.style.left = '${x}px'; p.style.top = '${y}px'; } })()`);
    await send('Input.dispatchMouseEvent', {type: 'mouseMoved', x, y});
  };

  // Moves the pointer smoothly, sending the mouse moves (hovers and drags see them)
  b.moveTo = async (x, y, ms = 600, buttons = 0) => {
    const from = {...b.pointer};
    const steps = Math.max(2, Math.round(ms / 25));
    for (let i = 1; i <= steps; i++)
    {
      const t = i / steps;
      const e = t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
      const px = from.x + (x - from.x) * e;
      const py = from.y + (y - from.y) * e;
      b.pointer = {x: px, y: py};
      await b.evaluate(`(() => { const p = document.getElementById('doc-pointer');
        if (p) { p.style.left = '${px}px'; p.style.top = '${py}px'; } })()`);
      await send('Input.dispatchMouseEvent',
                 {type: 'mouseMoved', x: px, y: py, buttons, button: buttons ? 'left' : 'none'});
      await sleep(ms / steps);
    }
  };

  b.mouseClick = async (x, y, ms = 600) => {
    await b.moveTo(x, y, ms);
    await b.evaluate(`window.__docRipple && window.__docRipple(${x}, ${y})`);
    await send('Input.dispatchMouseEvent', {type: 'mousePressed', x, y, button: 'left', buttons: 1, clickCount: 1});
    await sleep(60);
    await send('Input.dispatchMouseEvent', {type: 'mouseReleased', x, y, button: 'left', buttons: 0, clickCount: 1});
    await sleep(150);
  };

  b.drag = async (x0, y0, x1, y1, ms = 900) => {
    await b.moveTo(x0, y0, 500);
    await send('Input.dispatchMouseEvent', {type: 'mousePressed', x: x0, y: y0, button: 'left', buttons: 1, clickCount: 1});
    await b.moveTo(x1, y1, ms, 1);
    await send('Input.dispatchMouseEvent', {type: 'mouseReleased', x: x1, y: y1, button: 'left', buttons: 0, clickCount: 1});
    await sleep(150);
  };

  // The centre of the element matched by a selector (with text: the one containing it),
  // scrolled into view first
  b.locate = async (selector, text, {scroll = true, where = 'center'} = {}) => {
    const r = await b.evaluate(`(async () => {
      const all = [...document.querySelectorAll(${JSON.stringify(selector)})]
          .filter((e) => e.getClientRects().length > 0);
      const t = ${JSON.stringify(text || null)};
      const e = t === null ? all[0] : all.find((x) => x.textContent.trim() === t) ||
                                      all.find((x) => x.textContent.includes(t));
      if (!e) return null;
      if (${scroll}) {
        const r0 = e.getBoundingClientRect();
        if (r0.top < 70 || r0.bottom > innerHeight - 20) {
          window.scrollBy({top: r0.top - innerHeight * 0.35, behavior: 'smooth'});
          await new Promise((r) => setTimeout(r, 700));
        }
      }
      const r = e.getBoundingClientRect();
      return {x: r.left, y: r.top, w: r.width, h: r.height};
    })()`);
    if (!r)
      throw new Error(`no element ${selector} ${text || ''}`);
    const x = where === 'left' ? r.x + Math.min(24, r.w / 2) : r.x + r.w / 2;
    return {x, y: r.y + r.h / 2, rect: r};
  };

  // The window pixel of a longitude and latitude in a map of the explorer (the nth one shown,
  // -1 the last)
  b.mapPixel = async (lon, lat, nth = 0) => {
    const r = await b.evaluate(`(() => {
      const m = [...document.querySelectorAll('.xmap')].filter((e) => e.getClientRects().length).at(${nth});
      const [x, y] = m.screenOf(${lon}, ${lat});
      const r = m.querySelector('svg').getBoundingClientRect();
      return {x: r.left + x, y: r.top + y};
    })()`);
    return r;
  };

  b.click = async (selector, text, opts) => {
    const p = await b.locate(selector, text, opts);
    await b.mouseClick(p.x, p.y, (opts && opts.ms) || 600);
    return p;
  };

  // Types into the focused element one character at a time
  b.type = async (text, ms = 90) => {
    for (const ch of text)
    {
      await send('Input.insertText', {text: ch});
      await sleep(ms);
    }
  };

  b.key = async (key, code, keyCode) => {
    await send('Input.dispatchKeyEvent', {type: 'keyDown', key, code, windowsVirtualKeyCode: keyCode});
    await send('Input.dispatchKeyEvent', {type: 'keyUp', key, code, windowsVirtualKeyCode: keyCode});
  };

  // Chooses an option of a select element by its visible text
  // A selector 'label=Text' (optionally after a scope: '.results label=Top') is the select
  // element of the label starting with Text
  const labelled = async (selector) => {
    const m = /^(.*?)(?:^|\s)label=([^"\]].*)$/.exec(selector);
    if (!m)
      return selector;
    await b.evaluate(`(() => {
      document.querySelectorAll('[data-doc-target]').forEach((e) => e.removeAttribute('data-doc-target'));
      const l = [...document.querySelectorAll(${JSON.stringify((m[1] ? m[1] + ' ' : '') + 'label')})]
          .filter((e) => e.getClientRects().length && e.textContent.trim().startsWith(${JSON.stringify(m[2])}))
          .find((e) => e.querySelector('select'));
      if (!l) throw new Error('no label ' + ${JSON.stringify(m[2])});
      l.querySelector('select').setAttribute('data-doc-target', '');
    })()`);
    return '[data-doc-target]';
  };

  b.select = async (selector, text) => {
    selector = await labelled(selector);
    // The pointer goes there and a ripple shows the click, but no click is sent: headless
    // Chrome would draw the open option list in the captures
    const p = await b.locate(selector);
    await b.moveTo(p.x, p.y, 600);
    await b.evaluate(`window.__docRipple && window.__docRipple(${p.x}, ${p.y})`);
    await sleep(200);
    await b.evaluate(`(() => {
      const s = [...document.querySelectorAll(${JSON.stringify(selector)})].find((e) => e.getClientRects().length);
      const o = [...s.options].find((o) => o.textContent.trim() === ${JSON.stringify(text)}) ||
                [...s.options].find((o) => o.textContent.includes(${JSON.stringify(text)}));
      if (!o) throw new Error('no option ' + ${JSON.stringify(text)});
      s.value = o.value;
      // After this evaluation has returned: a change may load the page again
      setTimeout(() => {
        s.dispatchEvent(new Event('input', {bubbles: true}));
        s.dispatchEvent(new Event('change', {bubbles: true}));
      }, 0);
    })()`);
    await sleep(100);
    return p;
  };

  b.scrollTo = async (y, ms = 700) => {
    await b.evaluate(`window.scrollTo({top: ${y}, behavior: 'smooth'})`);
    await sleep(ms);
  };

  b.screenshot = async (clip) => {
    const params = {format: 'png'};
    if (clip)
    {
      params.clip = {...clip, scale: 1};
      params.captureBeyondViewport = true;
    }
    const r = await send('Page.captureScreenshot', params, 3000);
    return Buffer.from(r.data, 'base64');
  };

  // Records frames with their times while fn runs
  b.record = async (fn, {clip} = {}) => {
    const frames = [];
    let running = true;
    const t0 = Date.now();
    const loop = (async () => {
      while (running)
      {
        const t = Date.now() - t0;
        try
        {
          frames.push({t, png: await b.screenshot(clip)});
        }
        catch (e)
        {
          // The page is being loaded again: no frame
          await sleep(50);
        }
      }
    })();
    try
    {
      await fn();
    }
    finally
    {
      running = false;
      await loop;
    }
    frames.push({t: Date.now() - t0, png: await b.screenshot(clip)});
    return frames;
  };

  return b;
}

async function stopBrowser(b)
{
  const exited = new Promise((r) => b.child.once('exit', r));
  b.child.kill();
  await Promise.race([exited, sleep(5000)]);
  fs.rmSync(b.profile, {recursive: true, force: true});
}

module.exports = {startBrowser, stopBrowser, sleep};

// The scenes of the data explorer documentation. Each one opens the explorer of the test
// server in a known state and makes a still (ctx.still) or an animation (ctx.anim).

'use strict';

const TS = '/timeseries';
// The surface forecast of the test data, 2008-08-05 03 UTC - 2008-08-09 06 UTC
const PAL = {api: TS, producer: 'pal_skandinavia', source: 'querydata'};
// MEPS on 65 model levels around Helsinki, 2026-10-15 00-12 UTC
const MEPS = {api: TS, producer: 'meps_ml', source: 'querydata', locmode: 'place', location: 'Helsinki'};
// The results column
const RIGHT = {x: 440, y: 0, width: 800, height: 900};

// Runs the request of the open module and waits for its results
async function run(ctx, {click = true} = {})
{
  const {b} = ctx;
  // The primary button of the request, whatever the language
  if (click)
    await b.click('.request button.primary');
  else
    await b.evaluate(`document.querySelector('.request button.primary').click()`);
  await b.waitFor(`!!document.querySelector('.results-head') ||
                   !!document.querySelector('main .error')`, 'the results', 60000);
  await ctx.sleep(800);
}

// Moves the pointer over the first chart from x0 to x1 (fractions of its width)
async function hoverChart(ctx, x0, x1)
{
  const {b} = ctx;
  const c = await b.locate('.results .u-over, .results canvas', null, {scroll: false});
  const r = c.rect;
  await b.moveTo(r.x + r.w * x0, r.y + r.h * 0.5, 600);
  await b.moveTo(r.x + r.w * x1, r.y + r.h * 0.45, 2200);
  await ctx.sleep(600);
}

// Scrolls so that the element is at the top of the window
async function scrollToElement(ctx, selector, text, offset = 10)
{
  await ctx.b.evaluate(`(() => {
    const all = [...document.querySelectorAll(${JSON.stringify(selector)})].filter((e) => e.getClientRects().length);
    const t = ${JSON.stringify(text || null)};
    const e = t === null ? all[0] : all.find((x) => x.textContent.includes(t));
    window.scrollBy({top: e.getBoundingClientRect().top - ${offset}, behavior: 'smooth'});
  })()`);
  await ctx.sleep(800);
}

// The clip of the element matched (the bounding box of several with all), with a margin
async function clipOf(ctx, selector, {text, margin = 8, all = false, maxHeight} = {})
{
  const r = await ctx.b.evaluate(`(() => {
    const t = ${JSON.stringify(text || null)};
    let es = [...document.querySelectorAll(${JSON.stringify(selector)})].filter((e) => e.getClientRects().length);
    if (t !== null) es = es.filter((e) => e.textContent.includes(t));
    if (!${all}) es = es.slice(0, 1);
    if (!es.length) return null;
    const rs = es.map((e) => e.getBoundingClientRect());
    const x0 = Math.min(...rs.map((r) => r.left)), y0 = Math.min(...rs.map((r) => r.top));
    const x1 = Math.max(...rs.map((r) => r.right)), y1 = Math.max(...rs.map((r) => r.bottom));
    return {x: x0 + scrollX, y: y0 + scrollY, width: x1 - x0, height: y1 - y0};
  })()`);
  if (!r)
    throw new Error(`no element ${selector}`);
  const m = margin;
  const height = Math.min(r.height + 2 * m, maxHeight || Infinity);
  return {x: Math.max(0, r.x - m), y: Math.max(0, r.y - m), width: r.width + 2 * m, height};
}

// Numbered markers next to elements, for stills explained by a numbered list
async function markers(ctx, list)
{
  await ctx.b.evaluate(`(() => {
    for (const [i, [selector, text, dx, dy]] of ${JSON.stringify(list)}.entries()) {
      const e = [...document.querySelectorAll(selector)].filter((x) => x.getClientRects().length)
          .find((x) => text === null || x.textContent.includes(text));
      if (!e) throw new Error('no marker element ' + selector);
      const r = e.getBoundingClientRect();
      const m = document.createElement('div');
      m.className = 'doc-marker';
      m.textContent = String(i + 1);
      m.style.cssText = 'position:absolute;z-index:2147483640;width:22px;height:22px;border-radius:50%;' +
          'background:#e8591e;color:#fff;font:600 13px/22px system-ui,sans-serif;text-align:center;' +
          'box-shadow:0 1px 3px rgba(0,0,0,.4);pointer-events:none';
      m.style.left = (r.left + scrollX + (dx || 0) - 11) + 'px';
      m.style.top = (r.top + scrollY + (dy || 0) - 11) + 'px';
      document.body.append(m);
    }
  })()`);
}

module.exports = {
  // S1: the page and its parts, numbered
  async overview(ctx)
  {
    await ctx.open({api: TS, producer: 'pal_skandinavia', source: 'querydata', params: 'temperature,windspeedms',
                    locmode: 'place', location: 'Helsinki'});
    await run(ctx, {click: false});
    await ctx.b.evaluate(`window.scrollTo(0, 0)`);
    await markers(ctx, [
      ['nav .browse-button', null, 0, 0],
      ['nav select', null, 0, 0],
      ['nav a', 'API documentation', 0, 0],
      ['nav input', null, 0, 0],
      ['nav .settings-button', null, 0, 0],
      ['section.step h2', 'What', 0, 0],
      ['code.url', null, 0, 0],
      ['.results-head', null, 0, 0],
    ]);
    await ctx.still('overview');
  },

  // A1: searching the datasets and opening one in the TimeSeries module
  async browse(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: 'browse'});
    await ctx.anim('browse', async () => {
      await ctx.sleep(800);
      await b.click('input[aria-label="Search the data"]');
      await b.type('gust');
      await ctx.sleep(1200);
      await b.click('button.facet', 'Forecast models');
      await ctx.sleep(1500);
      const card = '.browse-card[data-dataset="pal_skandinavia"]';
      await b.click(`${card} summary`);
      await ctx.sleep(1500);
      await b.click(`${card} button`, 'Open in SmartMet TimeSeries API (querydata)');
      await b.waitFor(`!!document.querySelector('.chips .chip')`, 'the parameters');
      await ctx.sleep(2500);
    });
  },

  // A2: the datasets covering a point clicked on the map
  async browsePoint(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: 'browse'});
    await ctx.anim('browse-point', async () => {
      await ctx.sleep(600);
      await b.click('.browse-map button[aria-label="Zoom in"]');
      await ctx.sleep(500);
      await ctx.sleep(900);
      // The North Atlantic, then Finland
      let p = await b.mapPixel(-30, 50);
      await b.mouseClick(p.x, p.y);
      await ctx.sleep(2200);
      p = await b.mapPixel(25.5, 61.5);
      await b.mouseClick(p.x, p.y);
      await ctx.sleep(2500);
    });
  },

  // A3: a TimeSeries request from nothing: parameter and producer searches, a place, Run
  async timeseriesFirst(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS});
    await ctx.anim('timeseries-first', async () => {
      await ctx.sleep(600);
      await b.click('input.ac-input[aria-label="Parameter"]');
      await b.type('temp');
      await ctx.sleep(1200);
      await b.click('.ac-list li', 'Temperature');
      await ctx.sleep(1000);
      await b.click('input.ac-input[aria-label="Producer"]');
      await b.type('pal');
      await ctx.sleep(1200);
      await b.click('.ac-list li', 'pal_skandinavia');
      await ctx.sleep(1200);
      await b.click('button', 'Place');
      await b.click('input[aria-label="Location"]');
      await b.type('Helsinki');
      await ctx.sleep(1000);
      await run(ctx);
      await ctx.sleep(2500);
    });
  },

  // S2: a card of the Browse page
  async browseCard(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: 'browse', q: 'gust'});
    await b.waitFor(`!!document.querySelector('.browse-card[data-dataset="pal_skandinavia"]')`, 'the card');
    await ctx.still('browse-card', await clipOf(ctx, '.browse-card[data-dataset="pal_skandinavia"]'));
  },

  // S3: the details of a grid producer: runs, levels, the parameters
  async producerDetails(ctx)
  {
    await ctx.open({api: TS, producer: 'meps_ml', source: 'grid', params: 't-k', locmode: 'place',
                    location: 'Helsinki'});
    await ctx.sleep(1500);
    await ctx.still('producer-details', await clipOf(ctx, 'section.step', {text: 'What', margin: 4}));
  },

  // S4: the request and the code to run it
  async requestCode(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, producer: 'pal_skandinavia', source: 'querydata', params: 'temperature',
                    locmode: 'place', location: 'Helsinki'});
    await b.evaluate(`document.querySelector('details.code-details').open = true`);
    await b.click('button', 'Python');
    await ctx.still('request-code', await clipOf(ctx, '.request .url-row, .request details', {margin: 6, all: true}));
  },

  // A4: drawing a box and a radius on the map: the producers not covering them drop out
  async mapSelection(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, params: 'temperature'});
    await scrollToElement(ctx, 'h2', 'Where', 60);
    await ctx.anim('map-selection', async () => {
      await ctx.sleep(700);
      await b.click('section.step button[aria-label="Zoom out"]');
      await ctx.sleep(400);
      await b.click('section.step button[aria-label="Zoom out"]');
      await ctx.sleep(700);
      await b.click('.segmented button', 'Box');
      await ctx.sleep(500);
      // Iberia: outside the Nordic models
      let p0 = await b.mapPixel(-9, 36.5);
      let p1 = await b.mapPixel(3, 43.5);
      await b.drag(p0.x, p0.y, p1.x, p1.y, 1200);
      await ctx.sleep(1200);
      await scrollToElement(ctx, 'h2', 'What', 10);
      await ctx.sleep(2200);
      await scrollToElement(ctx, 'h2', 'Where', 60);
      await b.click('.segmented button', 'Radius');
      await ctx.sleep(500);
      // 100 km around Helsinki
      p0 = await b.mapPixel(24.94, 60.17);
      p1 = await b.mapPixel(26.75, 60.17);
      await b.drag(p0.x, p0.y, p1.x, p1.y, 1000);
      await ctx.sleep(1200);
      await scrollToElement(ctx, 'h2', 'What', 10);
      await ctx.sleep(2500);
    }, {crop: {x: 0, y: 0, width: 460, height: 900}});
  },

  // A5: an area: the statistics computed by the server, then every point as a swarm
  async areaStatistics(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature', locmode: 'bbox', location: '21,59.7,30,64'});
    await ctx.anim('area-statistics', async () => {
      await ctx.sleep(500);
      await run(ctx);
      await scrollToElement(ctx, '.results-head', null, 10);
      await hoverChart(ctx, 0.3, 0.75);
      await scrollToElement(ctx, 'h2', 'Output', 10);
      await b.click('[data-areavalues="points"]');
      await ctx.sleep(400);
      await scrollToElement(ctx, 'button.primary', 'Run', 200);
      await run(ctx);
      await scrollToElement(ctx, '.results-head', null, 10);
      await ctx.sleep(1200);
      await b.click('.results button', 'All locations');
      await ctx.sleep(1500);
      await hoverChart(ctx, 0.3, 0.75);
      await ctx.sleep(1500);
    });
  },

  // A3b: the parameters of a unit combined in one chart, a second unit on the right, and a
  // parameter of unknown unit left in a chart of its own
  async combineCharts(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature,dewpoint,feelslike,windspeedms,precipitation1h', locmode: 'place',
                    location: 'Helsinki'});
    await run(ctx, {click: false});
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.anim('combine-charts', async () => {
      await ctx.sleep(1800);
      await b.click('.results .chart-combine');
      await ctx.sleep(1500);
      await hoverChart(ctx, 0.2, 0.8);
      await ctx.sleep(1500);
    }, {crop: RIGHT});
  },

  // A4b: the percentiles of an area combined, each parameter in colours of its own; the swarm
  // of every point cannot be combined, and its lines are too many to hover
  async combinePercentiles(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature,dewpoint', locmode: 'bbox', location: '21,59.7,30,64',
                    areavalues: 'points'});
    await run(ctx, {click: false});
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.anim('combine-percentiles', async () => {
      await ctx.sleep(1800);
      await b.click('.results .chart-combine');
      await ctx.sleep(1500);
      await hoverChart(ctx, 0.25, 0.75);
      await b.click('.results button', 'All locations');
      await ctx.sleep(1200);
      await hoverChart(ctx, 0.3, 0.7);
      await ctx.sleep(1500);
    }, {crop: RIGHT});
  },

  // S5: the table of the results
  async table(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature,windspeedms,winddirection', locmode: 'place', location: 'Helsinki'});
    await run(ctx, {click: false});
    await b.click('.results-head [role=tab]', 'Table');
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.still('table', await clipOf(ctx, '.results', {maxHeight: 520}));
  },

  // A6: the values of every point on a map: the time slider, then a point clicked for its series
  async mapResults(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature', locmode: 'bbox', location: '19,59.5,32,70', areavalues: 'points'});
    await run(ctx, {click: false});
    await b.evaluate(`[...document.querySelectorAll('.results-head [role=tab]')].find((x) => x.textContent === 'Map').click()`);
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.sleep(1000);
    await ctx.anim('map-results', async () => {
      await ctx.sleep(600);
      await b.click('.time-play');
      await ctx.sleep(5000);
      await b.click('.time-play');
      await ctx.sleep(600);
      const p = await b.mapPixel(25.5, 64.5, -1);
      await b.mouseClick(p.x, p.y);
      await ctx.sleep(2500);
      await scrollToElement(ctx, '.value-probe, .probe', null, 120).catch(() => {});
      await ctx.sleep(2000);
    }, {crop: RIGHT});
  },

  // S6: a meteogram
  async meteogram(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature', locmode: 'place', location: 'Helsinki'});
    await b.click('button.small', 'Meteogram');
    await b.waitFor(`!!document.querySelector('.results-head')`, 'the meteogram', 60000);
    await ctx.sleep(1500);
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.still('meteogram', await clipOf(ctx, '.results'));
  },

  // S7: a producer compared with another one
  async comparison(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'temperature', locmode: 'place', location: 'Helsinki'});
    await run(ctx, {click: false});
    await b.select('select[aria-label="Compare with"]', 'ecmwf_eurooppa_pinta');
    await ctx.sleep(3000);
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.still('comparison', await clipOf(ctx, '.results', {maxHeight: 860}));
  },

  // A7: a sounding: the diagram buttons, the mouse-over readout, the other diagrams
  async sounding(ctx)
  {
    const {b} = ctx;
    await ctx.open(MEPS);
    await ctx.anim('sounding', async () => {
      await ctx.sleep(500);
      await b.click('button.small', 'Skew-T log-p');
      await b.waitFor(`!!document.querySelector('.results-head')`, 'the sounding', 60000);
      await ctx.sleep(1200);
      await scrollToElement(ctx, 'select[aria-label="Diagram"]', null, 160);
      const d = (await b.locate('.results .skewt svg', null, {scroll: false})).rect;
      await b.moveTo(d.x + d.w * 0.5, d.y + d.h * 0.15, 600);
      await b.moveTo(d.x + d.w * 0.45, d.y + d.h * 0.85, 2500);
      await ctx.sleep(500);
      await b.select('select[aria-label="Diagram"]', 'Tephigram');
      await ctx.sleep(2200);
      await b.select('select[aria-label="Diagram"]', 'Stüve');
      await ctx.sleep(2200);
      await b.select('select[aria-label="Diagram"]', 'Skew-T log-p');
      await ctx.sleep(800);
      await b.click('.time-play');
      await ctx.sleep(4500);
      await b.click('.time-play');
      await ctx.sleep(1000);
    }, {crop: RIGHT});
  },

  // A8: two sounding panels side by side, at different times
  async soundingPanels(ctx)
  {
    const {b} = ctx;
    await ctx.open({...MEPS, params: 'temperature,dewpoint,windspeedms,winddirection', diagram: 'skewt', tab: 'Sounding'});
    await run(ctx, {click: false});
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.anim('sounding-panels', async () => {
      await ctx.sleep(600);
      await b.click('.results button', 'Add a panel');
      await ctx.sleep(2000);
      const steps = await b.evaluate(`[...document.querySelectorAll('.results .time-step')].filter((x) => x.getClientRects().length).length`);
      for (let i = 0; i < 4; i++)
      {
        await b.evaluate(`window.__docNext = [...document.querySelectorAll('.results button[aria-label="Next time"]')].filter((x) => x.getClientRects().length).at(-1)`);
        const r = await b.evaluate(`(() => { const r = window.__docNext.getBoundingClientRect(); return {x: r.left + r.width / 2, y: r.top + r.height / 2}; })()`);
        await b.mouseClick(r.x, r.y, i ? 250 : 700);
        await ctx.sleep(900);
      }
      await ctx.sleep(2000);
    }, {crop: RIGHT});
  },

  // S8: the analysis of a sounding and its hodograph
  async soundingAnalysis(ctx)
  {
    const {b} = ctx;
    await ctx.open({...MEPS, params: 'temperature,dewpoint,windspeedms,winddirection', diagram: 'skewt', tab: 'Sounding'});
    await run(ctx, {click: false});
    await ctx.sleep(1000);
    await ctx.still('sounding-analysis', await clipOf(ctx, '.results .sk-analysis, .results .hodograph-box', {all: true}));
  },

  // A10: a time-height section and its readout
  async timeHeight(ctx)
  {
    const {b} = ctx;
    await ctx.open({...MEPS, params: 'temperature,windspeedms,winddirection,humidity', tab: 'Section'});
    await run(ctx, {click: false});
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.anim('time-height', async () => {
      await ctx.sleep(800);
      await b.select('.results label=Top', '100 hPa');
      await ctx.sleep(1200);
      await b.click('.results label.check', 'Wind');
      await ctx.sleep(1500);
      const d = (await b.locate('.results .section svg, .results .section canvas', null, {scroll: false})).rect;
      await b.moveTo(d.x + d.w * 0.1, d.y + d.h * 0.8, 700);
      await b.moveTo(d.x + d.w * 0.85, d.y + d.h * 0.25, 3500);
      await b.moveTo(d.x + d.w * 0.5, d.y + d.h * 0.6, 1500);
      await ctx.sleep(1200);
    }, {crop: RIGHT});
  },

  // A9: a path clicked on the map, its cross section and the overlays
  async crossSection(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, producer: 'meps_ml', source: 'querydata', params: 'temperature,windspeedms,winddirection,humidity'});
    await scrollToElement(ctx, 'h2', 'Where', 10);
    await ctx.anim('cross-section', async () => {
      await ctx.sleep(500);
      await b.click('.segmented button', 'Path');
      await ctx.sleep(400);
      await b.click('section.step button[aria-label="Show everything on the map"]');
      await ctx.sleep(900);
      for (const [lon, lat] of [[24.25, 59.75], [24.95, 60.17], [25.85, 60.5]])
      {
        const p = await b.mapPixel(lon, lat);
        await b.mouseClick(p.x, p.y, 700);
        await ctx.sleep(500);
      }
      await ctx.sleep(800);
      await scrollToElement(ctx, 'button.primary', 'Run', 200);
      await run(ctx);
      await scrollToElement(ctx, '.results-head', null, 10);
      await b.select('.results select[aria-label="Top of the cross section"]', '100 hPa');
      await ctx.sleep(1500);
      await b.click('.results label.check', 'Wind');
      await ctx.sleep(1200);
      await b.click('.results label.check', 'Humidity 90 %');
      await ctx.sleep(1200);
      await b.click('.results label.check', 'Flight levels');
      await ctx.sleep(1500);
      const d = (await b.locate('.results .section svg', null, {scroll: false})).rect;
      await b.moveTo(d.x + d.w * 0.2, d.y + d.h * 0.7, 600);
      await b.moveTo(d.x + d.w * 0.8, d.y + d.h * 0.4, 2500);
      await ctx.sleep(800);
    });
  },

  // A11: a flight along the path: the values met at the passing times
  async flight(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, producer: 'meps_ml', source: 'querydata', params: 'temperature,windspeedms,winddirection,humidity',
                    locmode: 'path', location: '24.25,59.75,24.95,60.17,25.85,60.5'});
    await scrollToElement(ctx, 'h2', 'Where', 10);
    await ctx.anim('flight', async () => {
      await ctx.sleep(600);
      await b.click('input[aria-label="Departure (UTC) text"]');
      await b.type('2026-10-15 03:00', 60);
      await b.click('input[aria-label="Arrival (UTC) text"]');
      await b.type('2026-10-15 09:00', 60);
      await b.key('Tab', 'Tab', 9);
      await ctx.sleep(800);
      await scrollToElement(ctx, 'button.primary', 'Run', 200);
      await run(ctx);
      await scrollToElement(ctx, '.results-head', null, 10);
      await b.select('.results select[aria-label="Top of the cross section"]', '100 hPa');
      await ctx.sleep(2000);
      await b.click('.results [role=tab]', 'Along the path');
      await ctx.sleep(2500);
      await hoverChart(ctx, 0.15, 0.85);
      await ctx.sleep(800);
    });
  },

  // A12: an ensemble: percentiles, all the members, the probability of a threshold, the EPSgram
  async ensemble(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, producer: 'ecmwf_eps', source: 'querydata', params: 'temperature', locmode: 'place',
                    location: 'Helsinki', start: '2014-02-20T06:00', end: '2014-03-07T00:00'});
    await run(ctx, {click: false});
    await scrollToElement(ctx, '.results-head', null, 10);
    await ctx.anim('ensemble', async () => {
      await ctx.sleep(1500);
      await b.click('.results button', 'All members');
      await ctx.sleep(2200);
      await b.click('.results button', 'Probability');
      await ctx.sleep(1200);
      await b.select('.results select[aria-label="Side of the threshold of temperature"]', 'Below');
      await ctx.sleep(600);
      await b.click('.results input[aria-label="Threshold of temperature"]');
      await b.evaluate(`document.activeElement.select()`);
      await b.type('-5', 200);
      await b.key('Enter', 'Enter', 13);
      await ctx.sleep(2500);
      await b.click('.results [role=tab]', 'Meteogram').catch(async () => {
        await scrollToElement(ctx, 'button.small', 'Meteogram', 200);
        await b.click('button.small', 'Meteogram');
      });
      await b.waitFor(`!!document.querySelector('.results [role=tab][aria-selected=true]') &&
                       document.querySelector('.results [role=tab][aria-selected=true]').textContent === 'Meteogram'`, 'the EPSgram', 60000);
      await ctx.sleep(1000);
      await scrollToElement(ctx, '.results-head', null, 10);
      await ctx.sleep(3000);
    }, {crop: RIGHT});
  },

  // A13: EDR: a collection, its parameters, an area drawn on the map, the results
  async edr(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: '/edr'});
    await ctx.anim('edr', async () => {
      await ctx.sleep(600);
      await b.click('input.ac-input[aria-label="Collection"]');
      await b.type('pal_sk');
      await ctx.sleep(1200);
      await b.click('.ac-list li', 'pal_skandinavia');
      await ctx.sleep(1500);
      await b.click('input.ac-input[aria-label="Parameter"]').catch(() => {});
      await b.type('temp');
      await ctx.sleep(1000);
      await b.click('.ac-list li', 'Temperature');
      await ctx.sleep(1000);
      await scrollToElement(ctx, 'h2', 'Where', 10);
      await b.click('.segmented button', 'Area');
      await ctx.sleep(500);
      const p0 = await b.mapPixel(22, 60);
      const p1 = await b.mapPixel(27, 62.5);
      await b.drag(p0.x, p0.y, p1.x, p1.y, 1200);
      await ctx.sleep(1000);
      await scrollToElement(ctx, 'button.primary', 'Run', 200);
      await run(ctx);
      await scrollToElement(ctx, '.results-head', null, 10);
      await ctx.sleep(2500);
      await b.click('.results [role=tab]', 'Map');
      await ctx.sleep(2500);
    });
  },

  // A14: Coverages: a box in another projection, the size estimate, two linked maps
  async coverages(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: '/coverages', collection: 'pal_skandinavia', params: 'temperature,windspeedms',
                    format: 'geotiff'});
    await ctx.anim('coverages', async () => {
      await ctx.sleep(600);
      await scrollToElement(ctx, 'h2', 'Where', 10);
      await b.click('section.step button', 'Box');
      await ctx.sleep(400);
      const p0 = await b.mapPixel(19, 59.5);
      const p1 = await b.mapPixel(32, 70);
      await b.drag(p0.x, p0.y, p1.x, p1.y, 1300);
      await ctx.sleep(1000);
      await b.select('select[aria-label="Output projection"]', 'ETRS-TM35FIN');
      await ctx.sleep(1800);
      await scrollToElement(ctx, 'code.url', null, 10);
      await ctx.sleep(1500);
      await b.click('button.small', 'Show maps');
      await b.waitFor(`document.querySelectorAll('.results .xmap, .coverage-maps-step .xmap').length >= 2`, 'the maps', 60000).catch(() => {});
      await ctx.sleep(2500);
      await scrollToElement(ctx, 'h2', 'Maps', 10);
      await ctx.sleep(800);
      const m = await b.locate('.coverage-maps-step .xmap svg', null, {scroll: false});
      await b.drag(m.x, m.y, m.x + 90, m.y + 40, 1200);
      await ctx.sleep(1200);
      await b.click('.coverage-maps-step .time-play');
      await ctx.sleep(4000);
      await b.click('.coverage-maps-step .time-play');
      await ctx.sleep(1500);
    });
  },

  // S10: the native grid of a Coverages collection and the estimated size of a request
  async coverageMetadata(ctx)
  {
    await ctx.open({api: '/coverages', collection: 'pal_skandinavia', params: 'temperature,windspeedms',
                    crs: 'EPSG:3067', location: '19,59.5,32,70', format: 'geotiff'});
    await ctx.sleep(1500);
    // The collection card and the request beside it
    const card = await clipOf(ctx, 'section.step', {text: 'What', margin: 4});
    await ctx.still('coverage-metadata', {x: 0, y: card.y, width: ctx.b.width, height: card.height});
  },

  // A15: the settings: knots and a 12-hour clock, then the page in Finnish
  async settings(ctx)
  {
    const {b} = ctx;
    await ctx.open({...PAL, params: 'windspeedms', locmode: 'place', location: 'Helsinki'});
    await run(ctx, {click: false});
    await ctx.anim('settings', async () => {
      await ctx.sleep(1200);
      await b.click('nav .settings-button');
      await ctx.sleep(900);
      await b.select('.settings-panel select[aria-label="Wind speed unit"]', 'kt');
      await ctx.sleep(1200);
      await b.select('.settings-panel select[aria-label="Clock"]', '12-hour');
      await ctx.sleep(1500);
      await b.click('.settings-panel button[aria-label="Close the settings"]');
      await ctx.sleep(2200);
      await b.click('nav .settings-button');
      await ctx.sleep(700);
      await b.select('.settings-panel select[aria-label="Language"]', 'Suomi');
      await b.waitFor(`document.documentElement.lang === 'fi' && !!document.querySelector('nav')`, 'the Finnish page', 30000);
      await ctx.sleep(1500);
      await run(ctx, {click: false});
      await ctx.sleep(3000);
    });
    // Back to English, the units and the clock of the defaults, for the other scenes
    await b.evaluate(`localStorage.clear()`);
  },

  // S9: the sensors of observation stations
  async sensors(ctx)
  {
    const {b} = ctx;
    await ctx.open({api: TS, producer: 'opendata', source: 'observation', params: 't2m', locmode: 'fmisid',
                    location: '101004', start: '2013-08-05T00:00', end: '2013-08-06T00:00', timestep: 'data'});
    await ctx.sleep(2500);
    await ctx.still('sensors', await clipOf(ctx, 'section.step', {text: 'What', margin: 4}));
  },

  // S11: an error of the server, shown below the request
  async error(ctx)
  {
    await ctx.open({...PAL, params: 'temperature', locmode: 'place', location: 'Atlantis'});
    await run(ctx, {click: false});
    await ctx.still('error', await clipOf(ctx, '.request .url-row, .request .request-note', {all: true}));
  },
};

// Prueba de la interfaz de MINDSCOPE con un puente JUCE simulado: node tests/ui.test.mjs
import { chromium } from '/opt/npm-tools/node_modules/playwright/index.mjs';
import path from 'node:path';
const file = 'file://' + path.resolve(process.argv[2] || 'ui/index.html') + '#test';
const out = process.argv[3] || '/tmp/ms-shots';
let fails = 0; const ok = (c, m) => { if (!c) { fails++; console.log('  FALLO: ' + m); } else console.log('  ok: ' + m); };

const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1100, height: 700 } });
const errors = []; page.on('pageerror', e => errors.push(e.message)); page.on('console', m => { if (m.type() === 'error') errors.push(m.text()); });
await page.addInitScript(() => {
  window.__sent = []; window.__ls = {};
  window.__JUCE__ = { backend: { emitEvent: (e, o) => window.__sent.push([e, JSON.parse(JSON.stringify(o || {}))]), addEventListener: (e, f) => { window.__ls[e] = f; } } };
  window.__fire = (e, v) => window.__ls[e] && window.__ls[e](v);
});
await page.goto(file);
await page.waitForTimeout(300);
const sent = () => page.evaluate(() => window.__sent);
const clear = () => page.evaluate(() => { window.__sent = []; });
const lastSettings = async () => { const s = (await sent()).filter(([e]) => e === 'msSettings'); return s.length ? s[s.length - 1][1] : null; };

ok((await sent()).some(([e]) => e === 'msReady'), 'avisa al plugin al arrancar (msReady)');

// ajustes que manda el plugin + frecuencias del espectro
const freqs = Array.from({ length: 256 }, (_, i) => Math.round(20 * Math.pow(900, i / 255) * 10));
await page.evaluate(f => window.__fire('msSettings', { theme: 'dark', slope: 4.5, speed: 1, target: -8, autoreset: true, wave: 4, view: 'all', sr: 48000, freqs: f }), freqs);

// 2 s de estado como lo manda el plugin (60 por segundo)
async function feed (frames, over = {}) {
  await page.evaluate(([n, over]) => {
    for (let k = 0; k < n; k++) {
      const w = []; for (let c = 0; c < 3; c++) { const ph = ((k * 3 + c) % 97) / 97; w.push(800, Math.round(900 * Math.exp(-ph * 6)), 350, 120); }
      const sl = Array.from({ length: 256 }, (_, i) => -300 - i);
      const sc = []; for (let i = 0; i < 200; i++) { const m = Math.sin(i / 7) * 600; sc.push(Math.round(m + 50), Math.round(m - 50)); }
      window.__fire('msState', Object.assign({ w, sl, sp: sl.map(x => x + 30), sc, m: -9.1, s: -8.2, i: -8.6, lra: 4.8, tp: -1.4, mmax: -6.3,
        pl: -1.2, pr: -1.6, tpw: -1.0, rl: -9, rr: -9.4, c: 0.55, cl: 0.98, ll: -16, ls: -30, play: true }, over));
    }
  }, [frames, over]);
  await page.waitForTimeout(700);   // deja pasar el aviso de graves (0,4 s)
  await page.evaluate(() => window.__fire('msState', { m: -9.1 }));
  await page.waitForTimeout(120);
}
await feed(120);

ok(await page.textContent('#vS') === '-8.2', 'short term del plugin');
ok(await page.textContent('#vI') === '-8.6', 'integrado del plugin');
ok((await page.textContent('#dI')).includes('-0.6 LU'), 'diferencia con el objetivo (-8 club): -0.6 LU');
ok((await page.textContent('#vTP')).startsWith('-1.4'), 'true peak');
ok(!(await page.getAttribute('#cTP', 'class')).includes('warn'), 'TP a -1.4: sin aviso');
ok((await page.textContent('#vLRA')).startsWith('4.8'), 'LRA');
ok(await page.textContent('#lowchip') === 'LOW MONO OK', 'graves en mono: LOW MONO OK');
ok(await page.textContent('#nC') === '+0.55', 'correlación general');

// los canvas tienen dibujo
const painted = id => page.evaluate(id => { const c = document.getElementById(id); const d = c.getContext('2d').getImageData(0, 0, c.width, c.height).data; let n = 0; for (let i = 3; i < d.length; i += 16) if (d[i] > 0) n++; return n; }, id);
for (const id of ['wave', 'spec', 'hist', 'scope']) ok(await painted(id) > 200, 'el canvas ' + id + ' dibuja');
await page.screenshot({ path: out + '-dark.png' });

// avisos: true peak por encima de -1 y graves en estéreo
await feed(40, { tp: -0.3, cl: 0.2, ls: -2 });
ok((await page.getAttribute('#cTP', 'class')).includes('warn'), 'TP a -0.3 dBTP: aviso en coral');
ok(await page.textContent('#lowchip') === 'WIDE LOWS', 'graves en estéreo: WIDE LOWS');
await page.screenshot({ path: out + '-warn.png' });

// RESET
await clear();
await page.click('#reset');
ok((await sent()).some(([e]) => e === 'msReset'), 'RESET avisa al plugin');

// lectura del espectro al pasar el ratón
const sb = await page.locator('#cvSpec').boundingBox();
await page.mouse.move(sb.x + sb.width * 0.4, sb.y + sb.height * 0.5); await page.waitForTimeout(100);
const rd = await page.textContent('#sread');
ok(/Hz/.test(rd) && /dB/.test(rd) && /[A-G]#?\d/.test(rd), 'lectura del espectro: frecuencia, nota y dB (' + rd + ')');
await page.mouse.move(5, 5);

// FREEZE: el espectro deja de cambiar
await page.click('#freeze');
const before = await page.evaluate(() => JSON.stringify(SL.slice(0, 5)));
await page.evaluate(() => window.__fire('msState', { sl: Array(256).fill(-100), sp: Array(256).fill(-90) }));
ok(await page.evaluate(() => JSON.stringify(SL.slice(0, 5))) === before, 'FREEZE congela el espectro');
await page.click('#freeze');

// cambios de vista, inclinación y onda: se guardan y se deshacen
await clear();
await page.click('#slope button:nth-child(2)');
ok((await lastSettings())?.slope === 3, 'inclinación 3 dB/oct se guarda en el plugin');
await page.click('#wlen button:nth-child(3)');
ok((await lastSettings())?.wave === 8, 'onda de 8 s se guarda');
await page.dblclick('#pSpec .nm');
ok(await page.isVisible('#pSpec') && !(await page.isVisible('#pWave')) && !(await page.isVisible('#pLoud')), 'doble clic en SPECTRUM: ocupa toda la ventana');
const big = await page.locator('#pSpec').boundingBox();
ok(big.width > 900, 'el espectro maximizado es ancho (' + Math.round(big.width) + ' px)');
await page.screenshot({ path: out + '-max.png' });
await page.keyboard.press('Meta+z');
ok(await page.isVisible('#pWave') && await page.isVisible('#pLoud'), 'Cmd+Z vuelve a las cuatro vistas');
await page.keyboard.press('Meta+z');
ok((await lastSettings())?.wave === 4, 'Cmd+Z deshace la longitud de la onda');
await page.click('#view button:nth-child(5)');
ok(await page.isVisible('#pSt') && !(await page.isVisible('#pSpec')), 'botón STEREO de la cabecera: solo estéreo');
await page.click('#view button:nth-child(1)');

// botones de una misma fila del mismo tamaño
const sameW = async sel => { const ws = await page.$$eval(sel, bs => bs.map(b => Math.round(b.getBoundingClientRect().width))); return ws.every(w => w === ws[0]); };
ok(await sameW('#view button'), 'botones de la cabecera iguales');
ok(await sameW('#wlen button') && await sameW('#slope button'), 'botones pequeños iguales');

// ajustes: tema claro, objetivo y auto reset
await page.click('#gear');
await page.click('#target button:nth-child(2)');
ok((await lastSettings())?.target === -14, 'objetivo -14 (streaming) se guarda');
ok((await page.textContent('#tchip')).includes('-14'), 'el chip muestra el objetivo');
await page.click('#autoreset button:nth-child(2)');
ok((await lastSettings())?.autoreset === false, 'auto reset apagado se guarda');
await page.click('#theme button:nth-child(2)');
ok(await page.evaluate(() => document.documentElement.classList.contains('light')), 'tema claro');
await page.screenshot({ path: out + '-settings.png' });
await page.click('#mclose');
await feed(30);
await page.screenshot({ path: out + '-light.png' });

// tamaño mínimo de la ventana: nada se sale
await page.setViewportSize({ width: 940, height: 600 });
await page.waitForTimeout(200);
const over = await page.evaluate(() => [...document.querySelectorAll('.panel')].filter(p => p.offsetParent).some(p => { const r = p.getBoundingClientRect(); return r.right > innerWidth - 18 || r.bottom > innerHeight - 18; }));
ok(!over, 'a 940 x 600 (tamaño mínimo) los paneles caben dentro del marco');
const fit = await page.evaluate(() => { const t = [...document.querySelectorAll('.ptitle')].every(x => x.scrollWidth <= x.clientWidth + 1);
  const sc = document.querySelector('#scope').getBoundingClientRect(), hi = document.querySelector('#hist').getBoundingClientRect(), bars = document.querySelector('.bars').getBoundingClientRect(), pl = document.querySelector('#pLoud').getBoundingClientRect();
  return { t, scope: Math.min(sc.width, sc.height), hist: hi.height, bars: bars.bottom <= pl.bottom }; });
ok(fit.t, 'a tamaño mínimo los títulos de los paneles caben');
ok(fit.scope >= 130 && fit.hist >= 22 && fit.bars, 'a tamaño mínimo se ven el vectorscopio (' + Math.round(fit.scope) + ' px), el historial y las barras L/R');
await page.screenshot({ path: out + '-small.png' });

ok(errors.length === 0, 'sin errores de JavaScript' + (errors.length ? ': ' + errors.join(' | ') : ''));
await browser.close();
console.log(fails ? `\n${fails} fallos` : '\nTodo bien');
process.exit(fails ? 1 : 0);

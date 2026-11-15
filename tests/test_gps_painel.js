// Painel aberto pelo servidor_sbp.py com a API Olho Vivo falsa: OSM primeiro, GPS ligado,
// onibus ao vivo no mapa e previsao GPS no popup da parada. E aberto como arquivo: aviso.
// Requer: npm i playwright; Leaflet/mqtt.js locais em VENDOR. Rodar da pasta tests/.
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
const { spawn } = require('child_process');
const path = require('path');
const V = process.env.VENDOR || '/tmp/vend/';
const py = process.env.PYTHON || 'python3';
const espera = ms => new Promise(r => setTimeout(r, ms));
(async () => {
  const falsa = spawn(py, [path.join(__dirname, 'olhovivo_falso.py'), '8765'], { env: { ...process.env, SBP_FAKE_AGORA: '18:00' } });
  const srv = spawn(py, [path.join(__dirname, '..', 'servidor_sbp.py'), '--porta', '8797', '--sem-navegador', '--sem-ponte'],
    { env: { ...process.env, SPTRANS_API: 'http://127.0.0.1:8765/v2.1', SPTRANS_TOKEN: 'token-de-teste' } });
  await espera(4500);
  let falhas = 0; const ok = (c, n) => { console.log((c ? '  ok   ' : '  FALHOU ') + n); if (!c) falhas++; };
  const browser = await chromium.launch();
  try {
    const page = await browser.newPage({ viewport: { width: 1400, height: 900 } });
    const erros = []; page.on('pageerror', e => erros.push(e.message));
    const tiles = [];
    await page.route('https://unpkg.com/leaflet@1.9.4/dist/leaflet.js', r => r.fulfill({ path: V + 'leaflet-1.9.4/dist/leaflet.js' }));
    await page.route('https://unpkg.com/leaflet@1.9.4/dist/leaflet.css', r => r.fulfill({ path: V + 'leaflet-1.9.4/dist/leaflet.css', contentType: 'text/css' }));
    await page.route('https://unpkg.com/mqtt@5.10.1/dist/mqtt.min.js', r => r.fulfill({ path: V + 'mqtt-5.10.1/dist/mqtt.min.js' }));
    await page.route(/openstreetmap|cartocdn/, r => { tiles.push(r.request().url()); r.abort(); });
    await page.route(/hivemq/, r => r.abort());
    await page.goto('http://127.0.0.1:8797/painel/');
    await page.waitForTimeout(800);
    ok(/ligado/.test(await page.textContent('#stGps')), 'pill: ' + await page.textContent('#stGps'));
    await page.click('#tExplorar'); await page.waitForTimeout(1500);
    ok(tiles.length && /openstreetmap/.test(tiles[0]), 'aberto por http: usa o OpenStreetMap primeiro');
    const prev = await page.textContent('#pqLinhas');
    ok(/5757-51/.test(prev) && /\d+ min/.test(prev) && /GPS/.test(prev), 'parada mostra quando chega cada ônibus (GPS): ' + prev.replace(/\s+/g, ' ').slice(0, 80));
    await page.locator('#pqLinhas li button', { hasText: '5757-51' }).first().click(); await page.waitForTimeout(1500);
    ok(/\b([4-9]|1\d) ônibus agora/.test(await page.textContent('#aoVivoInfo')), 'GPS ao vivo liga sozinho: ' + (await page.textContent('#aoVivoInfo')).trim());
    const amarelos = await page.locator('#mapa path[fill="#ffc53d"]').count();
    ok(amarelos >= 4, 'os ônibus aparecem no mapa sem clicar em nada (' + amarelos + ')');
    await page.click('#bAoVivo'); await page.waitForTimeout(200);
    ok(await page.locator('#mapa path[fill="#ffc53d"]').count() === 0, 'botão Pausar tira os ônibus do mapa');
    await page.click('#bAoVivo'); await page.waitForTimeout(1200);
    ok(await page.locator('#mapa path[fill="#ffc53d"]').count() >= 4, 'botão Ver ao vivo traz os ônibus de volta');
    await page.click('#tTotem'); await page.waitForTimeout(200);
    ok(await page.locator('#mapa path[fill="#ffc53d"]').count() === 0, 'saindo da aba o GPS para de consultar');
    await page.click('#tExplorar'); await page.waitForTimeout(1200);
    ok(await page.locator('#mapa path[fill="#ffc53d"]').count() >= 4, 'voltando pra aba o GPS religa sozinho');
    await page.locator('#lSentidos button', { hasText: 'Pedreira' }).click(); await page.waitForTimeout(200);
    await page.locator('#lParadas li button').first().click(); await page.waitForTimeout(1200);
    const pop = await page.textContent('.leaflet-popup-content');
    ok(/Chegando \(GPS/.test(pop) && /5757-51 → PEDREIRA: \d+ min/.test(pop), 'popup da parada mostra previsão GPS: ' + pop.split('Chegando')[1]);
    await page.locator('#lSentidos button', { hasText: 'Metrô Conceição' }).click(); await page.waitForTimeout(300);
    const primeira = page.locator('#lParadas li').first();
    ok(/aqui/.test(await primeira.getAttribute('class')) && /De onde ela vem/.test(await page.textContent('#lTitulo')),
      'sentido que termina aqui: lista invertida, "você está aqui" primeiro');
    ok(/−\d+ min/.test(await page.textContent('#lParadas li:nth-child(2) .min')), 'as paradas seguintes mostram quanto antes o ônibus passa');
    ok(erros.length === 0, 'sem erros de JS' + (erros.length ? ': ' + erros.join(' | ') : ''));

    const p2 = await browser.newPage();
    await p2.route(/unpkg|openstreetmap|cartocdn|hivemq/, r => r.abort());
    await p2.goto('file://' + path.resolve(__dirname, '../painel/index.html')); await p2.waitForTimeout(500);
    ok(/abra pelo servidor/.test(await p2.textContent('#stGps')), 'aberto como arquivo: ' + await p2.textContent('#stGps'));
  } finally {
    await browser.close(); srv.kill(); falsa.kill();
  }
  console.log(falhas ? falhas + ' FALHA(S)' : 'TODOS OS TESTES PASSARAM');
  process.exit(falhas ? 1 : 0);
})();
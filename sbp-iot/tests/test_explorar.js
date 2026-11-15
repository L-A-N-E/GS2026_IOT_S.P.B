// Teste E2E da aba Explorar no Chrome headless. CDNs servidas de copia local; tiles do mapa bloqueados (sem internet).
// Requer: npm i playwright (e um Chrome). Leaflet/mqtt.js locais em VENDOR (npm pack leaflet@1.9.4 mqtt@5.10.1).
const { chromium } = require(process.env.PLAYWRIGHT || 'playwright');
const fs = require('fs');
const V = process.env.VENDOR || '/tmp/vend/';
(async () => {
  const browser = await chromium.launch({ args: ['--no-sandbox'] });
  const ctx = await browser.newContext({ viewport: { width: 1400, height: 900 }, geolocation: { latitude: -23.6756, longitude: -46.6427 }, permissions: ['geolocation'] });
  const page = await ctx.newPage();
  const erros = [];
  page.on('pageerror', e => erros.push(e.message));
  page.on('console', m => { if (m.type() === 'error' && !/tile|ERR_|net::|WebSocket|Failed to load resource/i.test(m.text())) erros.push(m.text()); });
  await page.route('https://unpkg.com/leaflet@1.9.4/dist/leaflet.js', r => r.fulfill({ path: V + 'leaflet-1.9.4/dist/leaflet.js' }));
  await page.route('https://unpkg.com/leaflet@1.9.4/dist/leaflet.css', r => r.fulfill({ path: V + 'leaflet-1.9.4/dist/leaflet.css', contentType: 'text/css' }));
  await page.route('https://unpkg.com/mqtt@5.10.1/dist/mqtt.min.js', r => r.fulfill({ path: V + 'mqtt-5.10.1/dist/mqtt.min.js' }));
  await page.route(/cartocdn|arcgisonline|hivemq/, r => r.abort());
  await page.goto('file://' + require('path').resolve(__dirname, '../painel/index.html'));
  let falhas = 0; const ok = (c, n) => { console.log((c ? '  ok   ' : '  FALHOU ') + n); if (!c) falhas++; };

  ok(await page.evaluate(() => window.DADOS_LINHAS.linhas.length) > 1000, 'dados_linhas.js carregou (cidade inteira)');
  // Ponto Virtual com telemetria real -> clicar na linha abre o mapa
  const msgs = fs.readFileSync('/tmp/mqtt_publicados.jsonl', 'utf8').trim().split('\n');
  await page.evaluate(ms => ms.forEach(l => { const [t, m] = l.split('\t'); window.__tratar(t, m); }), msgs);
  await page.click('#tExplorar');
  await page.waitForTimeout(400);
  ok(await page.isVisible('#mapa') && !(await page.isVisible('#pNome')), 'aba Explorar abre e esconde a aba do totem');
  ok(await page.textContent('#lNum') === '5757-51', 'abre com a linha de exemplo 5757-51');
  ok(await page.locator('#mapa path.leaflet-interactive').count() >= 20, 'trajeto (2 sentidos) e paradas desenhados');

  await page.fill('#busca', 'pedreira'); await page.waitForTimeout(400);
  const nres = await page.locator('#resultados li').count();
  ok(nres > 3, 'busca "pedreira" acha linhas/paradas (' + nres + ')');
  await page.fill('#busca', 'conceicao'); await page.waitForTimeout(400);
  ok(await page.locator('#resultados li', { hasText: 'Metrô Conceição' }).count() > 0, 'busca sem acento acha "Metrô Conceição"');
  await page.fill('#busca', '8000'); await page.waitForTimeout(400);
  await page.locator('#resultados li button').first().click(); await page.waitForTimeout(400);
  ok((await page.textContent('#lNum')).startsWith('8000'), 'clicar no resultado abre a linha ' + await page.textContent('#lNum'));
  const nParadas = await page.locator('#lParadas li').count();
  ok(nParadas > 10, 'lista de paradas em ordem (' + nParadas + ')');
  ok(/1 ônibus a cada|sem ônibus/.test(await page.textContent('#lInfo')), 'intervalo atual: ' + (await page.textContent('#lInfo')).slice(0, 80));
  const b2 = page.locator('#lSentidos button').nth(1);
  if (await b2.count()) { const p1 = await page.textContent('#lParadas li:first-child'); await b2.click(); await page.waitForTimeout(200); ok(await page.textContent('#lParadas li:first-child') !== p1, 'trocar sentido troca a lista de paradas'); }
  await page.locator('#lParadas li button').nth(3).click(); await page.waitForTimeout(500);
  ok(await page.locator('.leaflet-popup-content').count() === 1, 'clicar numa parada da lista abre o popup no mapa');
  const nOutras = await page.locator('.leaflet-popup-content button').count();
  ok(nOutras >= 1, 'popup mostra as linhas que passam na parada (' + nOutras + ')');

  // volta pro totem e clica numa linha do Ponto Virtual
  await page.click('#tTotem'); await page.waitForTimeout(200);
  await page.locator('#pLinhas td.num button').first().click(); await page.waitForTimeout(500);
  ok(await page.isVisible('#mapa'), 'clicar numa linha do Ponto Virtual abre o mapa');
  ok(await page.locator('#lParadas li.aqui').count() === 1, 'a parada do totem fica destacada na lista');
  await page.screenshot({ path: __dirname + '/explorar_linha.png' });

  await page.click('#bOndeEstou'); await page.waitForTimeout(1500);
  const perto = await page.textContent('#resultados');
  ok(/Cupece/i.test(perto) && /\d+ m/.test(perto), 'onde estou (simulado na Av. Cupecê) lista paradas próximas');
  await page.screenshot({ path: __dirname + '/explorar_ondeestou.png' });
  ok(erros.length === 0, 'sem erros de JS' + (erros.length ? ': ' + erros.join(' | ') : ''));
  await browser.close();
  console.log(falhas ? falhas + ' FALHA(S)' : 'TODOS OS TESTES PASSARAM');
  process.exit(falhas ? 1 : 0);
})();

// Testa o painel web sem navegador: carrega o <script> do index.html com um DOM falso
// e injeta as mensagens MQTT reais geradas pelo simulador do firmware.
// Rodar: ./sim (gera /tmp/mqtt_publicados.jsonl) e depois: node test_painel.js
const fs = require('fs');
const html = fs.readFileSync(__dirname + '/../painel/index.html', 'utf8');
const js = html.split('<script>')[1].split('</script>')[0];
const els = {};
const mk = () => { const e = { textContent: '', className: '', style: {}, children: [], dataset: {}, title: '', value: '',
  classList: { add() {} }, append(...c) { e.children.push(...c); }, prepend(c) { e.children.unshift(c); }, replaceChildren() { e.children = []; },
  insertRow() { const r = mk(); r.insertCell = () => { const c = mk(); r.children.push(c); return c; }; e.children.push(r); return r; }, remove() {}, get lastChild() { return e.children.at(-1); } }; return e; };
global.document = { getElementById: id => els[id] ||= mk(), createElement: mk, querySelectorAll: () => [] };
global.window = {}; global.location = { search: '' }; global.speechSynthesis = { cancel() {}, speak() {}, getVoices: () => [] };
global.SpeechSynthesisUtterance = function (t) { this.text = t; };
eval(js);
let n = 0, falhas = 0;
const check = (c, nome) => { console.log((c ? '  ok   ' : '  FALHOU ') + nome); if (!c) falhas++; };
for (const l of fs.readFileSync('/tmp/mqtt_publicados.jsonl', 'utf8').trim().split('\n')) { const [t, m] = l.split('\t'); window.__tratar(t, m); n++; }
check(n > 20, n + ' mensagens reais do firmware processadas sem erro');
check(els.pNome.textContent.length > 3, 'nome do ponto: ' + els.pNome.textContent);
check(els.pLinhas.children.length >= 1, 'tabela de linhas preenchida (' + els.pLinhas.children.length + ')');
check(+els.kPres.textContent >= 2 && +els.kAud.textContent >= 2, 'KPIs IoB: ' + els.kPres.textContent + ' presencas, ' + els.kAud.textContent + ' audios');
check(/maior movimento/.test(els.leitura.textContent), 'leitura IoB: ' + els.leitura.textContent);
check(/Próximos? ônibus|O ônibus da linha/.test(els.fala.textContent), 'frase de voz: ' + els.fala.textContent);
check(els.feed.children.length >= 4, 'feed de eventos (' + els.feed.children.length + ')');
process.exit(falhas ? 1 : 0);

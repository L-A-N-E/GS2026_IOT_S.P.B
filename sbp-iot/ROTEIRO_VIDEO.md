# Roteiro do vídeo (até 4 min)

Deixe abertos lado a lado: o Wokwi (simulação rodando) e o `painel/index.html`. No painel, clique em **Ativar voz neste navegador** antes de gravar.

| Tempo | O que mostrar | O que falar |
|---|---|---|
| 0:00–0:35 | Slide ou README com a Ficha | "O S.B.P., Smart Bus Point, ataca um problema de quem pega ônibus em São Paulo: no ponto, ninguém sabe quando o ônibus chega. Para quem não enxerga ou está sem celular é pior ainda. A solução tem um totem no ponto, um app e um site. Esta entrega é o totem." |
| 0:35–1:05 | Circuito no Wokwi, apontando cada peça | "O ESP32 é o cérebro. O display mostra a previsão, o botão verde é o botão de acessibilidade, o sensor de presença percebe quem chega, o LED indica de longe se o ônibus está perto e o potenciômetro simula o trânsito." |
| 1:05–1:40 | Display no Metrô Conceição; ligar a chave da demo e esperar "ÔNIBUS NO PONTO" | "Os horários são reais, vêm do GTFS da SPTrans e ficam gravados no ESP32. Por isso o totem funciona até sem internet. Aqui é o ponto final da linha 5757-51: em cima o ponto e a hora, depois a linha, quando sai e os próximos. Acelerando o relógio dá pra ver a contagem até aparecer NO PONTO - EMBARQUE, e o LED vai de amarelo para verde." |
| 1:40–2:15 | Apertar o botão verde; o painel fala a frase | "Uma pessoa cega aperta o botão e ouve a previsão. O buzzer dá o código de bipes e a mesma frase vai por MQTT para o Ponto Virtual, que lê em voz alta." |
| 2:15–2:45 | Botão azul: Av. Cupecê; os 3 próximos; apertar o verde de novo | "O mesmo totem serve em qualquer parada da cidade. Neste ponto passam 12 linhas: o display mostra os 3 próximos ônibus, do mais perto pro mais longe, e o LED fala sempre do primeiro. O botão de áudio lê os três." |
| 2:45–3:10 | Girar o potenciômetro (a previsão aumenta); mandar um aviso pelo painel | "Com trânsito, a previsão atrasa e o LED muda. E o backend consegue falar com o totem: mando um aviso daqui e ele aparece no display." |
| 3:10–3:45 | Clicar no PIR algumas vezes; gráfico IoB do painel | "Isto é o IoB: o totem conta as chegadas por hora e quantas vezes o áudio é usado, sem identificar ninguém. O painel compara com a quantidade de ônibus daquela hora. Com esse dado, a prefeitura sabe em que horário o ponto lota e se a acessibilidade está sendo usada." |
| 3:45–4:00 | Voltar ao totem | "O totem é a parte física do S.B.P.: leva a informação para quem está no ponto, com ou sem celular." |

Antes de gravar: ensaie uma vez. O sensor de presença do Wokwi fica alguns segundos "ativo" depois de cada clique, então espere ele apagar antes de clicar de novo, senão conta uma chegada só.

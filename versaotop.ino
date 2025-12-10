#include <WiFi.h>
#include <WebServer.h>

#define PIN_SENSOR 35   // pino do SCT013

// ===== Config do CamarVolt AP =====
const char* ap_ssid     = "CAMARVOLT";
const char* ap_password = "camargo2001";   // pode mudar

WebServer server(80);

float corrente = 0.0;
float ruido_base = -1.0;

// ===== Parâmetros de energia (ajuste conforme sua rede) =====
const float VOLTAGEM = 127.0;   // se sua rede for 220V, mude para 220.0
const float FATOR_POT = 0.90;   // estimativa do fator de potência

// ===== Histórico de energia (até 1 hora, 3600 segundos) =====
const int HISTORY_SECONDS = 3600;
float energyHistory[HISTORY_SECONDS];  // Wh por segundo
unsigned long lastEnergyUpdate = 0;

// ==== Função para calcular energia nos últimos X segundos ====
float energiaUltimosSegundos(int segundos) {
  unsigned long agoraSeg = millis() / 1000;
  if (agoraSeg == 0) return 0.0;

  if (segundos > HISTORY_SECONDS) segundos = HISTORY_SECONDS;

  float soma = 0.0;
  int maxSeg = (agoraSeg < (unsigned long)segundos) ? agoraSeg : (unsigned long)segundos;

  for (int i = 0; i < maxSeg; i++) {
    unsigned long t = agoraSeg - i;
    int idx = t % HISTORY_SECONDS;
    soma += energyHistory[idx];
  }

  return soma; // Wh
}

// ===== Página principal (HTML + Chart.js + calibração) =====
void handleRoot() {
  String html =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'/>"
    "<meta name='viewport' content='width=device-width, initial-scale=1'/>"
    "<title>CamarVolt - Monitor</title>"

    // Chart.js CDN
    "<script src='https://cdn.jsdelivr.net/npm/chart.js'></script>"

    "<style>"
    "body{margin:0;font-family:Arial,Helvetica,sans-serif;background:#000;color:#f9b23a;}"
    "header{display:flex;align-items:center;justify-content:space-between;padding:10px 20px;background:#111;border-bottom:2px solid #f9b23a;}"
    ".brand{display:flex;align-items:center;gap:12px;}"
    ".brand img{height:40px;}"
    ".brand-title{font-size:22px;font-weight:bold;letter-spacing:1px;}"
    ".badge{display:inline-block;padding:2px 6px;border-radius:4px;border:1px solid #f9b23a;font-size:11px;margin-left:6px;}"
    "a{color:#f9b23a;text-decoration:none;}"
    "a:hover{text-decoration:underline;}"
    ".container{padding:20px;max-width:900px;margin:0 auto;}"
    ".card{background:#111;border:1px solid #333;border-radius:10px;padding:16px;margin-bottom:16px;box-shadow:0 0 10px rgba(0,0,0,0.6);}"
    ".card-title{font-size:18px;margin-bottom:8px;font-weight:bold;color:#f9b23a;}"
    ".current{font-size:46px;font-weight:bold;}"
    ".unit{font-size:22px;opacity:0.8;margin-left:4px;}"
    ".sub{font-size:13px;color:#ccc;}"
    "table{width:100%;border-collapse:collapse;margin-top:8px;}"
    "th,td{text-align:left;padding:8px;font-size:14px;}"
    "th{background:#1a1a1a;border-bottom:1px solid #333;}"
    "tr:nth-child(even){background:#141414;}"
    ".footer{margin-top:12px;font-size:12px;color:#aaa;text-align:center;opacity:0.7;}"
    ".btn{padding:8px 14px;background:#f9b23a;border:none;border-radius:6px;color:#000;font-weight:bold;cursor:pointer;margin-left:10px;}"
    ".btn:hover{filter:brightness(1.1);}"
    "#loading{font-size:24px;text-align:center;margin-top:80px;}"
    ".hidden{display:none;}"
    "</style>"

    "</head><body>"

    "<header>"
      "<div class='brand'>"
        "<img src='https://camarvolt.github.io/CamarVolt-Logo-Transparent.png' alt='CamarVolt Logo'/>"
        "<div class='brand-title'>CamarVolt <span class='badge'>BETA</span></div>"
      "</div>"
      "<div>"
        "<a href='https://camarvolt.github.io/' target='_blank'>Site oficial</a>"
        "<button class='btn' onclick='calibrar()'>Calibrar</button>"
      "</div>"
    "</header>"

    "<div id='loading'>Aguardando calibração do sensor...</div>"

    "<div id='main' class='hidden container'>"

      "<div class='card'>"
        "<div class='card-title'>Corrente instantânea</div>"
        "<div class='current' id='corrente'>0.00<span class='unit'>A</span></div>"
        "<div class='sub'>Leitura aproximada via sensor SCT-013 (calibrado por ruído).</div>"
      "</div>"

      "<div class='card'>"
        "<div class='card-title'>Corrente (últimos 60 segundos)</div>"
        "<canvas id='grafico' height='120'></canvas>"
      "</div>"

      "<div class='card'>"
        "<div class='card-title'>Consumo estimado de energia</div>"
        "<div class='sub'>"
        "Cálculo aproximado assumindo " + String(VOLTAGEM, 0) + "V, fator de potência " + String(FATOR_POT, 2) + ". "
        "Valores em kWh (quilowatt-hora)."
        "</div>"
        "<table>"
          "<tr><th>Janela</th><th>Energia (kWh)</th></tr>"
          "<tr><td>Último 1 minuto</td><td id='kwh1'>0.0000</td></tr>"
          "<tr><td>Últimos 5 minutos</td><td id='kwh5'>0.0000</td></tr>"
          "<tr><td>Últimos 30 minutos</td><td id='kwh30'>0.0000</td></tr>"
          "<tr><td>Última 1 hora</td><td id='kwh60'>0.0000</td></tr>"
        "</table>"
      "</div>"

      "<div class='footer'>"
        "CamarVolt &mdash; Monitor de Energia • Protótipo acadêmico • "
        "Assume tensão fixa e fator de potência médio para estimar consumo."
      "</div>"

    "</div>"

    "<script>"
    "let calibrado = false;"

    "let chartData = {"
      "labels: [],"
      "datasets: [{"
        "label:'Corrente (A)',"
        "borderColor:'#f9b23a',"
        "backgroundColor:'rgba(249,178,58,0.15)',"
        "data:[],"
        "tension:0.2,"
        "fill:true"
      "}]"
    "};"

    "const ctx = document.getElementById('grafico').getContext('2d');"
    "const grafico = new Chart(ctx, {"
      "type:'line',"
      "data:chartData,"
      "options:{"
        "responsive:true,"
        "plugins:{legend:{display:false}},"
        "scales:{"
          "x:{ticks:{color:'#ccc'}},"
          "y:{ticks:{color:'#ccc'},min:0}"
        "}"
      "}"
    "});"

    "async function atualizar(){"
      "let r = await fetch('/api/status');"
      "let j = await r.json();"

      "if(!j.calibrado){"
        "document.getElementById('loading').classList.remove('hidden');"
        "document.getElementById('main').classList.add('hidden');"
        "return;"
      "} else {"
        "if(!calibrado){"
          "calibrado = true;"
          "document.getElementById('loading').classList.add('hidden');"
          "document.getElementById('main').classList.remove('hidden');"
        "}"
      "}"

      "document.getElementById('corrente').innerHTML = j.corrente.toFixed(2) + '<span class=\"unit\">A</span>';"

      "document.getElementById('kwh1').textContent  = j.kwh1.toFixed(4);"
      "document.getElementById('kwh5').textContent  = j.kwh5.toFixed(4);"
      "document.getElementById('kwh30').textContent = j.kwh30.toFixed(4);"
      "document.getElementById('kwh60').textContent = j.kwh60.toFixed(4);"

      "const t = new Date().toLocaleTimeString();"
      "chartData.labels.push(t);"
      "chartData.datasets[0].data.push(j.corrente);"

      "if(chartData.labels.length > 60){"
        "chartData.labels.shift();"
        "chartData.datasets[0].data.shift();"
      "}"

      "grafico.update();"
    "}"

    "async function calibrar(){"
      "await fetch('/api/calibrar');"
      "calibrado = false;"
      "document.getElementById('loading').classList.remove('hidden');"
      "document.getElementById('main').classList.add('hidden');"
    "}"

    "setInterval(atualizar, 1000);"
    "</script>"

    "</body></html>";

  server.send(200, "text/html", html);
}

// ===== API: status em JSON =====
void handleStatus() {
  float wh1  = energiaUltimosSegundos(60);
  float wh5  = energiaUltimosSegundos(300);
  float wh30 = energiaUltimosSegundos(1800);
  float wh60 = energiaUltimosSegundos(3600);

  float kwh1  = wh1  / 1000.0;
  float kwh5  = wh5  / 1000.0;
  float kwh30 = wh30 / 1000.0;
  float kwh60 = wh60 / 1000.0;

  String json = "{";
  json += "\"corrente\":" + String(corrente, 3) + ",";
  json += "\"calibrado\":" + String(ruido_base >= 0 ? "true" : "false") + ",";
  json += "\"kwh1\":"  + String(kwh1, 6)  + ",";
  json += "\"kwh5\":"  + String(kwh5, 6)  + ",";
  json += "\"kwh30\":" + String(kwh30, 6) + ",";
  json += "\"kwh60\":" + String(kwh60, 6);
  json += "}";

  server.send(200, "application/json", json);
}

// ===== API: recalibrar ruído =====
void handleCalibrar() {
  ruido_base = -1.0;  // força recalibração na próxima leitura
  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nIniciando ESP32 + SCT013 (CamarVolt)...");

  // zera histórico
  for (int i = 0; i < HISTORY_SECONDS; i++) {
    energyHistory[i] = 0.0;
  }

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Access Point iniciado. SSID: ");
  Serial.println(ap_ssid);
  Serial.print("IP: ");
  Serial.println(IP);

  server.on("/", handleRoot);
  server.on("/api/status", handleStatus);
  server.on("/api/calibrar", handleCalibrar);
  server.begin();
  Serial.println("Servidor HTTP iniciado.");

  pinMode(PIN_SENSOR, INPUT);
}

void loop() {
  // ===== Leitura do SCT013 com RMS + calibração de ruído =====
  const int amostras = 600;
  float soma_quadrados = 0;

  for (int i = 0; i < amostras; i++) {
    int leitura = analogRead(PIN_SENSOR);            // 0–4095
    float tensao = (leitura * 3.3) / 4095.0;         // volts
    float ac = tensao - 1.65;                        // remove offset DC aproximado
    soma_quadrados += ac * ac;
    delayMicroseconds(200);
  }

  float valor_rms = sqrt(soma_quadrados / amostras);

  // calibra ruído na primeira vez (sem cabo no sensor)
  if (ruido_base < 0) {
    ruido_base = valor_rms;
    Serial.print("Ruido base calibrado: ");
    Serial.println(ruido_base, 6);
  }

  float valor_rms_corrigido = valor_rms - ruido_base;
  if (valor_rms_corrigido < 0) valor_rms_corrigido = 0;
  if (valor_rms_corrigido < 0.002) valor_rms_corrigido = 0;  // mata ruído bem pequeno

  // Corrente em A (ajuste fino se necessário)
  const float CALIBRACAO = 80.0;
  corrente = valor_rms_corrigido * CALIBRACAO;

  // ===== Atualiza histórico de energia 1x por segundo =====
  unsigned long agora = millis();
  if (agora - lastEnergyUpdate >= 1000) {
    lastEnergyUpdate = agora;

    float potencia = corrente * VOLTAGEM * FATOR_POT;   // Watts
    float eSeg = potencia / 3600.0;                     // Wh nesse 1 segundo

    unsigned long agoraSeg = agora / 1000;
    int idx = agoraSeg % HISTORY_SECONDS;
    energyHistory[idx] = eSeg;
  }

  server.handleClient();
}

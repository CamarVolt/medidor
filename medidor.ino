#include <WiFi.h>
#include <WebServer.h>

#define PIN_SENSOR 35   // seu SCT013 está ligado aqui

// ===== ESP32 como Access Point (Wi-Fi próprio) =====
const char* ap_ssid     = "CAMARVOLT";
const char* ap_password = "camargo2001";

WebServer server(80);

float corrente = 0.0;

// guarda o nível de ruído sem carga (calibrado na 1ª vez)
float ruido_base = -1.0;

// fator de calibração (ajustável)
const float CALIBRACAO = 80.0;   // se der muito alto/baixo, muda aqui

void handleRoot() {
  String html =
    "<html><head>"
    "<meta http-equiv='refresh' content='1'/>"
    "<style>"
    "body{font-family:Arial;text-align:center;background:#111;color:#eee;}"
    "h1{margin-top:30px;}"
    "h2{font-size:48px;}"
    "</style>"
    "</head><body>"
    "<h1>Monitor de Corrente - SCT013</h1>"
    "<h2>" + String(corrente, 2) + " A</h2>"
    "<p>Atualiza a cada 1 segundo</p>"
    "</body></html>";

  server.send(200, "text/html", html);
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  Serial.println("\nIniciando ESP32 + SCT013...");

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  IPAddress IP = WiFi.softAPIP();
  Serial.print("Access Point iniciado. SSID: ");
  Serial.println(ap_ssid);
  Serial.print("IP: ");
  Serial.println(IP);
  server.on("/", handleRoot);
  server.begin();
  Serial.println("Servidor HTTP iniciado.");

  pinMode(PIN_SENSOR, INPUT);
}

void loop() {
  const int amostras = 600;
  float soma_quadrados = 0;

  for (int i = 0; i < amostras; i++) {
    int leitura = analogRead(PIN_SENSOR);          // 0–4095
    float tensao = (leitura * 3.3) / 4095.0;       // volts
    float ac = tensao - 1.65;                      // remove DC aproximado
    soma_quadrados += ac * ac;
    delayMicroseconds(200);
  }

  float valor_rms = sqrt(soma_quadrados / amostras);

  // calibra o ruído na primeira vez (sem cabo no sensor!)
  if (ruido_base < 0) {
    ruido_base = valor_rms;
    Serial.print("Ruido base calibrado: ");
    Serial.println(ruido_base, 5);
  }

  // subtrai o ruído de fundo
  float valor_rms_corrigido = valor_rms - ruido_base;
  if (valor_rms_corrigido < 0) valor_rms_corrigido = 0;

  // mata resquício de ruído pequeno
  if (valor_rms_corrigido < 0.002) valor_rms_corrigido = 0;

  // converte pra Ampères (ajusta se quiser)
  corrente = valor_rms_corrigido * CALIBRACAO;

  server.handleClient();
}

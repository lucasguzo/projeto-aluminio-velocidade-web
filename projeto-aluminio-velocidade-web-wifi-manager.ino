#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <WiFiManager.h>      // Configuração de Wi-Fi via celular
#include <HTTPClient.h>       // Para checar versão no GitHub
#include <HTTPUpdate.h>       // Para baixar o firmware novo
#include <WiFiClientSecure.h>

// --- CONFIGURAÇÕES DE VERSÃO E OTA (GITHUB) ---
const String versaoAtual = "1.0.1"; // Mude aqui a cada novo upload pro GitHub
const char* URL_VERSAO = "https://raw.githubusercontent.com/lucasguzo/projeto-aluminio-velocidade-web/refs/heads/main/version.txt";
const char* URL_BINARIO = "https://github.com/lucasguzo/projeto-aluminio-velocidade-web/releases/download/projeto_aluminio_velocidade_web_ota/projeto-aluminio-velocidade-web-wifi-manager.ino.bin";

// --- CONFIGURAÇÕES FÍSICAS (SEU CÓDIGO) ---
const int pinA = 14; 
const int pinB = 27; 
const int PPR = 600; 
const float diametro = 0.063; 
const float PI_VAL = 3.14159;

// --- VARIÁVEIS DE CONTROLE ---
volatile long contadorPulsos = 0;
unsigned long tempoAnterior = 0;
unsigned long ultimaVerificacaoOTA = 0;
bool atualizando = false; // Trava para evitar conflito durante update

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// --- SEU HTML ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html><html><head><meta charset="utf-8">
<style>
  body { background: #000; color: #00ff00; font-family: 'Courier New', monospace; display: flex; flex-direction: column; align-items: center; justify-content: center; height: 100vh; margin: 0; }
  .box { border: 2px solid #00ff00; padding: 30px; border-radius: 10px; text-align: center; width: 400px; }
  #val { font-size: 80px; font-weight: bold; margin: 10px 0; }
  .unit { font-size: 20px; color: #008800; }
</style></head>
<body>
  <div class="box">
    <div style="letter-spacing: 5px;">VELOCIDADE EM TEMPO REAL</div>
    <div id="val">0.0</div>
    <div class="unit">METROS / MINUTO</div>
  </div>
  <script>
    var ws = new WebSocket('ws://' + window.location.hostname + ':81/');
    ws.onmessage = function(e) { document.getElementById('val').innerHTML = e.data; };
    ws.onclose = function() { setTimeout(function(){location.reload()}, 2000); };
  </script>
</body></html>
)rawliteral";

// --- SUA INTERRUPÇÃO ---
void IRAM_ATTR detectaSentido() {
  if (!atualizando) {
    if (digitalRead(pinB) == LOW) { contadorPulsos++; } else { contadorPulsos--; }
  }
}

// --- FUNÇÃO DE ATUALIZAÇÃO (ANÁLISE DO VÍDEO) ---
void verificarAtualizacao() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  Serial.println("Checando GitHub por atualizações...");
  WiFiClientSecure client;
  client.setInsecure(); // Necessário para GitHub HTTPS
  
  HTTPClient http;
  http.begin(client, URL_VERSAO);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    String versaoRemota = http.getString();
    versaoRemota.trim();

    if (versaoRemota != versaoAtual) {
      Serial.println("Nova versão: " + versaoRemota + ". Iniciando OTA...");
      atualizando = true;
      
      // Fecha conexões para liberar RAM e evitar Watchdog
      webSocket.disconnect();
      server.stop();
      
      t_httpUpdate_return ret = httpUpdate.update(client, URL_BINARIO);
      
      if (ret == HTTP_UPDATE_FAILED) {
        Serial.printf("Erro: %s\n", httpUpdate.getLastErrorString().c_str());
        atualizando = false;
        ESP.restart(); // Reinicia para tentar recuperar
      }
    }
  }
  http.end();
}

void setup() {
  Serial.begin(115200);

  // Configuração física
  pinMode(pinA, INPUT_PULLUP);
  pinMode(pinB, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(pinA), detectaSentido, RISING);

  // WiFiManager: Conecta ou cria AP para configurar
  WiFiManager wm;
  // wm.resetSettings(); // Use se quiser forçar o portal de configuração
  if(!wm.autoConnect("Encoder-Config-Portal")) {
      Serial.println("Falha ao conectar.");
      delay(3000);
      ESP.restart();
  }

  Serial.print("Conectado! IP: ");
  Serial.println(WiFi.localIP());
  Serial.print("MAC: ");
  Serial.println(WiFi.macAddress());

  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.begin();
  webSocket.begin();
  
  // Verifica update ao ligar
  verificarAtualizacao();
  
  tempoAnterior = millis();
}

void loop() {
  if (atualizando) return; // Se estiver baixando firmware, para o resto

  server.handleClient();
  webSocket.loop();

  unsigned long tempoAtual = millis();
  
  // SEU CÁLCULO DE VELOCIDADE
  if (tempoAtual - tempoAnterior >= 1000) { 
    noInterrupts();
    long deltaPulsos = contadorPulsos;
    contadorPulsos = 0;
    interrupts();

    float circunferencia = diametro * PI_VAL;
    float distanciaMetros = ((float)deltaPulsos / PPR) * circunferencia;
    float intervaloMinutos = (tempoAtual - tempoAnterior) / 60000.0;
    float metrosMinuto = distanciaMetros / intervaloMinutos;

    String payload = String(metrosMinuto, 1);
    webSocket.broadcastTXT(payload); 

    Serial.print("V: "); Serial.print(payload); Serial.println(" m/min");

    tempoAnterior = tempoAtual;
  }
}
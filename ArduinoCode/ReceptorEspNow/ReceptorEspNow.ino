#include <esp_now.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- CONFIGURAÇÕES DE REDE ---
const char* ssid = "Thigas iPhone";
const char* password = "thiago16";
const char* serverName = "https://tr2-api.tagfernandes.online/sensor"; 

// --- ESTRUTURA DOS DADOS ---
typedef struct struct_message {
  char s[10];
  float temperature;
  float humidity;
} struct_message;

struct_message myData;

// Variáveis para controle
bool newData = false; 
struct_message incomingReadings; 

// --- NOVA VARIÁVEL GLOBAL ---
// Armazena a latência do último POST realizado para enviar no próximo
unsigned long lastLatency = 0; 

// Callback de recebimento
void OnDataRecv(const esp_now_recv_info_t * info, const uint8_t *incomingData, int len) {
  memcpy(&incomingReadings, incomingData, sizeof(incomingReadings));
  newData = true;
  Serial.println("\n--- Dados Recebidos via ESP-NOW ---");
}

// Função para enviar o POST
void sendToAPI() {
  if(WiFi.status() == WL_CONNECTED){
    HTTPClient http;

    http.setTimeout(5000); 
    
    http.begin(serverName);
    http.addHeader("Content-Type", "application/json");

    // 1. CRIANDO O JSON
    JsonDocument doc; 

    // 2. PREENCHENDO O JSON
    doc["sensor"] = incomingReadings.s;
    doc["temperature"] = incomingReadings.temperature;
    doc["humidity"] = incomingReadings.humidity;
    
    // ADICIONADO: Envia a latência registrada no envio ANTERIOR
    doc["latencia"] = lastLatency; 

    String jsonPayload;
    serializeJson(doc, jsonPayload);

    Serial.println("Payload JSON: " + jsonPayload);

    // 3. ENVIO E CÁLCULO DA NOVA LATÊNCIA
    unsigned long startTimer = millis();
    
    int httpResponseCode = http.POST(jsonPayload);
    
    unsigned long endTimer = millis();
    
    // Atualiza a variável global para o PRÓXIMO envio
    lastLatency = endTimer - startTimer; 

    if(httpResponseCode > 0){
      String response = http.getString();
      Serial.print("Código HTTP: ");
      Serial.println(httpResponseCode);
      Serial.print("Resposta da API: ");
      Serial.println(response);
    }
    else {
      Serial.print("Erro no envio HTTP: ");
      Serial.println(httpResponseCode);
    }
    
    // Informa no Serial a latência que acabou de ocorrer (será enviada no próximo JSON)
    Serial.print("Latência deste envio (será usada no próximo): ");
    Serial.print(lastLatency);
    Serial.println(" ms");

    http.end();
  }
  else {
    Serial.println("Wi-Fi Desconectado! Tentando reconectar...");
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);

  WiFi.mode(WIFI_AP_STA); 
  WiFi.begin(ssid, password);
  
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWi-Fi Conectado!");
  
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ao iniciar ESP-NOW");
    ESP.restart();
  }

  esp_now_register_recv_cb(OnDataRecv);
  Serial.println("Gateway Pronto! Aguardando dados...");
}

void loop() {
  if (newData) {
    sendToAPI();
    newData = false;
  }
}
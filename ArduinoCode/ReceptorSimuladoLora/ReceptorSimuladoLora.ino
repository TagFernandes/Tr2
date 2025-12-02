#include <SPI.h>
#include <LoRa.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// --- PINOS HELTEC V2 ---
#define SS      18
#define RST     14
#define DIO0    26
#define BAND    915E6

// --- CONFIGURAÇÕES DE REDE ---
const char* ssid = "Thigas iPhone";
const char* password = "thiago16";
const char* serverUrl = "https://tr2-api.tagfernandes.online/sensor";

// --- VARIÁVEL GLOBAL DE LATÊNCIA ---
// Armazena quanto tempo demorou o último envio para mandar no próximo
unsigned long lastLatency = 0;

void setup() {
  Serial.begin(115200);

  // 1. Configura LoRa
  SPI.begin(5, 19, 27, 18); // SCK, MISO, MOSI, SS
  LoRa.setPins(SS, RST, DIO0);
  
  if (!LoRa.begin(BAND)) {
    Serial.println("Erro ao iniciar LoRa!");
    while (1);
  }
  Serial.println("LoRa Iniciado com sucesso.");

  // 2. Configura WiFi
  WiFi.begin(ssid, password);
  Serial.print("Conectando ao WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado!");
}

void loop() {
  // Verifica se chegou pacote LoRa
  int packetSize = LoRa.parsePacket();
  
  if (packetSize) {
    String receivedData = "";
    while (LoRa.available()) {
      receivedData += (char)LoRa.read();
    }
    
    Serial.println("\n--- Pacote LoRa Recebido ---");
    Serial.println("Payload Bruto: " + receivedData);

    // Processa o dado e envia para API
    enviarParaAPI(receivedData);
  }
}

void enviarParaAPI(String jsonLoRa) {
  if (WiFi.status() == WL_CONNECTED) {
    
    // 1. PARSE DO JSON RECEBIDO (DO EMISSOR LORA)
    // Exemplo esperado: {"s":"sala1", "t":25.5, "h":60.0}
    JsonDocument docIn; // ArduinoJson v7 (ou StaticJsonDocument<200> para v6)
    
    DeserializationError error = deserializeJson(docIn, jsonLoRa);

    if (error) {
      Serial.print("Falha ao ler JSON do LoRa: ");
      Serial.println(error.c_str());
      return;
    }

    // Extrai os dados
    const char* idSensor = docIn["s"];
    float temp = docIn["t"];
    float hum = docIn["h"];

    // 2. CRIAÇÃO DO JSON DA API (GATEWAY)
    JsonDocument docOut;
    
    docOut["sensor"] = idSensor;       // ID puro (sem "sensor_")
    docOut["temperature"] = temp;
    docOut["humidity"] = hum;
    docOut["latencia"] = lastLatency;  // Adiciona a latência do envio ANTERIOR

    String jsonAPI;
    serializeJson(docOut, jsonAPI);

    Serial.println("Enviando JSON para API: " + jsonAPI);

    // 3. ENVIO HTTP POST E CÁLCULO DE LATÊNCIA
    HTTPClient http;
    http.setTimeout(5000); // Timeout de 5s
    http.begin(serverUrl); 
    http.addHeader("Content-Type", "application/json");
    
    unsigned long startTimer = millis(); // Inicia cronômetro
    
    int httpResponseCode = http.POST(jsonAPI);
    
    unsigned long endTimer = millis();   // Para cronômetro
    
    // Atualiza a variável global para o próximo loop
    lastLatency = endTimer - startTimer;

    if (httpResponseCode > 0) {
      String response = http.getString();
      Serial.print("Código HTTP: ");
      Serial.println(httpResponseCode);
      Serial.print("Resposta API: ");
      Serial.println(response);
    } else {
      Serial.print("Erro no envio POST: ");
      Serial.println(httpResponseCode);
    }
    
    Serial.print("Latência registrada (ms): ");
    Serial.println(lastLatency);

    http.end(); // Fecha conexão
    
  } else {
    Serial.println("WiFi Desconectado! Tentando reconectar...");
    WiFi.reconnect();
  }
}
#include <SPI.h>
#include <LoRa.h>
#include <ArduinoJson.h> 

// --- PINOS HELTEC V2 ---
#define SS      18
#define RST     14
#define DIO0    26
#define BAND    915E6 

// --- CONFIGURAÇÃO DE TEMPO ---
#define uS_TO_S_FACTOR 1000000  
#define TIME_TO_SLEEP  30       // Dorme 30 segundos a cada ciclo
// 5 minutos = 300 segundos. 300 / 30 = 10 ciclos.
#define CYCLES_FOR_MANDATORY_SEND 10 

// --- VARIÁVEIS PERSISTENTES (MEMÓRIA RTC) ---
// Estas variáveis NÃO apagam durante o Deep Sleep
RTC_DATA_ATTR float lastSentTemp = -999.0;
RTC_DATA_ATTR float lastSentHum = -999.0;
RTC_DATA_ATTR int cycleCount = 0; 

void setup() {
  Serial.begin(115200);
  
  // 1. INICIALIZA LORA
  SPI.begin(5, 19, 27, 18);
  LoRa.setPins(SS, RST, DIO0);

  if (!LoRa.begin(BAND)) {
    Serial.println("Erro LoRa! Dormindo...");
    esp_sleep_enable_timer_wakeup(5 * uS_TO_S_FACTOR);
    esp_deep_sleep_start();
  }
  Serial.println("LoRa OK. Iniciando lógica...");

  // 2. VERIFICAÇÃO DE COLD BOOT (Primeira vez que liga a bateria)
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Primeira inicialização. Resetando memória RTC.");
    lastSentTemp = -999.0;
    lastSentHum = -999.0;
    cycleCount = CYCLES_FOR_MANDATORY_SEND; // Força envio imediato
  }

  // 3. SIMULAÇÃO DE DADOS (COM UMA CASA DECIMAL)
  // Gera temperatura
  float temperatura = random(150, 300) / 10.0; 
  // Gera umidade
  float umidade = random(300, 600) / 10.0;

  Serial.print("Leitura Atual -> T: "); Serial.print(temperatura);
  Serial.print(" | H: "); Serial.println(umidade);

  // 4. LÓGICA DE DECISÃO (SMART SEND)
  bool shouldSend = false;
  cycleCount++; // Incrementa o contador de ciclos (tempo)

  // Critério A: Passou 5 minutos? (Timer)
  if (cycleCount >= CYCLES_FOR_MANDATORY_SEND) {
    Serial.println("[Motivo] Timer de 5 min expirado.");
    shouldSend = true;
  }

  // Critério B: Mudança brusca? (Delta)
  // Temp variou >= 2.0 graus OU Umidade variou >= 5.0%
  if (abs(temperatura - lastSentTemp) >= 2.0 || abs(umidade - lastSentHum) >= 5.0) {
    Serial.println("[Motivo] Variação brusca detectada.");
    shouldSend = true;
  }

  // Critério C: Fora do intervalo seguro? (Crítico)
  if (temperatura < 18.0 || temperatura > 27.0 || umidade < 40.0 || umidade > 60.0) {
    Serial.println("[Motivo] Condição crítica (fora do range).");
    shouldSend = true;
  }

  // 5. AÇÃO: ENVIAR OU IGNORAR
  if (shouldSend) {
    // Prepara JSON
    JsonDocument doc; 
    doc["s"] = "heltec_01";
    doc["t"] = temperatura;
    doc["h"] = umidade;

    String loraPayload;
    serializeJson(doc, loraPayload);

    Serial.print("Enviando via LoRa: ");
    Serial.println(loraPayload);

    LoRa.beginPacket();
    LoRa.print(loraPayload);
    LoRa.endPacket();

    // Atualiza as referências para a próxima comparação
    lastSentTemp = temperatura;
    lastSentHum = umidade;
    cycleCount = 0; // Zera o timer de 5 min
    
  } else {
    Serial.println("Dados estáveis. Economizando bateria (Não enviou).");
  }

  // 6. DORMIR
  LoRa.sleep(); // Desliga o rádio LoRa
  
  Serial.println("Entrando em Deep Sleep por 30s...");
  Serial.flush();
  
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Nada aqui
}
#include <esp_now.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include "DHT.h"

// --- CONFIGURAÇÕES DO SENSOR ---
#define DHTPIN 4        
#define PIN_LED_ERRO 2  
#define PIN_LED_SEND_ERRO 5  
#define DHTTYPE DHT11   

DHT dht(DHTPIN, DHTTYPE);

// --- CONFIGURAÇÕES DE TEMPO E SLEEP ---
#define uS_TO_S_FACTOR 1000000  
#define TIME_TO_SLEEP  30       // Dorme por 30 segundos a cada ciclo
// Se dorme 30s, precisa de 10 ciclos para dar 5 minutos (30 * 10 = 300s)
#define CYCLES_FOR_MANDATORY_SEND 10 

// --- VARIÁVEIS NA MEMÓRIA RTC (Sobrevivem ao Deep Sleep) ---
RTC_DATA_ATTR float lastSentTemp = -999.0;
RTC_DATA_ATTR float lastSentHum = -999.0;
RTC_DATA_ATTR int cycleCount = 0; // Substitui o millis() para contar tempo

// --- CONFIGURAÇÕES DO ESP-NOW ---
uint8_t broadcastAddress[] = {0xD4, 0x8A, 0xFC, 0x9D, 0x95, 0x18}; 

typedef struct struct_message {
  char s[10];         
  float temperature;
  float humidity;
} struct_message;

struct_message myData;
bool deliverySuccess = false; 

// Callback 
void OnDataSent(const wifi_tx_info_t *info, esp_now_send_status_t status) {
  deliverySuccess = (status == ESP_NOW_SEND_SUCCESS);
}

// Função de envio
bool sendOnChannel(int channel) {
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  esp_now_peer_info_t peerInfo;
  memset(&peerInfo, 0, sizeof(peerInfo));
  memcpy(peerInfo.peer_addr, broadcastAddress, 6);
  peerInfo.channel = channel; 
  peerInfo.encrypt = false;

  if (esp_now_is_peer_exist(broadcastAddress)) {
    esp_now_del_peer(broadcastAddress);
  }
  esp_now_add_peer(&peerInfo);

  esp_err_t result = esp_now_send(broadcastAddress, (uint8_t *) &myData, sizeof(myData));
  delay(50); // Pequeno delay para processar o callback

  return (result == ESP_OK && deliverySuccess);
}

void setup() {
  Serial.begin(115200); 

  gpio_hold_dis((gpio_num_t)PIN_LED_SEND_ERRO);
  gpio_hold_dis((gpio_num_t)PIN_LED_ERRO);

  // --- VERIFICAÇÃO DE COLD BOOT (Primeira vez que liga a bateria) ---
  // Se não acordou pelo timer, assume que é a primeira vez e reseta variáveis
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_TIMER) {
    Serial.println("Primeira inicialização (Cold Boot). Resetando variáveis.");
    lastSentTemp = -999.0;
    lastSentHum = -999.0;
    cycleCount = CYCLES_FOR_MANDATORY_SEND; // Força envio no primeiro boot
  }

  pinMode(PIN_LED_ERRO, OUTPUT);
  pinMode(PIN_LED_SEND_ERRO, OUTPUT);
  digitalWrite(PIN_LED_ERRO, LOW);
  digitalWrite(PIN_LED_SEND_ERRO, LOW); 
  
  dht.begin();
  
  // DHT11 precisa de um tempo para estabilizar após acordar
  Serial.println("Aguardando sensor estabilizar...");
  delay(2000); 

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Erro ESP-NOW");
    // Se der erro crítico, dorme e tenta de novo depois
    goToDeepSleep();
  }
  esp_now_register_send_cb(OnDataSent);

  // 1. LEITURA DO SENSOR
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  // Verifica falha na leitura
  if (isnan(umidade) || isnan(temperatura)) {
    Serial.println(F("Falha sensor DHT!"));
    digitalWrite(PIN_LED_ERRO, HIGH); 
    delay(500); // Pisca brevemente para avisar
    // Se falhar, dorme e tenta no próximo ciclo
    goToDeepSleep();
  }

  Serial.print(F("Ciclo: ")); Serial.print(cycleCount);
  Serial.print(F(" | T: ")); Serial.print(temperatura);
  Serial.print(F(" | H: ")); Serial.println(umidade);

  // 2. LÓGICA DE DECISÃO DE ENVIO
  bool shouldSend = false;

  // Incrementa contador de tempo (ciclos)
  cycleCount++;

  // Critério A: Passou 5 minutos (10 ciclos de 30s)?
  if (cycleCount >= CYCLES_FOR_MANDATORY_SEND) {
    Serial.println("Motivo: Timer 5 min.");
    shouldSend = true;
  }

  // Critério B: Variação brusca
  if (abs(temperatura - lastSentTemp) >= 2.0 || abs(umidade - lastSentHum) >= 5.0) {
    Serial.println("Motivo: Variação brusca.");
    shouldSend = true;
  }

  // Critério C: Fora do range crítico
  if (temperatura < 18.0 || temperatura > 27.0 || umidade < 40.0 || umidade > 60.0) {
    Serial.println("Motivo: Range Crítico.");
    shouldSend = true;
  }

  // 3. ENVIO
  if (shouldSend) {
    strcpy(myData.s, "sensor_A"); 
    myData.temperature = temperatura;
    myData.humidity = umidade;

    bool sent = false;
    // Varre canais
    for (int ch = 1; ch <= 11; ch++) {
      if (sendOnChannel(ch)) {
        Serial.print("Enviado canal: "); Serial.println(ch);
        sent = true;
        break; 
      }
    }

    if (sent) {
      // Sucesso: Atualiza referências e zera contador de tempo
      lastSentTemp = temperatura;
      lastSentHum = umidade;
      cycleCount = 0; 
      digitalWrite(PIN_LED_SEND_ERRO, LOW);
    } else {
      // Falha: Pisca LED e mantem cycleCount alto para tentar de novo na próxima
      Serial.println("Falha no envio.");
      digitalWrite(PIN_LED_SEND_ERRO, HIGH);
      delay(100); 
    }
  } else {
    Serial.println("Dados estáveis. Dormindo...");
  }

  // 4. DORMIR
  goToDeepSleep();
}

void goToDeepSleep() {
  Serial.println("Entrando em Deep Sleep...");

  // 2. Trava o pino nesse estado (HOLD)
  // Isso impede que ele flutue durante o sono
  gpio_hold_en((gpio_num_t)PIN_LED_SEND_ERRO); 
  gpio_hold_en((gpio_num_t)PIN_LED_ERRO);
  
  // Habilita a função de HOLD durante o deep sleep
  gpio_deep_sleep_hold_en();

  Serial.flush(); 
  
  // Configura o timer e dorme
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_deep_sleep_start();
}

void loop() {
  // Nada aqui. O ESP32 reinicia no setup() quando acorda.
}
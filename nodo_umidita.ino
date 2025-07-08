#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>
#include "DHT.h" 

// Configurazione libreria DHT per la lettura dei sensori       
#define DHTTYPE DHT11
#define dht_dpin 25
DHT dht(dht_dpin, DHTTYPE);         

#define WIFI_SSID       "Progetto_iot"
#define WIFI_PASSWORD   "progetto_iot"

// Struttura dati da inviare tramite ESP-NOW
typedef struct struct_msg {
  float temperature;
  float humidity;
  unsigned long timestamp;
} struct_msg;

// MAC Address dei peer
uint8_t macRed[] = {0xE4, 0x65, 0xB8, 0x27, 0x5A, 0x2C};
//uint8_t macWhite[] = {0xE4, 0x65, 0xB8, 0x27, 0x62, 0x3C};
uint8_t macYellow[] = {0xFC, 0xB4, 0x67, 0xF1, 0x52, 0xC8};

// Timeout
const unsigned long TIMEOUT = 9000;

// Flag ML
bool flagML = false;

// Funzioni di callback
void onSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  Serial.print("Invio a ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.print(macStr);
  Serial.print(" -> ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "SUCCESSO" : "FALLITO");
}

void onReceive(const uint8_t* mac_addr, const uint8_t* data, int len) {
  // Ad esempio: stampa l'indirizzo MAC e i dati ricevuti
  Serial.print("Ricevuto da: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);

  Serial.print("Dati ricevuti: ");
  for (int i = 0; i < len; i++) {
    Serial.print(data[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
}

// Funzione di setup
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(11, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  dht.begin();
  delay(500);

  if (esp_now_init() != ESP_OK) {
    Serial.println("Errore ESP-NOW");
    return;
  }

  // Funzioni di callback
  esp_now_register_send_cb(onSent);
  esp_now_register_recv_cb(onReceive);

  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  int ch = WiFi.channel();
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  if (isnan(ch)) {
    Serial.println("Wifi Error");
    delay(3000);
    return;
  } else {
    Serial.print("Wifi Channel: ");
    Serial.println(ch);
  }

  esp_now_peer_info_t peerInfo = {};
  peerInfo.channel = ch;
  peerInfo.encrypt = false;

  memcpy(peerInfo.peer_addr,macRed,6);
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr,macYellow,6);
  esp_now_add_peer(&peerInfo);
}

void loop() {
  float hum = dht.readHumidity();
  if (isnan(hum)) {
    Serial.println("DHT: Humidity NaN");
    delay(3000);
    return;
  } else {
    Serial.print("H=");
    Serial.println(hum);
  }


  float temp;
  if (flagML) {
    Serial.println("ML TRUE");
    temp = ML_Temperature(hum);
    //return;
  } else {
    Serial.println("ML FALSE");
    temp = -1;
  }

  struct_msg toSend;
  toSend.temperature = temp;
  toSend.humidity = hum;
  toSend.timestamp = millis();

  esp_now_send(macYellow,(uint8_t *)&toSend, sizeof(toSend));
  delay(5000);  
}

float ML_Temperature(float hum) {
  return -1;
}


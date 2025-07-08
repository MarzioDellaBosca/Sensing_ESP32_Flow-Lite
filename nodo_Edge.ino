#include <esp_now.h>
#include <WiFi.h>                        
#include <InfluxDbClient.h>   
#include <InfluxDbCloud.h> 
#include "esp_task_wdt.h"
#include "esp_wifi.h"
#include <math.h> 

#define LED_PIN 32
#define TIMEOUT 7000

#define WIFI_SSID       "Progetto_iot"
#define WIFI_PASSWORD   "progetto_iot"
#define INFLUXDB_URL    "https://eu-central-1-1.aws.cloud2.influxdata.com"
#define INFLUXDB_ORG    "IoT"
#define INFLUXDB_BUCKET "Data"
#define INFLUXDB_TOKEN  "vDLGoulGIJjATb7ApU7-aEqf5DmL8rp0H0pEsI8WLT_fF87izC_nzzcLAFEkCxW9dbD6m_dWSh-hb_DiMgmVCA=="

#define BUFFER_SIZE 4
float tempBuffer[BUFFER_SIZE];
float humBuffer[BUFFER_SIZE];
int bufferIndexTemp = 0;
int bufferIndexHum = 0;

// MAC address
uint8_t macTemp[] = {0xE4, 0x65, 0xB8, 0x27, 0x5A, 0x2C}; // nodo temperatura
uint8_t macHum[] = {0xE4, 0x65, 0xB8, 0x27, 0x62, 0x3C}; // nodo umidità
uint8_t macModel[] = {0xFC, 0xB4, 0x67, 0xEF, 0xF2, 0xB8}; // nodo inferenza

// Variabili per il controllo del timestamp
unsigned long lastRecTimeTemp = 0;
unsigned long lastRecTimeHum = 0;
unsigned long lastRecTimeModel = 0;
unsigned long lastTempStampSent = 0;
unsigned long lastHumStampSent = 0;
bool isTempActive = false;
bool isHumActive = false;
bool isModelActive = false;
bool hasTempData = false;
bool hasHumData = false;
unsigned long previousCheck = 0;

// Struttura per l'invio dei dati
typedef struct struct_msg {
  float temp;
  float hum;
  unsigned long timestamp;
} struct_msg;

struct_msg lastTempNode;
struct_msg lastHumNode;
struct_msg lastModelNode;

// Definizione InfluxDBClient e Point
InfluxDBClient client(INFLUXDB_URL, INFLUXDB_ORG, INFLUXDB_BUCKET, INFLUXDB_TOKEN,
                      InfluxDbCloud2CACert);

Point sensors("Progetto_IoT");

// Funzione di stampa dei dati ricevuti
void helloPrint(const char* nodo, struct_msg receivedData) {
  Serial.print("Ricevuto da: ");
  Serial.println(nodo);
  Serial.print("Temperatura: ");
  Serial.println(receivedData.temp);  // usa variabile visibile nel contesto
  Serial.print("Umidità: ");
  Serial.println(receivedData.hum);
  Serial.print("Timestamp: ");
  Serial.println(receivedData.timestamp);
  Serial.println();
}

// Funzione di callback alla ricezione dei dati
void onReceive(const esp_now_recv_info_t *recv_info, const uint8_t *incomingData, int len) {
  struct_msg receivedData;
  memcpy(&receivedData, incomingData, sizeof(receivedData));

  const uint8_t *mac_addr = recv_info->src_addr;

  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);

  // Controllo del MAC address per identificare il nodo
  // Nodo temperatura
  if (memcmp(mac_addr, macTemp, 6) == 0) {

    lastRecTimeTemp = millis();

    if (isHumActive && isModelActive) {
      sendWarning(macModel, false);
      isModelActive = false;
    }

    isTempActive = true;

    helloPrint("Nodo Temperatura", receivedData);

    // Aggiorno il buffer e la struttura dati alla ricezione della temperatura dal nodo di sensing
    lastTempNode = struct_msg{receivedData.temp, -1.0, receivedData.timestamp};
    shiftLeftAndAdd(tempBuffer, receivedData.temp, BUFFER_SIZE);
    hasTempData = true;
    Serial.println("tempBuffer");
    for (int i = 0; i < BUFFER_SIZE; i++) {
      Serial.println(tempBuffer[i], 4);
    }

    // Nodo umidità
  } else if (memcmp(mac_addr, macHum, 6) == 0) {

    lastRecTimeHum = millis();

    if (isTempActive && isModelActive) {
      sendWarning(macModel, false);
      isModelActive = false;
    }

    isHumActive = true;
    helloPrint("Nodo Umidità", receivedData);

    // Aggiorno il buffer e la struttura dati alla ricezione della umidità dal nodo di sensing
    lastHumNode = struct_msg{-50.0, receivedData.hum, receivedData.timestamp};
    shiftLeftAndAdd(humBuffer, receivedData.hum, BUFFER_SIZE);
    hasHumData = true;
    Serial.println("humBuffer");
    for (int i = 0; i < BUFFER_SIZE; i++) {
      Serial.println(humBuffer[i], 4);
    }
    
  // Nodo Model
  } else if (memcmp(mac_addr, macModel, 6) == 0) {
    isModelActive = true;
    lastRecTimeModel = millis();
    Serial.println("Msg da Model arrivato!");
    helloPrint("Nodo Model", receivedData);
    lastModelNode = struct_msg{receivedData.temp, receivedData.hum, receivedData.timestamp};

    // Utilizzo la previsione del model solo se necessario
    if (!isTempActive && !isHumActive) {
      shiftLeftAndAdd(tempBuffer, receivedData.temp, BUFFER_SIZE);
      hasTempData = true;  
      shiftLeftAndAdd(humBuffer, receivedData.hum, BUFFER_SIZE);
      hasHumData = true;
    } else if(!isTempActive) {
      shiftLeftAndAdd(tempBuffer, receivedData.temp, BUFFER_SIZE);
      hasTempData = true;
    } else if(!isHumActive) {
      shiftLeftAndAdd(humBuffer, receivedData.hum, BUFFER_SIZE);
      hasHumData = true;
    }
    Serial.println("tempBuffer");
    for (int i = 0; i < BUFFER_SIZE; i++) {
      Serial.println(tempBuffer[i], 4);
    }
    Serial.println("humBuffer");
    for (int i = 0; i < BUFFER_SIZE; i++) {
      Serial.println(humBuffer[i], 4);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // configuro il watchdog per riavviare la CPU se non viene 
  // resettato entro 20 secondi

  esp_task_wdt_config_t config = {
    .timeout_ms = 20* 1000,    //  20 secondi
    .trigger_panic = true,     // Trigger panic == reset della CPU (reboot)
  };
  // configuro il WDT
  esp_task_wdt_reconfigure(&config);

  // abilito il WDT nel loop di Arduino (ogni invocazione del loop resetta il WDT)
  enableLoopWDT();

  WiFi.mode(WIFI_STA);
  if (esp_now_init() != ESP_OK) {
    Serial.println("Errore inizializzazione ESP-NOW");
    return;
  }
  esp_now_register_recv_cb(onReceive);

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int ch = WiFi.channel();
  esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);

  // Aggiungiamo i peer per poter trasmettere
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, macTemp, 6);
  peerInfo.channel = ch;
  peerInfo.encrypt = false;
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr, macHum, 6);
  esp_now_add_peer(&peerInfo);

  memcpy(peerInfo.peer_addr, macModel, 6);
  esp_now_add_peer(&peerInfo);

  // controllo se la connessione è avvenuta
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // se supero il loop significa che sono connesso
  // stampo info su connessione
  Serial.println("");
  Serial.println("WiFi connected");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  Serial.print("RRSI: ");
  Serial.println(WiFi.RSSI());

  // preparo la connessione su influxDB
  // aggiungo i tag nel Point per salvare in modo ordinato i dati sul server
  sensors.addTag("host", "ESP_Edge");
  sensors.addTag("location", "Urbino");
  sensors.addTag("room", "Cucina");

  // testiamo la connessione  su influxDB
  if(client.validateConnection()){
    Serial.println("Connected to influxDB");
  }else {
    Serial.println("Connection failed!");
    Serial.println(client.getLastErrorMessage());    
  }
  Serial.print("WiFi Channel: ");
  Serial.println(WiFi.channel());
}

// Funzione per inviare un avviso al nodo Model
void sendWarning(uint8_t *targetMac, bool warning) {
  esp_err_t result;

  if (warning) {  
    Serial.println("Sveglio model dentro sendWarning");

    float dataToSend[8];

    for (int i = 0; i < BUFFER_SIZE; i++) {
      dataToSend[i] = tempBuffer[i];
      dataToSend[i + BUFFER_SIZE] = humBuffer[i];
    }

    for (int i = 0; i < BUFFER_SIZE*2; i++) {
    Serial.println(dataToSend[i], 4);
    }
    
    result = esp_now_send(targetMac, (uint8_t *)dataToSend, sizeof(dataToSend));
  } else {
    uint8_t warningByte = 0;
    result = esp_now_send(targetMac, &warningByte, sizeof(warningByte));
  }

  if (result != ESP_OK) {
    Serial.print("Errore nell'invio: ");
    Serial.println(result);
  } 
}

void loop() {
  unsigned long now = millis();
  static unsigned long previousCheck = 0;
  static unsigned long previousBlink = 0;
  static bool ledState = LOW;

  // Controllo periodico ogni TIMEOUT ms per timeout nodi
  if (now - previousCheck >= TIMEOUT) {
    previousCheck = now;

    if (isTempActive && (now - lastRecTimeTemp > TIMEOUT) || (!isTempActive)) {
      Serial.println("Nodo Temperatura OFFLINE");
      if (!isModelActive && (now - lastRecTimeModel > TIMEOUT)) {
        Serial.println("Sveglio Model");
        sendWarning(macModel, true);
      }
      isTempActive = false;
    }

    if (isHumActive && (now - lastRecTimeHum > TIMEOUT) || (!isHumActive)) {
      Serial.println("Nodo Umidità OFFLINE");
      if (!isModelActive && (now - lastRecTimeModel > TIMEOUT)) {
        Serial.println("Sveglio Model");
        sendWarning(macModel, true);
      }
      isHumActive = false;
    }


    if (hasTempData){
      
      if (lastTempStampSent != lastTempNode.timestamp){
        if(isHumActive) {
          writeToInfluxDB(lastTempNode.temp, lastTempNode.hum, lastTempNode.timestamp);
        } else {
          writeToInfluxDB(lastTempNode.temp, lastModelNode.hum, lastTempNode.timestamp);
        }
        lastTempStampSent = lastTempNode.timestamp;
        hasTempData = false;
      }
    }

    if (hasHumData){

      if (lastHumStampSent != lastHumNode.timestamp){
        if (isTempActive) {
          writeToInfluxDB(lastHumNode.temp, lastHumNode.hum, lastHumNode.timestamp);
        } else {
          writeToInfluxDB(lastModelNode.temp, lastHumNode.hum, lastHumNode.timestamp);
        }
        lastHumStampSent = lastHumNode.timestamp;
        hasHumData = false;
      }
    }

    if (!isHumActive && !isTempActive && isModelActive) {
      writeToInfluxDB(lastModelNode.temp, lastModelNode.hum, millis());
    }
  }

  // Logica LED
  if (isTempActive && isHumActive) {
    // entrambi attivi -> LED spento
    digitalWrite(LED_PIN, LOW);
    ledState = LOW; // reset stato blink
  } else if (!isTempActive && !isHumActive) {
    // entrambi non attivi -> lampeggio 10Hz (cambia stato ogni 50 ms)
    if (now - previousBlink >= 250) {
      previousBlink = now;
      ledState = !ledState;
      digitalWrite(LED_PIN, ledState);
    }
  } else {
    // un solo nodo attivo -> LED acceso fisso
    digitalWrite(LED_PIN, HIGH);
    ledState = HIGH; // reset stato blink
  }

  // Qui puoi aggiungere altre istruzioni senza bloccare
}

/* funzione per scrivere su influxDB */
void writeToInfluxDB(float t, float h, unsigned long st){
  sensors.clearFields();
  
  if (t > -50) {
    sensors.addField("temperature", t);
  } else {
    sensors.addField("temperature", NAN);  // oppure: omettere il campo del tutto
  }

  if (h >= 0) {
    sensors.addField("humidity", h);
  } else {
    sensors.addField("humidity", NAN);
  }
  sensors.addField("timestamp", st);

  Serial.print("Writing: ");
  Serial.println(sensors.toLineProtocol());

  /* ora scriviamo sul server */
  if(!client.writePoint(sensors)){
    Serial.print("InfluxDB write failed ");
    Serial.println(client.getLastErrorMessage());
    //while(1) se ho attivato il WDT posso mettere in loop la CPU così il WDT riavvierà l'ESP
    // posso anche mettere un delay e poi esp_reastart()
    delay(2000);
    esp_restart();
  }  
}

void shiftLeftAndAdd(float* buffer, float newValue, int size) {
  for (int i = 0; i < size - 1; i++) {
    buffer[i] = buffer[i + 1];
  }
  buffer[size - 1] = newValue;
}
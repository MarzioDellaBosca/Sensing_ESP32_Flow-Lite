#include <esp_now.h>
#include <esp_wifi.h>
#include <WiFi.h>

#include <TensorFlowLite_ESP32.h>
#include "tensorflow/lite/micro/all_ops_resolver.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/schema/schema_generated.h"

#include "model_hum.h"
#include "model_temp.h"

#define INPUT_SIZE 8
#define WIFI_SSID       "Progetto_iot"
#define WIFI_PASSWORD   "progetto_iot"
#define VECTOR_SIZE 4

unsigned long sendStartTime = 0;
const unsigned long timeoutMs = 5000;

// Struttura dati da inviare tramite ESP-NOW
typedef struct struct_msg {
  float temp;
  float hum;
  unsigned long timestamp;
} struct_msg;

bool flagML = false;  // variabile globale per memorizzare il flag ricevuto

float temp[VECTOR_SIZE];
float hum[VECTOR_SIZE];

void updateBuffers(float newTemp, float newHum) {
  // Shift a sinistra di tutti gli elementi (i più vecchi si perdono)
  for (int i = 0; i < VECTOR_SIZE - 1; i++) {
    temp[i] = temp[i + 1];
    hum[i]  = hum[i + 1];
  }

  // Inserisci i nuovi valori alla fine
  temp[VECTOR_SIZE - 1] = newTemp;
  hum[VECTOR_SIZE - 1]  = newHum;

  // Debug (facoltativo)
  Serial.print("Temp buffer: ");
  for (int i = 0; i < VECTOR_SIZE; i++) {
    Serial.print(temp[i]); Serial.print(" ");
  }
  Serial.println();

  Serial.print("Hum buffer: ");
  for (int i = 0; i < VECTOR_SIZE; i++) {
    Serial.print(hum[i]); Serial.print(" ");
  }
  Serial.println();
}

void onReceive(const uint8_t *mac_addr, const uint8_t *data, int len) {
  Serial.print("Ricevuto da: ");
  char macStr[18];
  snprintf(macStr, sizeof(macStr), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac_addr[0], mac_addr[1], mac_addr[2],
           mac_addr[3], mac_addr[4], mac_addr[5]);
  Serial.println(macStr);

  if (len == sizeof(bool)) {
    // Ricevuto un booleano
    bool receivedFlag;
    memcpy(&receivedFlag, data, sizeof(bool));
    flagML = false;

    Serial.println("Ricevuto booleano → flagML = false");

  } else if (len == sizeof(float) * 8) {
    // Ricevuto un vettore di 8 float
    float receivedData[8];
    memcpy(receivedData, data, sizeof(receivedData));

    for (int i = 0; i < VECTOR_SIZE; i++) {
      temp[i] = receivedData[i];
      hum[i]  = receivedData[i + VECTOR_SIZE];
    }

    flagML = true;

    Serial.println("Ricevuto vettore di 8 float → flagML = true");
    Serial.print("Temp: ");
    for (int i = 0; i < VECTOR_SIZE; i++) {
      Serial.print(temp[i]); Serial.print(" ");
    }
    Serial.println();

    Serial.print("Hum: ");
    for (int i = 0; i < VECTOR_SIZE; i++) {
      Serial.print(hum[i]); Serial.print(" ");
    }
    Serial.println();

  } else {
    Serial.print("Ricevuto pacchetto con dimensione non riconosciuta: ");
    Serial.println(len);
  }
}

namespace {
  tflite::ErrorReporter* error_reporter = nullptr;

  const tflite::Model* model_hum = nullptr;
  tflite::MicroInterpreter* interpreter_hum = nullptr;
  TfLiteTensor* input_hum = nullptr;
  TfLiteTensor* output_hum = nullptr;

  const tflite::Model* model_temp = nullptr;
  tflite::MicroInterpreter* interpreter_temp = nullptr;
  TfLiteTensor* input_temp = nullptr;
  TfLiteTensor* output_temp = nullptr;

  constexpr int kTensorArenaSize = 6000;
  uint8_t tensor_arena_hum[kTensorArenaSize / 2];
  uint8_t tensor_arena_temp[kTensorArenaSize / 2];
}

// MAC del peer a cui inviare la stringa (esempio)
uint8_t peerMac[] = {0xFC, 0xB4, 0x67, 0xF1, 0x52, 0xC8}; // edge

void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("Send Status: Success");
  } else {
    Serial.println("Send Status: Fail");
  }
}

bool sendMessage(const struct_msg* msg, const uint8_t* peerMac) {
  esp_err_t result = esp_now_send(peerMac, (uint8_t*)msg, sizeof(struct_msg));
  Serial.println("Messaggio inviato!");
  return (result == ESP_OK);
}

void initESPNow() {
  // Arresta eventuali sessioni ESP-NOW attive
  esp_now_deinit();

  // Reinizializza ESP-NOW
  esp_err_t init_result = esp_now_init();
  if (init_result == ESP_OK) {
      //Serial.println("ESP-NOW inizializzato correttamente!");
  } else {
      Serial.print("Errore inizializzazione ESP-NOW: ");
      Serial.println(init_result);
      return;
  }

  // Registra la callback di invio
  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onReceive);
  // Ottieni il canale WiFi attuale
  int wifi_channel = WiFi.channel();

  // Crea info peer
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerMac, 6);
  peerInfo.channel = wifi_channel;
  peerInfo.encrypt = false;

  // Aggiungi peer
  if (esp_now_add_peer(&peerInfo) == ESP_OK) {
      //Serial.println("Peer aggiunto correttamente.");
  } else {
      Serial.println("Errore aggiunta peer");
  }
}

// Funzione per eseguire l'inferenza del modello di umidità
float runHumidityInference(float input_data[INPUT_SIZE]) {
  // Copio i dati nel tensore di input
  for (int i = 0; i < INPUT_SIZE; i++) {
    input_hum->data.f[i] = input_data[i];
  }
  
  // Eseguo l'inferenza
  TfLiteStatus invoke_status = interpreter_hum->Invoke();
  if (invoke_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "Humidity model invoke failed");
    return NAN; // Restituisco NaN in caso di errore
  }
  
  // Restituisco il risultato dal tensore di output
  return output_hum->data.f[0];
}

// Funzione per eseguire l'inferenza del modello di temperatura
float runTemperatureInference(float input_data[INPUT_SIZE]) {
  // Copio i dati nel tensore di input
  for (int i = 0; i < INPUT_SIZE; i++) {
    input_temp->data.f[i] = input_data[i];
  }
  
  // Eseguo l'inferenza
  TfLiteStatus invoke_status = interpreter_temp->Invoke();
  if (invoke_status != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "Temperature model invoke failed");
    return NAN; // Restituisco NaN in caso di errore
  }
  
  // Restituisco il risultato dal tensore di output
  return output_temp->data.f[0];
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  // Avvia connessione WiFi in modalità STA
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.print("Connessione WiFi in corso");
  while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      Serial.print(".");
  }
  Serial.println();
  Serial.print("Connesso al WiFi con IP: ");
  Serial.println(WiFi.localIP());

  // Prendi il canale WiFi usato (utile per ESP-NOW)
  int wifi_channel = WiFi.channel();
  Serial.print("WiFi Channel: ");
  Serial.println(wifi_channel);

  initESPNow();
  // Inizializza error reporter per TensorFlow Lite
  static tflite::MicroErrorReporter micro_error_reporter;
  error_reporter = &micro_error_reporter;

  // Inizializza modello umidità
  model_hum = tflite::GetModel(modell_cnn_hum);
  if (model_hum->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter, "Humidity model schema mismatch");
    return;
  }
  static tflite::AllOpsResolver resolver_hum;
  static tflite::MicroInterpreter static_interpreter_hum(
    model_hum, resolver_hum, tensor_arena_hum, kTensorArenaSize / 2, error_reporter);
  interpreter_hum = &static_interpreter_hum;
  if (interpreter_hum->AllocateTensors() != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "Humidity model AllocateTensors failed");
    return;
  }
  input_hum = interpreter_hum->input(0);
  output_hum = interpreter_hum->output(0);

  // Inizializza modello temperatura
  model_temp = tflite::GetModel(modell_cnn_temp);
  if (model_temp->version() != TFLITE_SCHEMA_VERSION) {
    TF_LITE_REPORT_ERROR(error_reporter, "Temperature model schema mismatch");
    return;
  }
  static tflite::AllOpsResolver resolver_temp;
  static tflite::MicroInterpreter static_interpreter_temp(
    model_temp, resolver_temp, tensor_arena_temp, kTensorArenaSize / 2, error_reporter);
  interpreter_temp = &static_interpreter_temp;
  if (interpreter_temp->AllocateTensors() != kTfLiteOk) {
    TF_LITE_REPORT_ERROR(error_reporter, "Temperature model AllocateTensors failed");
    return;
  }
  input_temp = interpreter_temp->input(0);
  output_temp = interpreter_temp->output(0);

  Serial.println("Modelli TensorFlow Lite inizializzati correttamente!");
}

void loop() {
  if (millis() - sendStartTime > timeoutMs) {
    sendStartTime = millis();  //  Aggiorna tempo per prossima esecuzione

    if (flagML) {
      float input_data[INPUT_SIZE];  // 8 elementi

      // Popola input_data intercalando temp e hum
      for (int i = 0; i < VECTOR_SIZE; i++) {  // t1h1t2h2..t8h8
        input_data[i * 2]     = temp[i];
        input_data[i * 2 + 1] = hum[i];
      }

      // Esegui inferenze
      float temp_pred = runTemperatureInference(input_data);
      float hum_pred = runHumidityInference(input_data);

      // Prepara dati da inviare
      struct_msg myData;
      myData.temp = temp_pred;
      myData.hum = hum_pred;
      myData.timestamp = millis();

      // Aggiorna i buffer con i nuovi valori predetti
      updateBuffers(myData.temp, myData.hum);

      // Invia messaggio
      sendMessage(&myData, peerMac);

      // Reinizializza ESP-NOW se necessario
      initESPNow();
    }
  }
}

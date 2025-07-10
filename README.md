# 🌱 Smart Sensing con ESP32 e TensorFlow Lite

📡 *Predizione e Continuità nel Monitoraggio Ambientale*

### 📘 *Programmazione per l’IoT*

**Laurea Magistrale in Informatica e Innovazione Digitale**
**DiSPeA – Università degli Studi di Urbino Carlo Bo**

---

## 🔍 Questo progetto nasce nell’ambito dell’**Internet of Things (IoT)** e propone una soluzione **resiliente e intelligente** per il monitoraggio ambientale.
🔹 Utilizza una rete di 4 nodi **ESP32**
🔹 Integra sensori DHT11 per **temperatura** 🌡️ e **umidità** 💧
🔹 Implementa modelli di **Machine Learning** (TensorFlow Lite) per la predizione in caso di malfunzionamento dei sensori
🔹 Gestisce i dati in tempo reale tramite **ESP-NOW** e **InfluxDB Cloud** ☁️

---

## 🧱 Architettura

* 🔴 **ESP32 Temperatura**
* ⚪ **ESP32 Umidità**
* 🟡 **ESP32 Edge (aggregatore + invio dati a InfluxDB)**
* 🔴 (KO) **ESP32 Modelli (predizione con TFLite)**

---

## 🔌 Hardware

* **ESP32** con Wi-Fi, Bluetooth e interfacce multiple
* **DHT11** sensori digitali per temperatura e umidità
* Programmazione tramite **Arduino IDE**

---

## 📡 Comunicazione

Comunicazione **peer-to-peer** tramite protocollo **ESP-NOW**
✅ Basso consumo
✅ Nessuna connessione a router necessaria
✅ Comunicazione diretta tra i dispositivi
🛜 I dati vengono successivamente inviati a **InfluxDB** tramite Wi-Fi

---

## 🧠 Inferenza & AI

In caso di problemi con i sensori, il nodo Edge attiva il nodo "Modelli" che:

* Riceve gli ultimi 4 campioni validi
* Stima il valore successivo (t5, h5) tramite modelli leggeri pre-addestrati (TensorFlow Lite)
* Resta attivo finché i nodi di sensing non tornano operativi

🏗️ **Modelli valutati**:

* CNN
* LSTM
* MLP (2 versioni)

📊 **Risultati migliori**: CNN con loss e MAE più bassi su temperatura e umidità

---

## 🧪 Dataset & Training

* Campionamento ogni 5 secondi per 3 settimane
* Predizione del valore successivo (t5/h5)
* Training set: 60%, Validation: 20%, Test: 20%

---

## 📊 Output & Visualizzazioni

I dati vengono salvati su InfluxDB e analizzati con **Matplotlib**
📈 Grafici temporali
📉 Istogrammi per temperatura e umidità
📦 Dati strutturati con: `host`, `humidity`, `location`, `room`, `temperature`, `timestamp`


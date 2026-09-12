#include <Arduino.h>
#include <DHT.h>
#include <driver/gpio.h> // Necesario para mantener el estado del pin DTR durante el Deep Sleep

// --- PINES MÓDULO SIM7070G ---
#define SIM_RX_PIN 17 
#define SIM_TX_PIN 16 
#define PWRKEY 19
#define SIM_DTR_PIN 22   // <--- PIN 22 para control de Hardware Sleep

// --- PINES SENSOR RS485 ---
#define RS485_RX_PIN 2
#define RS485_TX_PIN 1

// --- CONFIGURACIÓN SENSOR DHT22 ---
#define DHTPIN 18
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// --- PIN DEL RELÉ PARA SENSORES ---
#define RELAY_PIN 21
#define MOSFET_PIN 23

// --- CONFIGURACIÓN RED ---
const char* apn = "hologram";
const char* server = "backend.thinger.io";
const int port = 80; 

// --- VARIABLES DE SENSORES (Con RTC_DATA_ATTR para sobrevivir al Deep Sleep) ---
RTC_DATA_ATTR float humedad1 = NAN;
RTC_DATA_ATTR float temperatura1 = NAN;
RTC_DATA_ATTR float humedad2 = NAN;
RTC_DATA_ATTR float temperatura2 = NAN;
RTC_DATA_ATTR float humedadDHT = NAN;
RTC_DATA_ATTR float temperaturaDHT = NAN;

// Arrays Modbus
uint8_t reqSensor1[] = {0x01, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x0B};
uint8_t reqSensor2[] = {0x02, 0x03, 0x00, 0x00, 0x00, 0x02, 0xC4, 0x38};

// --- TIEMPO DE DEEP SLEEP (20 Minutos en microsegundos) ---
const uint64_t DEEP_SLEEP_TIME_US = 20ULL * 60ULL * 1000000ULL;

// Declaración de funciones
void powerOnModule();
bool setupNetwork();
void leerSensor(uint8_t id, uint8_t request[], size_t reqSize, float &hum, float &temp);
void sendThingerRawTCP(float lat, float lon);
bool sendATCommand(const char* cmd, const char* expected_response, int timeout);
void checkSignal();

void setup() {
  Serial.begin(115200);
  
  // Liberar el estado de los pines retenidos en el ciclo anterior
  gpio_hold_dis((gpio_num_t)SIM_DTR_PIN);

  Serial.println("\n==================================================");
  Serial.println(">>> DESPERTANDO DEL DEEP SLEEP - INICIANDO CICLO <<<");
  Serial.println("==================================================");

  // 1. Encender el relé para dar energía a los sensores
  Serial.println("[RELÉ] Encendiendo alimentación de sensores (HIGH)...");
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, HIGH); 
  pinMode(MOSFET_PIN, OUTPUT);
  digitalWrite(MOSFET_PIN, HIGH); 
  // Tiempo de calentamiento para sensores recién encendidos
  delay(3000); 
  
  // 2. Despertar al SIM7070G bajando el pin DTR
  Serial.println("[SIM] Despertando módulo SIM via DTR (LOW)...");
  pinMode(SIM_DTR_PIN, OUTPUT);
  digitalWrite(SIM_DTR_PIN, LOW); // LOW despierta el módulo UART
  delay(500); // Dar un momento a la UART para estabilizarse

  // Inicializamos puertos de forma independiente
  Serial0.begin(115200); 
  Serial1.begin(9600, SERIAL_8N1, RS485_RX_PIN, RS485_TX_PIN); 
  
  // Inicializar sensor DHT22
  dht.begin();

  // 3. Encendido y configuración del SIM7070G
  powerOnModule();
  bool redConectada = setupNetwork();

  Serial.println("\n--- LEYENDO SENSORES ---");
  delay(1000); 

  // 4. Leer Sensores RS485
  leerSensor(0x01, reqSensor1, sizeof(reqSensor1), humedad1, temperatura1);
  delay(1000); 
  
  leerSensor(0x02, reqSensor2, sizeof(reqSensor2), humedad2, temperatura2);
  delay(1000); 

  // 5. Leer Sensor DHT22
  Serial.print("Consultando Sensor DHT22...");
  float currentHumDHT = dht.readHumidity();
  float currentTempDHT = dht.readTemperature();

  if (isnan(currentHumDHT) || isnan(currentTempDHT)) {
    Serial.println(" -> Error de lectura del DHT22.");
    humedadDHT = NAN; 
    temperaturaDHT = NAN;
  } else {
    humedadDHT = currentHumDHT;
    temperaturaDHT = currentTempDHT;
    Serial.print(" -> OK | Hum: "); Serial.print(humedadDHT);
    Serial.print("% | Temp: "); Serial.println(String(temperaturaDHT) + "C");
  }

  // 6. Comprobar red y enviar datos
  if (redConectada) {
    if (!sendATCommand("AT+CNACT?", "+CNACT: 0,1", 2000)) {
      Serial.println("¡Alerta! Se ha perdido la conexión de red. Reconectando...");
      setupNetwork();
    } else {
      checkSignal();
      float lat = 40.416775; // Cambiar por tu latitud de referencia
      float lng = -3.70379;  // Cambiar por tu longitud de referencia
      sendThingerRawTCP(lat, lng);
    } 
  } else {
    Serial.println("Sin red disponible. Pasados el 1min y a deep sleep");
  }

  // 8. Apagar el relé de los sensores
  Serial.println("[RELÉ] Apagando alimentación de sensores (LOW)...");
  digitalWrite(RELAY_PIN, LOW); 
  digitalWrite(MOSFET_PIN, LOW);
  
  // 9. Apagado limpio del SIM7070G
  Serial.println("\n[SIM] Apagando módulo limpiamente con AT+CPOF...");
  sendATCommand("AT+CPOF", "OK", 3000);
  delay(2000); 

  // 10. Configurar e ir a Deep Sleep del ESP32
  Serial.println("\n>>> Ciclo completado. Configurando temporizador ESP32... <<<");
  Serial.printf(">>> Durmiendo por %llu minutos... <<<\n", DEEP_SLEEP_TIME_US / 60000000ULL);
  Serial.flush();
  
  esp_sleep_enable_timer_wakeup(DEEP_SLEEP_TIME_US);
  esp_deep_sleep_start();
}

void loop() {
  // En Deep Sleep, el programa nunca llega aquí.
}

// ==========================================
// FUNCIONES DEL SENSOR RS485
// ==========================================

void leerSensor(uint8_t id, uint8_t request[], size_t reqSize, float &hum, float &temp) {
  uint8_t frame[16] = {0}; 
  int i = 0;

  while (Serial1.available()) {
    Serial1.read();
  }

  Serial.print("Consultando Sensor 0x0"); Serial.println(id, HEX);
  Serial1.write(request, reqSize);
  Serial1.flush(); 

  unsigned long startTime = millis();
  while (i < 9 && (millis() - startTime) < 500) {
    if (Serial1.available()) {
      frame[i++] = Serial1.read();
    }
  }

  if (i >= 9 && frame[0] == id && frame[1] == 0x03 && frame[2] == 0x04) {
    uint16_t rawHum = (frame[3] << 8) | frame[4];
    uint16_t rawTemp  = (frame[5] << 8) | frame[6];

    if (rawHum != 0) hum = rawHum / 10.0;
    if (rawTemp != 0) temp = rawTemp / 10.0;

    Serial.print("-> OK | Hum: "); Serial.print(isnan(hum) ? "N/A" : String(hum)); 
    Serial.print("% | Temp: "); Serial.println(isnan(temp) ? "N/A" : String(temp) + "C");
  } else {
    Serial.println("-> Error de lectura Modbus o Timeout.");
    Serial.print("-> Manteniendo humedad del ciclo anterior: "); 
    Serial.println(isnan(hum) ? "N/A" : String(hum) + "%");
    temp = NAN;
  }
}

// ==========================================
// FUNCIONES DEL MÓDULO SIM7070G
// ==========================================
void powerOnModule() {
  Serial.println("Comprobando estado del modulo SIM...");
  
  if (sendATCommand("AT", "OK", 1000)) {
    Serial.println("El modulo ya estaba encendido.");
    return;
  }

  Serial.println("El modulo está apagado. Encendiendo en frío mediante PWRKEY...");
  pinMode(PWRKEY, OUTPUT);
  digitalWrite(PWRKEY, HIGH);
  delay(100);
  digitalWrite(PWRKEY, LOW);
  delay(1200); 
  digitalWrite(PWRKEY, HIGH);
  
  Serial.println("Esperando arranque del SIM7070G...");
  delay(5000); 

  for (int i = 0; i < 5; i++) {
    if (sendATCommand("AT", "OK", 2000)) {
      Serial.println("¡Módulo encendido con éxito!");
      return;
    }
    delay(1000);
  }
  Serial.println("¡ADVERTENCIA! El módulo SIM7070G no responde.");
}

bool setupNetwork() {
  Serial.println("--- CONFIGURANDO RED Y APN ---");
  unsigned long startAttempt = millis();
  const unsigned long TIMEOUT = 60000; 

  sendATCommand("ATE0", "OK", 2000); 

  sendATCommand("AT+CPSMS=0", "OK", 2000);     
  sendATCommand("AT+CEDRXS=0,5", "OK", 2000);  
  sendATCommand("AT+CSCLK=0", "OK", 2000);     

  sendATCommand("AT+CFUN=0", "OK", 3000);
  sendATCommand("AT+CFUN=1", "OK", 3000);
  delay(5000); 

  sendATCommand("AT+CPIN?", "READY", 5000);
  
  String cgcontCommand = "AT+CGDCONT=1,\"IP\",\"" + String(apn) + "\"";
  sendATCommand(cgcontCommand.c_str(), "OK", 3000);
  sendATCommand("AT+CGATT=1", "OK", 5000);

  while (!sendATCommand("AT+CGATT?", "+CGATT: 1", 3000)) {
    if (millis() - startAttempt > TIMEOUT) {
      Serial.println("Timeout: No se encontró red tras 1 minuto. Abortando...");
      return false; 
    }
    Serial.println("Esperando attach a red...");
    delay(3000);
  }

  sendATCommand("AT+CNACT=0,1", "OK", 5000);

  while (!sendATCommand("AT+CNACT?", "+CNACT: 0,1", 3000)) {
    if (millis() - startAttempt > TIMEOUT) {
      Serial.println("Timeout: No se pudo obtener IP tras 1 minuto. Abortando...");
      return false; 
    }
    Serial.println("Esperando asignacion de IP...");
    delay(3000);
  }
  
  Serial.println(">>> RED LISTA Y CON IP <<<");
  return true; 
}

void sendThingerRawTCP(float lat, float lon) {
  Serial.println(">>> INICIANDO ENVÍO A THINGER.IO");

  String t1_str = isnan(temperatura1) ? "null" : String(temperatura1, 2);
  String h1_str = isnan(humedad1) ? "null" : String(humedad1, 2);
  String t2_str = isnan(temperatura2) ? "null" : String(temperatura2, 2);
  String h2_str = isnan(humedad2) ? "null" : String(humedad2, 2);
  String tDHT_str = isnan(temperaturaDHT) ? "null" : String(temperaturaDHT, 2);
  String hDHT_str = isnan(humedadDHT) ? "null" : String(humedadDHT, 2);

  String payload = "{\"lat\":" + String(lat, 6) + 
                   ",\"lng\":" + String(lon, 6) + 
                   ",\"temp1\":" + t1_str + 
                   ",\"hum1\":" + h1_str + 
                   ",\"temp2\":" + t2_str + 
                   ",\"hum2\":" + h2_str + 
                   ",\"tempDHT\":" + tDHT_str + 
                   ",\"humDHT\":" + hDHT_str + "}";

  // REEMPLAZA "TU_USUARIO" y "TU_DISPOSITIVO" por tus datos reales
  String httpRequest = "POST /v3/users/TU_USUARIO/devices/TU_DISPOSITIVO/callback/data HTTP/1.1\r\n";
  httpRequest += "Host: backend.thinger.io\r\n";
  httpRequest += "Content-Type: application/json;charset=UTF-8\r\n";
  // REEMPLAZA "TU_TOKEN_BEARER" por tu token JWT de Thinger.io privado
  httpRequest += "Authorization: Bearer TU_TOKEN_BEARER\r\n";
  httpRequest += "Accept: application/json, text/plain, */*\r\n";
  httpRequest += "Content-Length: " + String(payload.length()) + "\r\n";
  httpRequest += "Connection: close\r\n";
  httpRequest += "\r\n";
  httpRequest += payload;

  if (!sendATCommand("AT+CAOPEN=0,0,\"TCP\",\"backend.thinger.io\",80", "+CAOPEN: 0,0", 15000)) {
    Serial.println("Error abriendo socket TCP. Se intentará en el próximo ciclo.");
    return;
  }

  String sendCmd = "AT+CASEND=0," + String(httpRequest.length());
  Serial0.println(sendCmd); 
  
  unsigned long startWait = millis();
  while (millis() - startWait < 3000) {
    if (Serial0.read() == '>') break;
  }
  
  Serial0.print(httpRequest);
  Serial.println("Enviando JSON: " + payload);

  bool dataWaiting = false;
  startWait = millis();
  while (millis() - startWait < 10000) { 
    if (Serial0.available()) {
      String line = Serial0.readStringUntil('\n');
      line.trim();
      if (line.indexOf("+CADATAIND: 0") != -1) {
        dataWaiting = true;
        break;
      }
    }
  }

  if (dataWaiting) {
    Serial.println("Envio confirmado por el servidor.");
    sendATCommand("AT+CARECV=0,500", "OK", 5000);
  } else {
    Serial.println("Aviso: El servidor no envió confirmación.");
  }

  sendATCommand("AT+CACLOSE=0", "OK", 3000);
}

bool sendATCommand(const char* cmd, const char* expected_response, int timeout) {
  while(Serial0.available()) Serial0.read(); 
  
  Serial0.println(cmd);
  
  String response = "";
  unsigned long time = millis();
  while ((millis() - time) < timeout) {                    
    if (Serial0.available()) {
      char c = Serial0.read();
      response += c;
      if (response.indexOf(expected_response) != -1) {
        return true;
      }
    }
  }
  return false;
}

void checkSignal() {
  while(Serial0.available()) Serial0.read(); 
  
  Serial0.println("AT+CSQ");
  
  String response = "";
  unsigned long time = millis();
  while ((millis() - time) < 3000) {                    
    if (Serial0.available()) {
      char c = Serial0.read();
      response += c;
      if (response.indexOf("+CSQ:") != -1) {
        delay(100); 
        while(Serial0.available()) response += (char)Serial0.read();
        break;
      }
    }
  }
  
  Serial.print("[SEÑAL] ");
  Serial.println(response);
}
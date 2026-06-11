#include <WiFi.h>
#include <HTTPClient.h>
#include <Keypad.h>
#include <SPI.h>
#include <MFRC522.h>
#include "soc/soc.h"          
#include "soc/gpio_reg.h"

// --- CONFIGURACIÓN RFID (SPI) ---
#define SS_PIN  5
#define RST_PIN 22
MFRC522 mfrc522(SS_PIN, RST_PIN); // Instancia del lector

// --- CONFIGURACIÓN WI-FI (Wokwi Gateway) ---
const char* ssid = "Wokwi-GUEST";
const char* password = "";
const String apiUrl = "https://6a20fe5cb1d0aaf32b4e90d6.mockapi.io/usuarios";

// --- CONFIGURACIÓN DEL TECLADO ---
const byte FILAS = 4;
const byte COLUMNAS = 4;
char keys[FILAS][COLUMNAS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte pinFilas[FILAS] = {13, 14, 27, 26}; 
byte pinColumnas[COLUMNAS] = {25, 33, 32, 4};
Keypad teclado = Keypad(makeKeymap(keys), pinFilas, pinColumnas, FILAS, COLUMNAS);

// --- VARIABLES DE ESTADO ---
enum EstadoSistema { ESPERANDO_TARJETA, ESPERANDO_PIN, VALIDANDO_NUBE, ACCESO_CONCEDIDO, CERRADURA_ABIERTA, ACCESO_DENEGADO };
EstadoSistema estadoActual = ESPERANDO_TARJETA;

unsigned long tiempoApertura = 0;

String pinIngresado = "";
String uidLeido = "";
const int RELE_PIN = 2; // Nuestro actuador Bare-Metal

void setup() {
  Serial.begin(115200);
  
  // Inicialización Bare-Metal del Pin 2
  REG_WRITE(GPIO_ENABLE_W1TS_REG, 1 << RELE_PIN); // Habilita el pin como salida
  REG_WRITE(GPIO_OUT_W1TC_REG, 1 << RELE_PIN);
  // Inicialización de Bus SPI y Lector RFID
  SPI.begin();
  mfrc522.PCD_Init();

  Serial.print("Conectando a WiFi...");
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Conectado!");
  Serial.println("--- SISTEMA INICIADO ---");
  Serial.println("Acerque una tarjeta al lector RFID...");
}

void loop() {
  switch (estadoActual) {
    
    case ESPERANDO_TARJETA:
      // Verificamos si hay una tarjeta nueva en el sensor
      if (mfrc522.PICC_IsNewCardPresent() && mfrc522.PICC_ReadCardSerial()) {
        uidLeido = "";
        // Extraemos el código hexadecimal byte por byte
        for (byte i = 0; i < mfrc522.uid.size; i++) {
          uidLeido += String(mfrc522.uid.uidByte[i] < 0x10 ? "0" : "");
          uidLeido += String(mfrc522.uid.uidByte[i], HEX);
        }
        uidLeido.toUpperCase();
        
        Serial.println("\nTarjeta detectada. UID: " + uidLeido);
        Serial.println("Ingrese PIN de 4 digitos y pulse '#':");
        pinIngresado = "";
        estadoActual = ESPERANDO_PIN;
        
        // Pausamos la lectura de la tarjeta actual para no leerla en bucle
        mfrc522.PICC_HaltA(); 
      }
      break;

    case ESPERANDO_PIN:
      {
        char tecla = teclado.getKey();
        if (tecla) {
          if (tecla == '#') { 
             // NUEVA VALIDACIÓN: Exigimos exactamente 4 dígitos
             if (pinIngresado.length() == 4) {
                 Serial.println("\nValidando credenciales en la nube...");
                 estadoActual = VALIDANDO_NUBE;
             } else {
                 Serial.println("\n[!] ERROR DE SEGURIDAD: El PIN debe ser exactamente de 4 digitos.");
                 delay(1500);
                 Serial.println("Ingrese PIN de 4 digitos y pulse '#':");
                 pinIngresado = ""; // Vaciamos el buffer para que intente de nuevo
             }
          } else if (tecla == '*') {
             // Opcional: Podrían usar '*' para borrar si se equivocan, pero por ahora lo ignoramos
          } else {
             // Solo permitimos almacenar un máximo de 4 caracteres
             if (pinIngresado.length() < 4) {
                 pinIngresado += tecla;
                 Serial.print("*"); // Ocultar el PIN por seguridad
             }
          }
        }
      }
      break;

    case VALIDANDO_NUBE:
      if (validarAccesoMockAPI()) {
        estadoActual = ACCESO_CONCEDIDO;
      } else {
        estadoActual = ACCESO_DENEGADO;
      }
      break;

    case ACCESO_CONCEDIDO:
      Serial.println("\nACCESO PERMITIDO: Abriendo cerradura...");
      
      // Encendemos el actuador (Bare-metal)
      REG_WRITE(GPIO_OUT_W1TS_REG, 1 << RELE_PIN); 
      
      // Guardamos el milisegundo exacto en el que se abrió
      tiempoApertura = millis(); 
      
      // Transicionamos al nuevo estado en lugar de pausar el sistema
      estadoActual = CERRADURA_ABIERTA;
      break;

    case CERRADURA_ABIERTA:
      // El procesador sigue corriendo a máxima velocidad y evalúa esta resta:
      // ¿El tiempo actual menos el tiempo en que abrimos es mayor o igual a 5000ms?
      if (millis() - tiempoApertura >= 5000) {
        
        // Apagamos el actuador (Bare-metal)
        REG_WRITE(GPIO_OUT_W1TC_REG, 1 << RELE_PIN); 
        
        Serial.println("Cerradura cerrada. --Puede volver a ingresar.");
        estadoActual = ESPERANDO_TARJETA;
      }
      break;

    case ACCESO_DENEGADO:
      Serial.println("\nACCESO DENEGADO. Por favor, intente nuevamente.");
      delay(2000); // Pausa de 2 segundos para dar feedback visual/sonoro al usuario
      estadoActual = ESPERANDO_TARJETA; // Reiniciamos el ciclo
      break;
  }
}

bool validarAccesoMockAPI() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    
    // CONSTRUCCIÓN DE LA URL CON PARÁMETROS DE FILTRADO REST
    // Produce un formato estructurado: https://.../usuarios?rfid_uid=AABBCCDD&pin=1234
    String urlConFiltro = apiUrl + "?rfid_uid=" + uidLeido + "&pin=" + pinIngresado;
    
    Serial.println("\n--- AUDITORÍA DE RED ---");
    Serial.println("Petición URL: " + urlConFiltro);
    
    http.begin(urlConFiltro);
    int httpResponseCode = http.GET();
    
    if (httpResponseCode > 0) {
      String payload = http.getString();
      Serial.println("Respuesta del Servidor: " + payload);
      Serial.println("------------------------\n");
      
      // ANÁLISIS ARQUITECTÓNICO DE LA RESPUESTA:
      // 1. Si las credenciales no coinciden, MockAPI responde con un array vacío "[]"
      if (payload == "[]" || payload == "\"Not found\"" || payload == "Not found" || payload.length() < 5) {
        Serial.println("Resultado: Credenciales no encontradas en el registro.");
        http.end();
        return false;
      }
      
      // 2. Si hay coincidencia, verificamos el estado lógico del permiso de acceso
      if (payload.indexOf("\"access_status\":true") > 0 || payload.indexOf("\"access_status\": true") > 0) {
        http.end();
        return true;
      } else {
        Serial.println("Resultado: Usuario identificado pero el acceso está REVOCADO.");
      }
    } else {
      Serial.print("Fallo de comunicación HTTP. Código de error: ");
      Serial.println(httpResponseCode);
    }
    http.end();
  } else {
    Serial.println("Error del Sistema: Interfaz Wi-Fi no disponible.");
  }
  return false; // Denegación por defecto ante fallos de infraestructura
}
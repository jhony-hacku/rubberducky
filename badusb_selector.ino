/*
 * BadUSB / Emulador HID con selector de 3 posiciones
 * -------------------------------------------------------------
 * Placa:  Arduino Leonardo / Pro Micro (ATmega32u4)
 * Función: segun la posicion del selector lee 1.txt, 2.txt o 3.txt
 *          de la SD e interpreta un subconjunto de DuckyScript.
 *
 * Comandos soportados:
 *   STRING <texto>      -> escribe texto
 *   DELAY <ms>          -> espera
 *   DEFAULTDELAY <ms>   -> retardo automatico tras cada comando
 *   REM <texto>         -> comentario (se ignora)
 *   REPEAT <n>          -> repite la linea anterior n veces
 *   <TECLA> / combos    -> ej: ENTER | GUI r | CTRL ALT DELETE
 *
 * Requisitos:
 *   - Libreria Arduino "Keyboard" >= 1.0.5 (soporte de layouts).
 *     Si usas una version antigua, cambia Keyboard.begin(KeyboardLayout_es_ES)
 *     por Keyboard.begin()  (quedara en layout US).
 */

#include <SPI.h>
#include <SD.h>
#include "Keyboard.h"

// ===== Pines =====
#define PIN_SEL_1 A1
#define PIN_SEL_2 A0
#define SD_CS     10

// ===== Config =====
#define LINE_MAX  128          // largo maximo de una linea del payload

File myFile;
int  oldPosition = -1;
bool sdOK = false;

unsigned int defaultDelay = 0;         // retardo automatico entre comandos
char prevLine[LINE_MAX] = "";          // para el comando REPEAT

void setup() {
  Serial.begin(9600);

  pinMode(PIN_SEL_1, INPUT_PULLUP);
  pinMode(PIN_SEL_2, INPUT_PULLUP);

  Serial.println(F("Inicializando SD..."));
  sdOK = SD.begin(SD_CS);
  Serial.println(sdOK ? F("SD OK") : F("Fallo SD"));

  // Arranca con la posicion actual para NO disparar payload al encender
  oldPosition = leerSelector();

  Serial.println(F("Sistema listo"));
}

void loop() {
  int posicion = leerSelector();

  if (posicion != oldPosition) {
    delay(30);                                 // antirrebote
    if (posicion != leerSelector()) return;    // fue ruido mecanico
    oldPosition = posicion;

    char archivo[8];
    snprintf(archivo, sizeof(archivo), "%d.txt", posicion);

    Serial.print(F("Posicion "));
    Serial.print(posicion);
    Serial.print(F(" -> "));
    Serial.println(archivo);

    if (sdOK) runBadUSB(archivo);
    else      Serial.println(F("SD no disponible"));
  }
}

// ===== Selector ON-OFF-ON =====
int leerSelector() {
  if (digitalRead(PIN_SEL_1) == LOW) return 1;   // arriba
  if (digitalRead(PIN_SEL_2) == LOW) return 3;   // abajo
  return 2;                                       // centro
}

// ================= BAD USB =================
void runBadUSB(const char* nombreArchivo) {
  myFile = SD.open(nombreArchivo);
  if (!myFile) {
    Serial.print(F("No se pudo abrir: "));
    Serial.println(nombreArchivo);
    return;
  }

  Keyboard.begin(KeyboardLayout_es_ES);   // layout espanol
  defaultDelay = 0;
  prevLine[0]  = '\0';

  char line[LINE_MAX];
  uint8_t idx = 0;

  while (myFile.available()) {
    char m = myFile.read();
    if (m == '\n') {
      line[idx] = '\0';
      manejarLinea(line);
      idx = 0;
    } else if (m != '\r' && idx < LINE_MAX - 1) {
      line[idx++] = m;
    }
    // si se supera LINE_MAX la linea se trunca, no se corrompe memoria
  }
  if (idx > 0) { line[idx] = '\0'; manejarLinea(line); }  // ultima linea sin \n

  myFile.close();
  Keyboard.releaseAll();                   // por si quedo alguna tecla sostenida
  Keyboard.end();
  Serial.println(F("Payload terminado"));
}

// Gestiona REPEAT y delega el resto en procesarLinea()
void manejarLinea(char* line) {
  if (strncmp(line, "REPEAT", 6) == 0 && (line[6] == ' ' || line[6] == '\0')) {
    int veces = (line[6] == ' ') ? atoi(line + 7) : 0;
    for (int i = 0; i < veces; i++) {
      char tmp[LINE_MAX];
      strncpy(tmp, prevLine, LINE_MAX);
      tmp[LINE_MAX - 1] = '\0';
      procesarLinea(tmp);
    }
  } else {
    strncpy(prevLine, line, LINE_MAX);      // guarda para un posible REPEAT
    prevLine[LINE_MAX - 1] = '\0';
    procesarLinea(line);
  }
}

void procesarLinea(char* l) {
  if (l[0] == '\0') return;                 // linea vacia

  char* sp = strchr(l, ' ');

  if (sp == NULL) {                          // una sola palabra -> tecla
    if (!Press(l)) teclaDesconocida(l);
    Keyboard.releaseAll();
  } else {
    *sp = '\0';
    char* arg = sp + 1;

    if      (strcmp(l, "STRING") == 0) Keyboard.print(arg);
    else if (strcmp(l, "DELAY")  == 0) delay(atoi(arg));
    else if (strcmp(l, "REM")    == 0) { /* comentario */ }
    else if (strcmp(l, "DEFAULTDELAY")  == 0 ||
             strcmp(l, "DEFAULT_DELAY") == 0) defaultDelay = atoi(arg);
    else {
      *sp = ' ';                             // combinacion: CTRL ALT DELETE
      char* tok = strtok(l, " ");
      while (tok != NULL) {
        if (!Press(tok)) teclaDesconocida(tok);
        tok = strtok(NULL, " ");
      }
      Keyboard.releaseAll();
    }
  }

  if (defaultDelay) delay(defaultDelay);
}

// Pulsa (mantiene) una tecla: caracter suelto o nombre especial.
// Devuelve false si no reconoce el nombre (ej. "FOO", "F13").
bool Press(const char* b) {
  if (strlen(b) == 1) { Keyboard.press(b[0]); return true; }

  if      (!strcmp(b, "ENTER"))                         Keyboard.press(KEY_RETURN);
  else if (!strcmp(b, "CTRL")  || !strcmp(b, "CONTROL"))Keyboard.press(KEY_LEFT_CTRL);
  else if (!strcmp(b, "SHIFT"))                         Keyboard.press(KEY_LEFT_SHIFT);
  else if (!strcmp(b, "ALT"))                           Keyboard.press(KEY_LEFT_ALT);
  else if (!strcmp(b, "GUI")   || !strcmp(b, "WINDOWS"))Keyboard.press(KEY_LEFT_GUI);
  else if (!strcmp(b, "UP")    || !strcmp(b, "UPARROW"))   Keyboard.press(KEY_UP_ARROW);
  else if (!strcmp(b, "DOWN")  || !strcmp(b, "DOWNARROW")) Keyboard.press(KEY_DOWN_ARROW);
  else if (!strcmp(b, "LEFT")  || !strcmp(b, "LEFTARROW")) Keyboard.press(KEY_LEFT_ARROW);
  else if (!strcmp(b, "RIGHT") || !strcmp(b, "RIGHTARROW"))Keyboard.press(KEY_RIGHT_ARROW);
  else if (!strcmp(b, "DELETE")|| !strcmp(b, "DEL"))    Keyboard.press(KEY_DELETE);
  else if (!strcmp(b, "PAGEUP"))                        Keyboard.press(KEY_PAGE_UP);
  else if (!strcmp(b, "PAGEDOWN"))                      Keyboard.press(KEY_PAGE_DOWN);
  else if (!strcmp(b, "HOME"))                          Keyboard.press(KEY_HOME);
  else if (!strcmp(b, "END"))                           Keyboard.press(KEY_END);
  else if (!strcmp(b, "ESC")   || !strcmp(b, "ESCAPE"))Keyboard.press(KEY_ESC);
  else if (!strcmp(b, "INSERT"))                        Keyboard.press(KEY_INSERT);
  else if (!strcmp(b, "TAB"))                           Keyboard.press(KEY_TAB);
  else if (!strcmp(b, "CAPSLOCK"))                      Keyboard.press(KEY_CAPS_LOCK);
  else if (!strcmp(b, "SPACE"))                         Keyboard.press(' ');
  else if (b[0] == 'F') {                               // F1..F12 (codigos consecutivos)
    int n = atoi(b + 1);
    if (n >= 1 && n <= 12) { Keyboard.press(KEY_F1 + (n - 1)); return true; }
    return false;                                       // ej. "F13", "FOO"
  }
  else return false;                                    // nombre no reconocido

  return true;
}

void teclaDesconocida(const char* b) {
  Serial.print(F("Tecla desconocida: "));
  Serial.println(b);
}

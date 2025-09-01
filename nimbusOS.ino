/**
 * NimbusOS.ino - Sistema operativo completo con networking, LCD, SD e telnet
 * 
 * Versione unificata con API compatibile con NibbleOS
 */

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <avr/pgmspace.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <LiquidCrystal_I2C.h>

// ===== CONFIGURAZIONE =====
#define OS_VERSION "1.0.0"
#define MAX_TASKS 6
#define BUF_SIZE 24
#define TASK_MAX_RUNTIME_MS 2000
#define SCHEDULER_FREQ_HZ 100

// Opzioni di caratteristiche
#define ENABLE_WATCHDOG
#define ENABLE_SUSPEND_RESUME
#define ENABLE_MESSAGING
#define ENABLE_NETWORKING
#define ENABLE_SD_CARD
#define ENABLE_LCD
#define ENABLE_TELNET
#define ENABLE_SCRIPT_ENGINE

// Pin
#define PIN_LED 13
#define PIN_SD_CS 4
#define PIN_ETH_CS 10

// Task states
#define T_READY 1
#define T_RUN 2
#define T_WAIT 3
#define T_SUSPENDED 4

// Priorità
#define PRIORITY_IDLE 0
#define PRIORITY_LOW 1
#define PRIORITY_NORMAL 2
#define PRIORITY_HIGH 3

// Buffer globale condiviso
static char gbuf[BUF_SIZE];

// Stringhe di comando
#define CMD_HELP "help"
#define CMD_TASKS "tasks"
#define CMD_MEM "mem"
#define CMD_LED "led"
#define CMD_ADC "adc"
#define CMD_NET "net"
#define CMD_TELNET "telnet"
#define CMD_LCD "lcd"
#define CMD_SD "sd"
#define CMD_RUN "run"
#define CMD_SUSPEND "suspend"
#define CMD_RESUME "resume"
#define CMD_POWER "power"

// Stringhe di aiuto in memoria flash
const char HELP_TEXT[] PROGMEM = 
    "Commands:\r\n"
    " help - Help\r\n"
    " tasks - Tasks\r\n"
    " mem - Memory\r\n"
    " led <n> - LED\r\n"
    " adc <p> - ADC\r\n"
    " net - Network\r\n"
    " telnet - Telnet\r\n"
    " lcd - LCD\r\n"
    " lcd <t> - Write\r\n"
    " sd - SD card\r\n"
    " run <file> - Run script\r\n"
    " suspend <id> - Suspend task\r\n"
    " resume <id> - Resume task\r\n"
    " power <0|1> - Power mode";

// Struttura task ottimizzata con bit-fields
struct Task {
    void (*fn)(void);
    uint16_t period;
    uint16_t last;
    uint8_t state : 3;   // Aumentato a 3 bit per aggiungere T_SUSPENDED
    uint8_t pri : 2;
    uint8_t id : 3;
    uint32_t sleepUntil;  // Quando risvegliare il task
    uint32_t startRun;    // Quando il task ha iniziato la sua esecuzione
    
    // Sistema messaggi
    struct {
        uint8_t type;
        uint8_t data[8];
        uint8_t size;
        bool pending;
    } message;
};

// OS minimale con protezione pseudo-preemptive
class NimbusOS {
public:
    Task t[MAX_TASKS];
    uint8_t n;
    uint8_t cur;
    volatile uint32_t ticks;
    uint32_t ctxSw;
    uint8_t suspendFlags;  // Un bit per ogni task sospeso
    bool lowPowerMode;     // Modalità risparmio energetico
    
    NimbusOS() : n(0), cur(0), ticks(0), ctxSw(0), suspendFlags(0), lowPowerMode(false) {
        memset(t, 0, sizeof(t));
    }
    
    int add(void (*f)(void), uint16_t p, uint8_t pr, const char* name = "Task") {
        if (n >= MAX_TASKS) return -1;
        t[n].fn = f;
        t[n].period = p;
        t[n].pri = pr;
        t[n].state = T_READY;
        t[n].id = n;
        t[n].last = 0;
        t[n].sleepUntil = 0;
        t[n].startRun = 0;
        t[n].message.pending = false;
        return n++;
    }
    
    void sleep(uint16_t ms) {
        t[cur].state = T_WAIT;
        t[cur].sleepUntil = millis() + ms;
    }
    
    // Controllo se il task corrente dovrebbe cedere il controllo
    bool shouldYield() {
        uint32_t now = millis();
        // Se il task è in esecuzione da troppo tempo, forza yield
        if (now - t[cur].startRun > TASK_MAX_RUNTIME_MS) {
            return true;
        }
        return false;
    }
    
    // Suspend/resume
    int suspendTask(uint8_t id) {
        #ifdef ENABLE_SUSPEND_RESUME
        if (id < n) {
            suspendFlags |= (1 << id);  // Imposta il bit di sospensione
            if (t[id].state != T_WAIT) {
                t[id].state = T_SUSPENDED;
            }
            return 0;  // Success
        }
        #endif
        return -1;  // Invalid task
    }
    
    int resumeTask(uint8_t id) {
        #ifdef ENABLE_SUSPEND_RESUME
        if (id < n) {
            suspendFlags &= ~(1 << id);  // Rimuove il bit di sospensione
            if (t[id].state == T_SUSPENDED) {
                t[id].state = T_READY;
            }
            return 0;  // Success
        }
        #endif
        return -1;  // Invalid task
    }
    
    // Sistema messaggi
    int sendMessage(uint8_t taskId, uint8_t msgType, const void* data, uint8_t size) {
        #ifdef ENABLE_MESSAGING
        if (taskId >= n || size > 8) {
            return -1;  // Invalid parameters
        }
        
        // Evita di sovrascrivere un messaggio non elaborato
        if (t[taskId].message.pending) {
            return -2;  // Message queue full
        }
        
        cli();
        t[taskId].message.type = msgType;
        memcpy(t[taskId].message.data, data, size);
        t[taskId].message.size = size;
        t[taskId].message.pending = true;
        sei();
        
        return 0;  // Success
        #else
        return -1;
        #endif
    }
    
    int receiveMessage(uint8_t* msgType, void* data, uint8_t maxSize) {
        #ifdef ENABLE_MESSAGING
        if (!t[cur].message.pending) {
            return 0;  // No message
        }
        
        cli();
        *msgType = t[cur].message.type;
        uint8_t copySize = min(maxSize, t[cur].message.size);
        memcpy(data, t[cur].message.data, copySize);
        t[cur].message.pending = false;
        sei();
        
        return copySize;  // Return message size
        #else
        return 0;
        #endif
    }
    
    // Power management
    void setLowPowerMode(bool enabled) {
        lowPowerMode = enabled;
        
        // Implementazione basata sul target
        if (enabled) {
            // Riduci frequenza CPU
            #if defined(__AVR_ATmega328P__)
            // Imposta divisore clock a 2 (8MHz invece di 16MHz)
            CLKPR = 0x80;  // Enable clock prescaler change
            CLKPR = 0x01;  // Divide by 2
            #endif
            
            // Disabilita periferiche non critiche
            #ifdef ENABLE_LCD
            if (lcdOk) {
                display.noBacklight();
            }
            #endif
        } else {
            // Ripristina frequenza normale
            #if defined(__AVR_ATmega328P__)
            CLKPR = 0x80;  // Enable clock prescaler change
            CLKPR = 0x00;  // No division (16MHz)
            #endif
            
            // Riattiva periferiche
            #ifdef ENABLE_LCD
            if (lcdOk) {
                display.backlight();
            }
            #endif
        }
    }
    
    void run() {
        // Timer1 setup - Configura per generare interrupt periodici
        cli();
        TCCR1A = 0;
        TCCR1B = (1 << WGM12) | (1 << CS11);  // CTC mode, prescaler=8
        OCR1A = F_CPU / 8 / SCHEDULER_FREQ_HZ - 1;  // Per ottenere esattamente SCHEDULER_FREQ_HZ
        TIMSK1 = (1 << OCIE1A);
        sei();
        
        #ifdef ENABLE_WATCHDOG
        wdt_enable(WDTO_2S);  // Watchdog a 2 secondi
        #endif
        
        while (1) {
            uint32_t now = millis();
            int8_t best = -1;
            uint8_t maxPri = 0;
            
            #ifdef ENABLE_WATCHDOG
            wdt_reset();  // Reset watchdog
            #endif
            
            for (uint8_t i = 0; i < n; i++) {
                // Salta i task sospesi
                #ifdef ENABLE_SUSPEND_RESUME
                if (suspendFlags & (1 << i)) {
                    continue;
                }
                #endif
                
                // Controlla timeout di sleep
                if (t[i].state == T_WAIT && now >= t[i].sleepUntil) {
                    t[i].state = T_READY;
                    t[i].sleepUntil = 0;
                }
                
                // Seleziona task con priorità più alta
                if (t[i].state == T_READY && (now - t[i].last) >= t[i].period) {
                    if (t[i].pri >= maxPri) {
                        maxPri = t[i].pri;
                        best = i;
                    }
                }
            }
            
            if (best >= 0) {
                cur = best;
                t[cur].state = T_RUN;
                t[cur].last = now;
                t[cur].startRun = now;  // Registra quando il task inizia
                t[cur].fn();
                if (t[cur].state == T_RUN) {
                    t[cur].state = T_READY;
                }
                ctxSw++;
            }
            
            // Se in modalità risparmio energetico e nessun task pronto, metti CPU in sleep
            if (best < 0 && lowPowerMode) {
                set_sleep_mode(SLEEP_MODE_IDLE);
                sleep_enable();
                sleep_mode();
                sleep_disable();
            } else {
                delay(1);  // Piccola pausa per evitare di consumare CPU inutilmente
            }
        }
    }
    
    // Funzioni utili
    uint16_t getFreeMemory() {
        extern int __heap_start, *__brkval;
        int v;
        return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
    }
    
    uint32_t getUptime() {
        return ticks / SCHEDULER_FREQ_HZ;
    }
};

NimbusOS os;

ISR(TIMER1_COMPA_vect) {
    os.ticks++;
}

// Macro per verificare quando cedere il controllo in loop lunghi
#define YIELD_IF_NEEDED() if (os.shouldYield()) return

// SPI Manager per multiplexare SD card e Ethernet
class SPIMgr {
    static uint8_t dev;
public:
    static void sel(uint8_t d) {
        if (dev != d) {
            digitalWrite(dev, HIGH);
            digitalWrite(d, LOW);
            dev = d;
        }
    }
    
    static void desel() {
        digitalWrite(PIN_SD_CS, HIGH);
        digitalWrite(PIN_ETH_CS, HIGH);
    }
};
uint8_t SPIMgr::dev = 0;

// Driver di rete Ethernet
#ifdef ENABLE_NETWORKING
static EthernetServer webServer(80);
static EthernetServer telnetServer(23);
static EthernetClient telnetClient;
static bool netOk = false;
static uint8_t telnetState = 0;
static uint8_t telnetIndex = 0;

bool netInit() {
    pinMode(PIN_ETH_CS, OUTPUT);
    SPIMgr::sel(PIN_ETH_CS);
    
    // Mac address e IP
    uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
    
    // Inizializza Ethernet
    Ethernet.begin(mac, IPAddress(192,168,1,100));
    delay(200);
    
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        Serial.println(F("Ethernet shield not detected"));
        SPIMgr::desel();
        return false;
    }
    
    webServer.begin();
    telnetServer.begin();
    
    SPIMgr::desel();
    return true;
}

bool checkHTTP() {
    SPIMgr::sel(PIN_ETH_CS);
    EthernetClient client = webServer.available();
    
    if (client) {
        char buf[32];
        uint8_t i = 0;
        
        // Leggi richiesta HTTP
        while (client.available() && i < 31) {
            char ch = client.read();
            buf[i++] = ch;
            if (i > 3 && buf[i-4] == '\r' && buf[i-2] == '\r') break;
            YIELD_IF_NEEDED();
        }
        buf[i] = 0;
        
        // Risposta standard
        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: text/plain"));
        client.println();
        
        // Processa richiesta
        if (strstr(buf, "GET /status")) {
            client.print(F("{\"uptime\":"));
            client.print(os.ticks/SCHEDULER_FREQ_HZ);
            client.print(F(",\"mem\":"));
            client.print(os.getFreeMemory());
            client.print(F(",\"tasks\":"));
            client.print(os.n);
            client.print(F(",\"switches\":"));
            client.print(os.ctxSw);
            client.println(F("}"));
        }
        else if (strstr(buf, "GET /led/on")) {
            digitalWrite(PIN_LED, HIGH);
            client.println(F("LED ON"));
        }
        else if (strstr(buf, "GET /led/off")) {
            digitalWrite(PIN_LED, LOW);
            client.println(F("LED OFF"));
        }
        else if (strstr(buf, "GET /tasks")) {
            // Lista dei task
            client.println(F("ID NAME       STATE PRI"));
            client.println(F("----------------------"));
            
            const char* states[] = {"IDLE", "RDY", "RUN", "WAIT", "SUSP"};
            
            for (int i = 0; i < os.n; i++) {
                client.print(i);
                client.print(F(": Task"));
                client.print(i);
                client.print(F("    "));
                client.print(states[os.t[i].state]);
                client.print(F("  "));
                client.println(os.t[i].pri);
            }
        }
        else {
            // Pagina principale
            client.println(F("NimbusOS Web Interface"));
            client.println(F("----------------------"));
            client.println(F("Available endpoints:"));
            client.println(F("/status - System status"));
            client.println(F("/led/on - Turn LED on"));
            client.println(F("/led/off - Turn LED off"));
            client.println(F("/tasks - List tasks"));
        }
        
        client.stop();
    }
    
    SPIMgr::desel();
    return true;
}

bool telnetAuth() {
    SPIMgr::sel(PIN_ETH_CS);
    
    if (!telnetClient.connected()) {
        telnetClient = telnetServer.available();
        if (telnetClient) {
            telnetState = 1;
            telnetIndex = 0;
            telnetClient.println(F("\r\n=== NimbusOS ==="));
            telnetClient.print(F("Login: "));
        }
    }
    
    if (telnetClient.connected()) {
        while (telnetClient.available()) {
            char c = telnetClient.read();
            
            if (c == '\r' || c == '\n') {
                gbuf[telnetIndex] = 0;
                
                if (telnetState == 1) {
                    if (strcmp(gbuf, "admin") == 0) {
                        telnetState = 2;
                        telnetClient.print(F("\r\nPassword: "));
                    } else {
                        telnetClient.println(F("\r\nInvalid login"));
                        telnetClient.stop();
                    }
                }
                else if (telnetState == 2) {
                    if (strcmp(gbuf, "nimbus") == 0) {
                        telnetState = 3;
                        telnetClient.println(F("\r\nWelcome to NimbusOS!"));
                        telnetClient.print(F("\r\n> "));
                    } else {
                        telnetClient.println(F("\r\nInvalid password"));
                        telnetClient.stop();
                    }
                }
                
                telnetIndex = 0;
            }
            else if (telnetIndex < BUF_SIZE-1 && c >= 32) {
                gbuf[telnetIndex++] = c;
                if (telnetState != 2) telnetClient.write(c);  // Non echo per password
            }
            
            YIELD_IF_NEEDED();
        }
    }
    
    SPIMgr::desel();
    return telnetState == 3;
}

bool telnetCmd(char* cmd) {
    if (telnetState != 3) return false;
    
    SPIMgr::sel(PIN_ETH_CS);
    
    while (telnetClient.available()) {
        char c = telnetClient.read();
        
        if (c == '\r' || c == '\n') {
            if (telnetIndex > 0) {
                gbuf[telnetIndex] = 0;
                strcpy(cmd, gbuf);
                telnetClient.println();
                telnetIndex = 0;
                SPIMgr::desel();
                return true;
            }
        }
        else if (telnetIndex < BUF_SIZE-1 && c >= 32) {
            gbuf[telnetIndex++] = c;
            telnetClient.write(c);  // Echo
        }
        
        YIELD_IF_NEEDED();
    }
    
    SPIMgr::desel();
    return false;
}

void telnetSend(const char* s) {
    if (telnetState == 3) {
        SPIMgr::sel(PIN_ETH_CS);
        telnetClient.print(s);
        SPIMgr::desel();
    }
}

void telnetDisconnect() {
    if (telnetClient.connected()) {
        SPIMgr::sel(PIN_ETH_CS);
        telnetClient.stop();
        telnetState = 0;
        telnetIndex = 0;
        SPIMgr::desel();
    }
}
#endif // ENABLE_NETWORKING

// LCD driver
#ifdef ENABLE_LCD
static LiquidCrystal_I2C display(0x27, 16, 2);
static bool lcdOk = false;

void lcdInit() {
    // Inizializza display LCD I2C
    display.init();
    display.backlight();
    lcdOk = true;
}

void lcdPrint(uint8_t r, const char* s) {
    if (!lcdOk) return;
    display.setCursor(0, r);
    display.print("                ");  // Clear row
    display.setCursor(0, r);
    display.print(s);
}

void lcdClear() {
    if (!lcdOk) return;
    display.clear();
}
#endif // ENABLE_LCD

// SD driver
#ifdef ENABLE_SD_CARD
static bool sdOk = false;

bool sdInit() {
    pinMode(PIN_SD_CS, OUTPUT);
    SPIMgr::sel(PIN_SD_CS);
    sdOk = SD.begin(PIN_SD_CS);
    SPIMgr::desel();
    return sdOk;
}

void sdLog(const char* message) {
    if (!sdOk) return;
    
    SPIMgr::sel(PIN_SD_CS);
    File logFile = SD.open("system.log", FILE_WRITE);
    if (logFile) {
        // Timestamp
        logFile.print(os.getUptime());
        logFile.print(F(": "));
        logFile.println(message);
        logFile.close();
    }
    SPIMgr::desel();
}

bool runScript(const char* filename) {
    if (!sdOk) return false;
    
    SPIMgr::sel(PIN_SD_CS);
    File scriptFile = SD.open(filename, FILE_READ);
    if (!scriptFile) {
        Serial.print(F("File not found: "));
        Serial.println(filename);
        SPIMgr::desel();
        return false;
    }
    
    Serial.print(F("Running script: "));
    Serial.println(filename);
    
    #ifdef ENABLE_LCD
    if (lcdOk) {
        char buf[17];
        snprintf(buf, sizeof(buf), "Script: %.8s", filename);
        lcdPrint(0, buf);
    }
    #endif
    
    // Legge ed esegue ogni riga dello script
    while (scriptFile.available()) {
        int i = 0;
        // Legge una riga
        while (scriptFile.available() && i < BUF_SIZE-1) {
            char c = scriptFile.read();
            if (c == '\n' || c == '\r') break;
            gbuf[i++] = c;
            YIELD_IF_NEEDED();
        }
        gbuf[i] = 0;
        
        // Se la riga non è un commento o vuota, eseguila
        if (i > 0 && gbuf[0] != '#') {
            Serial.print(F("> "));
            Serial.println(gbuf);
            processCommand(gbuf);
        }
        
        // Consuma eventuali caratteri di newline extra
        while (scriptFile.available() && (scriptFile.peek() == '\n' || scriptFile.peek() == '\r')) {
            scriptFile.read();
        }
        
        YIELD_IF_NEEDED();
    }
    
    scriptFile.close();
    SPIMgr::desel();
    
    #ifdef ENABLE_LCD
    if (lcdOk) {
        lcdPrint(0, "NimbusOS");
    }
    #endif
    
    return true;
}
#endif // ENABLE_SD_CARD

// Funzione di utilità per confronto stringhe
bool strcmpPM(const char* str, const char* keyword) {
    return strcmp(str, keyword) == 0;
}

// Processa comandi della shell
void processCommand(char* cmd) {
    char* args = strchr(cmd, ' ');
    if (args) {
        *args = '\0';
        args++;
    }
    
    if (strcmpPM(cmd, CMD_HELP)) {
        Serial.println((__FlashStringHelper*)HELP_TEXT);
    }
    else if (strcmpPM(cmd, CMD_TASKS)) {
        const char* states[] = {"IDLE", "RDY", "RUN", "WAIT", "SUSP"};
        Serial.println(F("ID NAME       STATE PRI"));
        Serial.println(F("----------------------"));
        
        for (int i = 0; i < os.n; i++) {
            Serial.print(i);
            Serial.print(F(": Task"));
            Serial.print(i);
            Serial.print(F("    "));
            Serial.print(states[os.t[i].state]);
            Serial.print(F("  "));
            Serial.println(os.t[i].pri);
            
            YIELD_IF_NEEDED();
        }
    }
    else if (strcmpPM(cmd, CMD_MEM)) {
        Serial.print(F("Free: ~"));
        Serial.print(os.getFreeMemory());
        Serial.println(F(" bytes"));
        Serial.print(F("Uptime: "));
        Serial.print(os.getUptime());
        Serial.println(F(" sec"));
        Serial.print(F("Context switches: "));
        Serial.println(os.ctxSw);
    }
    else if (strcmpPM(cmd, CMD_LED)) {
        if (args) {
            int val = atoi(args);
            pinMode(PIN_LED, OUTPUT);
            digitalWrite(PIN_LED, val);
            Serial.println(F("OK"));
        } else {
            Serial.println(F("Usage: led <0|1>"));
        }
    }
    else if (strcmpPM(cmd, CMD_ADC)) {
        if (args) {
            int pin = atoi(args);
            int val = analogRead(pin);
            Serial.print(F("ADC"));
            Serial.print(pin);
            Serial.print(F(" = "));
            Serial.println(val);
        } else {
            Serial.println(F("Usage: adc <pin>"));
        }
    }
    #ifdef ENABLE_NETWORKING
    else if (strcmpPM(cmd, CMD_NET)) {
        if (netOk) {
            Serial.println(F("Ethernet: Connected"));
            Serial.println(F("IP: 192.168.1.100"));
            Serial.println(F("HTTP: Port 80"));
            Serial.println(F("Telnet: Port 23"));
        } else {
            Serial.println(F("Ethernet: Not connected"));
            Serial.println(F("Shield not detected or not working"));
        }
    }
    else if (strcmpPM(cmd, CMD_TELNET)) {
        if (!netOk) {
            Serial.println(F("Telnet: Not available"));
            Serial.println(F("Ethernet shield not detected"));
            return;
        }
        
        Serial.print(F("Telnet: "));
        if (telnetClient.connected()) {
            Serial.println(telnetState == 3 ? F("Client logged in") : F("Client authenticating"));
        } else {
            Serial.println(F("Listening"));
        }
    }
    #endif
    #ifdef ENABLE_LCD
    else if (strcmpPM(cmd, CMD_LCD)) {
        if (args) {
            // Se ci sono argomenti, scrivili sul display
            if (lcdOk) {
                lcdPrint(1, args);  // Scrive sulla seconda riga
                Serial.println(F("OK - Written to LCD"));
            } else {
                Serial.println(F("LCD not found"));
            }
        } else {
            // Senza argomenti, mostra lo stato
            Serial.print(F("LCD: "));
            Serial.println(lcdOk ? F("Connected") : F("Not found"));
            if (lcdOk) {
                Serial.println(F("Display: 16x2 I2C"));
            }
        }
    }
    #endif
    #ifdef ENABLE_SD_CARD
    else if (strcmpPM(cmd, CMD_SD)) {
        Serial.print(F("SD Card: "));
        Serial.println(sdOk ? F("Mounted") : F("Not found"));
        if (sdOk) {
            Serial.println(F("Logging to: system.log"));
            
            // Mostra file nella root
            SPIMgr::sel(PIN_SD_CS);
            File root = SD.open("/");
            Serial.println(F("Files:"));
            
            while (true) {
                File entry = root.openNextFile();
                if (!entry) break;
                
                Serial.print(F("  "));
                Serial.print(entry.name());
                if (!entry.isDirectory()) {
                    Serial.print(F(" ("));
                    Serial.print(entry.size());
                    Serial.print(F(" bytes)"));
                }
                Serial.println();
                entry.close();
                
                YIELD_IF_NEEDED();
            }
            
            root.close();
            SPIMgr::desel();
        }
    }
    else if (strcmpPM(cmd, CMD_RUN)) {
        if (!sdOk) {
            Serial.println(F("SD Card not found"));
            return;
        }
        
        if (args) {
            runScript(args);
        } else {
            Serial.println(F("Usage: run <filename>"));
        }
    }
    #endif
    #ifdef ENABLE_SUSPEND_RESUME
    else if (strcmpPM(cmd, CMD_SUSPEND)) {
        if (args) {
            int id = atoi(args);
            if (id >= 0 && id < os.n) {
                os.suspendTask(id);
                Serial.print(F("Task "));
                Serial.print(id);
                Serial.println(F(" suspended"));
            } else {
                Serial.println(F("Invalid task ID"));
            }
        } else {
            Serial.println(F("Usage: suspend <task_id>"));
        }
    }
    else if (strcmpPM(cmd, CMD_RESUME)) {
        if (args) {
            int id = atoi(args);
            if (id >= 0 && id < os.n) {
                os.resumeTask(id);
                Serial.print(F("Task "));

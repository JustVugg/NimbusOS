/**
 * NimbusOS - Sistema operativo real-time per Arduino con networking, LCD e SD
 * Versione ottimizzata per Arduino Uno
 */

#include <Arduino.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <EEPROM.h>
#include <SPI.h>
#include <SD.h>
#include <Ethernet.h>
#include <LiquidCrystal_I2C.h>

// ===== CONFIGURAZIONE =====
#define MAX_TASKS 6           // Numero massimo di task
#define BUFFER_SIZE 20        // Dimensione buffer ridotta

// Pin
#define LED_PIN 13
#define SD_CS_PIN 4
#define ETH_CS_PIN 10

// Stati dei task - più espliciti
#define TASK_READY 1       // Task pronto per l'esecuzione
#define TASK_RUNNING 2     // Task in esecuzione
#define TASK_WAITING 3     // Task in attesa (sleep)
#define TASK_SUSPENDED 4   // Task sospeso esplicitamente

// Priorità
#define PRIORITY_LOW 1
#define PRIORITY_NORMAL 2
#define PRIORITY_HIGH 3

// Buffer e stato globale
static char globalBuffer[BUFFER_SIZE];
static bool lcdAvailable = false;
static bool sdAvailable = false;
static bool netAvailable = false;
static uint8_t telnetState = 0;
static uint8_t telnetIndex = 0;

// Dispositivi
static LiquidCrystal_I2C lcdDisplay(0x27, 16, 2);
static EthernetServer webServer(80);
static EthernetServer telnetServer(23);
static EthernetClient telnetClient;

// Struttura task compatta
struct Task {
    void (*function)(void);    // Funzione del task
    uint16_t period;           // Periodo
    uint16_t lastRun;          // Ultimo momento di esecuzione
    uint8_t state;             // Stato corrente
    uint8_t priority;          // Priorità
    uint32_t wakeTime;         // Quando risvegliare il task
    const char* name;          // Nome del task
};

// Sistema operativo
class NimbusOS {
public:
    Task tasks[MAX_TASKS];
    uint8_t taskCount;
    uint8_t currentTask;
    volatile uint32_t ticks;
    uint16_t contextSwitches;
    uint8_t suspendFlags;
    bool lowPowerMode;
    
    NimbusOS() : taskCount(0), currentTask(0), ticks(0), 
                 contextSwitches(0), suspendFlags(0), lowPowerMode(false) {
        memset(tasks, 0, sizeof(tasks));
    }
    
    int addTask(void (*function)(void), uint16_t period, uint8_t priority, const char* name) {
        if (taskCount >= MAX_TASKS) return -1;
        
        tasks[taskCount].function = function;
        tasks[taskCount].period = period;
        tasks[taskCount].priority = priority;
        tasks[taskCount].state = TASK_READY;
        tasks[taskCount].lastRun = 0;
        tasks[taskCount].wakeTime = 0;
        tasks[taskCount].name = name;
        
        return taskCount++;
    }
    
    void sleep(uint16_t ms) {
        tasks[currentTask].state = TASK_WAITING;
        tasks[currentTask].wakeTime = millis() + ms;
    }
    
    bool shouldYield() {
        return (millis() - tasks[currentTask].lastRun) > 2000;
    }
    
    int suspendTask(uint8_t id) {
        if (id < taskCount) {
            suspendFlags |= (1 << id);
            if (tasks[id].state != TASK_WAITING) {
                tasks[id].state = TASK_SUSPENDED;
            }
            return 0;
        }
        return -1;
    }
    
    int resumeTask(uint8_t id) {
        if (id < taskCount) {
            suspendFlags &= ~(1 << id);
            if (tasks[id].state == TASK_SUSPENDED) {
                tasks[id].state = TASK_READY;
            }
            return 0;
        }
        return -1;
    }
    
    void setLowPowerMode(bool enabled) {
        lowPowerMode = enabled;
        
        if (enabled) {
            if (lcdAvailable) lcdDisplay.noBacklight();
        } else {
            if (lcdAvailable) lcdDisplay.backlight();
        }
    }
    
    void run() {
        cli();
        TCCR1A = 0;
        TCCR1B = (1 << WGM12) | (1 << CS11);
        OCR1A = 19999;
        TIMSK1 = (1 << OCIE1A);
        sei();
        
        wdt_enable(WDTO_2S);
        
        while (1) {
            uint32_t now = millis();
            int8_t bestTask = -1;
            uint8_t highestPriority = 0;
            
            wdt_reset();
            
            for (uint8_t i = 0; i < taskCount; i++) {
                if (suspendFlags & (1 << i)) continue;
                
                if (tasks[i].state == TASK_WAITING && now >= tasks[i].wakeTime) {
                    tasks[i].state = TASK_READY;
                    tasks[i].wakeTime = 0;
                }
                
                if (tasks[i].state == TASK_READY && (now - tasks[i].lastRun) >= tasks[i].period) {
                    if (tasks[i].priority > highestPriority) {
                        highestPriority = tasks[i].priority;
                        bestTask = i;
                    }
                }
            }
            
            if (bestTask >= 0) {
                currentTask = bestTask;
                tasks[currentTask].state = TASK_RUNNING;
                tasks[currentTask].lastRun = now;
                tasks[currentTask].function();
                if (tasks[currentTask].state == TASK_RUNNING) {
                    tasks[currentTask].state = TASK_READY;
                }
                contextSwitches++;
            } else {
                delay(1);
            }
        }
    }
    
    uint16_t getFreeMemory() {
        extern int __heap_start, *__brkval;
        int v;
        return (int)&v - (__brkval == 0 ? (int)&__heap_start : (int)__brkval);
    }
    
    uint32_t getUptime() {
        return ticks / 100;
    }
};

NimbusOS nimbus;

ISR(TIMER1_COMPA_vect) {
    nimbus.ticks++;
}

#define YIELD_CHECK() if (nimbus.shouldYield()) return

// SPI Manager
class SPIMgr {
    static uint8_t currentDevice;
public:
    static void select(uint8_t devicePin) {
        if (currentDevice != devicePin) {
            digitalWrite(currentDevice, HIGH);
            digitalWrite(devicePin, LOW);
            currentDevice = devicePin;
        }
    }
    
    static void deselect() {
        digitalWrite(SD_CS_PIN, HIGH);
        digitalWrite(ETH_CS_PIN, HIGH);
    }
};
uint8_t SPIMgr::currentDevice = 0;

// Forward declarations
void processCommand(char* cmd);

// Driver LCD
void initLCD() {
    lcdDisplay.init();
    lcdDisplay.backlight();
    lcdAvailable = true;
}

void lcdPrint(uint8_t row, const char* text) {
    if (!lcdAvailable) return;
    lcdDisplay.setCursor(0, row);
    lcdDisplay.print(F("                "));
    lcdDisplay.setCursor(0, row);
    lcdDisplay.print(text);
}

// Ethernet network
bool initNetwork() {
    pinMode(ETH_CS_PIN, OUTPUT);
    SPIMgr::select(ETH_CS_PIN);
    
    uint8_t mac[] = {0xDE, 0xAD, 0xBE, 0xEF, 0xFE, 0xED};
    Ethernet.begin(mac, IPAddress(192,168,1,100));
    
    if (Ethernet.hardwareStatus() == EthernetNoHardware) {
        SPIMgr::deselect();
        return false;
    }
    
    webServer.begin();
    telnetServer.begin();
    
    SPIMgr::deselect();
    return true;
}

bool handleWeb() {
    if (!netAvailable) return false;
    
    SPIMgr::select(ETH_CS_PIN);
    EthernetClient client = webServer.available();
    
    if (client) {
        char buf[16];
        uint8_t i = 0;
        
        while (client.available() && i < 15) {
            char c = client.read();
            buf[i++] = c;
            if (i > 3 && buf[i-4] == '\r' && buf[i-2] == '\r') break;
            YIELD_CHECK();
        }
        buf[i] = 0;
        
        client.println(F("HTTP/1.1 200 OK"));
        client.println(F("Content-Type: text/plain"));
        client.println();
        
        if (strstr(buf, "GET /status")) {
            client.print(F("{\"up\":"));
            client.print(nimbus.getUptime());
            client.print(F(",\"mem\":"));
            client.print(nimbus.getFreeMemory());
            client.println(F("}"));
        }
        else if (strstr(buf, "GET /led/on")) {
            digitalWrite(LED_PIN, HIGH);
            client.println(F("LED ON"));
        }
        else if (strstr(buf, "GET /led/off")) {
            digitalWrite(LED_PIN, LOW);
            client.println(F("LED OFF"));
        }
        else {
            client.println(F("NimbusOS Web API"));
            client.println(F("/status, /led/on, /led/off"));
        }
        
        client.stop();
    }
    
    SPIMgr::deselect();
    return true;
}

// Funzione semplificata per telnet
bool handleTelnet() {
    if (!netAvailable) return false;
    
    SPIMgr::select(ETH_CS_PIN);
    
    if (!telnetClient.connected()) {
        telnetClient = telnetServer.available();
        if (telnetClient) {
            telnetState = 1;
            telnetIndex = 0;
            telnetClient.println(F("\r\n== NimbusOS =="));
            telnetClient.print(F("Login: "));
        }
    }
    
    if (telnetClient.connected()) {
        while (telnetClient.available()) {
            char c = telnetClient.read();
            
            if (c == '\r' || c == '\n') {
                globalBuffer[telnetIndex] = 0;
                
                if (telnetState == 1) {
                    if (strcmp(globalBuffer, "admin") == 0) {
                        telnetState = 2;
                        telnetClient.print(F("\r\nPass: "));
                    } else {
                        telnetClient.println(F("\r\nBad login"));
                        telnetClient.stop();
                    }
                }
                else if (telnetState == 2) {
                    if (strcmp(globalBuffer, "nimbus") == 0) {
                        telnetState = 3;
                        telnetClient.println(F("\r\nWelcome!"));
                        telnetClient.print(F("\r\n> "));
                    } else {
                        telnetClient.println(F("\r\nBad pass"));
                        telnetClient.stop();
                    }
                }
                else if (telnetState == 3) {
                    if (telnetIndex > 0) {
                        // Process command
                        if (strcmp(globalBuffer, "help") == 0) {
                            telnetClient.println(F("Commands: help tasks mem led exit"));
                        }
                        else if (strcmp(globalBuffer, "tasks") == 0) {
                            const char* states[] = {"", "READY", "RUN", "WAIT", "SUSP"};
                            
                            for (int i = 0; i < nimbus.taskCount; i++) {
                                telnetClient.print(i);
                                telnetClient.print(F(": "));
                                telnetClient.print(nimbus.tasks[i].name);
                                telnetClient.print(F(" - "));
                                telnetClient.println(states[nimbus.tasks[i].state]);
                                YIELD_CHECK();
                            }
                        }
                        else if (strcmp(globalBuffer, "mem") == 0) {
                            telnetClient.print(F("Mem:"));
                            telnetClient.println(nimbus.getFreeMemory());
                        }
                        else if (strncmp(globalBuffer, "led", 3) == 0) {
                            if (telnetIndex > 4) {
                                digitalWrite(LED_PIN, globalBuffer[4] - '0');
                                telnetClient.println(F("OK"));
                            }
                        }
                        else if (strcmp(globalBuffer, "exit") == 0) {
                            telnetClient.println(F("Bye"));
                            telnetClient.stop();
                            telnetState = 0;
                            SPIMgr::deselect();
                            return true;
                        }
                        
                        telnetClient.print(F("\r\n> "));
                    }
                }
                
                telnetIndex = 0;
            }
            else if (telnetIndex < BUFFER_SIZE-1 && c >= 32) {
                globalBuffer[telnetIndex++] = c;
                if (telnetState != 2) telnetClient.write(c);
            }
            
            YIELD_CHECK();
        }
    }
    
    SPIMgr::deselect();
    return telnetState == 3;
}

// SD card
bool initSD() {
    pinMode(SD_CS_PIN, OUTPUT);
    SPIMgr::select(SD_CS_PIN);
    sdAvailable = SD.begin(SD_CS_PIN);
    SPIMgr::deselect();
    return sdAvailable;
}

void logToSD(const char* msg) {
    if (!sdAvailable) return;
    
    SPIMgr::select(SD_CS_PIN);
    File f = SD.open("system.log", FILE_WRITE);
    if (f) {
        f.print(nimbus.getUptime());
        f.print(F(": "));
        f.println(msg);
        f.close();
    }
    SPIMgr::deselect();
}

bool runScript(const char* filename) {
    if (!sdAvailable) return false;
    
    SPIMgr::select(SD_CS_PIN);
    File f = SD.open(filename, FILE_READ);
    if (!f) {
        SPIMgr::deselect();
        return false;
    }
    
    if (lcdAvailable) {
        char buf[16];
        snprintf(buf, sizeof(buf), "Script: %.8s", filename);
        lcdPrint(0, buf);
    }
    
    while (f.available()) {
        int i = 0;
        while (f.available() && i < BUFFER_SIZE-1) {
            char c = f.read();
            if (c == '\n' || c == '\r') break;
            globalBuffer[i++] = c;
            YIELD_CHECK();
        }
        globalBuffer[i] = 0;
        
        if (i > 0 && globalBuffer[0] != '#') {
            Serial.print(F("> "));
            Serial.println(globalBuffer);
            processCommand(globalBuffer);
        }
        
        while (f.available() && (f.peek() == '\n' || f.peek() == '\r')) {
            f.read();
        }
        
        YIELD_CHECK();
    }
    
    f.close();
    SPIMgr::deselect();
    
    if (lcdAvailable) {
        lcdPrint(0, "NimbusOS");
    }
    
    return true;
}

// Processore comandi
void processCommand(char* cmd) {
    char* args = strchr(cmd, ' ');
    if (args) {
        *args = '\0';
        args++;
    }
    
    if (strcmp(cmd, "help") == 0) {
        Serial.println(F("Commands:"));
        Serial.println(F(" help - Help"));
        Serial.println(F(" tasks - Task list"));
        Serial.println(F(" mem - Memory info"));
        Serial.println(F(" led <0|1> - LED"));
        Serial.println(F(" lcd <txt> - LCD text"));
        Serial.println(F(" net - Network status"));
        Serial.println(F(" sd - SD card status"));
        Serial.println(F(" run <file> - Run script"));
        Serial.println(F(" suspend/resume <id>"));
    }
    else if (strcmp(cmd, "tasks") == 0) {
        const char* stateNames[] = {"", "READY", "RUN", "WAIT", "SUSP"};
        const char* prioNames[] = {"", "LOW", "NORMAL", "HIGH"};
        
        Serial.println(F("ID NAME       STATE  PRIORITY  PERIOD"));
        Serial.println(F("-------------------------------------"));
        
        for (int i = 0; i < nimbus.taskCount; i++) {
            Serial.print(i);
            Serial.print(F(": "));
            
            // Stampa il nome del task
            Serial.print(nimbus.tasks[i].name);
            // Padding per allineare le colonne
            int spaces = 10 - strlen(nimbus.tasks[i].name);
            for (int j = 0; j < spaces; j++) Serial.print(' ');
            
            Serial.print(F(" "));
            Serial.print(stateNames[nimbus.tasks[i].state]);
            Serial.print(F("  "));
            Serial.print(prioNames[nimbus.tasks[i].priority]);
            Serial.print(F("   "));
            Serial.println(nimbus.tasks[i].period);
            YIELD_CHECK();
        }
    }
    else if (strcmp(cmd, "mem") == 0) {
        Serial.print(F("Free: "));
        Serial.println(nimbus.getFreeMemory());
        Serial.print(F("Uptime: "));
        Serial.println(nimbus.getUptime());
    }
    else if (strcmp(cmd, "led") == 0) {
        if (args) {
            digitalWrite(LED_PIN, atoi(args));
            Serial.println(F("OK"));
        } else {
            Serial.println(F("Usage: led <0|1>"));
        }
    }
    else if (strcmp(cmd, "lcd") == 0) {
        if (args) {
            if (lcdAvailable) {
                lcdPrint(1, args);
                Serial.println(F("OK"));
            } else {
                Serial.println(F("LCD not found"));
            }
        } else {
            Serial.print(F("LCD: "));
            Serial.println(lcdAvailable ? F("OK") : F("Not found"));
        }
    }
    else if (strcmp(cmd, "net") == 0) {
        Serial.print(F("Net: "));
        Serial.println(netAvailable ? F("OK") : F("Not available"));
        if (netAvailable) {
            Serial.println(F("IP: 192.168.1.100"));
        }
    }
    else if (strcmp(cmd, "sd") == 0) {
        Serial.print(F("SD: "));
        Serial.println(sdAvailable ? F("OK") : F("Not found"));
        if (sdAvailable) {
            SPIMgr::select(SD_CS_PIN);
            File root = SD.open("/");
            Serial.println(F("Files:"));
            
            while (true) {
                File e = root.openNextFile();
                if (!e) break;
                Serial.println(e.name());
                e.close();
                YIELD_CHECK();
            }
            
            root.close();
            SPIMgr::deselect();
        }
    }
    else if (strcmp(cmd, "run") == 0) {
        if (args) {
            runScript(args);
        } else {
            Serial.println(F("Usage: run <file>"));
        }
    }
    else if (strcmp(cmd, "suspend") == 0) {
        if (args) {
            int id = atoi(args);
            nimbus.suspendTask(id);
            Serial.println(F("OK"));
        }
    }
    else if (strcmp(cmd, "resume") == 0) {
        if (args) {
            int id = atoi(args);
            nimbus.resumeTask(id);
            Serial.println(F("OK"));
        }
    }
    else if (strcmp(cmd, "power") == 0) {
        if (args) {
            nimbus.setLowPowerMode(atoi(args) != 0);
            Serial.println(F("OK"));
        }
    }
    else if (strcmp(cmd, "inf-loop") == 0) {
        Serial.println(F("Testing inf loop"));
        uint32_t cnt = 0;
        
        while (1) {
            cnt++;
            if (cnt % 10000 == 0) {
                Serial.print(F("."));
            }
            YIELD_CHECK();
        }
    }
}

// Shell task
void shellTask() {
    static uint8_t idx = 0;
    static bool init = false;
    
    if (!init) {
        Serial.begin(115200);
        Serial.println(F("\nNimbusOS v1.0"));
        Serial.print(F("> "));
        init = true;
    }
    
    while (Serial.available()) {
        char c = Serial.read();
        
        if (c == '\n' || c == '\r') {
            globalBuffer[idx] = '\0';
            Serial.println();
            processCommand(globalBuffer);
            idx = 0;
            Serial.print(F("> "));
        }
        else if (c == 127 || c == 8) {
            if (idx > 0) {
                idx--;
                Serial.print(F("\b \b"));
            }
        }
        else if (idx < BUFFER_SIZE-1 && c >= 32) {
            globalBuffer[idx++] = c;
            Serial.write(c);
        }
        
        YIELD_CHECK();
    }
}

// LED task
void ledTask() {
    static bool state = false;
    state = !state;
    digitalWrite(LED_PIN, state);
}

// Monitor task
void monitorTask() {
    static uint16_t count = 0;
    
    if (!nimbus.lowPowerMode) {
        Serial.print(F("\n["));
        Serial.print(count++);
        Serial.print(F("] Up:"));
        Serial.print(nimbus.getUptime());
        Serial.print(F("s Mem:"));
        Serial.println(nimbus.getFreeMemory());
    }
}

// Network task
void networkTask() {
    static bool init = false;
    
    if (!init) {
        netAvailable = initNetwork();
        init = true;
        Serial.println(netAvailable ? F("NET OK") : F("NET FAIL"));
        nimbus.sleep(1000);
        return;
    }
    
    if (netAvailable) {
        handleWeb();
        handleTelnet();
    }
    
    nimbus.sleep(100);
}

// LCD task
void lcdTask() {
    static bool init = false;
    static uint8_t screen = 0;
    static uint16_t last = 0;
    
    if (!init) {
        initLCD();
        lcdPrint(0, "NimbusOS");
        lcdPrint(1, "Starting...");
        init = true;
        nimbus.sleep(1000);
        return;
    }
    
    if (nimbus.ticks - last > 200) {
        char buf[16];
        
        switch (screen) {
            case 0:
                sprintf(buf, "Up:%lus M:%d", nimbus.getUptime(), nimbus.getFreeMemory());
                lcdPrint(0, buf);
                sprintf(buf, "Tasks:%d", nimbus.taskCount);
                lcdPrint(1, buf);
                break;
                
            case 1:
                if (netAvailable) {
                    lcdPrint(0, "IP:192.168.1.100");
                    lcdPrint(1, "HTTP/Telnet OK");
                } else {
                    lcdPrint(0, "Network:");
                    lcdPrint(1, "Not available");
                }
                break;
                
            case 2:
                lcdPrint(0, "SD Card:");
                lcdPrint(1, sdAvailable ? "Available" : "Not found");
                break;
        }
        
        screen = (screen + 1) % 3;
        last = nimbus.ticks;
    }
    
    nimbus.sleep(100);
}

// SD task
void sdTask() {
    static bool init = false;
    
    if (!init) {
        sdAvailable = initSD();
        init = true;
        Serial.println(sdAvailable ? F("SD OK") : F("SD FAIL"));
        
        if (sdAvailable) {
            logToSD("System started");
        }
        
        nimbus.sleep(3000);
        return;
    }
    
    static uint16_t lastLog = 0;
    if (nimbus.ticks - lastLog > 3000) {
        if (sdAvailable) {
            char buf[16];
            sprintf(buf, "Up:%lu M:%d", nimbus.getUptime(), nimbus.getFreeMemory());
            logToSD(buf);
        }
        lastLog = nimbus.ticks;
    }
    
    nimbus.sleep(5000);
}

// Watchdog task
void watchdogTask() {
    wdt_reset();
    nimbus.sleep(1000);
}

// Main
void setup() {
    pinMode(LED_PIN, OUTPUT);
    SPI.begin();
    SPIMgr::deselect();
    
    nimbus.addTask(shellTask, 50, PRIORITY_HIGH, "Shell");
    nimbus.addTask(ledTask, 500, PRIORITY_LOW, "LED");
    //nimbus.addTask(monitorTask, 10000, PRIORITY_LOW, "Monitor");
    //nimbus.addTask(networkTask, 100, PRIORITY_NORMAL, "Network");
    nimbus.addTask(lcdTask, 100, PRIORITY_LOW, "LCD");
    //nimbus.addTask(sdTask, 5000, PRIORITY_LOW, "SD");
    nimbus.addTask(watchdogTask, 1000, PRIORITY_HIGH, "Watchdog");
    
    nimbus.run();
}

void loop() {
    // Mai raggiunto
}

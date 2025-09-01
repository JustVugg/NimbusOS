/*
 * Custom Setup Example for NimbusOS
 * 
 * This shows how to modify the setup() function in NimbusOS.ino
 * to add your own tasks and initialization
 */

/*
INSTRUCTIONS:
1. Open NimbusOS.ino
2. Find the setup() function (at the bottom of the file)
3. Replace it with this custom version:
*/

/*
void setup() {
    Serial.begin(115200);
    Serial.println(F("NimbusOS Custom Configuration"));
    Serial.println(F("=========================="));
    
    // Initialize hardware components
    pinMode(LED_BUILTIN, OUTPUT);
    pinMode(7, INPUT_PULLUP);  // Button on pin 7
    
    #ifdef ENABLE_LCD
    lcdInit();
    if (lcdOk) {
        lcdPrint(0, "NimbusOS v1.0");
        lcdPrint(1, "Starting...");
    }
    #endif
    
    #ifdef ENABLE_SD_CARD
    if (sdInit()) {
        Serial.println(F("SD Card initialized"));
        sdLog("System startup");
    }
    #endif
    
    #ifdef ENABLE_NETWORKING
    if (netInit()) {
        Serial.println(F("Network initialized"));
        Serial.print(F("IP: "));
        Serial.println(Ethernet.localIP());
    }
    #endif
    
    // Add custom task for button handling
    os.add(buttonTask, 50, PRIORITY_HIGH);
    
    // Add custom task for data logging
    os.add(dataLogTask, 5000, PRIORITY_LOW);
    
    // Add the default shell task
    os.add(shellTask, 10, PRIORITY_HIGH);
    
    // Add network tasks if enabled
    #ifdef ENABLE_NETWORKING
    if (netOk) {
        os.add(netTask, 100, PRIORITY_NORMAL);
    }
    #endif
    
    Serial.println(F("Starting scheduler..."));
    
    // Start the OS - this never returns!
    os.run();
}

// Custom task: Button handler
void buttonTask() {
    static bool lastState = HIGH;
    bool currentState = digitalRead(7);
    
    if (currentState != lastState && currentState == LOW) {
        Serial.println(F("Button pressed!"));
        digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
        
        #ifdef ENABLE_LCD
        if (lcdOk) {
            lcdPrint(1, "Button pressed");
        }
        #endif
    }
    
    lastState = currentState;
}

// Custom task: Data logger
void dataLogTask() {
    #ifdef ENABLE_SD_CARD
    if (sdOk) {
        char logEntry[32];
        sprintf(logEntry, "A0=%d, Mem=%d", analogRead(A0), os.getFreeMemory());
        sdLog(logEntry);
        Serial.print(F("Logged: "));
        Serial.println(logEntry);
    }
    #endif
}
*/

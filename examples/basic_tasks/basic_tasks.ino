/*
 * Basic Tasks Example for NimbusOS
 * 
 * NOTE: This is a standalone example showing the concept.
 * To use NimbusOS, upload the main NimbusOS.ino to your board,
 * then modify the setup() function to add your custom tasks.
 */

// Example task functions to add to NimbusOS.ino

/*
// Add these task functions to your NimbusOS.ino file:

void blinkTask() {
    static bool ledState = false;
    digitalWrite(LED_BUILTIN, ledState);
    ledState = !ledState;
    os.sleep(500); // Sleep for 500ms
}

void sensorTask() {
    int value = analogRead(A0);
    Serial.print("Sensor A0: ");
    Serial.println(value);
    os.sleep(1000); // Read every second
}

void monitorTask() {
    Serial.print("Free RAM: ");
    Serial.print(os.getFreeMemory());
    Serial.println(" bytes");
    
    Serial.print("Uptime: ");
    Serial.print(os.getUptime());
    Serial.println(" seconds");
    
    os.sleep(5000); // Every 5 seconds
}

// Then modify the setup() function in NimbusOS.ino:

void setup() {
    Serial.begin(115200);
    Serial.println(F("NimbusOS Basic Tasks Example"));
    
    // Add your custom tasks
    os.add(blinkTask, 0, PRIORITY_NORMAL);
    os.add(sensorTask, 0, PRIORITY_HIGH);
    os.add(monitorTask, 0, PRIORITY_LOW);
    
    // Add the shell task for command interface
    os.add(shellTask, 10, PRIORITY_HIGH);
    
    // Initialize hardware if needed
    pinMode(LED_BUILTIN, OUTPUT);
    
    // Start the OS (never returns)
    os.run();
}
*/

// This file serves as documentation/example
// Copy the task functions above into your NimbusOS.ino file

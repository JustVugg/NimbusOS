# NimbusOS 🌩️

A lightweight, feature-rich Real-Time Operating System (RTOS) for Arduino with networking, storage, and display capabilities.

![Version](https://img.shields.io/badge/version-1.0.0-blue.svg)
![Platform](https://img.shields.io/badge/platform-Arduino-green.svg)
![License](https://img.shields.io/badge/license-MIT-orange.svg)

## 🚀 Features

### Core OS Features
- **Cooperative Multitasking**: Support for up to 6 concurrent tasks
- **Priority-based Scheduling**: 4 priority levels (IDLE, LOW, NORMAL, HIGH)
- **Task Management**: Suspend/resume capabilities
- **Pseudo-preemptive Protection**: Tasks automatically yield after max runtime
- **Inter-task Messaging**: Built-in message passing system
- **Power Management**: Low-power mode support

### Hardware Support
- **Networking**: Ethernet shield support with web server and Telnet
- **Storage**: SD card support with logging and script execution
- **Display**: I2C LCD (16x2) support
- **Watchdog Timer**: System stability protection

### Built-in Shell Commands
- `help` - Display available commands
- `tasks` - List all tasks with status
- `mem` - Show free memory and system stats
- `led <0|1>` - Control built-in LED
- `adc <pin>` - Read analog pin value
- `net` - Network status
- `telnet` - Telnet server status
- `lcd [text]` - LCD control/display text
- `sd` - SD card status and file listing
- `run <file>` - Execute script from SD card
- `suspend <id>` - Suspend a task
- `resume <id>` - Resume a task
- `power <0|1>` - Toggle power saving mode

## 📋 Requirements

### Hardware
- Arduino Uno/Mega or compatible board
- (Optional) Ethernet Shield (W5100/W5500)
- (Optional) SD Card module
- (Optional) I2C LCD Display (16x2)

### Software
- Arduino IDE 1.8.x or newer
- Required Libraries:
  - `Ethernet` (for networking features)
  - `SD` (for SD card support)
  - `LiquidCrystal_I2C` (for LCD support)

## 🔧 Installation

1. Clone this repository:
```bash
git clone https://github.com/JustVugg/NimbusOS.git
```

2. Install required libraries through Arduino Library Manager:
   - Ethernet
   - SD
   - LiquidCrystal I2C

3. Open `NimbusOS.ino` in Arduino IDE

4. Configure features in the code (optional):
```cpp
// Enable/disable features as needed
#define ENABLE_WATCHDOG
#define ENABLE_SUSPEND_RESUME
#define ENABLE_MESSAGING
#define ENABLE_NETWORKING
#define ENABLE_SD_CARD
#define ENABLE_LCD
#define ENABLE_TELNET
#define ENABLE_SCRIPT_ENGINE
```

5. Upload to your Arduino board

## 💻 Usage Example

```cpp
// Task function example
void blinkTask() {
    static bool ledState = false;
    digitalWrite(LED_BUILTIN, ledState);
    ledState = !ledState;
    os.sleep(500); // Sleep for 500ms
}

void sensorTask() {
    int value = analogRead(A0);
    Serial.print("Sensor: ");
    Serial.println(value);
    
    // Send message to another task
    uint8_t data = value >> 2; // Scale to 8-bit
    os.sendMessage(2, 1, &data, 1);
}

void setup() {
    Serial.begin(115200);
    
    // Add tasks (function, period_ms, priority)
    os.add(blinkTask, 0, PRIORITY_NORMAL);
    os.add(sensorTask, 1000, PRIORITY_HIGH);
    
    // Run the OS
    os.run(); // Never returns
}
```

## 🌐 Network Features

### Web Server (Port 80)
Access the built-in web interface:
- `http://192.168.1.100/` - Main page
- `http://192.168.1.100/status` - JSON system status
- `http://192.168.1.100/led/on` - Turn LED on
- `http://192.168.1.100/led/off` - Turn LED off
- `http://192.168.1.100/tasks` - Task list

### Telnet Server (Port 23)
Connect via telnet for remote shell access:
```bash
telnet 192.168.1.100
# Login: admin
# Password: nimbus
```

## 📝 Script Engine

Create automation scripts on SD card:
```bash
# example.txt
led 1
adc 0
tasks
led 0
```

Run with: `run example.txt`

## 🏗️ Architecture

NimbusOS uses a cooperative multitasking approach with priority-based scheduling:

1. **Task Scheduler**: Round-robin with priority selection
2. **Timer-based Ticks**: 100Hz system tick via Timer1
3. **Memory Protection**: Stack overflow detection via watchdog
4. **Resource Sharing**: SPI manager for SD/Ethernet multiplexing

## 🤝 Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

1. Fork the project
2. Create your feature branch (`git checkout -b feature/AmazingFeature`)
3. Commit your changes (`git commit -m 'Add some AmazingFeature'`)
4. Push to the branch (`git push origin feature/AmazingFeature`)
5. Open a Pull Request

## 📄 License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## 🙏 Acknowledgments

- Inspired by FreeRTOS and other embedded RTOS systems
- Built for the Arduino community
- Special thanks to all contributors

## 📞 Contact

justvugg - [@justvugg]([https://twitter.com/yourtwitter](https://x.com/justvugg))

Project Link: [https://github.com/JustVugg/NimbusOS](https://github.com/JustVugg/NimbusOS)

---
⭐ Star this repository if you find it useful!

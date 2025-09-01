# NimbusOS Examples

## How to Use These Examples

Since NimbusOS is a complete OS in a single file (`NimbusOS.ino`), these examples show how to:

1. **Add custom tasks** to the OS
2. **Modify the setup()** function for your needs
3. **Create scripts** for the SD card

## Adding Custom Tasks

1. Open `NimbusOS.ino`
2. Add your task functions before the `setup()` function
3. Register tasks in `setup()` using `os.add()`
4. Upload to your Arduino

## Example Structure

```cpp
// Your custom task
void myTask() {
    // Do something
    Serial.println("Hello from my task!");
    
    // Sleep for 1 second
    os.sleep(1000);
}

void setup() {
    Serial.begin(115200);
    
    // Add your task
    os.add(myTask, 0, PRIORITY_NORMAL);
    
    // Add the shell (for commands)
    os.add(shellTask, 10, PRIORITY_HIGH);

    
    // Start OS
    os.run();
}
```
### Available Examples
basic_tasks: Simple multitasking with LED and sensors
custom_setup: Advanced setup with all features
scripts: Example scripts for SD card execution

### Tips
Keep task functions short and use os.sleep()
Higher priority tasks run first
Use YIELD_IF_NEEDED() in long loops
Check feature flags before using hardware

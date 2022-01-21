# MQTTTouchScreen
A project using a Lolin TFT-2.4 touch screen module with a Wemos D1-mini ESP8266 development board plugged into the display.

The project subscribes to MQTT topics and displays status of some Tasmota devices.  It can control those devices when buttons are touched, also over MQTT to Tasmota devices.
The temperatures are displayed (along with battery and signal strength) after converting from C to F (easily changed in code).  These also come over MQTT.  For my thermometers setup, I'm using an ESP32 with Tasmota compiled with BLE option and Blerry installed.  This connects to several thermometers over Bluetooth.  The code differentiates between the thermometers by the name given to them in Blerry.

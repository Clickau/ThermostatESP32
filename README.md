# ThermostatESP32

This is the source code for a smart thermostat built on top of the ESP32. There is also an open source companion [Android app](https://github.com/Clickau/ThermostatAndroid) available. A schematic and PCB design is coming soon.
This project aims to provide a thermostat that can be controlled remotely. It sends a signal to the heater via a relay and gets the schedules from a Firebase database, where they are uploaded from the Android app. The user can also add a temporary schedule from the thermostat's UI.

## Building
<ul>
<li>Install ESP-IDF v4.0.1 or use VS Code with the ESP-IDF extension</li>
<li>Clone the project</li>
<li>Optionally configure the project (see below)</li>
<li>Upload to board</li>
</ul>

## Optional configuration
Basic configuration can be done by editing the file main/include/settings.h.
<ul>
<li>Pins<br>
You can change which pins the peripherals are connected to by changing the pinXXX constants.
</li>

<li>
Temperature and humidity sensor<br>
If you want to use a DHT11 instead of a DHT22, you only have to change dhtType to DHTesp::DHT11. To use a different sensor, you need to change sensorLoopTask to read the data from your sensor, and temporaryScheduleTempResolution and tempThreshold to be greater than or equal to the resolution of your temperature sensor.
</li>

<li>
Setup Wifi AP<br>
The SSID and password used to connect to the thermostat in Setup Wifi mode can be changed by modifying setupWifiAPSSID and setupWifiAPPassword. You can also change the IP address of the server with setupWifiServerIP.
</li>

<li>
Update URL<br>
The thermostat will automatically check for updates by requesting a file on my Github Pages website. If you want to manage updates on your own, you can change latestReleaseURL.
</li>
</ul>

## Usage
On each boot, a startup menu is displayed on the screen which allows you to choose between Normal Operation and Setup.

### Setup
On first boot, or every time something in your configuration changes, you have to configure the Thermostat from the Setup mode. When entering it, the Thermostat will create a WiFi network and display the SSID and password. You need to connect to that network and configure the Thermostat with your WiFi credentials, Firebase URL and secret, and timezone. The functionality will soon be added to the Android app and a python utility is also coming soon.
<ul>
<li>
Firebase<br>
You have to create your own Google Firebase database to use for this project. This can be done by going to https://console.firebase.google.com/ and creating a new project. You then have to create a new realtime database and copy its url (something like example-xxxxx.firebaseio.com) and secret key, which you can get by going to the settings wheel next to Project Overview, then Users and Permissions -> Service accounts -> Database secrets.
</li>

<li>
Timezone<br>
You have to set it to the Unix TZ string for your country. You can find more information on how to format it here: https://www.gnu.org/software/libc/manual/html_node/TZ-Variable.html.
</li>
</ul>

### Normal Operation
After entering Normal Operation, the thermostat will attempt to connect to your Wifi network. If it isn't able to, it will prompt you to do Setup Wifi again. After connecting, it will try to get the current time from NTP servers. If it isn't able to, it will prompt you to enter the current time manually. Then, it will try to connect to Firebase. After initializing, the thermostat will enter the main loop, in which it updates the display with the current temperature, humidity, time and errors, polls the database for changes to the schedules, reads the temperature and humidity from the sensor, evaluates the schedules, tries to fix errors and checks for updates. By pressing Enter, you can set a temporary schedule, with a duration between 15 minutes and 24 hours, or an infinite duration.

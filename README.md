# ESP8266-Plant-Monitor
This is a power optimised arduino based sketch for the ESP8266. It primarily serves to look after your houseplants. It will measure things like:
- Temperature
- Light
- Humidity
- Soil Moisture

Currently, the sketch is configured to use the BH1750 light sensor, capacitive soil moisture sensor which connects to the ADC and an Si7021 temp/humidity sensor due to it's good quality. Feel free to change the code to use different sensors, the configuration for the existing sensors are clearly commented, all you need to do is to import the library for your sensor and add the correct configuration. 

The sketch will then upload this sensor data over WiFi to an MQTT broker on your network. You can change the IP of this as well, although I highly recommend that you place the MQTT broker on a static connection, and check your router settings to ensure the IP you have set is not within the DHCP range of your router. Additionally, if you are using multiple ESP8266s I would highly advise using a separate IP address for each one, you can change this in the code as well as well as changing the serial number and MQTT ok addresses. 

If you are looking for the arduino IDE code, it's in the src folder, or if you want precompiled binaries which you can flash directly using esptool they are in the releases folder. 

This is still under development, so if there are any issues please do let me know and I will try to solve it. 

Have fun!

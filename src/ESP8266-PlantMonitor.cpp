
#include <Arduino.h>
#include <Wire.h>
#include <BH1750.h>
#include "Adafruit_Si7021.h"
#include <ESP8266WiFi.h>
#include <PubSubClient.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266httpUpdate.h>



/*------------------S   E   R   I   A   L       N   U   M   B   E   R-----------------------*/

const char* serialNo = "ESP01";
const int FW_VERSION = 0001;

/*------------------------------------------------------------------------------------------*/

void readvalues();
void reconnect();
void sendMQTTmessage();
uint32_t calculateCRC32( const uint8_t *data, size_t length );
void WriteRTCValues();



// R T C   M E M O R Y
struct {
  uint32_t crc32;   // 4 bytes
  uint8_t channel;  // 1 byte,   5 in total
  uint8_t ap_mac[6]; // 6 bytes, 11 in total
  uint8_t padding;  // 1 byte,  12 in total
} rtcData;
bool rtcValid = false;
//Can store 30 measurements in the RTC memory, handy in case the Pi server is down for some reason.
//Normally it should only ever accumulate 8 measurements before it is overwritten...



// B H 1 7 5 0   O P T I O N S
BH1750 lightMeter;
float lux = 0;
const int lightMeterPin = 15;

// S i 7 0 2 1   O P T I O N S
bool enableHeater = false;
Adafruit_Si7021 Si7021 = Adafruit_Si7021(); //Assigning Si7021 to human friendly name
float humidity = 0;
float temperature = 0;
const int Si7021Pin = 13;

// S O I L   M O I S T U R E   O P T I O N S
const int AirValue = 864;   //Value of Soil Moisture sensor in air.
const int WaterValue = 435;  //Value of Soil Moisture sensor fully submerged in water (not including electronics obviously!)
int soilMoistureValue = 0;
const int soilMoisturePin = 12;

// T I M E  &  N E T W O R K   R E L A T E D   S T U F F
const char* WLAN_SSID = "CC7C Hyperoptic 1Gb Fibre 2.4Ghz";
const char* WLAN_PASSWD = "DQtpESbjxJSS";
const char* mqtt_server = "192.168.1.99";
WiFiClient espClient;
PubSubClient client(espClient);

void setup() {
  Serial.begin(9600); // open serial port, set the baud rate to 9600 bps
  WiFi.mode(WIFI_OFF);
  WiFi.forceSleepBegin();
  Serial.println("WiFi disabled successfully!");
  yield(); //Lets ESP8266 handle background tasks
  Wire.begin(); //Initialising i2C
  lightMeter.begin(); //Initialising BH1750
  Si7021.begin(); //Initialising Si7021
  rtcValid = false;
  if ( ESP.rtcUserMemoryRead( 0, (uint32_t*)&rtcData, sizeof( rtcData ) ) ) {
    // Calculate the CRC of what we just read from RTC memory, but skip the first 4 bytes as that's the checksum itself.
    uint32_t crc = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
    if ( crc == rtcData.crc32 ) {
      rtcValid = true;
    }
  }
}

// R E A D I N G   V A L U E S
void readvalues() {
  lux = lightMeter.readLightLevel(); // outputted directly as lux, you will need to change this if you use an analog variant
  delay(120);
  soilMoistureValue = map(analogRead(A0), WaterValue, AirValue, 0, 255);  //Maps value to nice analog 0,255. Will most likely map value to an actual human readable thing on software side on Pi
  yield();
  humidity = Si7021.readHumidity();
  temperature = Si7021.readTemperature();
  yield();
  //debug statements, will remove later to clean up code and save cpu cycles. Later revisions may have sensor reading alongside wifi code to harness scheduling and save time
  Serial.print("Light");
  Serial.println(lux);
  Serial.print("Soil Moisture");
  Serial.println(soilMoistureValue);
  Serial.print("Humidity");
  Serial.println(humidity);
  Serial.print("Temperature");
  Serial.println(temperature);
}

void loop() {
  //I P   A D D R E S S
  IPAddress ip( 192, 168, 1, 2 );
  IPAddress gateway( 192, 168, 1, 1 );
  IPAddress subnet( 255, 255, 255, 0 );
  IPAddress dns(192, 168, 1, 1);
  
  WiFi.forceSleepWake();
  yield();
  WiFi.persistent(false); 
  WiFi.mode(WIFI_STA);
  WiFi.config(ip, gateway, subnet);
  Serial.println("Initialising WiFi Connection!");

  if (rtcValid) {       // The RTC data was good, make a quick connection
    WiFi.begin( WLAN_SSID, WLAN_PASSWD, rtcData.channel, rtcData.ap_mac, true );
    Serial.println("RTC GOOD!");
  }
  else {      // The RTC data was not valid, so make a regular connection
    WiFi.begin( WLAN_SSID, WLAN_PASSWD );
    Serial.println("RTC BAD!");
  }

  int retries = 0;    //Awaiting WiFi connection
  int wifiStatus = WiFi.status();
  while (wifiStatus != WL_CONNECTED) {
    retries++;
    if (retries == 100) {   // RTC fast connect is not working - details may be wrong. Resetting wifi and trying again normally
      Serial.println("RTC not working! Retry.");
      WiFi.disconnect();
      delay(10);
      WiFi.forceSleepBegin();
      delay(10);
      WiFi.forceSleepWake();
      delay(10);
      WiFi.begin( WLAN_SSID, WLAN_PASSWD );
    }
    if (retries == 300) {
      // Giving up after 15 seconds - will try in 30 mins time.  
      WiFi.disconnect(true);
      delay(1);
      WiFi.mode(WIFI_OFF);
      Serial.println("Gave up! Going back to sleep!");
      ESP.deepSleep(18e8, WAKE_RF_DISABLED);
      return;
    }
    delay(50);
    wifiStatus = WiFi.status();
  }

  Serial.println("WiFi connected!");
  Serial.println("IP address: ");
  Serial.println(WiFi.localIP());
  WriteRTCValues();
  client.setServer(mqtt_server, 1883); //Initialising MQTT Connection
  readvalues(); //Reads values from sensors
  sendMQTTmessage(); //Sends sensor values to RPi MQTT server
  Serial.println("Writing RTC!");
  Serial.println("Going back to sleep!");
  WiFi.disconnect(true);
  yield();
  // WAKE_RF_DISABLED to keep the WiFi radio disabled when we wake up
  ESP.deepSleep(18e8, WAKE_RF_DISABLED);
}

void sendMQTTmessage()
{
  if (!client.connected()) {
    reconnect();
  }
  client.publish("plants/moneyplant/telems/humidity", String(humidity).c_str(), false);
  client.publish("plants/moneyplant/telems/temperature", String(temperature).c_str(), false);
  client.publish("plants/moneyplant/telems/light", String(lux).c_str(),false);
  client.publish("plants/moneyplant/telems/soilmoisture", String(soilMoistureValue).c_str(),false);
  
  /* Close MQTT client cleanly */
  client.disconnect();
}

void reconnect() {
  while (!client.connected())
  {
    String ClientId = "ESP01";
    ClientId += String(random(0xffff), HEX);
    if (client.connect(ClientId.c_str()))
      //if your MQTT server is protected with a password, use the next line instead of the revious
      //if (client.connect(ClientId.c_str()),mqtt_user,mqtt_password))
    {
      Serial.println("Connected");
      client.publish("home/karan/ESP01/connection", "OK");
    } else {
      Serial.print("failed, rc= ");
      Serial.print(client.state());
      yield();
    }
  }
}


// the CRC routine
uint32_t calculateCRC32( const uint8_t *data, size_t length ) {
  uint32_t crc = 0xffffffff;
  while ( length-- ) {
    uint8_t c = *data++;
    for ( uint32_t i = 0x80; i > 0; i >>= 1 ) {
      bool bit = crc & 0x80000000;
      if ( c & i ) {
        bit = !bit;
      }

      crc <<= 1;
      if ( bit ) {
        crc ^= 0x04c11db7;
      }
    }
  }

  return crc;
}

void WriteRTCValues() {
  rtcData.channel = WiFi.channel();
  memcpy( rtcData.ap_mac, WiFi.BSSID(), 6 ); // Copy 6 bytes of BSSID (AP's MAC address)
  rtcData.crc32 = calculateCRC32( ((uint8_t*)&rtcData) + 4, sizeof( rtcData ) - 4 );
  ESP.rtcUserMemoryWrite( 0, (uint32_t*)&rtcData, sizeof( rtcData ) );
}

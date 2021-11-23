#include "EspMQTTClient.h"
//#include "Wire.h"
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <WiFiUdp.h>

#define LED_PIN     4
#define LED_COUNT  50
#define BRIGHTNESS 100 // Set BRIGHTNESS to about 1/5 (max = 255)
#define UDP_PORT 4210

#define PERIODIC_MESSAGE_INTERVAL     30*1000 /*!< periodicity of messages in milliseconds */
#define DSP_VOLUME_CHANNELS_NUM       24 /*!< num address in memory (indirect memory table) */


#define MIN_MAX_VALUES_NUM 100

uint8_t mqtt_mutex = 0;
unsigned long current_ms = 0;
unsigned long previous_ms = 0;

uint8_t incoming_packet[255];
uint8_t incoming_packet_length = 0;

uint16_t volumes[DSP_VOLUME_CHANNELS_NUM];
uint16_t normalized_volumes[DSP_VOLUME_CHANNELS_NUM];
uint16_t min_values[MIN_MAX_VALUES_NUM];
uint16_t max_values[MIN_MAX_VALUES_NUM];

Adafruit_NeoPixel strip(LED_COUNT, LED_PIN, NEO_RGB + NEO_KHZ400);
WiFiUDP udp_client;


EspMQTTClient mqtt_client(
  "IoT_Dobbi",            //WiFi name
  "canned-ways-incense",  //WiFi password
  "192.168.88.111",       //MQTT server address
  "", "",                 //MQTT username and password
  "ESP8266_audiolight",   //Client name
  1883                    //MQTT port
);

//---------------------------------------------//

uint16_t map(uint16_t x, uint16_t in_min, uint16_t in_max, uint16_t out_min, uint16_t out_max) {
  return (x - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}


void PrintHex8(uint8_t *data, uint8_t length) // prints 8-bit data in hex
{
  char tmp[length*2+1];
  byte first;
  byte second;
  for (int i=0; i<length; i++) {
    first = (data[i] >> 4) & 0x0f;
    second = data[i] & 0x0f;
    tmp[i*2] = first+48;
    tmp[i*2+1] = second+48;
    if (first > 9) tmp[i*2] += 39;
    if (second > 9) tmp[i*2+1] += 39;
  }
  tmp[length*2] = 0;
  Serial.print(tmp);
}

uint16_t find_maximum(uint16_t a[], uint8_t n) {
  uint8_t c, index = 0;
  for (c = 1; c < n; c++)
    if (a[c] > a[index])
      index = c;
  return index;
}

uint16_t find_minimum(uint16_t a[], uint8_t n) {
  uint8_t c, index = 0;
  for (c = 1; c < n; c++)
    if (a[c] < a[index])
      index = c;
  return index;
}


uint16_t update_min_ring_buffer(uint16_t min) {
  for (uint8_t i = 0; i < MIN_MAX_VALUES_NUM - 1; i++) {
    min_values[i] = min_values[i + 1];
  }
  min_values[MIN_MAX_VALUES_NUM - 1] = min;
  return min_values[find_minimum(min_values, MIN_MAX_VALUES_NUM)];
}


uint16_t update_max_ring_buffer(uint16_t max) {
  //Serial.print("MRB1: ");
  //for (uint8_t i = 0; i < MIN_MAX_VALUES_NUM; i++) {
  //  Serial.print(max_values[i]);
  //  Serial.print(",");
  //}
  //Serial.print("\n");

  for (uint8_t i = 0; i < MIN_MAX_VALUES_NUM - 1; i++) {
    max_values[i] = max_values[i + 1];
  }

  //  Serial.print("MRB2: ");
  //for (uint8_t i = 0; i < MIN_MAX_VALUES_NUM; i++) {
  //  Serial.print(max_values[i]);
  //  Serial.print(",");
  //}
  //Serial.print("\n");


  max_values[MIN_MAX_VALUES_NUM - 1] = max;

  //  Serial.print("MRB3: ");
  //for (uint8_t i = 0; i < MIN_MAX_VALUES_NUM; i++) {
  //  Serial.print(max_values[i]);
  //  Serial.print(",");
  //}
  //Serial.print("\n");

  return max_values[find_maximum(max_values, MIN_MAX_VALUES_NUM)];
}



void power_message_received(const String& topic, const String& message) {
  //printf("Recieved payload from MQTT %s, parsed volume %d\n", message.c_str(), volume);
  if (mqtt_mutex == 0) {
    mqtt_mutex = 1;
    //set_volume(global_volume_percent);
    mqtt_mutex = 0;
  }
  else{
    Serial.println("MQTT mutex is locked");
  }
}


void onConnectionEstablished()
{
  mqtt_client.subscribe("audiolight/power", power_message_received);
  mqtt_client.publish("audiolight/status", "audiolight module started");
}


//---------------------------------------------//

void setup()
{
  Serial.begin(115200);
  mqtt_client.enableDebuggingMessages();
  strip.begin();           // INITIALIZE NeoPixel strip object (REQUIRED)
  strip.show();            // Turn OFF all pixels ASAP
  strip.setBrightness(BRIGHTNESS);
  udp_client.begin(UDP_PORT);
}

void colorWipe(uint32_t color, int wait) {
  for(int i=0; i < strip.numPixels(); i++) {
    strip.setPixelColor(i, color);
  }
  strip.show();
}

void color_update() {
  for (uint8_t i = 0; i < DSP_VOLUME_CHANNELS_NUM; i++) {
    uint8_t current_value = normalized_volumes[i];
    if (current_value > 0 && current_value <= 85) {
      strip.setPixelColor(i*2-1, strip.Color(current_value, 0, 0));
      strip.setPixelColor(i*2, strip.Color(current_value, 0, 0));
    }
    else if (current_value > 85 && current_value <= 170) {
      strip.setPixelColor(i*2-1, strip.Color(255, current_value - 85, 0));
      strip.setPixelColor(i*2, strip.Color(255, current_value - 85, 0));
    }
    else if (current_value > 170 && current_value <= 255) {
      strip.setPixelColor(i*2, strip.Color(255, 255, current_value - 170));
      strip.setPixelColor(i*2-1, strip.Color(255, 255, current_value - 170));
    }
  }
  strip.show();
}

void incoming_udp_packet() {
  //Serial.print("UDP packet contents: ");

  uint32_t volumes_32t[DSP_VOLUME_CHANNELS_NUM];
  //Serial.print("R: ");
  for (uint8_t i = 0; i < DSP_VOLUME_CHANNELS_NUM; i++) {
    uint32_t volume = 0x00 | (0x00 << 8) | (incoming_packet[i*2 + 1] << 16) | (incoming_packet[i*2 + 0] << 24);
    volumes[i] = volume/1000000;
    //Serial.print(volumes[i]);
    //Serial.print(",");
  }



  uint16_t current_min_value = volumes[find_minimum(volumes, DSP_VOLUME_CHANNELS_NUM)];
  uint16_t current_max_value = volumes[find_maximum(volumes, DSP_VOLUME_CHANNELS_NUM)];

  uint16_t min_value = update_min_ring_buffer(current_min_value);
  uint16_t max_value = update_max_ring_buffer(current_max_value);


  //Serial.print("\nCMin: ");
  //Serial.print(current_min_value);
  //Serial.print(", CMax: ");
  //Serial.print(current_max_value);
  //Serial.print("\n");
  //Serial.print("\nAMin: ");
  //Serial.print(min_value);
  //Serial.print(", AMax: ");
  //Serial.print(max_value);
  //Serial.print("\n");

  if (max_value == min_value) { return; }


  //Serial.print("\nN: ");
  for (uint8_t i = 0; i < DSP_VOLUME_CHANNELS_NUM; i++) {
    //uint8_t i = 0;
    uint16_t current_value = volumes[i];
    //int32_t new_volume = map(current_value, min_value, max_value, 0, 255);
    normalized_volumes[i] = map(current_value, min_value, max_value, 0, 255);
    //Serial.print(normalized_volumes[i]);
    //Serial.print(",");

    //Serial.print(new_volume);
    //Serial.print(" = map(");
    //Serial.print(current_value);
    //Serial.print(",");
    //Serial.print(min_value);
    //Serial.print(",");
    //Serial.print(max_value);
    //Serial.print(",");
    //Serial.print(0);
    //Serial.print(",");
    //Serial.print(255);
    //Serial.print(")\n");
  }
  //Serial.print("\n\n");
  Serial.print(".");
  color_update();
}

void loop()
{
  //colorWipe(strip.Color(255, 0, 0),50); // Red
  mqtt_client.loop();
  current_ms = millis();

  if (udp_client.parsePacket()) {
    incoming_packet_length = udp_client.read(incoming_packet, 255);
    incoming_udp_packet();
  }

  if (current_ms - previous_ms >= PERIODIC_MESSAGE_INTERVAL) {
    previous_ms = current_ms;
    //Serial.println("Publishing to MQTT periodic message");
    //mqtt_client.publish("audiolight/status", "ok");
  }
}

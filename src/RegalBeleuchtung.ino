#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

#define FASTLED_ALLOW_INTERRUPTS 0
#include <FastLED.h>

//ESP MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

//Webserver
ESP8266WebServer webServer(80);

//flag for saving data
bool shouldSaveConfig = false;

//define your default values here, if there are different values in config.json, they are overwritten.
char node_name[64] = "weltenraum_regalled";
char mqtt_server[64] = "broker.hivemq.com";
char mqtt_port[6] = "1883";

long long r;
long long g;
long long b;

String mode = "rainbow";

//defines for APA102
#define DATA_PIN 6
#define CLOCK_PIN 5

//defines for WS2812
//#define DATA_PIN 2

#define NUM_LEDS 60
//state of LEDs
CRGB leds[NUM_LEDS];

#define MQTT_KEEPALIVE = 600

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
        int i;
        char buffer[64];
        char ans_string[128] =  {0};
        memcpy(ans_string, payload, length);
        sprintf(buffer, "%s/result", node_name);
        Serial.println("Callback");

        payload[length] = 0;

        Serial.println((const char*)payload);
        String pay = (char*)payload;

        //parse JSON here..... to drive LEDs

        StaticJsonBuffer<200> jsonBuffer;

        JsonObject& root = jsonBuffer.parseObject(pay);

        mode = root["mode"].asString();
        int id1    = root["leds"][1];
        //int id2   = root["leds"][1];

        Serial.println("JSON: mode=" + mode + " - id1=" + id1);

        mqttClient.publish(buffer, "ACK");
}



String formatBytes(size_t bytes){
  if (bytes < 1024){
    return String(bytes)+"B";
  } else if(bytes < (1024 * 1024)){
    return String(bytes/1024.0)+"KB";
  } else if(bytes < (1024 * 1024 * 1024)){
    return String(bytes/1024.0/1024.0)+"MB";
  } else {
    return String(bytes/1024.0/1024.0/1024.0)+"GB";
  }
}

//callback notifying us of the need to save config
void saveConfigCallback () {
        Serial.println("Should save config");
        shouldSaveConfig = true;
}

void setup()
{
  // put your setup code here, to run once:
  Serial.begin(115200);
  Serial.println();


  //clean FS, for testing
  //SPIFFS.format();

  //read configuration from FS json
  Serial.println("mounting FS...");

  if (SPIFFS.begin()) {
          Serial.println("mounted file system");
          if (SPIFFS.exists("/config.json")) {
                  //file exists, reading and loading
                  Serial.println("reading config file");
                  File configFile = SPIFFS.open("/config.json", "r");
                  if (configFile) {
                          Serial.println("opened config file");
                          size_t size = configFile.size();
                          // Allocate a buffer to store contents of the file.
                          std::unique_ptr<char[]> buf(new char[size]);

                          configFile.readBytes(buf.get(), size);
                          DynamicJsonBuffer jsonBuffer;
                          JsonObject& json = jsonBuffer.parseObject(buf.get());
                          json.printTo(Serial);
                          if (json.success()) {
                                  Serial.println("\nparsed json");
                                  if(json.containsKey("node_name"))
                                    strcpy(node_name, json["node_name"]);
                                  if(json.containsKey("mqtt_server"))
                                    strcpy(mqtt_server, json["mqtt_server"]);
                                  if(json.containsKey("mqtt_port"))
                                    strcpy(mqtt_port, json["mqtt_port"]);

                          } else {
                                  Serial.println("failed to load json config");
                          }
                  }
          }
  } else {
          Serial.println("failed to mount FS");
  }
  //end read
  {
    Dir dir = SPIFFS.openDir("/");
        while (dir.next()) {
          String fileName = dir.fileName();
          size_t fileSize = dir.fileSize();
          Serial.printf("FS File: %s, size: %s\n", fileName.c_str(), formatBytes(fileSize).c_str());
    }
  }

  // The extra parameters to be configured (can be either global or just in the setup)
  // After connecting, parameter.getValue() will get you the configured value
  // id/name placeholder/prompt default length
  WiFiManagerParameter custom_node_name("name", "node name", node_name, 40);
  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);

  //WiFiManager
  //Local intialization. Once its business is done, there is no need to keep it around
  WiFiManager wifiManager;

  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  //set static ip
  //wifiManager.setSTAStaticIPConfig(IPAddress(10,0,1,99), IPAddress(10,0,1,1), IPAddress(255,255,255,0));

  //add all your parameters here
  wifiManager.addParameter(&custom_node_name);
  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);

  //reset settings - for testing
  //wifiManager.resetSettings();

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("LED-ESP-02", "4223422342234223")) {
          Serial.println("failed to connect and hit timeout");
          delay(3000);
          //reset and try again, or maybe put it to deep sleep
          ESP.reset();
          delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(node_name, custom_node_name.getValue());
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());

  for(int i =  0; i < 6; i++)
  {
          if(*(i + mqtt_port) == 0)
                  break;
          else if((*(i + mqtt_port) < '0') || (*(i + mqtt_port) > '9') )
          {
                  strcpy(mqtt_port, "1883");
                  break;
          }
  }

  //save the custom parameters to FS
  if (shouldSaveConfig) {
          Serial.println("saving config");
          DynamicJsonBuffer jsonBuffer;
          JsonObject& json = jsonBuffer.createObject();
          json["mqtt_server"] = mqtt_server;
          json["mqtt_port"] = mqtt_port;
          json["node_name"] = node_name;

          File configFile = SPIFFS.open("/config.json", "w");
          if (!configFile) {
                  Serial.println("failed to open config file for writing");
          }

          json.printTo(Serial);
          json.printTo(configFile);
          configFile.close();
          //end save
  }

  Serial.println("local ip");
  Serial.println(WiFi.localIP());

  ArduinoOTA.begin();

  mqttClient.setServer(mqtt_server, atoi(mqtt_port));
  mqttClient.setCallback(mqtt_callback);

  char buffer[64];
  sprintf(buffer, "%s", node_name);
  Serial.print("DNS name: ");
  Serial.println(buffer);
  MDNS.begin(node_name);

  //TODO: setup webserver pages here
  webServer.begin();

  MDNS.addService("http", "tcp", 80);

  //setup LEDs
  // APA102
  FastLED.addLeds<APA102, DATA_PIN, CLOCK_PIN, BGR, DATA_RATE_MHZ(1)>(leds, NUM_LEDS);
  //WS2812
  //FastLED.addLeds<WS2812B, DATA_PIN>(leds, NUM_LEDS);
//  FastLED.addLeds<WS2812B, DATA_PIN, GRB>(leds, NUM_LEDS).setCorrection( TypicalLEDStrip );
  int i;
  for (i =0; i < NUM_LEDS; i++)
  {
    leds[i] = CRGB::Blue;
  }
}

void mqtt_reconnect()
{
        Serial.println("Reconnect MQTT");
        char buffer[64];

        if(mqttClient.connect(node_name))
        {
                Serial.println("MQTT connected");

                sprintf(buffer, "%s/command", node_name);
                mqttClient.subscribe(node_name);
        }
        else
        {
          Serial.println("MQTT not connected");
          Serial.println(mqtt_server);
          Serial.println(mqtt_port);

        }


        sprintf(buffer, "MQTT state is: %d", mqttClient.state());
        Serial.println(buffer);
}

void showStrip() {
   FastLED.show();
}

void setPixel(int Pixel, byte red, byte green, byte blue) {
   leds[Pixel].r = red;
   leds[Pixel].g = green;
   leds[Pixel].b = blue;
 }

 void setAll(byte red, byte green, byte blue) {
  for(int i = 0; i < NUM_LEDS; i++ ) {
    setPixel(i, red, green, blue);
  }
  showStrip();
}

void Strobe(byte red, byte green, byte blue, int StrobeCount, int FlashDelay, int EndPause){
  for(int j = 0; j < StrobeCount; j++) {
    setAll(red,green,blue);
    showStrip();
    delay(FlashDelay);
    setAll(0,0,0);
    showStrip();
    delay(FlashDelay);
  }

 delay(EndPause);
}

void setColor (char* color) {
  long long colnumber = strtol( &color[1], NULL, 16);

  r = colnumber >> 16;
  g = colnumber >> 8 & 0xFF;
  b = colnumber & 0xFF;

  int i;

  for (i =0; i < NUM_LEDS; i++)
  {
    leds[i].r = r;
    leds[i].g = g;
    leds[i].b = b;
    //leds[i] = CRGB::Green;
  }
  FastLED.show();
}


byte * Wheel(byte WheelPos) {
  static byte c[3];

  if(WheelPos < 85) {
   c[0]=WheelPos * 3;
   c[1]=255 - WheelPos * 3;
   c[2]=0;
  } else if(WheelPos < 170) {
   WheelPos -= 85;
   c[0]=255 - WheelPos * 3;
   c[1]=0;
   c[2]=WheelPos * 3;
  } else {
   WheelPos -= 170;
   c[0]=0;
   c[1]=WheelPos * 3;
   c[2]=255 - WheelPos * 3;
  }

  return c;
}

void rainbowCycle(int SpeedDelay) {
  byte *c;
  uint16_t i, j;

  for(j=0; j<256*5; j++) { // 5 cycles of all colors on wheel
    for(i=0; i< NUM_LEDS; i++) {
      c=Wheel(((i * 256 / NUM_LEDS) + j) & 255);
      setPixel(i, *c, *(c+1), *(c+2));
    }
    showStrip();
    delay(SpeedDelay);
  }
}


void loop() {
        if (!mqttClient.connected()) {
                mqtt_reconnect();
        }
        mqttClient.loop();
        ArduinoOTA.handle();

        Serial.println("mode: " + mode);
        if (mode == "rainbow") {
          rainbowCycle(50);
        } else if (mode == "strobe") {
          Strobe(0xff, 0xff, 0xff, 1, 50, 1000);
        } else if (mode == "static") {
          setAll(0x0a, 0xff, 0xe7);
        }
      delay(30);
}

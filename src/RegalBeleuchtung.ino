#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <ESP8266mDNS.h>
#include <PubSubClient.h>
#include <ArduinoOTA.h>

//ESP MQTT
WiFiClient espClient;
PubSubClient mqttClient(espClient);

//Webserver
ESP8266WebServer webServer(80);

//flag for saving data
bool shouldSaveConfig = false;

//define your default values here, if there are different values in config.json, they are overwritten.
char node_name[64] = "weltenraum_led";
char mqtt_server[64] = "mqtt.shack";
char mqtt_port[6] = "1883";

void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
        char buffer[64];
        char ans_string[128] =  {0};
        memcpy(ans_string, payload, length);
        sprintf(buffer, "%s/result", node_name);
        Serial.println("Callback");

        payload[length] = 0;

        Serial.println((const char*)payload);

        //parse JSON here..... to drive LEDs

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

  //set minimu quality of signal so it ignores AP's under that quality
  //defaults to 8%
  //wifiManager.setMinimumSignalQuality();

  //sets timeout until configuration portal gets turned off
  //useful to make it all retry or go to sleep
  //in seconds
  //wifiManager.setTimeout(120);

  //fetches ssid and pass and tries to connect
  //if it does not connect it starts an access point with the specified name
  //here  "AutoConnectAP"
  //and goes into a blocking loop awaiting configuration
  if (!wifiManager.autoConnect("DaliMasterSettings", "password")) {
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
  sprintf(buffer, "%s.local", node_name);
  Serial.print("DNS name: ");
  Serial.println(buffer);
  MDNS.begin(node_name);

  //TODO: setup webserver pages here
  webServer.begin();

  MDNS.addService("http", "tcp", 80);
}

void mqtt_reconnect()
{
        Serial.println("Reconnect MQTT");
        char buffer[64];

        if(mqttClient.connect(node_name))
        {
                Serial.println("MQTT connected");

                sprintf(buffer, "%s/command", node_name);
                mqttClient.subscribe(buffer);
        }
        else
        {
                Serial.println("MQTT not connected");
        }


        sprintf(buffer, "MQTT state is: %d", mqttClient.state());
        Serial.println(buffer);
}


void loop() {
        if (!mqttClient.connected()) {
                mqtt_reconnect();
        }
        mqttClient.loop();
        ArduinoOTA.handle();
}

#include <ESP8266WiFi.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <credentials.h>


const String prefix = MQTT_PREFIX;
const char* deviceName = DEVICE_NAME;

#define LED D4
WiFiClient espClient;
PubSubClient client(espClient);

int temp = 0;
const int debounce_size = 30;
int debounce_counter = 0;
bool debounce_init_done = false;
int debounce_array[debounce_size];
String status;
String tele = "tele";

void sendData(String subtopic, const char* data, bool retained) {
  subtopic = prefix + subtopic;
  client.publish(subtopic.c_str(), data, retained);
}

void sendData(String subtopic, String data, bool retained) {
  sendData(subtopic, data.c_str(), retained);
}

void sendData(String subtopic, String data) {
  sendData(subtopic, data, false);
}

void sendStatusIfChanged(const String &newStatus) {
  if (!newStatus.equals(status)) {
    status = newStatus;
    sendData("status", status);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) 
  {
    Serial.print("Attempting MQTT connection...");
    // Attempt to connect
    //if you MQTT broker has clientID,username and password
    //please change following line to    if (client.connect(clientId,userName,passWord))
    if (client.connect(deviceName, MQTT_USER, MQTT_PASSWORD))
    {
      Serial.println("connected");
     //once connected to MQTT broker, subscribe command if any
      sendData("ip", WiFi.localIP().toString(), true);
      sendData("rssi", String(WiFi.RSSI()), true);
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 6 seconds before retrying
      delay(6000);
    }
  }
}

bool checkRes(String res) {
  return res[0] == 'D' && res.length() == 33;
}

int debounce_temp(int reading) {
  debounce_array[debounce_counter] = reading;
  debounce_counter++;
  if (debounce_counter >= debounce_size) {
    debounce_counter = 0;
    debounce_init_done = true;
  }
  if (debounce_init_done){
    int sum = 0;
    for (int i = 0; i < debounce_size; i++) {
      sum += debounce_array[i];
    }
    return sum / debounce_size;
  }
  return 0;
}

void parseTempAndSendIfChanged(String tempString) {
  int newTemp = tempString.toInt();
  int debouncedTemp = debounce_temp(newTemp);
  if (debouncedTemp && debouncedTemp != temp) {
      temp = debouncedTemp;
      sendData("temp", String(temp));
  }
}

void parseFirstLineAndSendIfChanged(String substring) {
  substring.trim();
  substring.replace("\xF5", "ü");
  sendStatusIfChanged(substring);
}

void detectScreen(String screen) {
  // Detect normal operation (Second line conatins "Heizgas:")
  if (screen[18] == 'H' && screen[25] == ':') {
    parseTempAndSendIfChanged(screen.substring(27, 30));
    parseFirstLineAndSendIfChanged(screen.substring(1, 17));
  } else if (screen[20] == 'H' && screen[29] == 'r') {
    sendStatusIfChanged("Heizfehler");
  } else {
    sendData(tele, "Cannot detect screen: " + screen, true);
  }
}

void setup() {
  pinMode(LED, OUTPUT);
  Serial.begin(9600);
  Serial.setTimeout(100);
  Serial.println("Booting");
  WiFi.mode(WIFI_STA);
  WiFi.hostname(deviceName);
  WiFi.begin(WIFI_SSID, WIFI_PSK);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection Failed! Rebooting...");
    delay(5000);
    ESP.restart();
  }

  client.setServer(MQTT_SERVER, 1883);
  reconnect();

  // Port defaults to 8266
  // ArduinoOTA.setPort(8266);

  ArduinoOTA.setHostname(deviceName);

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH) {
      type = "sketch";
    } else { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) {
      Serial.println("Auth Failed");
    } else if (error == OTA_BEGIN_ERROR) {
      Serial.println("Begin Failed");
    } else if (error == OTA_CONNECT_ERROR) {
      Serial.println("Connect Failed");
    } else if (error == OTA_RECEIVE_ERROR) {
      Serial.println("Receive Failed");
    } else if (error == OTA_END_ERROR) {
      Serial.println("End Failed");
    }
  });
  ArduinoOTA.begin();
  Serial.println("Ready");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  sendData(tele, "UP", true);
}

void loop() {
  ArduinoOTA.handle();
  if (!client.connected()) {
    reconnect();
  }
  client.loop();
  if (Serial.available() > 32) {
    digitalWrite(LED, LOW);
    String screen = Serial.readStringUntil(0);
    if (checkRes(screen)) {
      detectScreen(screen);
    } else {
      sendData(tele, "Screen not valid: " + screen, true);
    }
    digitalWrite(LED, HIGH);
  }
}
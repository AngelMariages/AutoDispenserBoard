#include <ArduinoJson.h>
#include <EEPROM.h>
#include <MQTT.h>
#include <NTPClient.h>
#include <WiFiEsp.h>
#include <WiFiEspClient.h>
#include <WiFiEspUdp.h>

#define _ESPLOGLEVEL_ 4

#include "SoftwareSerial.h"

SoftwareSerial ESP8266Serial(2, 3);

bool isWifiInit = false;
const char ssid[] = "CALATERE";
const char pass[] = "WiFi@CaLaTere";

const int MAX_DISPENSERS = 3;
const char clientId[] = "client-2";

WiFiEspClient wifiClient;
WiFiEspUDP ntpUDP;
NTPClient timeClient(ntpUDP);
MQTTClient mqtt;

struct DispenserConfig {
    unsigned long lastUpdate;
    int days[7];
    int hours[3];
};

DispenserConfig dispenserConfigs[MAX_DISPENSERS] = {};

void cleanDispenserConfigs() {
    for (int i = 0; i < MAX_DISPENSERS; i++) {
        dispenserConfigs[i].lastUpdate = 0;
    }
}

void setup() {
    Serial.begin(9600);
    ESP8266Serial.begin(9600);

    while (!Serial) {
        ;
    }

    while (!ESP8266Serial) {
        ;
    }

    cleanDispenserConfigs();
    readAllDispenserConfigs();

    initWifi(true);
    initMQTT();
}

unsigned long lastMillis = 0;
unsigned long lastNTP = 0;

void loop() {
    if (WiFi.status() == WL_CONNECTED || wifiClient.connected()) {
        if (!mqtt.connected() || !mqtt.loop()) {
            // Serial.println(F("Reconnecting to MQTT..."));
            initMQTT();
            return;
        }
        delay(5);
        timeClient.update();

        if (millis() - lastMillis > 10000) {
            lastMillis = millis();
            mqtt.publish("/hello",
                         "hey " + String(timeClient.getFormattedTime()), false,
                         2);
        }
    } else {
        // Serial.println(F("Not connected to wifi!"));
        delay(1000);
    }
}

void mqttMessageReceived(MQTTClient *refClient, char topic[], char bytes[],
                         int length) {
    String payload = String((const char *)bytes);

    // Serial.print(F("Message received: "));
    // Serial.print(topic);

    if (strcmp(topic, "/random") == 0) {
        MQTTClient cli = *refClient;

        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, bytes);

        if (error) {
            return;
        }

        const char *c = doc["c"];  // clientName
        int id = doc["id"];        // dispenserId

        JsonArray d = doc["d"];
        int days[7];
        for (int i = 0; i < 7; i++) {
            days[i] = d[i];
        }

        JsonArray h = doc["h"];

        int hours[3];
        for (int i = 0; i < 3; i++) {
            hours[i] = h[i];
        }

        if (c == NULL || d == NULL || h == NULL || id == NULL) {
            // Serial.println(F("Invalid message!"));
            return;
        }

        saveDispenserConfig(id, days, hours);

        readAllDispenserConfigs();

        // Serial.println(F("Saved!"));

        String msg = "";

        for (int i = 0; i < MAX_DISPENSERS; i++) {
            if (dispenserConfigs[i].lastUpdate == 0) {
                continue;
            }

            msg += "d[";
            msg += i;
            msg += "]:";
            msg += dispenserConfigs[i].lastUpdate;
            msg += ",";
        }

        Serial.println(msg);

        cli.publish("/hello", msg, false, 2);
    }
}

void saveDispenserConfig(int id, int days[], int hours[]) {
    DispenserConfig config;
    config.lastUpdate = timeClient.getEpochTime();
    for (int i = 0; i < 7; i++) {
        config.days[i] = days[i];
    }
    for (int i = 0; i < 3; i++) {
        config.hours[i] = hours[i];
    }

    int size = sizeof(DispenserConfig);
    int address = (id - 1) * size;

    EEPROM.put(address, config);
}

void readAllDispenserConfigs() {
    int size = sizeof(DispenserConfig);
    int address = 0;

    for (int i = 0; i < MAX_DISPENSERS; i++) {
        DispenserConfig config;
        EEPROM.get(address, config);
        Serial.print("Dispenser ");
        Serial.println(i);
        Serial.println("Last update: " + String(config.lastUpdate));

        if (config.lastUpdate <= 0) {
            break;
        }

        dispenserConfigs[i] = config;

        Serial.println("Days: \t");
        for (int j = 0; j < 7; j++) {
            Serial.print(config.days[j]);
        }
        Serial.println();

        Serial.println("Hours: \t");
        for (int j = 0; j < 3; j++) {
            Serial.print(config.hours[j]);
        }
        Serial.println();
    }
}

void initMQTT() {
    mqtt.begin("192.168.0.23", wifiClient);
    mqtt.onMessageAdvanced(mqttMessageReceived);
    mqtt.ref = &mqtt;

    // Serial.println(F("Connecting to MQTT..."));

    const char *clientId = "AutoDispenserBoard";

    while (!mqtt.connect(clientId, false)) {
        // Serial.print(".");
        delay(1000);
    }

    // Serial.println(F("Connected to MQTT!"));

    mqtt.subscribe("/random");

    // Serial.println(F("Subscribed to /random: "));
}

void initWifi(bool shouldRetry) {
    if (!isWifiInit) {
        WiFi.init(&ESP8266Serial);
        isWifiInit = true;
    }

    if (WiFi.status() == WL_NO_SHIELD) {
        // Serial.println(F("WiFi shield not present"));
        // Serial.println(F("Stopping..."));
        while (true)
            ;
    }

    while (WiFi.status() != WL_CONNECTED) {
        // Serial.print(F("Attempting to connect to SSID: "));
        // Serial.println(ssid);
        WiFi.begin(ssid, pass);
        delay(1000);
    }

    // Serial.println(F("Connected to wifi"));
    timeClient.begin();
}
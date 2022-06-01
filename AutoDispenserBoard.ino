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
const char mqttClient[] = "AutoDispenserBoard";

WiFiEspClient wifiClient;
WiFiEspUDP ntpUDP;
NTPClient timeClient(ntpUDP);
MQTTClient mqtt;

int DISPENSER_1 = 5;
char msgToSend[3][100] = {"", "", ""};

struct DispenserConfig {
    unsigned long lastUpdate;
    int days[7];
    int hours[3];
};

DispenserConfig dispenserConfigs[MAX_DISPENSERS] = {
    {0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0}},
    {0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0}},
    {0, {0, 0, 0, 0, 0, 0, 0}, {0, 0, 0}}};

void setup() {
    pinMode(DISPENSER_1, OUTPUT);

    Serial.begin(9600);
    ESP8266Serial.begin(9600);

    while (!Serial) {
        ;
    }

    while (!ESP8266Serial) {
        ;
    }

    WiFi.init(&ESP8266Serial);

    readAllDispenserConfigs();

    initWifi();
    setupMQTT();
    initMQTT();
    timeClient.begin();
}

unsigned long lastMillis = 0;
unsigned long lastNTP = 0;

void loop() {
    mqtt.loop();

    if (millis() - lastMillis > 10000) {
        timeClient.update();

        lastMillis = millis();
        char msg[100];
        sprintf(msg, "Time: %s", timeClient.getFormattedTime().c_str());
        mqtt.publish("/hello", msg, false, 2);
    }

    // Check if any msgToSend is not empty
    // and send it to mqtt
    for (int i = 0; i < MAX_DISPENSERS; i++) {
        if (strlen(msgToSend[i]) > 0) {
            mqtt.publish("/hello", msgToSend[i], false, 2);
            msgToSend[i][0] = '\0';
        }
    }
}

void mqttMessage(String &t, String &p) {
    char *topic = (char *)t.c_str();
    char *payload = (char *)p.c_str();

    digitalWrite(DISPENSER_1, HIGH);

    if (strcmp(topic, "/random") == 0) {
        StaticJsonDocument<128> doc;
        DeserializationError error = deserializeJson(doc, payload);

        if (error) {
            delay(500);
            digitalWrite(DISPENSER_1, LOW);
            delay(500);
            digitalWrite(DISPENSER_1, HIGH);
            delay(500);
            digitalWrite(DISPENSER_1, LOW);
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

        doc.clear();

        saveDispenserConfig(id, days, hours);

        readAllDispenserConfigs();

        // Serial.println(F("Saved!"));

        for (int i = 0; i < MAX_DISPENSERS; i++) {
            sprintf(msgToSend[i], "d[%d]=%lu", i,
                    dispenserConfigs[i].lastUpdate);
        }
    }

    digitalWrite(DISPENSER_1, LOW);
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
    int address = 0;

    for (int i = 0; i < MAX_DISPENSERS; i++) {
        EEPROM.get(address, dispenserConfigs[i]);
        Serial.print("Dispenser ");
        Serial.println(i);
        char msg[64] = "";
        sprintf(msg, "d[%d]=%lu", i, dispenserConfigs[i].lastUpdate);
        Serial.println(msg);

        Serial.println("Days: \t");
        for (int j = 0; j < 7; j++) {
            Serial.print(dispenserConfigs[i].days[j]);
        }
        Serial.println();

        Serial.println("Hours: \t");
        for (int j = 0; j < 3; j++) {
            Serial.print(dispenserConfigs[i].hours[j]);
        }
        Serial.println();

        address += sizeof(DispenserConfig);
    }
}

void setupMQTT() {
    mqtt.begin("192.168.0.23", wifiClient);
    mqtt.onMessage(mqttMessage);
    mqtt.ref = &mqtt;
}

void initMQTT() {
    while (!mqtt.connect(mqttClient, false)) {
        delay(1000);
    }

    mqtt.subscribe("/random", 2);
}

void initWifi() {
    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }
}
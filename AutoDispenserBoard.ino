#include <EEPROM.h>
#include <MQTT.h>
#include <SoftwareSerial.h>
#include <WiFiEspAT.h>
#include <time.h>

SoftwareSerial ESP8266Serial(2, 3);

const static char ssid[] = "CALATERE";
const static char pass[] = "WiFi@CaLaTere";

const static char mqttServer[] = "192.168.0.23";
const static char clientId[] = "client-2";
const static char timeTopic[] = "/ad/time";
const static char helloTopic[] = "/ad/hello";
char clientTopic[15];

WiFiClient wifiClient;
MQTTClient mqtt;

#define MAX_DISPENSERS 3
int dispenser_pins[MAX_DISPENSERS] = {5, 6, 7};
time_t current_time = 0;
bool timeSet = false;
unsigned long lastAddedMillis = 0;

bool dispenserStatus[MAX_DISPENSERS] = {0, 0, 0};
unsigned long dispensed[MAX_DISPENSERS][10] = {0};

struct DispenserConfig {
    unsigned int days[7];
    unsigned int hours[3];
    unsigned int minutes[3];
};

DispenserConfig dispenserConfigs[MAX_DISPENSERS];

void setup() {
    randomSeed(analogRead(0));

    Serial.begin(9600);

    while (!Serial)
        ;

    for (int i = 0; i < MAX_DISPENSERS; i++) {
        pinMode(dispenser_pins[i], OUTPUT);
    }

    ESP8266Serial.begin(9600);
    WiFi.init(ESP8266Serial);

    readAllDispenserConfigs(true);

    initWifi();
    delay(1000);

    mqtt.begin(mqttServer, wifiClient);
    mqtt.onMessage(mqttMessage);

    initMQTT();
}

unsigned long lastMsg = 0;

void loop() {
    mqtt.loop();

    if (!mqtt.connected() || !wifiClient.connected()) {
        initMQTT();
        delay(1000);
    }

    if (millis() - lastMsg > 10000) {
        lastMsg = millis();
        char msg[100];
        memset(msg, 0, 100);
        // sprintf(msg, "Time: %s", timeClient.getFormattedTime().c_str());
        // Format lastMillis t_time to human readable format
        sprintf(msg, "Time: %s", ctime(&current_time));

        // sprintf(msg, "Time: %02d-%02d-%02d %02d:%02d:%02d", year(), month(),
        // day(), hour(), minute(), second());
        mqtt.publish("/hello", msg, false, 2);

        if (timeSet) {
            readAllDispenserConfigs(false);
            checkForDispense();
            current_time += (millis() - lastAddedMillis) / 1000;
            lastAddedMillis = millis();

            sendDispensedLog();
        }
    }

    delay(10);
}

void checkForDispense() {
    for (int i = 0; i < MAX_DISPENSERS; i++) {
        int day = weekday();
        if (dispenserConfigs[i].days[day] == 0) {
            continue;
        }

        for (int j = 0; j < 3; j++) {
            if (dispenserConfigs[i].hours[j] == hour()) {
                for (int k = 0; k < 3; k++) {
                    if (dispenserConfigs[i].minutes[k] == minute()) {
                        if (!dispenserStatus[i]) {
                            digitalWrite(dispenser_pins[i], HIGH);
                            dispenserStatus[i] = true;
                            saveDispenseLog(i);
                        } else {
                            digitalWrite(dispenser_pins[i], LOW);
                        }
                    }
                }
            }
        }
    }
}

void saveDispenseLog(int index) {
    if (index < 0 || index >= MAX_DISPENSERS) {
        return;
    }

    int i = 0;
    while (dispensed[index][i] != 0) {
        i++;
    }

    dispensed[index][i] = epoch();
}

void sendDispensedLog() {
    char topic[30];
    memset(topic, 0, 30);
    sprintf(topic, "/ad/disp/%s", clientId);

    for (int i = 0; i < MAX_DISPENSERS; i++) {
        for (int j = 0; j < 10; j++) {
            if (dispensed[i][j] == 0) {
                continue;
            }

            char msg[100];
            memset(msg, 0, 100);
            sprintf(msg, "%d-%lu", i, dispensed[i][j]);
            if (mqtt.publish(topic, msg, false, 1)) {
                dispensed[i][j] = 0;
            }
        }
    }
}

char weekday() {
    struct tm* tm_now = localtime(&current_time);
    return tm_now->tm_wday;
}

char hour() {
    struct tm* tm_now = localtime(&current_time);
    return tm_now->tm_hour;
}

char minute() {
    struct tm* tm_now = localtime(&current_time);
    return tm_now->tm_min;
}

unsigned long epoch() { return (unsigned long)current_time; }

void mqttMessage(String& t, String& p) {
    char* topic = (char*)t.c_str();
    char* payload = (char*)p.c_str();

    if (strcmp(topic, timeTopic) == 0) {
        unsigned long epoch = strtoul(payload, NULL, 10);
        if (epoch > 1641042000) {  // Jan 1 2022
            current_time = epoch;
            timeSet = true;
        }
    } else if (strcmp(topic, clientTopic) == 0) {
        Serial.println(payload);
        parseDispenserConfig(payload);
    }
}

void parseDispenserConfig(char* payload) {
    char* token = strtok(payload, "-");
    int index;
    unsigned int days[7];
    unsigned int hours[3];
    unsigned int minutes[3];

    sscanf(token, "%d", &index);
    token = strtok(NULL, "-");

    for (int i = 0; i < 7; i++) {
        sscanf(token, "%d", &days[i]);
        token = strtok(NULL, "-");
    }

    for (int i = 0; i < 3; i++) {
        sscanf(token, "%d", &hours[i]);
        token = strtok(NULL, "-");
    }

    for (int i = 0; i < 3; i++) {
        sscanf(token, "%d", &minutes[i]);
        token = strtok(NULL, "-");
    }

    if (index >= 0 && index < MAX_DISPENSERS) {
        saveDispenserConfig(index, days, hours, minutes);
    }
}

void saveDispenserConfig(int id, unsigned int days[], unsigned int hours[],
                         unsigned int minutes[]) {
    for (int i = 0; i < 7; i++) {
        dispenserConfigs[id].days[i] = days[i];
        Serial.println(dispenserConfigs[id].days[i]);
    }
    for (int i = 0; i < 3; i++) {
        dispenserConfigs[id].hours[i] = hours[i];
        Serial.println(dispenserConfigs[id].hours[i]);
    }
    for (int i = 0; i < 3; i++) {
        dispenserConfigs[id].minutes[i] = minutes[i];
        Serial.println(dispenserConfigs[id].minutes[i]);
    }

    EEPROM.put(id * sizeof(DispenserConfig), dispenserConfigs[id]);
}

void readAllDispenserConfigs(bool debug) {
    for (int i = 0; i < MAX_DISPENSERS; i++) {
        int address = i * sizeof(DispenserConfig);

        EEPROM.get(address, dispenserConfigs[i]);
        if (debug) {
            Serial.print(F("Dispenser "));
            Serial.println(i);

            for (int j = 0; j < 7; j++) {
                Serial.print("\t");
                Serial.print(dispenserConfigs[i].days[j]);
            }

            Serial.println();

            for (int j = 0; j < 3; j++) {
                Serial.print("\t");
                Serial.print(dispenserConfigs[i].hours[j]);
            }

            Serial.println();

            for (int j = 0; j < 3; j++) {
                Serial.print("\t");
                Serial.print(dispenserConfigs[i].minutes[j]);
            }

            Serial.println();
            Serial.println(F("END"));
        }
    }
}

void initMQTT() {
    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(1000);
    }

    char mqttUser[6];

    snprintf(mqttUser, 6, "AD-%02d", random(99));

    int retries = 0;
    while (!mqtt.connect(mqttUser)) {
        retries++;
        Serial.print(",");
        delay(1000);
        if (retries > 10) {
            restart();
        }
    }

    mqtt.publish(helloTopic, clientId, false, 2);

    snprintf(clientTopic, 15, "/ad/c/%s", clientId);

    mqtt.subscribe(clientTopic);
    mqtt.subscribe(timeTopic);
}

void(* resetFunc) (void) = 0;

void restart() {
    Serial.println("Restarting");
    // resetFunc();
}

void initWifi() {
    if (WiFi.status() == WL_NO_MODULE) {
        Serial.println(F("OUT!"));
        while (true);
    }

    WiFi.disconnect();

    WiFi.begin(ssid, pass);

    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
    }
}

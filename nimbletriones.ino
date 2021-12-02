#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include <string>
#include "wificreds.h"


// Define your WiFi settings in wificreds.h

#ifndef HOSTNAME
  #define HOSTNAME "blegateway"
#endif
#define LED 2


//NimBLE Stuff
const NimBLEUUID NOTIFY_SERVICE ("FFD0");
const NimBLEUUID NOTIFY_CHAR    ("FFD4");
const NimBLEUUID MAIN_SERVICE   ("FFD5");
const NimBLEUUID WRITE_CHAR     ("FFD9");
NimBLEClient* pClient = nullptr;
NimBLERemoteService* pSvc = nullptr;
NimBLERemoteCharacteristic* pChr = nullptr;
NimBLERemoteDescriptor* pDsc = nullptr;
NimBLERemoteService* nSvc = nullptr;
NimBLERemoteCharacteristic* nChr = nullptr;
NimBLERemoteDescriptor* nDsc = nullptr;

// MQTT Stuff
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
//const char broker[] = "smarthome";
const char broker[] = "192.168.42.121";
const int  port     = 1883;
const char MQTT_PUB_TOPIC[] = "triones/status"; // Where output from this code goes
const char MQTT_SUB_TOPIC[] = "triones/control"; // Where this code receives instructions

// Forward declarations
// I don't know how to C++
bool setRGB(uint8_t, uint8_t, uint8_t, uint8_t);
bool findTrionesDevices();
bool getStatus();
bool connect(NimBLEAddress);
bool turnOn();
bool turnOff();
bool setMode(uint8_t, uint8_t);

unsigned long previousMillis = 0;

void sendMqttMessage(std::string message) {
    Serial.print("Sending mqtt: ");
    Serial.println(message.c_str());
    mqttClient.beginMessage(MQTT_PUB_TOPIC);
    mqttClient.print(message.c_str());
    mqttClient.endMessage();
}

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        std::string trionesName ("Triones"); // The name of the thing we're looking for
        if (advertisedDevice->haveName()) {
            std::string deviceName = advertisedDevice->getName();
            if (deviceName.find(trionesName) != std::string::npos) {
                digitalWrite(LED, !digitalRead(LED));
                std::string addr =  advertisedDevice->getAddress().toString();
                int rssi = advertisedDevice->getRSSI();
                std::string jsonString;
                jsonString += "{\"mac\":\"";
                jsonString += addr;
                jsonString += "\", \"name\":\"";
                jsonString += deviceName;
                jsonString += "\", \"rssi\":";
                jsonString += std::to_string(rssi);
                jsonString += ", \"scanningDevice\",\"";
                jsonString += WiFi.localIP().toString().c_str();
                jsonString += "\"}";
                sendMqttMessage(jsonString);
            }
        }
    }
};

void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
    // The protocol for my devices looks like 0x66,0x4,power,mode,0x20,speed,red,green,blue,white,0x3,0x99
    // This is a response to a status update
    // Hex response looks like
    // Off (but red)
    // ['0x66', '0x4', '0x24', '0x41', '0x20', '0x1', '0xff', '0x0', '0x0', '0x0', '0x3', '0x99']
    // Off but blue
    // ['0x66', '0x4', '0x24', '0x41', '0x20', '0x1', '0x0', '0x0', '0xff', '0x0', '0x3', '0x99']
    // On but green
    // ['0x66', '0x4', '0x23', '0x41', '0x20', '0x1', '0x0', '0xff', '0x0', '0x0', '0x3', '0x99']
    // json_status = json.dumps({"mac":self.mac, "power":power, "rgb":rgb, "speed": speed, "mode":mode})
    
        if (length == 12) {
            if ((pData[0] == 0x66) && (pData[11] == 0x99)) {
                digitalWrite(LED, !digitalRead(LED));
                String power_state = (pData[2] == 0x23) ? "true" : "false";
                uint8_t mode  = pData[3];
                uint8_t speed = pData[5];
                uint8_t red   = pData[6];
                uint8_t green = pData[7];
                uint8_t blue  = pData[8];
                std::string addr = pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString();
                char buffer[95];
                sprintf(buffer, "{\"mac\":\"%s\", \"power\":%s, \"rgb\":[%u,%u,%u], \"speed\", %u, \"mode\":%u}", addr.c_str(), power_state, red, green, blue, speed, mode);
                sendMqttMessage(buffer);
            }
        }
}

void mqttCallback(int messageSize) {
    digitalWrite(LED, !digitalRead(LED));
    Serial.println("Received mqtt message");
    String mqttTopic = mqttClient.messageTopic();
    if (mqttTopic == "triones/control") {
        StaticJsonDocument<200> doc;  // Shrink this later?
        uint8_t buffer[messageSize];
        mqttClient.read(buffer, sizeof(buffer));
        deserializeJson(doc, buffer);

        if (doc["action"] == "ping") {
            String jsonStr;
            jsonStr += "{\"mac\":\"";
            jsonStr += WiFi.macAddress();
            jsonStr += "\",\"active\":true}";
            mqttClient.beginMessage(MQTT_PUB_TOPIC);
            mqttClient.print(jsonStr);
            mqttClient.endMessage();
            //return;
        }

        if (doc["action"] == "scan") {
            findTrionesDevices();
            //return;
        }

        else if (doc["action"] == "status") {
            const char* mac = doc["mac"];
            Serial.println("Get status of: ");
            Serial.println(mac);
            NimBLEAddress addr = NimBLEAddress(mac);
            if (connect(addr)) {
                getStatus();
                // If we tear down the connection before the callback has fired
                // we loose the status data, and the buffers fill up on the remote device
                // I tried to explicity read the chr, but that didn't work.  This does
                // work though.  ?  
                delay(500);
            } else {
                sendMqttMessage("{\"state\":false}");
            }
        }

        else if (doc["action"] == "set") {
            const char* mac = doc["mac"];
            NimBLEAddress addr = NimBLEAddress(mac);
            if (connect(addr)) {
                if (!doc["power"].isNull()) {
                    if (doc["power"] == true) {
                        Serial.println("Turning on");
                        turnOn();
                    }
                }

                if (doc["rgb"]) {
                    setRGB(doc["rgb"][0], doc["rgb"][1], doc["rgb"][2], doc["percentage"]);
                }

                if (doc["mode"]) {
                    if (!doc["speed"].isNull()){
                        setMode(doc["mode"], doc["speed"]);
                    } else {
                        setMode(doc["mode"], 127);
                    }
                }
                if (!doc["power"].isNull()) {
                    // Power off should be the last thing we do so
                    // that all the other settings can be applied
                    if (doc["power"] == false) {
                        Serial.println("Turning off");
                        if (turnOff()) {
                            Serial.println("Turn off OK");
                        };
                    }
                }
                disconnect();
            } else {
                Serial.print("Failed to connect to Triones device: ");
                Serial.println(mac);
                sendMqttMessage("{\"state\":false}");
            }
        }
    disconnect();
    }
}

void disconnect(){
    digitalWrite(LED, !digitalRead(LED));
    Serial.println("Here");
    NimBLEDevice::deleteClient(pClient);
    pClient = nullptr;
    delay(750); // Prevent next connection happening too soon?
}

bool connect(NimBLEAddress bleAddr){
    digitalWrite(LED, !digitalRead(LED));
    pClient = NimBLEDevice::createClient();
    pClient->setConnectTimeout(10); // seconds
    if (!pClient->connect(bleAddr)) {
        Serial.println("Failed to connect in connect function");
    } else {
        // This should be the happy path
        if (pClient->isConnected()) {
            Serial.print("Connected correctly.  RSSI: ");
            Serial.println(pClient->getRssi());
            digitalWrite(LED, !digitalRead(LED));
            //pClient->setConnectionParams(12,12,0,51); // Do we really need this?  Seems to work without it.
            return true;
        }
    }
    NimBLEDevice::deleteClient(pClient);
    pClient=nullptr;
    return false;
}

bool writeData(const uint8_t * payload, size_t length){
    digitalWrite(LED, !digitalRead(LED));
    if (pClient->isConnected()) {     
        nSvc = pClient->getService(NOTIFY_SERVICE);
        if (nSvc) {
            nChr = nSvc->getCharacteristic(NOTIFY_CHAR);
            if (nChr->canNotify()) {
                nChr->subscribe(true, notifyCB);
            }
        } else {
            Serial.println("Failed to subscribe to notify.");
        }
        pSvc = pClient->getService(MAIN_SERVICE);
        if (pSvc) {
            pChr = pSvc->getCharacteristic(WRITE_CHAR);
            if (pChr->canWrite()){
                pChr->writeValue(payload, length);
                digitalWrite(LED, !digitalRead(LED));
                delay(500); // Give it time to respond
                return true;
            } else Serial.println("WRITE: Couldnt write");
        } else {
            Serial.println("Failed to write data.");
        }
    }
    digitalWrite(LED, !digitalRead(LED));    
    return false;
}

bool turnOn(){
    digitalWrite(LED, !digitalRead(LED));
    uint8_t payload[3]= {0xCC, 0x23, 0x33};
    if (writeData(payload, 3)) {
        return true;
    } else {
        sendMqttMessage("{\"state\":false}");
    }
    return false;
}

bool turnOff(){
    digitalWrite(LED, !digitalRead(LED));
    uint8_t payload[3] = {0xCC, 0x24, 0x33};
    if (writeData(payload,3)){
        return true;
    } else {
        sendMqttMessage("{\"state\":false}");
    }
    return false;
}

bool setRGB(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness=100) {
    digitalWrite(LED, !digitalRead(LED));
    if (brightness == 0) {
        return turnOff();
    } else {
        // Not sure if we need to do this or not.  Test it.
        turnOn();
    }
    uint8_t rgb[7] = {0x56, 00, 00, 00, 00, 0xF0, 0xAA};
    float scale_factor = brightness / 100.0;
    rgb[1] = red * scale_factor;
    rgb[2] = green * scale_factor;
    rgb[3] = blue * scale_factor;
    Serial.println("Set RGB: ");
    Serial.println(rgb[1]);
    Serial.println(rgb[2]);
    Serial.println(rgb[3]);
    if (writeData(rgb, 7)) {
        return true;
    } else {
        writeData(rgb, 7);
        return false;
    }
}

bool setMode(uint8_t mode, uint8_t speed=127){
    digitalWrite(LED, !digitalRead(LED));
    // # 37 : 0x25: Seven color cross fade
    // # 38 : 0x26: Red gradual change
    // # 39 : 0x27: Green gradual change
    // # 40 : 0x28: Blue gradual change
    // # 41 : 0x29: Yellow gradual change
    // # 42 : 0x2A: Cyan gradual change
    // # 43 : 0x2B: Purple gradual change
    // # 44 : 0x2C: White gradual change
    // # 45 : 0x2D: Red, Green cross fade
    // # 46 : 0x2E: Red blue cross fade
    // # 47 : 0x2F: Green blue cross fade
    // # 48 : 0x30: Seven color strobe flash
    // # 49 : 0x31: Red strobe flash
    // # 50 : 0x32: Green strobe flash
    // # 51 : 0x33: Blue strobe flash
    // # 52 : 0x34: Yellow strobe flash
    // # 53 : 0x35: Cyan strobe flash
    // # 54 : 0x36: Purple strobe flash
    // # 55 : 0x37: White strobe flash
    // # 56 : 0x38: Seven color jumping change
    // # 65 : 0x41: Looks like this might be solid colour?
    //
    // Speed - 1 = fast --> 255 = slow

    if ((mode >= 0x25) && (mode <= 0x38)) {
        uint8_t mode_payload[4] = {0xBB, 0x27, 0x7F, 0x44};
        mode_payload[1] = mode;
        mode_payload[2] = speed;
        return writeData(mode_payload, 4);
    } else return false;
}

bool getStatus(){
    digitalWrite(LED, !digitalRead(LED));
    uint8_t payload[3] = {0xEF, 0x01, 0x77};
    if (writeData(payload, 3)) {
        return true;
    } else {
        Serial.println("Failed request status from device");
        sendMqttMessage("\"state\":false}");
        return false;
    }
}

bool findTrionesDevices(){
    digitalWrite(LED, !digitalRead(LED));
    NimBLEScan* pBLEScan =NimBLEDevice::getScan();
    if (!pBLEScan) {
        return false;
    }
    if (pBLEScan->isScanning()) {
        // Somehow we're already scanning
        return false;
    }
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    Serial.println("Doing scan for 20 seconds... this blocks");
    pBLEScan->start(30,false);//, nullptr, false);
    pBLEScan->stop();
    delay(500); // Give time for the callbacks to finish up before this all goes out of scope
    pBLEScan->start(30,false);//, nullptr, false);
    pBLEScan->stop();
    delay(500);
    Serial.println("Done scanning");
    sendMqttMessage("{\"state\":true,\"status\":\"Scan complete\"}");
    return true;
}


void setup (){
    Serial.begin(115200);

    // Setup Nimble
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::init("");
    
    // Set up Wifi
    Serial.println("Setting up Wifi...");
    WiFi.mode(WIFI_STA);
    WiFi.hostname(HOSTNAME);
    WiFi.begin(STASSID, STAPSK);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Couldn't connect to Wifi! Rebooting...");
        delay(5000);
        ESP.restart();
    }
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(HOSTNAME);

    //Set up OTA
    ArduinoOTA.setHostname(HOSTNAME);
    ArduinoOTA.begin();

    // Setup MQTT
    if (mqttClient.connect(broker, port)) {
        mqttClient.subscribe(MQTT_SUB_TOPIC);
        mqttClient.onMessage(mqttCallback);
    } else {
        Serial.println("MQTT connection failed!");
    }
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */

    pinMode(LED, OUTPUT);
    sendMqttMessage("BLE Relay Ready"); // TODO:  Replace this with a proper JSON message including the MAC address
}


void loop (){
    mqttClient.poll();
    ArduinoOTA.handle();
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 1000) {
        previousMillis = millis();
        digitalWrite(LED, !digitalRead(LED));
    }
}

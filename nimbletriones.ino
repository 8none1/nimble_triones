#include <NimBLEDevice.h>
#include <WiFi.h>
//#include <WiFiUdp.h>
//#include <ArduinoOTA.h>
#include <ArduinoMqttClient.h>
#include <ArduinoJson.h>
#include <string>
#include "wificreds.h"


// Define your WiFi settings in wificreds.h

#ifndef HOSTNAME
  #define HOSTNAME "blegateway"
#endif

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
                std::string addr =  advertisedDevice->getAddress().toString();
                int rssi = advertisedDevice->getRSSI();
                std::string jsonString;
                jsonString += "{\"mac\":\"";
                jsonString += addr;
                jsonString += "\", \"name\":\"";
                jsonString += deviceName;
                jsonString += "\", \"rssi\":";
                jsonString += std::to_string(rssi);
                jsonString += ", \"scanHost\",\"";
                jsonString += WiFi.localIP().toString().c_str();
                jsonString += "\"}";
                Serial.println(jsonString.c_str());
                mqttClient.beginMessage(MQTT_PUB_TOPIC);
                mqttClient.print(jsonString.c_str());
                mqttClient.endMessage();
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
            // Some rudimentary checks that this is what we're looking for
            if ((pData[0] == 0x66) && (pData[11] == 0x99)) {
                String power_state = (pData[2] == 0x23) ? "true" : "false";
                uint8_t mode  = pData[3];
                uint8_t speed = pData[5];
                uint8_t red   = pData[6];
                uint8_t green = pData[7];
                uint8_t blue  = pData[8];
                std::string addr = pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString();
                char buffer[95];
                sprintf(buffer, "{\"mac\":\"%s\", \"power\":%s, \"rgb\":[%u,%u,%u], \"speed\", %u, \"mode\":%u}", addr.c_str(), power_state, red, green, blue, speed, mode);
                mqttClient.beginMessage(MQTT_PUB_TOPIC);
                mqttClient.print(buffer);
                mqttClient.endMessage();
            }
        }
}

void mqttCallback(int messageSize) {
    // I was trying to avoid doing all the work in one callback function
    // but here we are.  I will trying a break this out later, once things work
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
            return;
        }

        if (doc["action"] == "scan") {
            findTrionesDevices();
            return;
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
                pClient->disconnect();
            }
            return;
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

                pClient->disconnect();
            } else {
                Serial.print("Failed to connect to Triones device: ");
                Serial.println(mac);
                sendMqttMessage("{\"state\":false}");
            }
        }
    }
}


bool connect(NimBLEAddress bleAddr){
    pClient = NimBLEDevice::createClient();
    pClient->setConnectTimeout(5); // seconds
    if (!pClient->connect(bleAddr)) {
        Serial.println("Failed to connect");
    } else {
        // This should be the happy path
        if (pClient->isConnected()) {
            Serial.print("Connected correctly.  RSSI: ");
            Serial.println(pClient->getRssi());
            //pClient->setConnectionParams(12,12,0,51); // Do we really need this?  Seems to work without it.
            return true;
        }
    }
    NimBLEDevice::deleteClient(pClient);
    return false;
}

bool setupSvcAndChr(){
    if (pClient->isConnected()) {
        pSvc = pClient->getService(MAIN_SERVICE);
        nSvc = pClient->getService(NOTIFY_SERVICE);
        if (nSvc) {
            nChr = nSvc->getCharacteristic(NOTIFY_CHAR);
            if (nChr->canNotify()) {
                if(!nChr->subscribe(true, notifyCB)) {
                    Serial.println("Characteristic doesn't support notification");
                    Serial.println("Although this isn't fatal, it's not good.");
                } else Serial.println("Subscribed to notifications!");
                // this is clunky.  TODO: rework flow.
            }
        } else Serial.println("Couldn't connect to notification service.");
        
        // now do the same for the main service
        if (pSvc) {
            pChr = pSvc->getCharacteristic(WRITE_CHAR);
            if (pChr) {
                if (pChr->canWrite()) {
                    Serial.println("Connected to the write characteristic!");
                    // Believe it or not, if we got here, everything should be good
                    return true;
                } else Serial.println("Can't write to the main characteristic.");
            } else Serial.println("Couldnt connect to main characteristic.");
        } else Serial.println("Couldn't connect to main write service.");

    } else Serial.println("Client wasn't connected, couldn't do anything.");

    // This whole thing is spaghetti.  At least we're failing safe.
    return false;
}

bool writeData(const uint8_t * payload, size_t length){
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
                return true;
            } else Serial.println("WRITE: Couldnt write");
        } else {
            Serial.println("Failed to write data.");
        }
    }
    
    return false;
}

bool turnOn(){
    uint8_t payload[3]= {0xCC, 0x23, 0x33};
    if (writeData(payload, 3)) {
        return true;
    } else {
        sendMqttMessage("{\"state\":false}");
    }
    return false;
}

bool turnOff(){
    uint8_t payload[3] = {0xCC, 0x24, 0x33};
    if (writeData(payload,3)){
        return true;
    } else {
        sendMqttMessage("{\"state\":false}");
        return false;
    }
}

bool setRGB(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness) {
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
    uint8_t payload[3] = {0xEF, 0x01, 0x77};
    if (writeData(payload, 3)) {
        return true;
    } else {
        sendMqttMessage("Failed request status from device");
        return false;
    }
}

bool findTrionesDevices(){
    NimBLEScan* pBLEScan =NimBLEDevice::getScan();
    if (!pBLEScan) {
        return false;
    }
    if (pBLEScan->isScanning()) {
        // Somehow we're already scanning
        return false;
    }
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    //pBLEScan->setActiveScan(false);
    //pBLEScan->setInterval(97); // How often the scan occurs / switches channels; in milliseconds,
    //pBLEScan->setWindow(37);  // How long to scan during the interval; in milliseconds.
    // I'm not sure that this works the way I thought it did.  I wonder if a pointer to
    // the result is getting passed to the callback, and then getting removed because
    // either the scan goes out of scope, or a new device is found and then it's removed.
    // It seems that would be bad way to work, so maybe it doesn't, but.. ?
    // Until I can get the callback working reliably then I'm going to comment this out.
    //pBLEScan->setMaxResults(0); // do not store the scan results, use callback only.
    Serial.println("Doing scan for 20 seconds... this blocks");
    pBLEScan->start(20,false);//, nullptr, false);
    pBLEScan->stop();
    delay(1000); // Give time for the callbacks to finish up before this all goes out of scope
    Serial.println("Done scanning");
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

    // Set up OTA
    // ArduinoOTA.setHostname(HOSTNAME);
    // ArduinoOTA
    //     .onStart([]() {
    //     String type;
    //     if (ArduinoOTA.getCommand() == U_FLASH)
    //         type = "sketch";
    //     else // U_SPIFFS
    //         type = "filesystem";

    //     // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
    //     Serial.println("Start updating " + type);
    //     })
    //     .onEnd([]() {
    //     Serial.println("\nEnd");
    //     })
    //     .onProgress([](unsigned int progress, unsigned int total) {
    //     Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    //     })
    //     .onError([](ota_error_t error) {
    //     Serial.printf("Error[%u]: ", error);
    //     if (error == OTA_AUTH_ERROR) Serial.println("Auth Failed");
    //     else if (error == OTA_BEGIN_ERROR) Serial.println("Begin Failed");
    //     else if (error == OTA_CONNECT_ERROR) Serial.println("Connect Failed");
    //     else if (error == OTA_RECEIVE_ERROR) Serial.println("Receive Failed");
    //     else if (error == OTA_END_ERROR) Serial.println("End Failed");
    //     });
    // ArduinoOTA.begin();

    // Setup MQTT
    if (mqttClient.connect(broker, port)) {
        mqttClient.subscribe(MQTT_SUB_TOPIC);
        mqttClient.onMessage(mqttCallback);
    } else {
        Serial.println("MQTT connection failed!");
    }
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
}


void loop (){
    // TODO next:  Add scan and status support to call functions on mqtt receive 
    
    mqttClient.poll();
    //int messageSize = mqttClient.parseMessage();
     
    //Serial.println("Starting loop");
    //NimBLEAddress addr = NimBLEAddress("78:82:a4:00:05:1e");
    //bool a = connect(addr);
    
    //} else Serial.println("MAIN LOOP: Failed to connect.");
    //findTrionesDevices();
    //Serial.println("Waiting 10s until next loop.");
    //delay(10000);
    /////////////////////////////////////
    // Once an hour announce all the triones devices we can see.
    // Process incoming MQTT requests
    // See what happens when things are left to run for a while.  Does it
    // lock up the sockets?  Does it just start crashing all the time?
    
}

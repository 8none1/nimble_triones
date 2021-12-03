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
bool findTrionesDevices();
bool do_write(NimBLEAddress bleAddr, const uint8_t *payload, size_t len);
unsigned long previousMillis = 0;

void sendMqttMessage(std::string message) {
    Serial.print("Sending mqtt: ");
    Serial.println(message.c_str());
    mqttClient.beginMessage(MQTT_PUB_TOPIC);
    mqttClient.print(message.c_str());
    mqttClient.endMessage();
}

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient * pClient) {
        // Let the lights tell us what they want to do.
        // It's possible that tweaking this would make it more reliable though?
        //pClient->updateConnParams(120,120,120,60);
        digitalWrite(LED, !digitalRead(LED));
    };

    void onDisconnect(NimBLEClient * pClient) {
        //Serial.println("In disconnect callback");
        digitalWrite(LED, !digitalRead(LED));
    };

    bool onConnParamsUpdateRequest(NimBLEClient * pClient, const ble_gap_upd_params * params) {
        Serial.println("Doing some connection params stuff");
        // https://github.com/h2zero/NimBLE-Arduino/issues/140#issuecomment-720100023
        /*
            The first 2 parameters are multiples of 0.625ms, they dictate how often a packet is sent to maintain the connection.
            The third lets the peripheral skip the amount of connection packets you specify, usually 0.
            The fourth is a multiple of 10ms that dictates how long to wait before considering the connection dropped if no packet is received, I usually keep this around 100-200 (1-2 seconds).
            The last 2 parameters I would suggest not specifying, they are the scan parameters used when calling connect() as it will scan to find the device you're connecting to, the default values work well.
        */
        // if(params->itvl_min < 24) { /** 1.25ms units */
        //     return false;
        // } else if(params->itvl_max > 40) { /** 1.25ms units */
        //     return false;
        // } else if(params->latency > 2) { /** Number of intervals allowed to skip */
        //     return false;
        // } else if(params->supervision_timeout > 100) { /** 10ms units */
        //     return false;
        // }

        // We're just going to accept what they ask for, anything to try and get it to work
        // Update: this might be a bad idea, because we have to trust that what the devices are asking for is actually reasonable
        // and that's probably not a safe assumption.  So, once the call back stuff is fixed, think about this some more...
        // some initial testing says letting the lights do what they want makes them a bit happier and easier to talk to.
        digitalWrite(LED, !digitalRead(LED));
        return true;        
    };

    uint32_t onPasskeyRequest() {
        // Unused
        Serial.println("In on passkey request");
        return 1234;
    }
};

static ClientCallbacks clientCB;

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        Serial.println("In the onresult callback");
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
                digitalWrite(LED, !digitalRead(LED));
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
    Serial.println("In notify callback");
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
                digitalWrite(LED, !digitalRead(LED));
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
        std::string action = doc["action"];

        if (action == "ping") {
            String jsonStr;
            jsonStr += "{\"mac\":\"";
            jsonStr += WiFi.macAddress();
            jsonStr += "\",\"active\":true}";
            mqttClient.beginMessage(MQTT_PUB_TOPIC);
            mqttClient.print(jsonStr);
            mqttClient.endMessage();
            digitalWrite(LED, !digitalRead(LED));
            return;
        }

        if (action == "scan") {
            findTrionesDevices();
            digitalWrite(LED, !digitalRead(LED));
            return;
        }

        else if (action == "status") {
            digitalWrite(LED, !digitalRead(LED));
            const char* mac = doc["mac"];
            //Serial.println("Get status of: ");
            //Serial.println(mac);
            NimBLEAddress addr = NimBLEAddress(mac);
            uint8_t payload[] = {0xEF, 0x01, 0x77};
            if (do_write(addr, payload, sizeof(payload))) {
                Serial.println("Status request successful");
                return;
            } else {
                Serial.println("Status failed");
                sendMqttMessage("{\"state\":false}");
                return;
            }
        }
        else if (action == "set") {
            const char* mac = doc["mac"];
            NimBLEAddress addr = NimBLEAddress(mac);

            if (!doc["power"].isNull()) {
                if (doc["power"] == true) {
                    digitalWrite(LED, !digitalRead(LED));
                    Serial.println("Turning on");
                    uint8_t payload[] = {0xCC, 0x23, 0x33};
                    if (do_write(addr, payload, sizeof(payload))) {
                        Serial.println("Powered on device");
                    } else {
                        Serial.println("Power on failed");
                    }
                return;
                }
            }

            if (doc["rgb"]) {
                uint8_t payload[7] = {0x56, 00, 00, 00, 00, 0xF0, 0xAA};
                uint8_t percentage;
                if (doc["percentage"] == nullptr) {
                    percentage = 100;
                } else {
                    percentage = doc["percentage"];
                }
                float scale_factor = percentage / 100.0;
                payload[1] = (int) doc["rgb"][0] * scale_factor;
                payload[2] = (int) doc["rgb"][1] * scale_factor;
                payload[3] = (int) doc["rgb"][2] * scale_factor;
                do_write(addr, payload, sizeof(payload));
                digitalWrite(LED, !digitalRead(LED));
                return;
            }

            if (doc["mode"]) {
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
                // Speed : 1 = fast --> 255 = slow
                uint8_t mode = doc["mode"];
                uint8_t speed;
                if (doc["speed"].isNull()) {
                    speed = 127;
                } else {
                    speed = doc["speed"];
                };

                if ((mode >= 0x25) && (mode <= 0x38)) {
                    uint8_t payload[4] = {0xBB, 0x27, 0x7F, 0x44};
                    payload[1] = mode;
                    payload[2] = speed;
                    do_write(addr, payload, sizeof(payload));
                    digitalWrite(LED, !digitalRead(LED));
                };
            };

            if (!doc["power"].isNull()) {
                // Power off should be the last thing we do so
                // that all the other settings can be applied
                if (doc["power"] == false) {
                    uint8_t payload[] = {0xCC, 0x24, 0x33};
                    if (do_write(addr, payload, sizeof(payload))) {
                        digitalWrite(LED, !digitalRead(LED));
                        Serial.println("Power off worked");
                    } else {
                        Serial.println("Power off failed");
                    };
                return;
                };
            };
            
        };
    };
}


bool do_write(NimBLEAddress bleAddr, const uint8_t* payload, size_t length){
    digitalWrite(LED, !digitalRead(LED));
    NimBLEClient * pClient = nullptr;
    if (NimBLEDevice::getClientListSize()) {
        // There are some existing clients available, try and find one to reuse
        pClient = NimBLEDevice::getClientByPeerAddress(bleAddr);
        if (pClient) {
            if (!pClient->connect(bleAddr, false)) {
                Serial.println("Tried to reconnect to known device, but failed");
                // Actually this seems to mean that it's already connected, and you're good
                // to go.  No need to return false?
                //return false;
            } else {
                Serial.println("Correctly reconnected to device");
            }
        } else {
            Serial.println("No existing client, trying an old one");
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }
    // If we get here we are either connected OR we need to try and reuse an old client
    if (!pClient) {
        // There wasn't an old one to reuse, create a new one
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max connections reached.  No more available.");
            return false;
        }
        pClient = NimBLEDevice::createClient();
        Serial.println("Created new client");
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectionParams(12,12,10,100);// This seems to really help, but might be tweaked further?
        pClient->setConnectionParams(7,7,0,200);// This seems to really help, but might be tweaked further?
        pClient->setConnectTimeout(5);

        if (!pClient->connect(bleAddr)) {
            // Created a new client to use, but it failed, so get rid of it
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Created a new client, but still failed to connect. Removed client");
            return false;
        }
    }

    if(!pClient->isConnected()) {
        if (!pClient->connect(bleAddr)) {
            Serial.println("We created a new client and tried to connect, and it was connected, but now its not?");
            return false;
        }
    }

    // If we get here, then we really should be connected by now
    Serial.print("Connected to device: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    // Serial.print("RSSI: ");
    // Serial.println(pClient->getRssi());

    // Do the actual work...
    NimBLERemoteService* pSvc = nullptr;
    NimBLERemoteCharacteristic* pChr = nullptr;
    NimBLERemoteService* nSvc = nullptr;
    NimBLERemoteCharacteristic* nChr = nullptr;

    nSvc = pClient->getService(NOTIFY_SERVICE);

    if (nSvc) {
        Serial.println("nSvc correct");
        nChr = nSvc->getCharacteristic(NOTIFY_CHAR);
        if (nChr->canNotify()) {
            nChr->subscribe(true, &notifyCB);
            //nChr->subscribe(true, notifyCB);
        };
    } else {
        Serial.println("Failed to get nsvc");
        return false;
    }

    if (!pClient) {
        Serial.println("Client has gone away");
        return false;
    }
    pSvc = pClient->getService(MAIN_SERVICE);
    if (pSvc) {
        pChr = pSvc->getCharacteristic(WRITE_CHAR);
        if (pChr->canWrite()){
            if (pChr->writeValue(payload, length, false)) {
                Serial.println("Write complete");
            };
            digitalWrite(LED, !digitalRead(LED));
        } else Serial.println("WRITE: Couldnt write");
    } else {
        Serial.println("Failed to write data.");
    }
    //pClient->disconnect();
    return true;
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
    Serial.println("Doing scan for 30 seconds... this blocks");
    pBLEScan->start(30,false);//, nullptr, false);
    pBLEScan->stop();
    delay(500); // Give time for the callbacks to finish up before this all goes out of scope
    //pBLEScan->start(30,false);//, nullptr, false);
    //pBLEScan->stop();
    //delay(500);
    Serial.println("Done scanning");
    sendMqttMessage("{\"state\":true,\"status\":\"Scan complete\"}");
    return true;
}


void setup (){
    Serial.begin(115200);

    // Setup Nimble
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
    //NimBLEDevice::setMTU(7);  // This is what the lights ask for, so set up front. This made it much more reliable to connect first time.
    
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
    //  After all the effort I went to in order to enable OTA, it seems that OTA is quite chatty on the wifi
    //  and makes BT LE harder.  So disabling it for now.  Maybe it can come back later.  If you need it, then there
    //  only a few lines to uncomment
    //ArduinoOTA.setHostname(HOSTNAME);
    //ArduinoOTA.begin();

    // Setup MQTT
    if (mqttClient.connect(broker, port)) {
        mqttClient.subscribe(MQTT_SUB_TOPIC);
        mqttClient.onMessage(mqttCallback);
    } else {
        Serial.println("MQTT connection failed!");
    }
    pinMode(LED, OUTPUT);
    digitalWrite(LED, !digitalRead(LED));
    sendMqttMessage("BLE Relay Ready"); // TODO:  Replace this with a proper JSON message including the IP address
}


void loop (){
    mqttClient.poll();
    //ArduinoOTA.handle();

    // Flash the light, of course
    // unsigned long currentMillis = millis();
    // if (currentMillis - previousMillis >= 1000) {
    //     previousMillis = millis();
    //     digitalWrite(LED, !digitalRead(LED));
    // }
}

#include "nimbletriones.h"
#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <string>
#include "wificreds.h"
#include <WebServer.h>
#include <algorithm>



// Define your WiFi settings in wificreds.h

#ifndef HOSTNAME
  #define HOSTNAME "blegateway"
#endif
#define LED 2


// Store IP address & MAC globally
IPAddress ipAddr;
byte mac[6];
std::string hostname = HOSTNAME;
unsigned long previousMillis = 0;

const uint8_t MAXDEVICES = 7;
typedef struct {
    std::string macAddr;
    int rssi;
    bool subscribed;
} TrionesDevice;

TrionesDevice localDevices[MAXDEVICES];

// Triones service specifics
const NimBLEUUID NOTIFY_SERVICE ("FFD0");
const NimBLEUUID NOTIFY_CHAR    ("FFD4");
const NimBLEUUID MAIN_SERVICE   ("FFD5");
const NimBLEUUID WRITE_CHAR     ("FFD9");

// WiFi client
WiFiClient wifiClient;

// MQTT client
const char* BROKER = "mqtt"; // mqtt server name
//PubSubClient::setBufferSize(1000);
PubSubClient mqttClient(wifiClient);

std::string mqttStateTopic     = "triones/status/";   // + MAC address of Triones device
std::string mqttControlTopic   = "triones/control/";  // Listen for commands on this topic
std::string mqttBroadcastTopic = "triones/broadcast"; // Inter-ESP32 communication for device discovery
std::string mqttGlobalTopic    = mqttControlTopic  + "global"; // Global control commands

// Web Server
WebServer server(80);

// Forward declarations
// I don't know how to C++
bool findTrionesDevices();
bool do_write(NimBLEAddress bleAddr, const uint8_t *payload, size_t len);

uint8_t findDeviceIndex(std::string macAddr) {
    for (int i = 0; i < MAXDEVICES; i++) {
        if (localDevices[i].macAddr == macAddr) {
            return i;
        }
    }
    return 255;
}

void mqttReconnect(){
    while (!mqttClient.connected()) {
        Serial.println("Reconnecting to MQTT server...");
        if (mqttClient.connect(hostname.c_str())) {
            Serial.println("MQTT connected");
            mqttClient.subscribe(mqttBroadcastTopic.c_str());
            mqttClient.subscribe(mqttGlobalTopic.c_str());
            mqttResubscribeToLocalDevices();
        } else {
            Serial.print("failed, rc=");
            Serial.print(mqttClient.state());
            Serial.println(" try again in 5 seconds");
            delay(5000);
        }
    }
}

void mqttResubscribeToLocalDevices(){
    for (int i = 0; i < MAXDEVICES; i++) {
        if (localDevices[i].macAddr != "" && localDevices[i].macAddr != "FISH" && localDevices[i].rssi < 0) {
            std::string topic = mqttControlTopic + localDevices[i].macAddr;
            if (mqttClient.subscribe(topic.c_str())) {
                Serial.print("Resubscribed to ");
                Serial.println(topic.c_str());
                localDevices[i].subscribed = true;
            } else {
                Serial.print("Failed to resubscribe to ");
                Serial.println(topic.c_str());
                localDevices[i].subscribed = false;
            }
        }
    }
}

void restartESP32() {                
    Serial.println("Restarting ESP32...");
    size_t numClients = NimBLEDevice::getClientListSize();
    if (numClients > 0) {
        std::list<NimBLEClient*> *clientList = NimBLEDevice::getClientList();
        for (auto it = clientList->begin(); it != clientList->end(); it++) {
            if ((*it)->isConnected()) {
                Serial.println((*it)->getPeerAddress().toString().c_str());
                Serial.println("Disconnecting");
                (*it)->disconnect();
            }
        }
    }
    NimBLEDevice::deinit();
    delay(500);
    ESP.restart();
    return;
}

void sendMqttMessage(const JsonDocument& local_doc, const std::string topic) {
    char buffer[256];
    serializeJson(local_doc, buffer);
    mqttClient.publish(topic.c_str(), buffer);
};

void sendMqttMessage(std::string message, std::string topic) {
    mqttClient.publish(topic.c_str(), message.c_str());
}

class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient * pClient) {
        //pClient->updateConnParams(120,120,120,60); // This is more lenient 
        //pClient->setConnectionParams(7,7,0,200);// This is what the lights would like us to use.  It does work, but it doesn't leave much room to manoeuver 
        digitalWrite(LED, !digitalRead(LED));
        NimBLEAddress addr = pClient->getPeerAddress();
        std::string mac = addr.toString();
        Serial.print("Device connected: ");
        Serial.println(mac.c_str());
    };

    void onDisconnect(NimBLEClient * pClient) {
        digitalWrite(LED, !digitalRead(LED));
        StaticJsonDocument<90> tempDoc;
        tempDoc["controller"] = hostname;
        tempDoc["disconnected"] = true;
        std::string tmac = pClient->getPeerAddress().toString();
        sendMqttMessage(tempDoc, mqttStateTopic + tmac);
        Serial.print("Disconnected from device: ");
        const char* mac = tempDoc["mac"];
        Serial.println(mac);
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
        //  if(params->itvl_min < 24) { /** 1.25ms units */
        //      return false;
        //  } else if(params->itvl_max > 40) { /** 1.25ms units */
        //      return false;
        //  };
        // } else if(params->latency > 2) { /** Number of intervals allowed to skip */
        //     return false;
        // } else if(params->supervision_timeout > 100) { /** 10ms units */
        //     return false;
        // }

        // We're just going to accept what they ask for, anything to try and get it to work.
        // Update: this might be a bad idea, because we have to trust that what the devices are asking for is actually reasonable
        // and that's probably not a safe assumption.  So, once the call back stuff is fixed, think about this some more...
        // Thought about is some more.. some initial testing says letting the lights do what they want makes them a bit happier and easier to talk to. So leaving this like this for now.
        digitalWrite(LED, !digitalRead(LED));
        return true;        
    };
};
static ClientCallbacks clientCB;

void sendDeviceTable() {
    StaticJsonDocument<512> tableDoc;
    tableDoc["scanningHost"] = ipAddr.toString();
    tableDoc["ctl"] = hostname;
    tableDoc["type"] = "deviceTable";
    JsonArray devices = tableDoc.createNestedArray("devices");
    Serial.println("Here's what's in the table:");
    for (int i=0; i < MAXDEVICES; i++) {
        std::string mac = localDevices[i].macAddr.c_str();
        if (mac != "FISH") {
            Serial.print(i);
            Serial.print(" : ");
            Serial.print(localDevices[i].macAddr.c_str());
            Serial.print(" : ");
            Serial.print(localDevices[i].rssi);
            Serial.print("  :  Mqtt subscribed? : ");
            Serial.println(localDevices[i].subscribed ? "True" : "False");
            JsonObject device = devices.createNestedObject();
            device["mac"]  = localDevices[i].macAddr;
            device["rssi"] = localDevices[i].rssi;
        }
    };
    sendMqttMessage(tableDoc, mqttStateTopic+hostname); // the mqtt client has a payload limit of 256 bytes, if you're running more than 5 Triones devices this will be a problem.
}



void onScanComplete(NimBLEScanResults results) {
    Serial.println("Done scanning");
    sendDeviceTable();
};

class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
        digitalWrite(LED, !digitalRead(LED));
        // This is only for devices found with a scan on this ESP32
        // not devices seen over MQTT from other ESP32s

        std::string trionesName ("Triones");
        if (advertisedDevice->haveName()) {
            std::string deviceName = advertisedDevice->getName();
            if (deviceName.find(trionesName) != std::string::npos) {
                digitalWrite(LED, !digitalRead(LED));
                StaticJsonDocument<164> doc; // per https://arduinojson.org/v6/assistant/
                doc["mac"]  = advertisedDevice->getAddress().toString();
                doc["name"] = advertisedDevice->getName();
                doc["rssi"] = advertisedDevice->getRSSI();
                doc["scanningDevice"] = WiFi.localIP().toString(); // Used to identify messages we sent
                doc["ctl"] = hostname.c_str();

                TrionesDevice discoveredDevice;

                discoveredDevice.macAddr = doc["mac"].as<std::string>();
                discoveredDevice.rssi = doc["rssi"];
                for (int i=0; i < MAXDEVICES; i++) {
                    // BUG: It would be possible to have more than one entry in the table with the same mac.  Need to clean that up.
                    std::string mac = localDevices[i].macAddr;
                    int rssi = localDevices[i].rssi;
                    if (discoveredDevice.macAddr == mac) {
                        // We've already got this in our list
                        // but it might have come from somewhere else.  Who wins?
                        // Serial.println("Checking dupe");
                        int realRssi = (rssi > 0 ? -rssi : rssi); // invert realRssi if it's remote
                        if (discoveredDevice.rssi > realRssi) { // TODO:  There seems to be a logic problem here somewhere where its not sending advertising when I think it should be.  We're only sending scantopic messages when the device we found was better?
                            // The device we found in this scan has better RSSI
                            localDevices[i].rssi = discoveredDevice.rssi;
                            // Serial.print("Updated existing device with better rssi from scan: ");
                            // Serial.println(discoveredDevice.macAddr.c_str());
                            // Send a message saying that we found this device with the new better RSSI so that others can update their tables
                            if ( !localDevices[i].subscribed) {
                                // Not subscribed to this somehow
                                std::string topic = mqttControlTopic + localDevices[i].macAddr;
                                if (mqttClient.subscribe(topic.c_str())) {
                                    localDevices[i].subscribed = true;
                                    Serial.print("Subscribed to ");
                                    Serial.println(topic.c_str());
                                    sendMqttMessage(doc, mqttBroadcastTopic);
                                } else {
                                    Serial.print("ERROR: Failed to subscribe to ");
                                    Serial.println(topic.c_str());
                                    // TODO: We should probably remove this device from our list
                                    // since we didn't manage to set up a subscription for
                                    // it.
                                }
                            }
                        };
                        return;
                    }
                };

                // If we are here, then we didnt have an existing entry for this mac, so find a slot for it in the table, and add it.
                for (int i=0; i < MAXDEVICES; i++) {
                    std::string mac = localDevices[i].macAddr;
                    if (mac == "FISH") {
                        // spare slot, let's write in here
                        //std::string newMac = discoveredDevice.macAddr;
                        localDevices[i] = discoveredDevice;
                        Serial.print("Added new device: ");
                        Serial.println(localDevices[i].macAddr.c_str());
                        std::string topic = mqttControlTopic + localDevices[i].macAddr;
                        // TODO: move this out in to a function
                        if (mqttClient.subscribe(topic.c_str())) {
                            localDevices[i].subscribed = true;
                            Serial.print("Subscribed to ");
                            Serial.println(topic.c_str());
                            sendMqttMessage(doc, mqttBroadcastTopic);
                        } else {
                            Serial.print("ERROR: Failed to subscribe to ");
                            Serial.println(topic.c_str());
                        }
                        //std::string scanTopic = mqttStateTopic;
                        //scanTopic += "/scan";
                        //sendMqttMessage(doc,mqttBroadcastTopic);
                        return;
                    };
                };
                Serial.println("Did have room in the table for this device");
            };
        };
    };
};

void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
    if (length == 12) {
        if ((pData[0] == 0x66) && (pData[11] == 0x99)) { // static
            digitalWrite(LED, !digitalRead(LED));
            //const std::string power_state = (pData[2] == 0x23) ? "true" : "false"; // 0x23 is on, 0x24 is off
            const bool power_state = (pData[2] == 0x23) ? true : false; // 0x23 is on, 0x24 is off
            const uint8_t mode  = pData[3]; // See mode section for full list
            uint8_t speed = pData[5]; // 1 fast, 31 slow
            speed = 32 - speed;
            speed = speed  * 100 / 31; // convert to 0-100
            speed = (speed > 100 ? 100 : speed);
            speed = (speed < 1  ? 1  : speed);
            const uint8_t red   = pData[6]; // R
            const uint8_t green = pData[7]; // G
            const uint8_t blue  = pData[8]; // B
            const std::string tmac = pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString();
            const size_t capacity = JSON_ARRAY_SIZE(3) + JSON_OBJECT_SIZE(7) + 120;
            StaticJsonDocument<capacity> statusDoc;
            statusDoc["type"]  = "status report";
            statusDoc["ctl"]   = hostname;
            statusDoc["mac"]   = tmac;
            statusDoc["power"] = power_state;
            statusDoc["mode"]  = mode;
            statusDoc["speed"] = speed;
            JsonArray rgb = statusDoc.createNestedArray("rgb");
            rgb.add(red);
            rgb.add(green);
            rgb.add(blue);
            sendMqttMessage(statusDoc, mqttStateTopic+tmac);
            digitalWrite(LED, !digitalRead(LED));
        }
    }
}

void mqttCallback(char* topic, const byte* payload, unsigned int length) {
    digitalWrite(LED, !digitalRead(LED));
    const std::string mt = topic;
    StaticJsonDocument<200> doc;  // Shrink this later? 200 seems OK though.  100 caused stack overflows
    DeserializationError err = deserializeJson(doc, payload, length);

    if (err) {
        Serial.println(F("Couldn't understand the JSON object. Giving up"));
        return;
    }

    if (mt == mqttBroadcastTopic) {
        // A scan result was received
        std::string myIpAddr = ipAddr.toString().c_str();
        if (doc["scanningDevice"] == myIpAddr) {
            //Serial.println("Received scan result, but it was one that I sent");
            return;
        } else {
            const std::string remoteMac = doc["mac"];
            const int8_t remoteRssi     = doc["rssi"];
            const std::string ctl       = doc["ctl"];
            const std::string name      = doc["name"];
            Serial.print("Triones device found by remote worker: ");
            Serial.print(remoteMac.c_str());
            Serial.print(" ");
            Serial.print("RSSI: ");
            Serial.println(remoteRssi);
            Serial.println("Looking in my device table.");
            for (int i=0; i < MAXDEVICES; i++) {
                TrionesDevice localDevice = localDevices[i];
                std::string localMac = localDevice.macAddr;
                if (localMac == remoteMac) {
                    Serial.println("Found the same device in our list");
                    const int8_t localRssi = localDevice.rssi;
                    Serial.print("Local RSSI: ");
                    Serial.println(localRssi);

                    if (localRssi <= remoteRssi) {
                        // Our device has worse rssi, so replace it with the remote one.
                        // We indicate remote by inverting the rssi so it's above zero
                        Serial.print("Other end is better. Remote RSSI: ");
                        Serial.println(remoteRssi);
                        localDevice.rssi = -remoteRssi;
                        Serial.print("Inverted RSSI: ");
                        Serial.println(localDevice.rssi);
                        if (localDevice.subscribed) {
                            std::string topic = mqttControlTopic + localMac;
                            std::string message = "Unsubscribing from " + topic;
                            Serial.println(message.c_str());
                            mqttClient.unsubscribe(topic.c_str());
                        }
                        localDevice.subscribed = false;
                        localDevices[i] = localDevice;
                        return;
                    } else if (localRssi > remoteRssi && localRssi < 0) {
                        // localRssi > remoteRssi
                        Serial.println("We know about this device already, but our rssi is better. Sending an update to everyone");
                        // mac matches but new remote rssi is worse
                        // So remote doesn't know that we are in a better position to service this device.  Send a scan result to force others to refresh
                        // TODO:  If we really have this device in our local table, are we actually subscribed to it? If not, then perhaps we don't
                        // send this broadcast message at all?

                        StaticJsonDocument<200> scanDoc;
                        scanDoc["scanningDevice"] = myIpAddr;
                        scanDoc["mac"] = localMac;
                        scanDoc["rssi"] = localDevice.rssi;
                        scanDoc["ctl"] = hostname;
                        scanDoc["name"] = name;
                        Serial.println("Sending a new fake scan result to everyone to refresh their device tables");
                        sendMqttMessage(scanDoc, mqttBroadcastTopic);
                        return;
                    } else {
                        return;
                    };
                };
            };
            // If we got here, then we ran through the table without finding a match, so we need to add this device as new
            // The device is being handled by a remote controller though, so we do not subscribe to it
            for (int i=0; i < MAXDEVICES; i++) {
                std::string m = localDevices[i].macAddr;
                if (m == "FISH") {
                    TrionesDevice device;
                    device.macAddr = remoteMac;
                    device.rssi = -remoteRssi; // negate to represent remote
                    device.subscribed = false;
                    localDevices[i] = device;
                    Serial.println("Found free slot for remote and added it");
                    return;
                };
            };
            // If we get here then I think we couldn't find a slot
            Serial.println("Couldn't find a slot for remote device");
        };
        return;
    };

    // If no action was supplied we can bug out early since we won't be able
    // to do anything with the message
    std::string action = doc["action"];
    if ( doc["action"].isNull() ) {
        Serial.println("No action supplied.");
        return;
    };

    // Deal with global actions
    if (mt == mqttGlobalTopic) {
        if (action == "ping") {
            StaticJsonDocument<90> ping_doc;
            ping_doc["ipAddr"] = ipAddr.toString();
            ping_doc["active"] = true;
            ping_doc["ctl"] = hostname.c_str();
            sendMqttMessage(ping_doc, mqttStateTopic + hostname);
            digitalWrite(LED, !digitalRead(LED));
            return;
        } else if (action == "scan") {
            findTrionesDevices();
            digitalWrite(LED, !digitalRead(LED));
            return;
        } else if (action == "devicetable") {
            Serial.println("Show device table");
            sendDeviceTable();
            digitalWrite(LED, !digitalRead(LED));
            return;            
        } else {
            return;
        }
    };

    // Deal with actions directed to a specific Triones device
    std::size_t found = mt.find(mqttControlTopic);
    if (found != std::string::npos) {
        const std::string mac = mt.substr(mt.find_last_of('/') + 1);
        std::string::difference_type i = std::count(mac.begin(), mac.end(), ':');
        if (i != 5) {
            Serial.println("Invalid MAC address in topic");
            return;
        };

        if (action == "restart") {
            restartESP32();
        };

        if (action == "disconnect") {
            // Disconnect from the device, by stay subscribed to mqtt events about it.
            const char* mac = doc["mac"];
            NimBLEAddress addr = NimBLEAddress(mac);
            NimBLEClient * pClient = nullptr;
            pClient = NimBLEDevice::getClientByPeerAddress(addr);
            if (pClient) {
                if (pClient->isConnected()) {
                    Serial.println("Disconnecting client");
                    pClient->disconnect();
                }
            }
            return;
        };

        if (action == "status") {
            digitalWrite(LED, !digitalRead(LED));
            NimBLEAddress addr = NimBLEAddress(mac);
            uint8_t payload[] = {0xEF, 0x01, 0x77};
            if ( ! do_write(addr, payload, sizeof(payload))) {
                Serial.println("Failed to send status request");
            }
        }

        if (action == "set") {
            NimBLEAddress addr = NimBLEAddress(mac);
            bool powerState = false;
            bool rgbSet = false;
            uint16_t rgbArray[3] = {0};
            if ( ! doc["rgb"].isNull() ) {
                // We need to validate these inputs...
                rgbArray[0] = (doc["rgb"][0] < 256) ? doc["rgb"][0] : 255;
                rgbArray[1] = (doc["rgb"][1] < 256) ? doc["rgb"][1] : 255;
                rgbArray[2] = (doc["rgb"][2] < 256) ? doc["rgb"][2] : 255;
                rgbSet = true;
            };

            uint8_t brightness = 100;
            if ( ! doc["percentage"].isNull() ) {
                brightness = (doc["percentage"] <= 100 ? doc["percentage"] : 100);
                brightness = (brightness > 0 ? brightness : 100); // If brightness is zero, set it to 100.  Brightness zero is not a thing, these lights have a specific power off state rather than zero brightness.
            };
                       
            // Power set
            if ( ! doc["power"].isNull()) {
                digitalWrite(LED, !digitalRead(LED));
                uint8_t power = ( doc["power"] == true ? 0x23 : 0x24 );
                uint8_t payload[] = {0xCC, power, 0x33};
                if (power == 0x23) powerState=true;
                do_write(addr, payload, sizeof(payload));
            };

            // RGB & brightness set
            if (rgbSet) {
                digitalWrite(LED, !digitalRead(LED));
                //                          r   g   b   w  (cheaper lights don't have white leds, so leave as zero)
                uint8_t payload[7] = {0x56, 00, 00, 00, 00, 0xF0, 0xAA};

                if (powerState) {
                    if ( (rgbArray[0] == 0) && (rgbArray[1] == 0) && (rgbArray[2] == 0) ) {
                        // Everything was zero, so presto changeo...
                        rgbArray[0] = 255;
                        rgbArray[1] = 255;
                        rgbArray[2] = 255;
                    }
                };

                // Things are going to get strange in here.  Alexa sends the RGB values tweaked to account for the brightness.
                float scale_factor = float(brightness) / 100.0;
                for (int i=0; i<3; i++) {
                    rgbArray[i] = int(rgbArray[i] * scale_factor);
                    if (rgbArray[i] > 255) rgbArray[i] = 255;
                };

                for (int i=0; i<3; i++) {
                    // Sometimes Alexa can send 256 which overflows a uint8_t
                    if (rgbArray[i] > 255) rgbArray[i] = 255;
                    payload[i+1] = uint8_t(rgbArray[i]);
                };

                do_write(addr, payload, sizeof(payload));
            };
            
            if ( ! doc["mode"].isNull() ) {
                uint8_t mode = 0;
                uint8_t speed = 15;
                digitalWrite(LED, !digitalRead(LED));
                if (doc["mode"].is<char*>()) {
                    String modeStr = doc["mode"];
                    modeStr.toLowerCase();
                    mode = get_mode_id(modeStr.c_str());
                } else if (doc["mode"].is<int>()) {
                    mode = doc["mode"];
                } else {
                    Serial.println("Mode is incorrect");
                }             
                
                if ( ! doc["speed"].isNull() && doc["speed"].is<int>()) {
                    // Speed : 1 = fast --> 31 = slow
                    speed = doc["speed"];
                    speed = 31 - uint8_t ( float(speed) * 0.31) + 1;
                    speed = (speed > 31 ? 31 : speed);
                    speed = (speed < 1  ? 1  : speed);
                };

                if ( ((mode >= 0x25) && (mode <= 0x38)) || ( (mode >= 0x61) && (mode <= 0x62) ) ) {
                    uint8_t payload[4] = {0xBB, 0x27, 0x7F, 0x44};
                    payload[1] = mode;
                    payload[2] = speed;
                    do_write(addr, payload, sizeof(payload));
                    Serial.print("Mode: ");
                    Serial.println(mode);
                    Serial.print("Speed: ");
                    Serial.println(speed);
                };
            };            
        };
    };
};



bool do_write(NimBLEAddress bleAddr, const uint8_t* payload, size_t length){
    digitalWrite(LED, !digitalRead(LED));
    NimBLEClient * pClient = nullptr;

    if (NimBLEDevice::getClientListSize()) {
        pClient = NimBLEDevice::getClientByPeerAddress(bleAddr);
        if (pClient) {
            if ( ! pClient->isConnected() ) {
                if ( ! pClient->connect(bleAddr, false)) {
                    Serial.println("Tried to reconnect to known device, but failed");
                }
            }
        } else {
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }
    
    // If we get here we are either connected OR we need to try and reuse an old client before creating a new client
    if (!pClient) {
        // There wasn't an old one to reuse, create a new one
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Max connections reached.  No more available.");
            return false;
        }
        pClient = NimBLEDevice::createClient();
        pClient->setConnectionParams(7,7,0,200);
        //  Testing shows that this does seem to allow initial connections and reconnections more readily. Even though the tolerances are much lower
        //  it does seem to work.  It will retry at RSSI -90, but will connect usually within the first 5 tries.
        pClient->setClientCallbacks(&clientCB, false);
        pClient->setConnectTimeout(5);
        pClient->connect(bleAddr, false);
    }

    if(!pClient->isConnected()) {
        int i = 0;
        while ( (i <= 10) && (!pClient->isConnected()) ) {
            if (!pClient->connect(bleAddr, false)) {
                Serial.print("Failed to connect. In retry loop: ");
                Serial.println(i);
                digitalWrite(LED, !digitalRead(LED));
                delay(500);
                i++;
            }
        };
        if (!pClient->isConnected()){
            Serial.println("Retrying failed.  Sorry");
            // StaticJsonDocument<128> tempDoc;
            // tempDoc["ipAddr"] = WiFi.localIP().toString();
            // tempDoc["ctl"] = hostname.c_str();
            // tempDoc["status"] = "Connect failed";
            // tempDoc["state"] = false;
            // tempDoc["mac"] = bleAddr.toString();
            // sendMqttMessage(tempDoc, mqttStateTopic+hostname);
            NimBLEDevice::deleteClient(pClient);
            return false;
        }
    }

    Serial.print("Connected to device: ");
    Serial.println(pClient->getPeerAddress().toString().c_str());

    NimBLERemoteService*        pSvc = nullptr;
    NimBLERemoteCharacteristic* pChr = nullptr;
    NimBLERemoteService*        nSvc = nullptr;
    NimBLERemoteCharacteristic* nChr = nullptr;

    nSvc = pClient->getService(NOTIFY_SERVICE);

    if (nSvc) {
        //Serial.println("nSvc correct");
        nChr = nSvc->getCharacteristic(NOTIFY_CHAR);
        if (nChr->canNotify()) {
            nChr->subscribe(true, &notifyCB);
        };
    } else {
        //Serial.println("Failed to get nsvc");
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
    return true;
}

bool findTrionesDevices(){
    digitalWrite(LED, !digitalRead(LED));
    NimBLEScan* pBLEScan = NimBLEDevice::getScan();
    if (!pBLEScan) {
        return false;
    }
    if (pBLEScan->isScanning()) {
        return false;
    }
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    pBLEScan->clearDuplicateCache();
    pBLEScan->clearResults();

    for (int i=0; i < MAXDEVICES; i++) {
        const char* mac = localDevices[i].macAddr.c_str();
        NimBLEAddress addr = NimBLEAddress(mac);
        NimBLEClient * pClient = nullptr;
        pClient = NimBLEDevice::getClientByPeerAddress(addr);
        if (pClient) {
            if (pClient->isConnected()) {
                Serial.println("Device is already connected here, not removing");
            }
        } else {
            // A device is in our table, but we're not connected to it. 
            // TODO:Clear any subscriptions 
            localDevices[i].macAddr = "FISH"; // Could be FISH, could be \0, could be "undefined".  I like FISH.
        }
    };
    unsigned long s = random(10) + 1;
    s = s * 1000;
    Serial.println("Delay: " + s);
    unsigned long currentMillis = millis();
    unsigned long nextMillis = currentMillis + s;
    while ( (millis() < nextMillis) && (millis() >= currentMillis) ) { // If you don't use the = in >= it's so fast is equates to false
        mqttClient.loop();  
    };
    Serial.println("Starting scan...");
    pBLEScan->start(15, *onScanComplete, false); // This was 30 seconds
    return true;
}

void setup (){
    Serial.begin(115200);

    // Setup Nimble
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::setScanDuplicateCacheSize(10);
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // max power
    NimBLEDevice::setMTU(23);  // This is what the lights ask for, so set up front. I think this made it more reliable to connect first time.
    
    // Set up Wifi
    Serial.println("Setting up Wifi...");
    WiFi.macAddress(mac);
    Serial.print("MAC Address: ");
    Serial.println(WiFi.macAddress());
    hostname = HOSTNAME;
    char macChar[4] = {0};
    sprintf(macChar, "%02X%02X", mac[4], mac[5]);
    hostname.append(macChar);
    Serial.print("Hostname: ");
    Serial.println(hostname.c_str());
    WiFi.hostname(hostname.c_str());

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(true);
    WiFi.begin(STASSID, STAPSK);
    while (WiFi.waitForConnectResult() != WL_CONNECTED) {
        Serial.println("Couldn't connect to Wifi! Rebooting...");
        delay(5000);
        ESP.restart();
    }
    ipAddr = WiFi.localIP();
    Serial.print("IP address: ");
    Serial.println(ipAddr);


    // Setup MQTT
    //mqttControlTopic += ipAddr.toString().c_str();     
    mqttClient.setServer(BROKER, 1883);
    boolean bufferResize = mqttClient.setBufferSize(1000);
    if (!bufferResize) {
        Serial.println("Failed to resize MQTT buffer");
    }
    mqttClient.setCallback(mqttCallback);
    mqttReconnect();

    // Setup LED
    pinMode(LED, OUTPUT);
    digitalWrite(LED, !digitalRead(LED));

    //Set up OTA
    ArduinoOTA.setHostname(hostname.c_str());
    ArduinoOTA.begin();

    StaticJsonDocument<100> tempDoc;
    tempDoc["ipAddr"] = WiFi.localIP().toString();
    tempDoc["status"] = "Ready";
    tempDoc["state"] = true;
    tempDoc["ctl"] = hostname.c_str();
    sendMqttMessage(tempDoc, mqttStateTopic+hostname);

    server.on("/reboot", HTTP_POST, [](){
        server.send(200, F("text/plain"), "Rebooting...");
        delay(2000);
        ESP.restart();
    });
    server.begin();

    Serial.println("READY");
    
    // When a device comes online it asks everyone to do a scan
    sendMqttMessage("{\"action\":\"scan\"}", "triones/control/global");
}


void loop (){
    mqttClient.loop();
    ArduinoOTA.handle();
    server.handleClient();

    // Flash the light, of course
    unsigned long currentMillis = millis();
    if (currentMillis - previousMillis >= 2500) {
        previousMillis = millis();
        digitalWrite(LED, !digitalRead(LED));
    }

    if (!mqttClient.connected()) {
        mqttReconnect();
    }
}

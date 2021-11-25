#include <NimBLEDevice.h>
#include <WiFi.h>
//#include <WiFiUdp.h>
//#include <ArduinoOTA.h>
#include <ArduinoMqttClient.h>
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
NimBLEScan* pBLEScan =NimBLEDevice::getScan();
//pBLEScan = NimBLEDevice::getScan();

// MQTT Stuff
WiFiClient wifiClient;
MqttClient mqttClient(wifiClient);
const char broker[] = "smarthome";
const int  port     = 1883;
const char topic[] = "arduino/simple";



class MyAdvertisedDeviceCallbacks: public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* advertisedDevice) {
      //Serial.printf("Advertised Device: %s \n", advertisedDevice->toString().c_str());
      if (advertisedDevice->haveName()) {
            String device_name = advertisedDevice->getName().c_str();
            if(device_name.indexOf("Triones") > -1) {
                // Finally found what we're looking for
                String addr = advertisedDevice->getAddress().toString().c_str();
                int rssi = advertisedDevice->getRSSI();
                Serial.print("XXX Device Name: ");
                Serial.println(device_name);
                Serial.println(device_name);
                mqttClient.beginMessage(topic);
                String json_str = "{\"deviceName\":\""+device_name;
                json_str += "\", \"MAC\":\"" + addr;
                json_str += "\",\"RSSI\":"+String(rssi);
                json_str += "}";
                mqttClient.print(json_str);
                mqttClient.endMessage();
            }
      }
    }
};

/** Notification / Indication receiving handler callback */
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
    
    // std::string str = (isNotify == true) ? "Notification" : "Indication";
    // str += " from ";
    // /** NimBLEAddress and NimBLEUUID have std::string operators */
    // str += std::string(pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress());
    // str += ": Service = " + std::string(pRemoteCharacteristic->getRemoteService()->getUUID());
    // str += ", Characteristic = " + std::string(pRemoteCharacteristic->getUUID());
    // str += ", Value = " + std::string((char*)pData, length);
    // Serial.println(str.c_str());
    if (length == 12) {
        // Some rudimentary checks that this is what we're looking for
        if ((pData[0] == 0x66) && (pData[11] == 0x99)) {
            String power_state = (pData[2] == 0x23) ? "true" : "false";
            int mode = pData[3];
            int speed = pData[5];
            int red = pData[6];
            int green = pData[7];
            int blue = pData[8];
            String addr = pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress().toString().c_str();
            // int white = pData[9];
            // {"mac":"aa:bb:cc:dd:ee:ff", "power":false, "rgb":[255,255,255], "speed":255, "mode": 255}
            char buffer[95];
            sprintf(buffer, "{\"mac\":\"%s\", \"power\":%s, \"rgb\":[%d,%d,%d], \"speed\", %d, \"mode\":%d}", addr, power_state, red, green, blue, speed, mode);
            mqttClient.beginMessage(topic);
            mqttClient.print(buffer);
            mqttClient.endMessage();
        }
    }
    // for (size_t i=0; i<length; i++) {
    //     Serial.print(pData[i], HEX);
    //     Serial.print(":");
    // };
    //Serial.println("\n^ hex");
}


bool connect(NimBLEAddress bleAddr){
    pClient = NimBLEDevice::createClient();
    pClient->setConnectTimeout(5); // seconds
    if (!pClient->connect(bleAddr)) {
        Serial.println("Failed to connect");
    } else {
        // This should be the happy path
        if (pClient->isConnected()) {
            Serial.println("Connected correctly");
            Serial.print("RSSI:");
            Serial.println(pClient->getRssi());
            //pClient->setConnectionParams(12,12,0,51); // Do we really need this?  Seems to work without it.
            return true;
        }
    }
    NimBLEDevice::deleteClient(pClient);
    return false;
}

bool setupSvcAndChr(){
    // Ok, so most of this seems redundant.  Setting these services up here
    // doesn't make them persist.  If you want to write to one later, 
    // you have to set the whole thing up again.  All this whole haystack is
    // doing is testing that the SVCs and CHARs exist and do what we expect them to.
    // There is some value in that, but I'm not convinced it's worth doing.
    // Update:  It would make sense that if you pClient->disconnect then the SVCs
    // and CHARs would go away.  So we'll do this here to make sure that the device
    // can do the things we would expect it to do.

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
    return writeData(payload, 3);
}

bool turnOff(){
    uint8_t payload[3] = {0xCC, 0x24, 0x33};
    return writeData(payload,3);
}

bool setRGB(uint8_t red, uint8_t green, uint8_t blue, uint8_t brightness=100) {
    if (brightness == 0) {
        return turnOff();
    }
    uint8_t rgb[7] = {0x56, 00, 00, 00, 00, 0xF0, 0xAA};
    uint8_t scale_factor = brightness / 100;
    rgb[1] = red * scale_factor;
    rgb[2] = green * scale_factor;
    rgb[3] = blue * scale_factor;
    return writeData(rgb, 7);
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
    return writeData(payload, 3);
}

bool findTrionesDevices(){
    NimBLEScan* pBLEScan =NimBLEDevice::getScan();
    if (!pBLEScan) {
        return false;
    }
    if (pBLEScan->isScanning()) {
        return false;
    }
    pBLEScan->setAdvertisedDeviceCallbacks(new MyAdvertisedDeviceCallbacks(), false);
    pBLEScan->setActiveScan(false);
    //pBLEScan->setInterval(97); // How often the scan occurs / switches channels; in milliseconds,
    //pBLEScan->setWindow(37);  // How long to scan during the interval; in milliseconds.
    pBLEScan->setMaxResults(0); // do not store the scan results, use callback only.
    Serial.println("Doing scan for 20 seconds...");
    pBLEScan->start(20,false);//, nullptr, false);
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
    if (!mqttClient.connect(broker, port)) Serial.println("MQTT connection failed!");
    mqttClient.subscribe(topic);

    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
}


void loop (){
    mqttClient.poll();
    int messageSize = mqttClient.parseMessage();
    if (messageSize) {
        Serial.print("Received a message with topic '");
        Serial.print(mqttClient.messageTopic());
        Serial.print("', length ");
        Serial.print(messageSize);
        Serial.println(" bytes:");
        while (mqttClient.available()) {
            Serial.print((char)mqttClient.read());
        }
        Serial.println();
    }
    
    Serial.println("Starting loop");
    NimBLEAddress addr = NimBLEAddress("78:82:a4:00:05:1e");
    Serial.println("Set address");
    bool a = connect(addr);
    Serial.println("Connect run");
    Serial.println(a);
    if (a) {
        Serial.println("MAIN LOOP: Connected to device");
        delay(1000);
        Serial.println("Turning on...");
        turnOn();
        delay(1000);
        Serial.println("Getting status");
        getStatus();
        delay(1000);
        setRGB(255,0,0);
        delay(500);
        setRGB(0,255,0);
        delay(500);
        setRGB(0,0,255);
        delay(500);
        setRGB(255,0,0);
        delay(500);
        setMode(0x2E,20);
        delay(5000);
        Serial.println("Turning off");
        turnOff();
        delay(1000);
    } else Serial.println("MAIN LOOP: Failed to connect.");
    findTrionesDevices();
    Serial.println("2 second pause");
    delay(2000);
    Serial.println("Waiting 10s until next loop.");
    delay(10000);
    /////////////////////////////////////
    // Once an hour announce all the triones devices we can see.
    // Process incoming MQTT requests
    // See what happens when things are left to run for a while.  Does it
    // lock up the sockets?  Does it just start crashing all the time?
    
}

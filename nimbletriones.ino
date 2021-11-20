#include <NimBLEDevice.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <ArduinoMqttClient.h>
#include "wificreds.h"

// Define your WiFi settings in wificreds.h

#ifndef HOSTNAME
  #define HOSTNAME "blegateway"
#endif

const NimBLEUUID NOTIFY_SERVICE ("FFD0");
const NimBLEUUID NOTIFY_CHAR   ("FFD4");
const NimBLEUUID MAIN_SERVICE  ("FFD5");
const NimBLEUUID WRITE_CHAR    ("FFD9");

NimBLEClient* pClient = nullptr;
NimBLERemoteService* pSvc = nullptr;
NimBLERemoteCharacteristic* pChr = nullptr;
NimBLERemoteDescriptor* pDsc = nullptr;
NimBLERemoteService* nSvc = nullptr;
NimBLERemoteCharacteristic* nChr = nullptr;
NimBLERemoteDescriptor* nDsc = nullptr;


/** Notification / Indication receiving handler callback */
void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify){
    std::string str = (isNotify == true) ? "Notification" : "Indication";
    str += " from ";
    /** NimBLEAddress and NimBLEUUID have std::string operators */
    str += std::string(pRemoteCharacteristic->getRemoteService()->getClient()->getPeerAddress());
    str += ": Service = " + std::string(pRemoteCharacteristic->getRemoteService()->getUUID());
    str += ", Characteristic = " + std::string(pRemoteCharacteristic->getUUID());
    str += ", Value = " + std::string((char*)pData, length);
    Serial.println(str.c_str());
    for (size_t i=0; i<length; i++) {
        Serial.print(pData[i], HEX);
        Serial.print(":");
    };
    Serial.println("\n^ hex");
}


bool connect(NimBLEAddress bleAddr){
    if (NimBLEDevice::getClientListSize()) {
        // We might already know about this device, in which case
        // connect to it in the special way to not have to refersh
        // the service database.  It's faster.
        pClient = NimBLEDevice::getClientByPeerAddress(bleAddr);
        if (pClient) {
            if(!pClient->connect(bleAddr, false)) {
                Serial.println("Reconnect failed");
                return false;
            }
            Serial.println("Reconnected to client");
        }
        else {
            // We don't have a client connection which we can use
            // but maybe we used to have one?  Let's look...
            pClient = NimBLEDevice::getDisconnectedClient();
        }
    }

    if (!pClient) {
        // We didnt have a disconnect one either, so we're going to create a
        // new connection from scratch.
        if(NimBLEDevice::getClientListSize() >= NIMBLE_MAX_CONNECTIONS) {
            Serial.println("Too many clients already.  Can't connect to more.");
            return false;
        }

        // Now we actually get on with creating a new client
        pClient = NimBLEDevice::createClient();
        Serial.println("New client created");
        pClient->setConnectTimeout(5); // seconds

        if (!pClient->connect(bleAddr)) {
            // After all that we still failed to connect, so tidy up and leave.
            NimBLEDevice::deleteClient(pClient);
            Serial.println("Tried to connect to device, but failed. :(");
            return false;
        }
    }

    if (!pClient->isConnected()) {
        if (!pClient->connect(bleAddr)) {
            Serial.println("Failed to straight up connect");
            return false;
        }
    }

    Serial.println("Connected!  Here's some info you don't need to know:");
    Serial.println(pClient->getPeerAddress().toString().c_str());
    Serial.print("RSSI:");
    Serial.println(pClient->getRssi());
    //pClient->setConnectionParams(12,12,0,51); // Do we really need this?
    return true;
}

bool setupSvcAndChr(){
    // Ok, so most of this seems redundant.  Setting these services up here
    // doesn't make them persist.  If you want to write to one later, 
    // you have to set the whole thing up again.  All this whole haystack is
    // doing is testing that the SVCs and CHARs exist and do what we expect them to.
    // There is some value in that, but I'm not convinced it's worth doing.
    
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
        Serial.println("WRITE: connected");
        // I didn't think we would need to get those again each time we wanted to
        // talk.  But I tried just reusing the old ones, and it shit the bed.
        // I don't understand C++
        // Just going to assume we can set them up again since it must have worked last
        // time we tried, otherwise the connect() function would have failed.  :shrug:
        // Additional:  The notifications don't carry over either by the looks of it.
        // ??
        // Further more, if you fail to do one thing in the correct order when setting
        // up all the services and characteristics, when you try and talk to a char.
        // It just panics and restarts the whole MCU.  WTF?
        // AND another thing... these lights.... fucking hell.  They fall over as soon
        // as you look at them.  If you leave notification unread they stop responding
        // for a while.  
        // What do you expect for 5 quid, I suppose.  Anyway, this sort of works...
        nSvc = pClient->getService(NOTIFY_SERVICE);
        nChr = nSvc->getCharacteristic(NOTIFY_CHAR);
        if (nChr->canNotify()) {
            nChr->subscribe(true, notifyCB);
        }
        pSvc = pClient->getService(MAIN_SERVICE);
        pChr = pSvc->getCharacteristic(WRITE_CHAR);
        if (pChr->canWrite()){
            Serial.println("WRITE: SVC and CHR OK");
            pChr->writeValue(payload, length);
            return true;
        } else Serial.println("WRITE: Couldnt write");
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

bool getStatus(){
    uint8_t payload[3] = {0xEF, 0x01, 0x77};
    return writeData(payload, 3);
}


void setup (){
    Serial.begin(115200);
    Serial.println("Starting NimBLE Client");
    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); /** +9db */
}


void loop (){    
    Serial.println("Starting loop");
    NimBLEAddress addr = NimBLEAddress("78:82:a4:00:05:1e");
    bool a = connect(addr);
    if (a) {
        Serial.println("MAIN LOOP: Connected to device");
        delay(1000);
        Serial.println("Turning on...");
        turnOn();
        delay(1000);
        Serial.println("Getting status");
        getStatus();
        delay(1000);
        Serial.println("Turning off");
        turnOff();
    } else Serial.println("MAIN LOOP: Failed to connect.");
    pClient->disconnect();
    Serial.println("Waiting 10s");
    delay(10000);

}



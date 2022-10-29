#!/usr/bin/env python3

import paho.mqtt.client as mqtt
import json
import time


def on_connect(client, userdata, flags, rc):
    print("Connected to MQTT with result code "+str(rc))
    client.subscribe("triones/#")

def on_message(client, userdata, msg):
    try:
        payload = json.loads(msg.payload)
    except json.decoder.JSONDecodeError:
        print("Invalid JSON payload: " + msg.payload)
        return
    print(payload)
    if "type" in payload:
        if (payload["type"] == "deviceTable"):
            controller = payload["ctl"]
            ip = payload["scanningHost"]
            device_table = payload["devices"]

            for device in device_table:
                if "mac" in device and "rssi" in device:    
                    mac = device["mac"]
                    rssi = device["rssi"]
                    print("MAC: " + mac + " RSSI: " + str(rssi))

                    if mac in devices:
                        # We know about this triones device
                        found = False
                        for count,value in enumerate(devices[mac]):
                            # value should be a dictionary with ctl and rssi
                            if value['ctl'] == controller:
                                # Update the rssi
                                devices[mac][count]['rssi'] = rssi
                                found = True
                        if found == False:
                            # if we get here, the mac is known, but not with this controller yet
                            # so we should add it
                            devices[mac].append({'ctl': controller, 'rssi': rssi})
                    else:
                        # This triones device has never been seen before
                        devices[mac] = [] 
                        devices[mac].append({'ctl':controller, 'rssi':rssi})
        

def pretty_print_device_table():
    #num_triones = len(devices)
    #table = []

    for mac in devices:
        print("Triones MAC: " + mac, end="\t")
        for r in devices[mac]:
            print("Controller: " + r['ctl'] + " RSSI: " + str(r['rssi']), end="\t")
        print("")

devices = {}

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect("mqtt", 1883)

t = time.time()
while True:
    client.loop()
    if time.time() > t+60:
        pretty_print_device_table()
        t = time.time()


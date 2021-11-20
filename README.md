# nimble_triones
Control Triones lights from an ESP32 using the Nimble BTLE stack


# The story so far....

1) I'm a cheapskate
2) I like controlling things with software
3) When you've got a hammer, everything is a nail


I bought some cheapo LED strips from Amazon and it turned out they have a BTLE interface in the PSU.  The app you can download is fine and everything, but I've got a Node Red install that needs fresh meat.

## Python & Pi
I tried with Python and a Raspberry Pi:  https://github.com/8none1/pytrionesmqtt
It works, but Bluez doesn't like these lights.  Bluepy seems OK (it's API is ❤️).  But ultimately it only worked occasionally.

## C++ & ESP32 native BLE library
I found this project and forked it:  https://github.com/8none1/ble-light-bulb
Well done to kakopappa, this got me going on an ESP32 and it was faster and more reliable than the Pi.
But, urgh.  The ESP32 library is a) unstable af. b) massive af. (I ran out of room for my sketch using the BLE library and the OTA library!) 
It works, but I couldn't enable OTA updates and MQTT support without running out of space.  So I sacked it off.

## C++ & ESP32 Nimble library
I really hate C++.
You can tell just how much I hate it by looking at the spaghetti code I wrote.
It works.  Kinda.
I still need to try and add OTA support etc.
If anyone out there understands C++ and would like to take this idea and make it useable, please feel free and save me from this hell.


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

Update:  I've spent a few evenings over the last week getting to grips with C++ and NimBLE.  I'm starting to really like NimBLE.  I understand some of the C++iness about it now.
I still have a few kinks to work out, mainly around null pointers, but it's pretty much working now.
The main problem is the very unreliable initial connection. I'm still trying to get to the bottom of this. (See below)

I also discovered that if you keep a callback registered and the connection up then the lights will tell you when someone presses the power button on the remote to power the lights on.  But that's all it does.  It doesn't tell you when they change colour or anything, so it's pretty much useless. Much like the rest of these lights.

Update 2021-12-03:  The current version pretty much works.  It connects first time about 80% of the time, if you're in range. Reconnections now work and make it a lot faster.  
The big big big problem is still the range of the ESP32.  My iPhone 8 can talk to the lights from the other end of the house, but the ESP32 struggles at half that distance.  (I don't live in a castle).
I might try tweaking the connection params some more.
In the meantime, it needs some tidy up but it's ready for testing and replacing my Pi based bluepy version.


# Problems that still exist
 - The Bluetooth lights lock up some times.  It seems the only thing you can do to fix it is power cycle them
   - I think I might have a bit of a better understanding about what's happening here now.  If you ask the device for the status, and then fail to read that status and leave it kind of hanging around, then the device starts sending back malformed packets. If you leave enough of these queued up, Bluetooth stops responding on the device.  Read your notifications!
   - Edit: Yeah, maybe not.  I think this might be more down to the performance of the BT antenna on the ESP32 and wifi co-existence.

 - If you enable OTA then you suddenly can't connect to BLE devices (yes, the whole point of this endevour was to enable OTA).  Edit: this might not be true.
 - If you enable mDNS you can't connect to BLE devices.  Edit: this might not be true.
 
 It's starting to look like I would need one ESP32 within about 6 feet of the lights in order to make them work reliably.  This increases the cost of one set of 5M LED lights to about £13 (£7 for the lights, £5 for the ESP32).  e.g "CNSUNWAY" 5M LED strip https://amzn.to/3lwSw8e  (affiliate link)

 vs the cheapest Wifi controlled lights I can find on Amazon for £15:  https://amzn.to/32XYTLv  (affiliate link)

 If I hadn't already bought a load of the Bluetooth ones, I would spend my money on the Wifi connected ones instead.  I haven't checked, but I would guess that the wifi ones are using the Tuya platform.  Tuya have documented their API and you can (currently) get an API key for free.  There are lots of projects out there to help you.  Save yourself a lot of time and bother.







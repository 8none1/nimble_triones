# DRAFT README - NOT COMPLETE


# nimble_triones
Control Triones based LED strips from an ESP32 using the Nimble BTLE stack and MQTT.

![image](https://user-images.githubusercontent.com/6552931/126961723-b64c8e99-0da0-4924-b254-b4c116330f11.png)


# The story so far....

1) I'm a cheapskate
2) I like controlling things with software
3) When you've got a hammer, everything looks like a nail


I bought some cheapo LED strips from Amazon and it turned out they have a BTLE interface in the PSU.  The app you can download is fine and everything, but I've got a Node Red install that needs fresh meat.

## Python based Raspberry Pi alternative
I tried with Python and a Raspberry Pi:  https://github.com/8none1/pytrionesmqtt
It works, but Bluez doesn't like these lights.  Bluepy seems OK (it's API is ❤️).  But ultimately it only worked occasionally. 
It wasn't reliable enough to become part of a home automation system, so I went looking for something else.

## C++ & ESP32 native BLE library
I found this project and forked it:  https://github.com/8none1/ble-light-bulb
Well done to kakopappa, this got me going on an ESP32 and it was faster and more reliable than the Pi.
But, urgh.  The ESP32 BT LE stack is unstable, but more importantly it's massive. I ran out of storage space for my sketch using the ESP32 Bluetooth library and the OTA library!
It works, but I couldn't enable OTA updates and MQTT support without running out of space.  So I sacked it off.

## C++ & ESP32 Nimble library
This is where this sketch comes in.  Caveat: I really hate C++. You can tell just how much I hate it by looking at the spaghetti code I wrote.
However - this works. Kinda.  OTA support is currently disabled, but it can be enabled if you want.

### How to use this sketch

#### Prerequisites 
 - Arduino IDE.  There are no doubt ways to work without using the Arduino IDE, but that's up to you to work out.
 - ESP32 board support in the Arduino IDE.  Easy enough, just google "esp32 board manager arduino".
 - NimBLE library.  It's in the library manager in the Arduino IDE, install it from there.
 - ArduinoJson library. Ditto.
 - ArduinoMqtt library.  Ditto.  Make sure you install the "official" library.  There are others, I just happened to use this one.
 - This sketch
 - An MQTT server
 - (Optional) Something like Node Red to help you glue MQTT messages and other external actions together

 1. Open this sketch in the Arduino IDE, install the board support package, install the libraries
 1. Change the address of the mqtt server to your local server (`const char broker[] = "mqtt";`)
 1. Create a `wificreds.h` file in the same directory as the sketch with contents like this:
 ```
 #define STASSID "your_ssid"
 #define STAPSK  "your_wifi_password"
 ```
 4. Change the max number of devices you have, default is 5.  If you have fewer than this, just leave it alone.  (`const uint8_t maxDevices = 5;`)
 1. Compile it and upload it to your board
 1. I think that's about it from the ESP32 side.

 You can have more than one device in your network.  They will change their hostnames dynamically to include the last octet of the IP address they get from DHCP.  They will communicate with each other
 (and you and your computers) over MQTT.  They do this learn about who has the best connection quality to a given device and respond to requests that they are best suited to answer.  Neat!

 #### How to structure your MQTT messages

 These devices look for JSON formatted MQTT topics to know, specific topics.  You can change these to be whatever you want of course, but the code is a bit of a mess so be warned.

 The topics are:
  * mqttControlTopic - `triones/control/<device IP address as an unquoted string, e.g. 192.168.0.1>` - send a control payload to one specific device
  * mqttGlobalTopic - `triones/control/global` - send a control payload to all devices in the network
  * mqttPubTopic - `triones/status` - devices in the network reporting their state and status

*Control Payloads*
* `{"action":"scan"}` - ask the ESP32s to do a BT LE scan and report what they can see to the mqttPubTopic.  Other devices also listen to this topic to learn about other LED strips around the network.
 * `{"action":"ping", "mac":"aa:bb:cc:dd:ee:ff"}` - asks for a simple reply from the device to the mqttPubTopic topic.
 * `{"action":"disconnect", "mac":"aa:bb:cc:dd:ee:ff"}` - disconnect from this LED controller
 * `{"action":"status", "mac":"aa:bb:cc:dd:ee:ff"}` - Report the status of this device to the mqttPubTopic topic
 * `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", ..... }` - Send a command to change colour, mode, brightness, power, etc to this device.  See below for more information.

 *Set Control Payloads*
 These actually turn your lights on and off.
  All `set` actions must have a mac address, and then one or many of the following:

 * `power` - JSON boolean so `true` or `false` (unquoted) e.g. `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", "power":true}`
 * `rgb` - JSON byte array (0-255) for R,G,B like [0,255,0]. e.g. red `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", "rgb":[255,0,0]}`
  * `rgb` also supports the optional `percentage` key to adjust brightness.  This simple divides the RGB values by the percentage, so 50% red 255 becomes red 128. e.g. `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", "rgb":[255,0,0], "percentage":50}`
 * `mode` - value between 37 and 56. See the code for what each mode does. e.g. `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", "mode":41}`
  * `mode` also supports the optional `speed` key.  Speed is between 1 (fast) and 31 (slow) e.g. `{"action":"set", "mac":"aa:bb:cc:dd:ee:ff", "mode":41, "speed": 4}`

You can include more than one control type in your JSON payload, so for example, if you wanted to power on and set the colour to blue you could:

```
{ 
  "mac" : "aa:bb:cc:dd:ee:ff",
  "action":"set",
  "rgb" : [255,0,0],
  "power":true
}
```

The ESP will take care of connecting, or retrying 10 times before giving up.  It will then stay connected for as long as it can.  This makes subsequent commands almost instantaneous and will generate "power on" message when you switch the LEDs on with the remote.  N.B: only power on messages are generated when using the remote, that seems to be just how it works.  If you want more accurate information about what the lights are doing, just send a status message.  I used Node Red to send a status request every 10 minutes.




Update:  I've spent a few evenings over the last week getting to grips with C++ and NimBLE.  I'm starting to really like NimBLE.  I understand some of the C++iness about it now.
I still have a few kinks to work out, mainly around null pointers, but it's pretty much working now.
The main problem is the very unreliable initial connection. I'm still trying to get to the bottom of this. (See below)

I also discovered that if you keep a callback registered and the connection up then the lights will tell you when someone presses the power button on the remote to power the lights on.  But that's all it does.  It doesn't tell you when they change colour or anything, so it's pretty much useless. Much like the rest of these lights.

Update 2021-12-03:  The current version pretty much works.  It connects first time about 80% of the time, if you're in range. Reconnection now work and make it a lot faster.  
The big big big problem is still the range of the ESP32.  My iPhone 8 can talk to the lights from the other end of the house, but the ESP32 struggles at half that distance.  (I don't live in a castle).
I might try tweaking the connection params some more.
In the meantime, it needs some tidy up but it's ready for testing and replacing my Pi based bluepy version.


# Problems that still exist
 - The biggest problem seems to be range.  At an RSSI of -80 or worse (so lower numbers) things get a bit hit and miss.  I've also had an ESP32 about 1M from a Triones device and report RSSI of -75, so yeah.  Crappy.
 - The Bluetooth lights lock up some times.  It seems the only thing you can do to fix it is power cycle them
   - I think I might have a bit of a better understanding about what's happening here now.  If you ask the device for the status, and then fail to read that status and leave it kind of hanging around, then the device starts sending back malformed packets. If you leave enough of these queued up, Bluetooth stops responding on the device.  Read your notifications!
   - Edit: Maybe not.  I think this might be more down to the performance of the BT antenna on the ESP32 and wifi co-existence.

 - If you enable OTA then you suddenly can't connect to BLE devices (yes, the whole point of this endeavour was to enable OTA).  Edit: this might not be true.  I have had it working, but have given up for now because the Arduino IDE kept asking for a password to flash, and I hadn't set one.  Haven't bothered to debug this.
 - If you enable mDNS you can't connect to BLE devices.  Edit: this might not be true.  I should renable it at some point.
 
 It's starting to look like I would need one ESP32 within about 6 feet of the lights in order to make them work reliably.  This increases the cost of one set of 5M LED lights to about £13 (£7 for the lights, £5 for the ESP32).  e.g "CNSUNWAY" 5M LED strip https://amzn.to/3lwSw8e  (affiliate link)

 vs the cheapest Wifi controlled lights I can find on Amazon for £15:  https://amzn.to/32XYTLv  (affiliate link)

 If I hadn't already bought a load of the Bluetooth ones, I would spend my money on the Wifi connected ones instead.  I haven't checked, but I would guess that the wifi ones are using the Tuya platform.  Tuya have documented their API and you can (currently) get an API key for free.  There are lots of projects out there to help you.  Save yourself a lot of time and bother.
 Update:  I've some some mild success at ranges of about 4M indoors.  That's probably good enough to cover a few bedrooms, so might not be so bad after all.





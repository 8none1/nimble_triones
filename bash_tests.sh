#!/bin/bash

disconnect_matty () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"disconnect\",\"mac\":\"78:82:04:00:02:16\"}"
}
disconnect_study () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"disconnect\",\"mac\":\"78:82:a4:00:05:1e\"}"
}
do_scan () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"scan\"}"
}
matty_green () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"set\", \"rgb\":[0,255,0], \"mac\":\"78:82:04:00:02:16\"}"
}
matty_off () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"set\", \"power\":false, \"mac\":\"78:82:04:00:02:16\"}"
}
matty_on () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"set\", \"power\":true, \"mac\":\"78:82:04:00:02:16\"}"
}
matty_red () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"set\", \"rgb\":[255,0,0], \"mac\":\"78:82:04:00:02:16\"}"
}

status_test_matty () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"status\", \"mac\":\"78:82:04:00:02:16\"}"
}
status_test_study () 
{ 
    mosquitto_pub -h smarthome -t "triones/control" -m "{\"action\":\"status\", \"mac\":\"78:82:a4:00:05:1e\"}"
}

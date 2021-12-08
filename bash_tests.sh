#!/bin/bash

# `source` this file to use it

export M="78:82:04:00:02:16"
export ST="78:82:a4:00:05:1e"
export SA="b8:82:a4:00:24:43"
export TD="192.168.42.60"
export D2="192.168.42.26"
export P1="mosquitto_pub -h smarthome -t "




disconnect_matty () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"disconnect\",\"mac\":\"$M\"}"
}
disconnect_study () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"disconnect\",\"mac\":\"$ST\"}"
}

disconnect_sam () 
{ 
    $P1"triones/control" -m "{\"action\":\"disconnect\",\"mac\":\"$SA\"}"
}

disconnect_study () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"disconnect\",\"mac\":\"$ST\"}"
}

do_scan () 
{ 
    $P1"triones/control/global" -m "{\"action\":\"scan\"}"
}
matty_green () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"set\", \"rgb\":[0,255,0], \"mac\":\"$M\"}"
}
matty_off () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"set\", \"power\":false, \"mac\":\"$M\"}"
}
matty_on () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"set\", \"power\":true, \"mac\":\"$M\"}"
}
matty_red () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"set\", \"rgb\":[255,0,0], \"mac\":\"$M\"}"
}

status_test_matty () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"status\", \"mac\":\"$M\"}"
}
status_test_study () 
{ 
    $P1"triones/control/$TD" -m "{\"action\":\"status\", \"mac\":\"$ST\"}"
}

status_test_sam () 
{ 
    $P1"triones/control/$D2" -m "{\"action\":\"status\", \"mac\":\"$SA\"}"
}

ble_ping ()
{
    $P1"triones/control/global" -m "{\"action\":\"ping\"}"
}

compound_test () 
{
    $P1"triones/control/$TD" -m "{\"action\":\"set\", \"power\":true, \"rgb\":[255,0,0], \"mac\":\"$ST\"}"
}

mode ()
{
    $P1"triones/control/$TD" -m "{\"mac\":\"$ST\", \"mode\":$1, \"action\":\"set\"}"
}

global_test ()
{
    $P1"triones/control/global" -m "{\"mac\":\"$ST\", \"action\":\"status\"}"
}

global_set_test ()
{
    $P1"triones/control/global" -m "{\"mac\":\"$ST\", \"action\":\"set\", \"rgb\":[255,0,0]}"
}
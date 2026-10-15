#!/bin/bash
# Drives a fresh app start to the Create-a-Sim name screen (1520x720 device).
t() { adb shell input tap $1 $2; sleep ${3:-2}; }
sleep 28
t 912 399 5     # Começar o Jogo
t 764 433 2     # dismiss save-slot tip
t 653 333 9     # Novo Jogo (slot 1)
t 764 433 2     # dismiss CAS tip
t 1487 682 3    # confirm appearance
t 238 289 3     # confirm personality
for y in 99 139 179 219; do t 1353 $y 1; done
adb shell input swipe 1353 220 1353 110 500; sleep 1
t 1353 219 1
t 1457 289 3    # confirm traits

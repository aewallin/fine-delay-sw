#!/bin/bash

modprobe fmc
modprobe spec
insmod spec-fine-delay.ko wrc=1

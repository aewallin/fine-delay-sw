#!/bin/bash

rmmod rawrabbit
rmmod fine_delay
rmmod spec
cp wrc.bin /lib/firmware/spec-B0005-cpu.bin
cp spec_top_wr.bin  /lib/firmware/spec-B0005.bin

insmod ../zio/zio.ko
insmod ../spec-sw/kernel/spec.ko lm32=0xc0000
insmod spec-fine-delay.ko regs=0x80000

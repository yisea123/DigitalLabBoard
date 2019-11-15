/*
 * Power.cpp
 *
 *  Created on: 14 Nov 2019
 *      Author: podonoghue
 */

#include "Power.h"

using namespace USBDM;

/**
 * Initialise Power object
 */
void Power::powerInitialise() {
   PowerButton::setInput(PinPull_Up, PinAction_None, PinFilter_None);
   PowerEnableControl::setOutput(PinDriveStrength_High, PinDriveMode_PushPull, PinSlewRate_Slow);
   Adc0::configure(AdcResolution_8bit_se, AdcClockSource_Busdiv2);
   Adc0::setAveraging(AdcAveraging_8);
   Adc0::setCallback(callback);
   Adc0::enableNvicInterrupts(NvicPriority_Normal);
}

/**
 * Poll power button and monitor target Vdd
 */
void Power::pollPower() {
   if (powerOnRecoveryCount>0) {
      powerOnRecoveryCount--;
   }
   bool currentButtonValue = PowerButton::read();

   if (lastButtonValue != currentButtonValue) {
      stableCount     = 0;
      lastButtonValue = currentButtonValue;
      return;
   }
   if (stableCount < POWER_BUTTON_DELAY_COUNT+1) {
      stableCount++;
   }
   if (stableCount == POWER_BUTTON_DELAY_COUNT) {
      if (currentButtonValue) {
         powerOn = !powerOn;
         if (powerOn) {
            powerOnRecoveryCount = POWER_ON_DELAY_COUNT;
         }
         else {
            powerOffNotify();
         }
         PowerEnableControl::write(powerOn);
      }
   }
   if (powerOnRecoveryCount == 1) {
      powerOnNotify();
   }
   if ((powerOnRecoveryCount == 0) && powerOn) {
      VddSample::startConversion(AdcInterrupt_Enabled);
   }
}

/**
 * ADC Callback used to monitor overloads
 *
 * @param result
 * @param channel
 */
void Power::vddSampleCallback(uint32_t result, int) {
   if (powerOn &&(result<(Adc0::getSingleEndedMaximum(AdcResolution_8bit_se)*0.8))) {
      powerOffNotify();
      PowerEnableControl::off();
      powerOn = false;
   }
}

Power *Power::This    = nullptr;
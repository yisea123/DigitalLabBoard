/**
 =================================================================================
 * @file    Waveform.cpp
 * @brief   Generate square waves over a wide range of frequencies using the FTM
 =================================================================================
 */
#include <FrequencyGenerator.h>
#include "hardware.h"
#include "Configure.h"

using namespace USBDM;

static constexpr unsigned LOW_FREQ_DIVISION_FACTOR = 100;

/**
 * Interrupt handler for FrequencyGenerator Timer interrupts
 * This sets the next interrupt/pin toggle for a half-period from the last event
 *
 * @param[in] status Flags indicating interrupt source channel(s)
 */
void FrequencyGenerator::ftmCallback(uint8_t status) {
   static unsigned iterationCounter = 0;

   // Check channel
   if (status & FrequencyGeneratorTimerChannel::CHANNEL_MASK) {
      iterationCounter++;
      if (iterationCounter >= LOW_FREQ_DIVISION_FACTOR) {
         iterationCounter = 0;
         // Set on next edge
         FrequencyGeneratorTimerChannel::setMode(FtmChMode_OutputCompareSet);
      }
      else if (iterationCounter == (LOW_FREQ_DIVISION_FACTOR/2)) {
         // Clear on next edge
         FrequencyGeneratorTimerChannel::setMode(FtmChMode_OutputCompareClear);
      }
      // Note: The pin is toggled directly by hardware
      // Counter roll-over is used to control interval
   }
}

/**
 * Initialise Clock generator
 */
void FrequencyGenerator::initialiseWaveform() {
   FrequencyButtons::setInput(PinPull_Up, PinAction_None, PinFilter_None);

   /**
    * FTM channel set as Output compare with pin Toggle mode and using a callback function
    */
   // Configure base FTM (affects all channels)
   FrequencyGeneratorTimer::configure(
         FtmMode_LeftAlign,       // Left-aligned is required for OC/IC
         FtmClockSource_System);  // Bus clock most accurate source

   static auto cb = [](uint8_t status) {
      This->ftmCallback(status);
   };

   // Set callback function (may be shared by multiple channels)
   FrequencyGeneratorTimer::setChannelCallback(cb);

   // Enable FTM interrupts (required for channel interrupts)
   FrequencyGeneratorTimer::enableNvicInterrupts(NvicPriority_MidHigh);

   // Configure pin associated with channel
   FrequencyGeneratorTimerChannel::setOutput(
         PinDriveStrength_High,
         PinDriveMode_PushPull,
         PinSlewRate_Slow);

   // Check if configuration failed
   USBDM::checkError();
}

/**
 * Set frequency of generator
 *
 * @param frequency
 *
 * @note Frequency generator is initialised on 1st call to this routine.
 */
void FrequencyGenerator::setFrequency(unsigned frequency) {
   usbdm_assert((frequency == Frequency_Off)||((frequency>=Frequency_Min)&&(frequency<=Frequency_Max)), "Illegal frequency");

   currentFrequency = frequency;
   if (frequency == Frequency_Off) {
      /*
       * Off strategy
       *
       * No interrupts
       * Output pin stays low (cleared on all events)
       */
      FrequencyGeneratorTimerChannel::configure(FtmChMode_OutputCompareClear, FtmChannelAction_None);
   }
   else if (frequency < 100*Hz) {
      /*
       * Low frequency strategy
       *
       * Interrupts
       * Event time is manipulated by call-back.
       * Output pin action manipulated by call-back
       * Set x50, Clear x50 so effectively /100 of event frequency
       */
      FrequencyGeneratorTimer::setPeriod(1/((float)LOW_FREQ_DIVISION_FACTOR*frequency));
      FrequencyGeneratorTimerChannel::configure(FtmChMode_OutputCompareClear, FtmChannelAction_Irq);
   }
   else {
      /*
       * High frequency strategy
       *
       * No interrupts
       * Output pin action is toggle on all events
       * Event at fixed counter value (1 tick)
       * Event interval is controlled by changing the FTM period.
       */
      FrequencyGeneratorTimerChannel::configure(FtmChMode_OutputCompareToggle, FtmChannelAction_None);

      FrequencyGeneratorTimer::setPeriod(1/(2.0*frequency));

      // Trigger pin action when counter crosses 1 tick
      FrequencyGeneratorTimerChannel::setEventTime(1);
   }
}

/**
 * Get the current waveform frequency
 *
 * @return Frequency in Hz
 */
unsigned FrequencyGenerator::getFrequency() {
   return currentFrequency;
}

/**
 * Display current Frequency on LCD
 * This is done on the function queue
 *
 * @param frequency
 */
void FrequencyGenerator::refreshFrequency() {

   // Updating OLED is time consuming and uses shared I2C so done on function queue
   static auto f = []() {
      const char *units     = "    ";
      unsigned    frequency = This->currentFrequency;

      This->oled.clearDisplay();
      This->oled.setFont(fontVeryLarge);

      if (frequency == Frequency_Off) {
         This->oled.write("  Off");
      }
      else {
         if (frequency >= 1*MHz) {
            units = " MHz";
            frequency /= 1000000;
         }
         else if (frequency >= 1*kHz) {
            units = " kHz";
            frequency /= 1000;
         }
         else if (frequency != 0) {
            units = "  Hz";
         }
         This->oled.moveXY(5, 4);
         This->oled.setWidth(3).setPadding(Padding_LeadingSpaces).write(frequency).write(units).write(" ");
      }
      This->oled.resetFormat();
      This->oled.refreshImage();
   };

   functionQueue.enQueue(f);
}

/**
 * Table of available frequencies
 */
const float FrequencyGenerator::freqs[] = {
      0,
      1,2,5,
      10,20,50,
      100,200,500,
      1*kHz,2*kHz,5*kHz,
      10*kHz,20*kHz,50*kHz,
      100*kHz,200*kHz,500*kHz,
      1*MHz,2*MHz,
};

/**
 * Do all polling operations
 */
void FrequencyGenerator::pollButtons() {

   static unsigned lastButtonValue     = 0;
   static unsigned stableCount         = 0;
   static unsigned frequencyIndex      = 0;

   if (!powerOn) {
      return;
   }

   unsigned currentButtonValue = FrequencyButtons::read();

   if (lastButtonValue != currentButtonValue) {
      stableCount     = 0;
      lastButtonValue = currentButtonValue;
      return;
   }
   if (stableCount < DEBOUNCE_INTERVAL_COUNT+1) {
      stableCount++;
   }
   if (stableCount == DEBOUNCE_INTERVAL_COUNT) {
      switch(currentButtonValue) {
         case FREQUENCY_UP_BUTTON:
            frequencyIndex++;
            if (frequencyIndex>= (sizeof(freqs)/sizeof(freqs[0]))) {
               // Wrap around
               frequencyIndex = 0;
            }
            setFrequency(freqs[frequencyIndex]);
            refreshFrequency();
            break;
         case FREQUENCY_DOWN_BUTTON:
            frequencyIndex--;
            if (frequencyIndex >= (sizeof(freqs)/sizeof(freqs[0]))) {
               // Wrap around
               frequencyIndex = (sizeof(freqs)/sizeof(freqs[0]) - 1);
            }
            setFrequency(freqs[frequencyIndex]);
            refreshFrequency();
            break;
         default:
            break;
      }
   }
}

/**
 * Notification that soft power-on has occurred
 */
void FrequencyGenerator::softPowerOn() {

   setFrequency(savedFrequency);
   powerOn = true;

   initialiseOled();

   if (startupMessage != nullptr) {
      // Updating OLED is time consuming and uses shared I2C so done on function queue
      static auto f = []() {
         // Display startup message once only
         This->oled.setFont(fontLarge);
         This->oled.write(This->startupMessage);
         This->oled.refreshImage();
         This->startupMessage = nullptr;
      };
      functionQueue.enQueue(f);
      return;
   }

   refreshFrequency();
}

/**
 * Notification that soft power-off is about to occur
 */
void FrequencyGenerator::softPowerOff() {

   powerOn = false;

   savedFrequency = getFrequency();
   setFrequency(Frequency_Off);
}

/**
 * Test loop for debug
 */
void FrequencyGenerator::testLoop() {
   for(;;) {
      for (unsigned index=0; index<sizeof(freqs)/sizeof(freqs[0]); index++) {
         setFrequency(freqs[index]);
//         refreshFrequency();
         waitMS(2000);
      }
   }
}

FrequencyGenerator *FrequencyGenerator::This = nullptr;


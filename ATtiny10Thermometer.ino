/* ATtiny10 Thermometer v2 - see http://www.technoblogy.com/show?2G8A

   David Johnson-Davies - www.technoblogy.com - 7th March 2021
   ATtiny10 @ 1MHz (internal oscillator)
   
   CC BY 4.0
   Licensed under a Creative Commons Attribution 4.0 International license: 
   http://creativecommons.org/licenses/by/4.0/
*/

#include <avr/sleep.h>
#include <avr/interrupt.h>

// One Wire Protocol **********************************************

// Buffer to read data or ROM code
static union {
  uint8_t DataBytes[9];
  unsigned int DataWords[4];
};

const int OneWirePin = 0;
const int RedPin = 1;
const int GreenPin = 2;

const int ReadROM = 0x33;
const int MatchROM = 0x55;
const int SkipROM = 0xCC;
const int ConvertT = 0x44;
const int ReadScratchpad = 0xBE;

inline void PinLow () {
  DDRB = DDRB | 1<<OneWirePin;
}

inline void PinRelease () {
  DDRB = DDRB & ~(1<<OneWirePin);
}

// Returns 0 or 1
inline uint8_t PinRead () {
  return PINB>>OneWirePin & 1;
}

void DelayMicros (unsigned int micro) {
  TCNT0 = 0; TIFR0 = 1<<OCF0A;
  OCR0A = micro;
  while ((TIFR0 & 1<<OCF0A) == 0);
}

void LowRelease (int low, int high) {
  PinLow();
  DelayMicros(low);
  PinRelease();
  DelayMicros(high);
}

uint8_t OneWireSetup () {
  TCCR0A = 0<<WGM00;                // Normal mode
  TCCR0B = 0<<WGM02 | 2<<CS00;      // Normal mode; 1MHz clock
 }

uint8_t OneWireReset () {
  uint8_t data = 1;
  LowRelease(480, 70);
  data = PinRead();
  DelayMicros(410);
  return data;                      // 0 = device present
}

void OneWireWrite (uint8_t data) {
  int del;
  for (int i = 0; i<8; i++) {
    if ((data & 1) == 1) del = 6; else del = 60;
    LowRelease(del, 70 - del);
    data = data >> 1;
  }
}

uint8_t OneWireRead () {
  uint8_t data = 0;
  for (int i = 0; i<8; i++) {
    LowRelease(6, 9);
    data = data | PinRead()<<i;
    DelayMicros(55);
  }
  return data;
}

// Read bytes into array, least significant byte first
void OneWireReadBytes (int bytes) {
  for (int i=0; i<bytes; i++) {
    DataBytes[i] = OneWireRead();
  }
}

// Calculate CRC over buffer - 0x00 is correct
uint8_t OneWireCRC (int bytes) {
  uint8_t crc = 0;
  for (int j=0; j<bytes; j++) {
    crc = crc ^ DataBytes[j];
    for (int i=0; i<8; i++) crc = crc>>1 ^ ((crc & 1) ? 0x8c : 0);
  }
  return crc;
}

// Display Error
void DisplayError (int pin) {
  PORTB = PORTB | 1<<pin;           // Pin high
  WDDelay(6);                       // 1 second flash
  PORTB = PORTB & ~(1<<pin);        // Pin low
  WDDelay(6);                       // 1 second gap
}

void Pulse (int pin) {
  PORTB = PORTB | 1<<pin;           // Pin high
  WDDelay(0);                       // 12msec flash
  PORTB = PORTB & ~(1<<pin);        // Pin low
  WDDelay(6);                       // 1 second gap
}

// Flash result on LEDs **********************************************

// Convert value to negabinary
unsigned int NegaBinary (unsigned int value) {
  return (value + 0xAAAAAAAA) ^ 0xAAAAAAAA;
}

// Flash 8-bit integer value skipping leading zeros
void Flash (unsigned int value) {
  int b, zeros = false;
  for (int i=7; i>=0; i--) {
    b = value>>i & 1;
    if (zeros || b || (i==0)) {
      if (value>>i & 1) Pulse(RedPin); else Pulse(GreenPin);  
      zeros = true;
    }
  }
}

// Watchdog timer **********************************************

// Use Watchdog for time delay; n=0 is 16ms; n=6 is 1sec ; n=9 is 8secs, 
void WDDelay (int n) {
  set_sleep_mode(SLEEP_MODE_PWR_DOWN);
  WDTCSR = 1<<WDIE | (n & 0x8)<<2 | (n & 0x7);
  sleep_enable();
  sleep_cpu();
}

ISR(WDT_vect) {
  WDTCSR = 0<<WDIE;
}

// Temperature sensor **********************************************

// Read temperature of a single DS18B20 or MAX31820 on the bus in 1/16ths of a degree
int Temperature () {
  if (OneWireReset() != 0) {
    DisplayError(RedPin);           // Device not found
  } else {
    OneWireWrite(SkipROM);
    OneWireWrite(ConvertT);
    while (OneWireRead() != 0xFF);
    OneWireReset();
    OneWireWrite(SkipROM);
    OneWireWrite(ReadScratchpad);
    OneWireReadBytes(9);
    if (OneWireCRC(9) == 0) {
      return DataWords[0];
    } else DisplayError(GreenPin);  // CRC error
  }
  return 0;
}

// Setup **********************************************

void setup() {
  sei();
  CCP = 0xD8;                       // Allow change of protected register
  CLKPSR = 0;                       // Switch to 8MHz clock
  // Configure pins
  DDRB = 1<<RedPin | 1<<GreenPin | 0<<OneWirePin;
  PORTB = 0;                        // All low
  OneWireSetup();
}

// Flash temperature using "Easy Binary"
void loop () {
  WDDelay(9);                       // 8 second delay
  WDDelay(9);                       // and another 8 second delay;
  int temp = Temperature();         // In sixteenths of a degree
  if (temp < 0) { Pulse(GreenPin); temp = -temp; }
  Flash(temp>>4);
}

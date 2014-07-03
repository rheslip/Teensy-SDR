#include <Audio.h>
#include <Bounce.h>
#include <Wire.h>
#include <SD.h>
#include <Adafruit_GFX.h>    // Core graphics library
#include <Adafruit_S6D02A1.h> // Hardware-specific library
#include <SPI.h>
#include "filters.h"

//SPI connections for Banggood 1.8" display
#define sclk 5
#define mosi 4
#define cs   2
#define dc   3
#define rst  1  // you can also connect this to the Arduino reset

// use HW SPI interface for TFT
//Adafruit_S6D02A1 tft = Adafruit_S6D02A1(cs, dc, rst);
// Option 1: use any pins but a little slower
Adafruit_S6D02A1 tft = Adafruit_S6D02A1(cs, dc, mosi, sclk, rst);

const int myInput = AUDIO_INPUT_LINEIN;
//const int myInput = AUDIO_INPUT_MIC;

// Create the Audio components.  These should be created in the
// order data flows, inputs/sources -> processing -> outputs
//
// each filter requires a set up parameters
//  http://forum.pjrc.com/threads/24793-Audio-Library?p=40179&viewfull=1#post40179
// lowpass filter at end of audio chain
int postFilterParameters[] = {  // lowpass, Fc=800 Hz, Q=0.707
  3224322, 6448644, 3224322, 1974735214, -913890679, 0, 0, 0};

// complementary Hilbert filters - one shifts +45, other shifts -45
AudioFilterFIR           Filter_I;
AudioFilterFIR           Filter_Q;

//AudioInputAnalog analogPinInput(16); // analog A2 (pin 16)
AudioInputI2S       audioInput;         // audio shield: mic or line-in
AudioFilterBiquad   postFilter(postFilterParameters);
AudioAnalyzeFFT256  myFFT(1);
AudioSynthWaveform  NCO;
AudioMultiplier2         Mult_I;
AudioMultiplier2         Mult_Q;
AudioMixer4         summer;
AudioOutputI2S      audioOutput;        // audio shield: headphones & line-out
//AudioOutputPWM      pwmOutput;          // audio output with PWM on pins 3 & 4


  
// Create Audio connections between the components
//
AudioConnection c1(audioInput, 0, Mult_I, 0);  // audio inputs to multipliers
AudioConnection c2(audioInput, 1, Mult_Q, 0);
AudioConnection c3(NCO, 0, Mult_I, 1);       // 2nd input to multipliers is NCO (local oscillator)
AudioConnection c4(NCO, 0, Mult_Q, 1);
AudioConnection c5(audioInput, 0, myFFT, 0);  // FFT on input for display
AudioConnection c6(Mult_I, 0, Filter_I, 0);  // output of multipliers is frequency shifted down (and up)
AudioConnection c7(Mult_Q, 0, Filter_Q, 0);  // Hilbert filters select the lower frequency output and introduce +-45 phase shifts
AudioConnection c8(Filter_I, 0, summer, 0);  // sum the shifted filter outputs which will supress the image
AudioConnection c9(Filter_Q, 0, summer, 1);
AudioConnection c10(summer, 0, postFilter, 0);  // throw in some more lowpass filtering
AudioConnection c11(postFilter, 0, audioOutput, 0);  // output the sum on both channels
AudioConnection c12(postFilter, 0, audioOutput, 1);
//AudioConnection c9(summer, 0, audioOutput, 0);
//AudioConnection c10(summer, 0, audioOutput, 1);


// Create an object to control the audio shield.
// 
AudioControlSGTL5000 audioShield;


// Bounce objects to read two pushbuttons (pins 0 and 1)
//
Bounce button0 = Bounce(0, 12);
//Bounce button1 = Bounce(1, 12);  // 12 ms debounce time

elapsedMillis msec=0;
unsigned long last_time = millis();
int ncofreq= 11000;
//int ncofreq= 9650;
int tune_freq=ncofreq;
int cursor_pos=0;


void setup() {
  pinMode(0, INPUT_PULLUP);
//  pinMode(1, INPUT_PULLUP);

  // Audio connections require memory to work.  For more
  // detailed information, see the MemoryAndCpuUsage example
  AudioMemory(12);

  // Enable the audio shield and set the output volume.
  audioShield.enable();
  audioShield.inputSelect(myInput);
  audioShield.volume(90);
  audioShield.unmuteLineout();
//  audioShield.dap_avc(2,2,0,-5.0,20.0,5.0);
//  audioShield.dap_avc_enable();
  AudioNoInterrupts();
  NCO.begin(1.0,ncofreq,TONE_TYPE_SINE);
    // Initialize the Hilbert filters
  Filter_I.begin(hilbert45,NUM_COEFFS); // this doesn't always work - filter seems to go into passthru
  Filter_Q.begin(hilbertm45,NUM_COEFFS);
  AudioInterrupts(); 
  tft.initR(INITR_BLACKTAB);   // initialize a S6D02A1S chip, black tab
  tft.setRotation(1);
  tft.fillScreen(S6D02A1_BLACK);
  tft.setCursor(0, 115);
  tft.setTextColor(S6D02A1_WHITE);
  tft.setTextWrap(true);
  tft.print("      Teensy 3.1 SDR");

}

void loop() {


  // every 50 ms, adjust the freq
  if (msec > 50) {
    int tune = analogRead(15);
    tune_freq=ncofreq+(tune-512)*20;
//    toneHigh.frequency(tune_freq);
    msec = 0;
  }

  cursor_pos=(tune_freq)/86; // tuning indicator
  
// draw spectrum display
  if (myFFT.available()) {
     int scale=2;
     for (int16_t x=0; x < 160; x+=3) {
//    tft.drawFastVLine(x, 0, myFFT.output[x]/scale, S6D02A1_GREEN);
       int bar=abs(myFFT.output[x*8/10])/scale;
       if (bar >110) bar=110;
//       if (abs(x-cursor_pos) <4) tft.drawFastVLine(x, 110-bar,bar, S6D02A1_BLUE);
       tft.drawFastVLine(x, 110-bar,bar, S6D02A1_GREEN);
//     tft.drawFastVLine(x+1, 20, abs(myFFT.output[x]/scale), S6D02A1_GREEN);
       tft.drawFastVLine(x, 0, 110-bar, S6D02A1_BLACK);    
//     tft.drawFastVLine(x+1, 20+abs(myFFT.output[x]/scale), 80, S6D02A1_BLACK); 

    } 
    tft.drawFastVLine(80, 0,110, S6D02A1_BLUE); // show mid screen tune position
  }   
   
  // Change this to if(1) for measurement output
if(1) {
/*
  For PlaySynthMusic this produces:
  Proc = 20 (21),  Mem = 2 (8)
*/
  if(millis() - last_time >= 5000) {
    Serial.print("Proc = ");
    Serial.print(AudioProcessorUsage());
    Serial.print(" (");    
    Serial.print(AudioProcessorUsageMax());
    Serial.print("),  Mem = ");
    Serial.print(AudioMemoryUsage());
    Serial.print(" (");    
    Serial.print(AudioMemoryUsageMax());
    Serial.println(")");
    last_time = millis();
  }
}
}



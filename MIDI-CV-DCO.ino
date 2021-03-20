//MIDI to CV controller

//Set UART to 31250 baud
#define USART_BAUDRATE 31250
#define BAUD_PRESCALE (((F_CPU / (USART_BAUDRATE * 16UL))) - 1)

//Data tables for DACs and Timers
//VCO DAC (1v/octave)
const unsigned int VCODAC[61] = { 0,68,137,205,273,341,410,478,546,614,683,751,819,887,956,1024,
                                  1092,1161,1229,1297,1365,1434,1502,1570,1638,1707,1775,1843,1911,1980,2048,2116,
                                  2185,2253,2321,2389,2458,2526,2594,2662,2731,2799,2867,2935,3004,3072,3140,3209,
                                  3277,3345,3413,3482,3550,3618,3686,3755,3823,3891,3959,4028,4095 };

//DCO Timer
const unsigned int DCOValues[61] = { 30581,28860,27241,25714,24272,22910,21622,20408,19264,18182,17161,16197,15288,14430,13620,12857,
                                     12134,11453,10811,10204,9631,9091,8581,8099,7645,7216,6811,6428,6068,5727,5405,5102,
                                     4816,4545,4290,4050,3822,3608,3405,3214,3034,2863,2703,2551,2408,2273,2145,2025,
                                     1911,1804,1703,1607,1517,1432,1351,1276,1204,1136,1073,1012,956 };

//DCO DAC offset CV
const unsigned int DCODAC[61] = { 64,64,80,80,80,80,80,96,112,112,112,128,128,144,160,160,
                                  192,208,224,224,240,256,272,288,304,320,336,352,384,400,416,448,
                                  464,496,528,560,608,640,688,720,768,816,880,880,1008,1056,1136,1216,
                                  1328,1408,1504,1600,1728,1872,2000,2144,2288,2464,2688,2880,3120 };

//General
byte clr;
                                      
//MIDI In
unsigned char midiCh = 15;                                //MIDI Channel 16
unsigned char midiByte = 0;                               //Serial incoming data
unsigned char thirdByte = false;                          //MIDI Running status 3rd byte
unsigned char runningStatus;                              //MIDI status byte
unsigned char midi_a = 0;                                 //MIDI First byte
unsigned char midi_b = 0;                                 //MIDI Second byte

unsigned char stat;                                       //MIDI Status Byte
unsigned char channel;                                    //MIDI Channel Byte
unsigned char triggerNote;                                //Note Triggered
unsigned char lastNote;                                   //Last note for key rollover
  
const unsigned char LowestNote = 24;                      //Start of the note range, or what represents 0
const unsigned char HighestNote = 84;                     //End of the note range

//Trigger output
unsigned char trigcount = 0;                              //Trigger pulse counter
unsigned char trigtime = 10;                              //Trigger time in milliseconds
unsigned char trigenab = false;                           //Trigger enabled for timer
unsigned char envtrig = true;                             //Re trigger enable

//Pitch Bend
const unsigned int oneVolt = 819;                         //12 bit DAC level for 1 volt
unsigned char lastBend = 0x40;                            //Pitch bend previous
unsigned int bendval = 0;
unsigned int vcobend = 819;
unsigned int dcobend = 0;

//Timer
unsigned int timerVAL = 9090;
unsigned int prevtimer = 9090;

void setup() {

  cli();                                                  //Disable Interrupts
  
  DDRB = DDRB | 0b00111111;                               //Pin direction (1 = out, 0 = in)
  DDRC = 0b00111111;
  DDRD = DDRD | 0b11111100;

  //Setup UART
  UCSR0B = (1 << RXEN0) | (1 << TXEN0);                   // Turn on the transmission and reception circuitry
  UCSR0C = (1 << USBS0) | (3 << UCSZ00);
  UBRR0H = (BAUD_PRESCALE >> 8);                          // Load upper 8-bits of the baud rate value into the high byte of the UBRR register
  UBRR0L = BAUD_PRESCALE;                                 // Load lower 8-bits of the baud rate value into the low byte of the UBRR register  
  UCSR0B |= (1 << RXCIE0);                                // Enable the USART Recieve Complete interrupt (USART_RXC)

  //Setup SPI
  SPCR = (1<<SPE) | (1<<MSTR) | (0<<SPR1) | (0<<SPR0);    
  clr=SPSR;
  clr=SPDR;

  //Setup Timer 1
  TCCR1A = 0;                                             // set entire TCCR1A register to 0
  TCCR1B = 0;                                             // same for TCCR1B
  TIMSK1 = 0;
  TCNT1  = 0;                                             // initialize counter value to 0
  OCR1A = 9090;                                           // Set frequency
  TCCR1B |= (1 << WGM12);                                 // CTC Mode
  TCCR1B |= (0 << CS12) | (1 << CS11) | (0 << CS10);      // Set CS12, CS11 and CS10 bits for 8 prescaler
  TCCR1A |= (1 << COM1A0);                                // Toggle OC1A for square wave output
  TIMSK1 |= (1 << OCIE1A);                                // Enable timer interrupt
  
  PORTD = (PORTD & ~0b11111100);                          // Clear all PORT D
  PORTD |= (1 << PORTD4);                                 // Slave Selects HIGH
  PORTD |= (1 << PORTD5);

  OCR0A = 0xAF;
  TIMSK0 |= (1 << OCIE0A);                                //Timer 0 is setup to provide millis count, we can use the interrupt for our own purpose 

  sei();                                                  // allow interrupts

  DACWrite(0,VCODAC[21]);                                 // VCO DAC initial setting
  DACWrite(1,DCODAC[21]);                                 // DCO Offset initial setting

  DACWrite(2,0);                                          // Mod DAC
  DACWrite(3,oneVolt);                                    // Pitch bend DAC
  
}



void loop() {
  // everything is interrupt driven
  
}



//*************************************************************
//UART ISR

ISR(USART_RX_vect) {

    midiByte = UDR0;
        
    if(midiByte >=0xF0){return;}
        
    if (midiByte & 0b10000000) {                              // Header byte received
          runningStatus = midiByte;
          thirdByte = false;
    }
        
    else {
      if (thirdByte == true) {                                //Second data byte received
          midi_b = midiByte; 
          handleMIDI(runningStatus, midi_a, midi_b);          //Incoming data complete
          thirdByte = false;
          return;
      } else {                                                //First data byte received
          if (!runningStatus) {return;}                       //invalid data byte
             midi_a = midiByte;
                thirdByte = true;
                return;
            }
        }
    return;
}


//Timer 0 interrupts every millisecond
ISR(TIMER0_COMPA_vect){
    if(trigenab == true) {
            trigcount ++;                                       //Only count if the trigger is active 
            if(trigcount >= trigtime){
                trigcount = 0;                                  //Reset trigger time
                trigenab = false;                               //Disable any further counts
                PORTD &= ~(1<<PORTD3);                          //Trigger off
            }
    }
  return;
}

ISR(TIMER1_COMPA_vect){

  PORTD &= ~(1<<PORTD6);                                        //LDAC off - allow DAC to load
  OCR1A = timerVAL;                                             //Setting the timer in the interrupt stops any glitches
  PORTD |= (1<<PORTD6);                                         //LDAC on - stop dac from loading
  
  return;
}


void handleMIDI(unsigned char midiByte, unsigned char midi_a, unsigned char midi_b) {
  
    stat = (midiByte & 0b11110000);                             //Status value
    channel = (midiByte & 0b00001111);                          //Channel value
        
    if(channel != midiCh){return;}                              //Ignore channel not for us

    //Modulation
    if(stat == 0xB0 && midi_a == 0x01){
      unsigned int val = midi_b <<4;
      DACWrite(2,val);                                          //Output voltage to DAC (0 to 2.5V)
    }

    //Pitch Bend
    else if(stat == 0xE0){

    unsigned int timerCalc;

      // We can use the LSB of the pitch bend to create bend values of 64 - 0 - 64

      unsigned char multiplier = 5;
      if(triggerNote <=45){multiplier = 8;}                     //Gives lower frequencies more bend as the timer values are not as close together


     if(midi_b <lastBend && midi_a == 0){                       //Bend down from centre
        bendval = (64 - midi_b); 
        vcobend = oneVolt - (bendval <<1);
        timerVAL = prevtimer + (bendval * multiplier);
     }    
        
     else if(midi_b < lastBend || midi_b > lastBend){           //Bend down from max or up 
          bendval =(midi_b - 63);
          vcobend = oneVolt + (bendval <<1);
          timerVAL = prevtimer - (bendval * multiplier);
     }
      
      if(midi_b == 0x40){                                       //Bend reset
          timerVAL = prevtimer;
          vcobend = oneVolt;
      }
        
      DACWrite(3,vcobend);                                      //Write to analog DAC
      lastBend = midi_b;                                        //Store last value
      return;
    }

    
    //Note On
    else if(stat == 0x90 && midi_a >0 && midi_a >=LowestNote){              
      triggerNote = midi_a - LowestNote;                        //Triggered note for array
     
      if(midi_a > HighestNote){return;}                         //Return if the note is too high
      if(midi_b == 0){NoteOff(); return;}                       //Call note off if note on with velocity zero is received.

      timerVAL = DCOValues[triggerNote];                        //Timer will load on interrupt to prevent glitches
      prevtimer = timerVAL;                                     //Track previous used for pitch bend
      
      DACWrite(1,DCODAC[triggerNote]);                          //Write DAC for DCO offset
      DACWrite(0,VCODAC[triggerNote]);                          //Write DAC to VCO
        
      if((PORTD & 4) == 0){
        PORTD = (PORTD & ~0b00001100) | 12;                     //Switch on gate and trigger
        trigenab = true;
      }
        
      else if(envtrig == true && (PORTD & 4) == 4){             //Re trigger if enabled
        PORTD |= (1<<PORTD3);                                   //Switch on trigger
        trigenab = true;
      }
        
        trigcount = 0;                                          //Reset trigger counter for timer
        lastNote = midi_a;                                      //Store this note played incase of note rollover
        
        return;
    }

   //Note Off
    else if(stat == 0x80 && midi_a >=LowestNote){NoteOff();}

    return;
  
}

void NoteOff(void) {
  if(lastNote == midi_a){                                       //Will only turn off the note that was last pressed e.g rollover key is now the new OFF key
      PORTD = (PORTD & ~0b00001100);                            //Gate and trigger off
      trigenab = false;                                         //Clear and disable everything
    }
    return;
}


//*************************************************************
//12 bit DAC

void DACWrite(byte DAC, unsigned int data) 
{
  byte dacSPI0 = 0;
  byte dacSPI1 = 0;
  byte dacMSB;
  byte dacLSB;
  
  dacMSB = (data >> 8) & 0xFF;
  
  if(DAC == 1 || DAC == 3){ dacMSB |= (1 << 7);}            //set DAC A or DAC B bit

  dacMSB |= 0b01110000;                                     //DAC parameters 
  dacLSB = (data & 0xFF);

  if(DAC == 0 || DAC == 1){PORTD &= ~(1 << PORTD4);}        //Enable DAC chip 0
  if(DAC == 2 || DAC == 3){PORTD &= ~(1 << PORTD5);}        //Enable DAC chip 1
  
  SPDR = dacMSB; 
  while (!(SPSR & (1<<SPIF))){ };

  SPDR = dacLSB;
  while (!(SPSR & (1<<SPIF))){ };
  

  PORTD |= (1 << PORTD4);                                   //Slave Selects HIGH
  PORTD |= (1 << PORTD5);

  return;
}



/* 
 DCO Timer Calculation
  
 ((16000000 / 8) / Freq) / 2 
 or:
 (2000000 / Freq) / 2
   
 The timer is an 8 prescale so it divides down to 2MHz.
 The freq is divided by 2 because it is a 50% duty cycle, We need to double the output frequency
 because the timer needs to toggle ON and then toggle OFF the output.

 
 ********************************************************************

 2021 RH Electronics

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 see <https://www.gnu.org/licenses/>
 */

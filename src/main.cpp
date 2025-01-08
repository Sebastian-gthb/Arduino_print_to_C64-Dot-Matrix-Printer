/*
This code is to comunicate from a Arduino (Atmega or Attiny) to an old C64 dot matrix printer over the serial bus of the C64.

The idea: Instead of using read and write pins and attaching a 7506 (negator open collector) to the write pins... we simply use only one PIN which
we configure as follows:
- release = set PIN to input (and we will only read from it if it is released)
- active low = set PIN to output low 
So we reconfigure the PIN between input and output insted of writing 0 and 1 to the PIN. Thasts the magic of this code.

Then i have written some functions for the Seikosha SP-180VC control codes. They use ESC/P codes, so hopefully you can used it on other printers.

ToDo:
      1.) Fimproving errorhandling (currently at least Arduino reset --> if problem occurs --> output message --> cb_bus_init --> jump out meaningfully, bevore open command)
      2.) converting to the printer codepage (cbm_convert_code): Currently converts upper and lower case letters and numbers. Special characters are
          a problem. We can use a 256 byte array to convert all characters like converted_byte = translation_byte_array[byte_to_convert]
          The letter "A" has a byte value of 97 (decimal) and in the translation_byte_array at position 61 is the byte value of the
          letter "A" from the printer code page = 61 (decimal)
*/
#include <Arduino.h>

// Mapping the C64 serial port lines to Arduino's digital I/O pins
// to protect the Arduino you can connect 1k Ohm resistors between the pin and the serial lines (bus)
int CBM_ATN    = 3;
int CBM_RESET  = 6;
int CBM_CLK    = 4;
int CBM_DATA   = 5;

#define PrintButton 12    //a button to start printing

// VARIABLES
int  i = 0;
bool waiting=true;
bool contact=true;


// FUNCTIONS
void(* resetFunc) (void) = 0;  // Declare reset fuction for the Arduino. If you call this it jump to address 0 and reboot.

void cmb_bus_signal_release(int wire)     // set the signal on the wire to release (inactive / logic=0 / read)
{
  pinMode(wire, INPUT_PULLUP);
}

void cmb_bus_signal_active(int wire)     // set the signal on the wire to active (logic=1)
{
  pinMode(wire, OUTPUT);
  digitalWrite(wire, LOW);
}

// initialisie the serial bus + all attached devices
void cbm_bus_init()
{
  // set the signal to inactive
  cmb_bus_signal_release(CBM_RESET);
  cmb_bus_signal_release(CBM_ATN);
  cmb_bus_signal_release(CBM_CLK);
  cmb_bus_signal_release(CBM_DATA);
  
  // Reset devices
  cmb_bus_signal_active(CBM_RESET);
  delay(100); // 100 ms
  cmb_bus_signal_release(CBM_RESET);
  delay(3000); // 3 seconds
}

void cmb_bus_send_byte(byte daten, bool eoi)
{

  //Stepp 1+2: Signalize "Ready to send" (CLK release) and waiting for "Ready for Data" (DATA release) from Device
  waiting=true;
  i=0;
  cmb_bus_signal_release(CBM_DATA);          // DATA is at this point already released, but to avoid a programming error with the digitalRead if this is not ensured, I set it to inactive/release here again
  cmb_bus_signal_release(CBM_CLK);
  do{
    contact = digitalRead(CBM_DATA);
    if ( contact ) {
      waiting=false;
    }
    else {
      i++;                                      //waiting as long DATA is not high
      if (i > 99999) {
        Serial.println("Error Stepp 2: devices not ready for data (DAT=0) after 100.000 loops");
        resetFunc();                            //Arduino reset ... sorry bad errorhandling
      }
    }
  }while(waiting);

  
  //for the last Byte to send, a Intermission (EOI) is required. wait >200micsek and the device response or only of the response from the device after 200micek.
  if (eoi) {
    
    waiting=true;            //waiting that the device pull down DATA after 200micsek
    i=0;
    do{
      contact = digitalRead(CBM_DATA);
      if ( contact ) {
        i++;                                      //waiting as long DATA is not high
        if (i > 999) {
          Serial.println("Error EOI: Device not responding EOI (DAT=0) after 1000 loops");
          resetFunc();                            //Arduino reset ... sorry bad errorhandling
        }
      }
      else {
        waiting=false;
      }
    }while(waiting);
    
    waiting=true;
    i=0;
    do{
      contact = digitalRead(CBM_DATA);
      if ( contact ) {
        waiting=false;
      }
      else {
        i++;                                      //waiting as long DATA is not high
        if (i > 999) {
          Serial.println("Error EOI: Device not ending EOI responding (DAT=1) after 1000 loops");
          resetFunc();                            //Arduino reset ... sorry bad errorhandling
        }
      }
    }while(waiting);
  }


  delayMicroseconds(40);   //no Intermission (EOI) typ.40 micsec. time Try (0-60 typ.30) or Tne (40-200)


  //sending Byte...
  cmb_bus_signal_active(CBM_CLK);
  int pulsTs = 60;    //min 20, typ 70
  int pulsTv = 15;    //min 20, typ 20, if C64 Listener=60

  for (int i=0; i<8 ; i++) {
    delayMicroseconds(pulsTs);                        //wait time to set data (Ts)
    if (daten >> i & 0x01) {
      cmb_bus_signal_release(CBM_DATA);               //bits on the data line are negated (not documented)  1 is “release” and not “active”!!! (optimization=actually you can omit the line, as DATA is always released)
    }
    else {
      cmb_bus_signal_active(CBM_DATA);
    }
    //delayMicroseconds(20);                            //nach dem setzen der DATA Leitung noch 25us warten (nicht dokumentiert)
    cmb_bus_signal_release(CBM_CLK);                  //say date now valide (CLK = inactive)
    delayMicroseconds(pulsTv);                        //wait time data is valide (Tv)
    cmb_bus_signal_active(CBM_CLK);                   //say data not valide to change data bit or at end of byte transmission
    cmb_bus_signal_release(CBM_DATA);                 //der C64 setzt DATA immer wieder auf inaktiv wenn es ein Bit lang aktive war (nicht dokumentiert)
  }
  

  //warte auf Bestaetigung das das Byte vom Device empfangen wurde
  waiting=true;
  i=0;
  cmb_bus_signal_release(CBM_DATA);
  do{
    contact = digitalRead(CBM_DATA);
    if ( contact ) {
      i++;                                      //waiting as long DATA is not high
      if (i > 999) {                            //no ACK from Device (DAT=1) after 1000 loops
        Serial.println("Error Stepp 4: no ACK from Device (DAT=1) after 1000 loops");             // no ACK from Device for the last Byte
        resetFunc();                            //Arduino reset ... sorry bad errorhandling
      }
    }
    else {
      waiting=false;
    }
  }while(waiting);
}



//Seikosha SP-180VC control codes
void cmb_prncmd_cr()                      // line feed and carriage return
{
  cmb_bus_send_byte(13, false);
}
void cmb_prncmd_italic(bool state)        // italic / kursiv    (true/false)
{
  cmb_bus_send_byte(27, false);
  if (state) {
    cmb_bus_send_byte(52, false);     //on
  }
  else {
    cmb_bus_send_byte(53, false);     //off
  }
}
void cmb_prncmd_underline(bool state)    // underline   (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(45, false);
  if (state) {
    cmb_bus_send_byte(1,  false);     //on
  }
  else {
    cmb_bus_send_byte(0,  false);     //off
  }
}
void cmb_prncmd_bold(bool state)         // bold / fett   (true/false)
{
  cmb_bus_send_byte(27, false);
  if (state) {
    cmb_bus_send_byte(69, false);     //on
  }
  else {
    cmb_bus_send_byte(70, false);     //off
  }
}
void cmb_prncmd_negative(bool state)     // negative    (true/false)
{
  if (state) {
    cmb_bus_send_byte(18, false);     //on
  }
  else {
    cmb_bus_send_byte(146,false);     //off
  }
}
void cmb_prncmd_big(bool state)          // double width  (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(87, false);
  if (state) {
    cmb_bus_send_byte(1,  false);     //on
  }
  else {
    cmb_bus_send_byte(0,  false);     //off  
  }
}
void cmb_prncmd_smallspacing(bool state)   // small spacing / geringer Zeichenabstand  (true/false)
{
  cmb_bus_send_byte(27, false);
  if (state) {
    cmb_bus_send_byte(77, false);   //small
  }
  else {
    cmb_bus_send_byte(80, false);   //normal
  }
}
void cmb_prncmd_superscript(byte state)       //superscript / hoch- oder tiefgestellt  (0=off,1=upper,2=lower)
{
  cmb_bus_send_byte(27, false);
  if (state == 0) {
    cmb_bus_send_byte(84, false);   //superscript on
  }
  else if (state == 1) {
    cmb_bus_send_byte(83, false);   //superscript up
    cmb_bus_send_byte(0,  false);
  }
  else if (state == 2) {  
    cmb_bus_send_byte(83, false);   //superscript down
    cmb_bus_send_byte(1,  false);
  }
}
void cmb_prncmd_graphic(bool state)         // graphic printing mode  (true/false)
{
  if (state) {
    cmb_bus_send_byte(8, false);    //on
  }
  else { 
    cmb_bus_send_byte(15, false);   //off
  }
}
void cmb_prncmd_doubstrike(bool state)         //doubble strike / doppelter Anschlag  (true/false)
{
  cmb_bus_send_byte(27, false);
  if (state) {
    cmb_bus_send_byte(71, false);   //an
  }
  else { 
    cmb_bus_send_byte(72, false);   //aus
  }
}
void cmb_prncmd_nlq(bool state)              // Near Letter Quality (NLQ) / hohe Qualitaet    (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(120,false);
  if (state) {
    cmb_bus_send_byte(1,  false);   //on
  }
  else {
    cmb_bus_send_byte(0,  false);   //off
  }
}
void cmb_prncmd_unidirectional(bool state)      // unidirectional printing / einheitliche Druckrichtung (from left to right)    (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(85, false);
  if (state) {
    cmb_bus_send_byte(1,  false);   //printing direction from left to right
  }
  else {
    cmb_bus_send_byte(0,  false);   //printing direction alternating from left and right (faster)  
  }
}
void cmb_prncmd_leftstartposin(word value)      //left printing starting position in 1/60 inch    (value as word)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(16, false);
  cmb_bus_send_byte(highByte(value), false);   //high byte
  cmb_bus_send_byte(lowByte(value),  false);   //low byte
}
void cmb_prncmd_leftmarin(byte value)      // left margin in number of characters / linke Rand in Anzahl Zeichen     (value as byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(108, false);
  cmb_bus_send_byte(value, false);
}
void cmb_prncmd_rightmarin(byte value)      // right margin in number of characters / rechter Rand in Anzahl Zeichen    (value as byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(81, false);
  cmb_bus_send_byte(value, false);
}
void cmb_prncmd_linefeed(byte value)      // line spacing in 1/216 inch / Zeilenabstand in 1/216 Zoll (23 is normal)     (value as byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(51, false);
  cmb_bus_send_byte(value, false);
}

int cbm_convert_code(char data)      //convert ASCII letters to the code table of the printer
{
  if (data >= 0x41 && data <= 0x5A)
  { // Convert from lower to upper (a->A)
    return data + 32;
  }
  else if (data >= 0x61 && data <= 0x7A)
  { // Convert from upper to lower (A->a)
    return data - 32;
  }
  else
  { // Not an alpha value, no conversion needed
    return data;
  }
}

void cmb_bus_printtxt(const char* text)
{
  int laenge = strlen(text);
  for (int i=0; i<laenge; i++)
  {
    cmb_bus_send_byte(cbm_convert_code(text[i]),false);
  }
  //cmb_bus_send_byte(13, true);
}


void cbm_bus_command(int primcommand, int primaddress, int seccommand, int secaddress)
{

  //call attention on the serial bus to prepare to send a command

  //Step 0: Call Attention und warte das alle Devices ueber DATA low melden das sie bereit sind
  waiting=true;
  contact=false;
  i=0;
  cmb_bus_signal_release(CBM_DATA);          // DATA is at this point already released, but to avoid a programming error with the digitalRead if this is not ensured, I set it to inactive/release here again
  
  cmb_bus_signal_active(CBM_ATN);
  cmb_bus_signal_active(CBM_CLK);
  do{
    contact = digitalRead(CBM_DATA);
    if ( contact ) {
      i++;                                      //waiting as long DATA is not high
      if (i > 999) {
        Serial.println("Error Step 0: devices not in attention (DAT=1) after 1000 loops");
        resetFunc();                            //Arduino reset ... sorry bad errorhandling
      }
    }
    else {
      waiting=false;                              //will normaly be low after 4 loops
    }
  }while(waiting);

  delayMicroseconds(60); // not defined but a smal delay



  cmb_bus_send_byte(primcommand + primaddress, false);

  if (seccommand + secaddress > 0) {
    delayMicroseconds(100);
    cmb_bus_send_byte(seccommand + secaddress, false);
  }


  //release ATN... all done!
  delayMicroseconds(10);       //Tr >20
  cmb_bus_signal_release(CBM_ATN);
}

void printing_text()
{
  cmb_bus_printtxt("1 Text normal");
  cmb_prncmd_cr();
    
  cmb_prncmd_italic(true);
  cmb_bus_printtxt("2 Text kursiv");
  cmb_prncmd_italic(false);
  cmb_prncmd_cr();

  cmb_prncmd_underline(true);
  cmb_bus_printtxt("3 Text unterstrichen");
  cmb_prncmd_underline(false);
  cmb_prncmd_cr();
      
  cmb_prncmd_bold(true);
  cmb_bus_printtxt("4 Text fett");
  cmb_prncmd_bold(false);
  cmb_prncmd_cr();

  cmb_prncmd_big(true);
  cmb_bus_printtxt("5 Text doppelte Breite");
  cmb_prncmd_big(false);
  cmb_prncmd_cr();

  cmb_prncmd_nlq(true);
  cmb_bus_printtxt("6 Text hohe Qualitaet");
  cmb_prncmd_nlq(false);
  cmb_prncmd_cr();

  cmb_bus_printtxt("7 Text normal");
  cmb_bus_send_byte(13, true);   //carriage return + end (last byte)

}

void printing_graphic()
{
  cmb_prncmd_nlq(true);
  cmb_prncmd_graphic(true);


/*
Each byte >=128 represents a column of 7 pixel (yes not 8). Bit 0 to 6 are the pixel information. The upper dot is bit 0 and the bottom pixel is bit 6.
Bit 7 (bytes <128) indicates whether the byte contains pixel information or specifies command. Byte 26 means, that the next byte indicates how often the next pixel byte will be printed.
Example:
201, 201, 201 <-- prints 3  columns with 3 pixels (upper, middle and bottom)
 26,  15, 201 <-- prints 15 columns with 3 pixels (upper, middle and bottom)

Each row ends with byte 13 (decimal) = CR.
*/
                    // 19x weiss,                                                                  ____4x 145       ___5x  137       ____5x 241
  byte testbild[] = {26, 19, 128, 192, 224, 176, 144, 152, 136, 140, 132, 132, 198, 162, 162, 162, 26, 4, 145, 153, 26, 5, 137, 153, 26, 5, 241, 226, 226, 226, 194, 196, 132, 132, 140, 136, 152, 144, 160, 224, 192, 13,
                     26, 14, 128, 224, 248, 252, 254, 199, 129, 26, 5, 128, 188, 194, 161, 144, 184, 248, 248, 252, 252, 250, 250, 250, 254, 254, 26, 4, 246, 255, 239, 239, 231, 175, 255, 143,143, 191, 191, 255, 254, 252, 248, 224, 128, 128, 128, 129, 130, 140, 152, 152, 136, 136, 136, 136, 132, 196, 198, 194, 226, 26, 4, 162, 146, 147, 145, 145, 145, 147, 210, 210, 26, 5, 226, 196, 196, 132, 132, 136, 136, 136, 144, 144, 160, 160, 192, 13,
                     26, 13, 128, 192, 195, 207, 223, 191, 255, 254, 252, 248, 240, 240, 224, 224, 193, 193, 195, 134, 132, 132, 137, 26, 4, 139, 159, 26, 5, 151, 159, 159, 143, 143, 136, 143, 132, 132, 132, 199, 195, 195, 193, 161, 161, 144, 240, 224, 26, 5, 128, 134, 158, 177, 193, 161, 176, 144, 144, 248, 232, 232, 248, 236, 244, 228, 236, 136, 134, 130, 195, 195, 219, 159, 159, 239, 255, 159, 159, 191, 255, 254, 254, 252, 248, 224, 128, 128, 128, 129, 130, 140, 184, 224, 13,
                     128, 128, 128, 224, 224, 208, 216, 168, 228, 228, 218, 217, 213, 156, 134, 128, 240, 255, 254, 240, 185, 143, 199, 227, 187, 187, 187, 251, 151, 135, 207, 191, 254, 254, 190, 158, 206, 206, 210, 210, 210, 146, 162, 226, 26, 4, 194, 193, 193, 225, 225, 160, 160, 176, 144, 144, 136, 142, 191, 255, 255, 254, 248, 240, 240, 224, 192, 192, 129, 131, 130, 132, 136, 137, 147, 26, 5, 191, 26, 4, 254, 255, 223, 223, 255, 255, 223, 160, 160, 160, 144, 152, 159, 143, 135, 135, 129, 128, 128, 192, 192, 160, 144, 248, 143, 13,
                     130, 135, 143, 159, 191, 188, 176, 184, 143, 159, 156, 191, 255, 254, 242, 194, 255, 255, 253, 132, 254, 131, 249, 255, 255, 248, 144, 254, 194, 249, 252, 231, 225, 200, 212, 214, 209, 223, 223, 175, 167, 179, 208, 248, 183, 140, 135, 225, 144, 140, 226, 255, 248, 184, 152, 132, 194, 178, 138, 254, 224, 253, 253, 255, 151, 135, 207, 175, 239, 239, 255, 254, 254, 254, 252, 156, 188, 188, 26, 6, 248, 200, 200, 200, 26, 9, 136, 132, 132, 132, 194, 194, 194, 161, 176, 144, 136, 134, 129, 13,
                     26, 15, 128, 26, 4, 129, 131, 131, 129, 129, 129, 131, 135, 134, 135, 143, 135, 143, 143, 143, 159, 159, 159, 150, 26, 4, 144, 142, 143, 159, 191, 254, 254, 254, 253, 253, 133, 133, 130, 130, 242, 249, 248, 254, 254, 247, 247, 215, 145, 176, 172, 170, 169, 248, 255, 255, 255, 207, 199, 161, 216, 254, 252, 240, 128, 143, 159, 143, 195, 240, 252, 186, 143, 134, 226, 154, 138, 134, 254, 250, 250, 250, 135, 133, 221, 241, 13,
                   //                         131,     5x 135  143, 142, 140, 140, 136, 143, 143, 159, 159, 191, 188, 152, 143, 159, 191, 255, 255, 254, 226, 194, 191, 191, 255, 254, 252, 252, 251, 251, 250, 155, 139, 139, 137, 132, 132, 194, 255, 143
                     26, 47, 128, 26, 9, 129, 131, 26, 5, 135, 143, 142, 140, 140, 136, 143, 143, 159, 159, 191, 188, 152, 143, 159, 191, 255, 255, 254, 226, 194, 191, 191, 255, 254, 252, 252, 251, 251, 250, 155, 139, 139, 137, 132, 132, 194, 255, 143, 13,
                     26, 80, 128, 129, 129, 128, 128, 128, 129, 131, 131, 131, 135, 135, 134, 134, 134, 130, 130, 129, 13 };    
  int laenge = sizeof(testbild);
  
  for (int i=0; i<laenge; i++)
  {
    cmb_bus_send_byte(testbild[i], false);
  }

  cmb_prncmd_graphic(false);
  cmb_prncmd_nlq(false);
  cmb_bus_send_byte(13, true);   //carriage return + end (last byte)
}




// Arduino setup function is run once when the sketch starts.
void setup()
{ 

  pinMode(PrintButton, INPUT_PULLUP);

  // Begin serial communication with PC.
  Serial.begin(115200);
  Serial.println("running...");

  // init serial bus and reset all devices
  cbm_bus_init();

}  
  
  
// Arduino loop function is run over and over again, forever.
void loop()
{

  if (!digitalRead(PrintButton)) {
    cbm_bus_command(0x20, 4, 0x60, 7);     //primcommand 0x20=open, primaddr 4=printer, seccommand 0x60, secaddr (0=graphic 7=business)

    printing_graphic();
    //printing_text();
    

    delay(500);
    cbm_bus_command(0x20, 4, 0xE0, 7);    // close channel (device 4, secaddr 7)
    delay(100);
    cbm_bus_command(0x3F, 0, 0, 0);       // 3Fh = unlisten all devices  <-- I don't know if this is necessary, but the C64 does it
  }
  
  delay(100);


}
#include <Arduino.h>

// Mapping of C64 serial port lines to Arduino's digital I/O pins
int CBM_ATN    = 3;
int CBM_RESET  = 6;
int CBM_CLK    = 4;
int CBM_DATA   = 5;

#define PrintButton 12    //a button to start printing

// VARIABLES
int  i = 0;
bool warte=true;
bool kontakt=true;


// FUNCTIONS
void(* resetFunc) (void) = 0;  // declare reset fuction at address 0

/*
 !!!!Idee: anstatt read und write Pins zu nutzen und an die write Pins noch einen 7506 (negierer open collector) dran zu hängen... nutzen wir einfach nur jeweils einen PIN, den wir wie folgt konfirgurieren:
 - release = PIN auf Input setzen (und nur wenn er released ist werden wir auch davon lesen muessen)
 - aktive low = PIN auf Output low setzen

 */
void cmb_bus_signal_release(int Leitung)
{
  pinMode(Leitung, INPUT_PULLUP);
}

void cmb_bus_signal_active(int Leitung)
{
  pinMode(Leitung, OUTPUT);
  digitalWrite(Leitung, LOW);
}

// initialisiere den seriellen Bus + die angeschlossenen Geraete
void cbm_bus_init()
{
  // Signalpegel auf inaktiv setzen
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

  //Stepp 1+2: Signalisiere "Ready to send" (CLK release) und warte auf "Ready for Data" (DATA release) vom Device
  warte=true;
  i=0;
  cmb_bus_signal_release(CBM_DATA);          // DATA muss hier eigentlich schon released sein, aber damit es nicht zu einem Programmfehler mit dem digitalRead kommt, wenn das nicht sichergestellt ist, setze ich es hier noch mal auf inaktiv/release
  cmb_bus_signal_release(CBM_CLK);
  do{
    kontakt = digitalRead(CBM_DATA);
    if ( kontakt ) {
      warte=false;
    }
    else {
      i++;                                      //so lange DATA nicht high ist warten
      if (i > 99999) {
        Serial.println("Error Stepp 2: devices not ready for data (DAT=0) after 100.000 loops");
        resetFunc();                            //Arduino neustarten
      }
    }
  }while(warte);

  
  //for the last Byte to send, a Intermission (EOI) is required. wait >200micsek and the device response or only of the response from the device after 200micek.
  if (eoi) {
    
    warte=true;            //warte das das Device nach ca. 200miksk. DATA nach unten zieht
    i=0;
    do{
      kontakt = digitalRead(CBM_DATA);
      if ( kontakt ) {
        i++;                                      //so lange DATA nicht high ist warte 1000x
        if (i > 999) {
          Serial.println("Error EOI: Device not responding EOI (DAT=0) after 1000 loops");
          resetFunc();                            //Arduino neustarten
        }
      }
      else {
        warte=false;
      }
    }while(warte);
    
    warte=true;
    i=0;
    do{
      kontakt = digitalRead(CBM_DATA);
      if ( kontakt ) {
        warte=false;
      }
      else {
        i++;                                      //so lange DATA nicht high ist warte 1000x
        if (i > 999) {
          Serial.println("Error EOI: Device not ending EOI responding (DAT=1) after 1000 loops");
          resetFunc();                            //Arduino neustarten
        }
      }
    }while(warte);
  }


  delayMicroseconds(40);   //kein Intermission (EOI) typ.40 mikrosek. time Try (0-60 typ.30) oder Tne (40-200)


  //sende Byte...
  cmb_bus_signal_active(CBM_CLK);
  int pulsTs = 60;    //min 20, typ 70                          (war auf 60 gesetzt)
  int pulsTv = 15;    //min 20, typ 20, wenn C64 Listener 60    (war auf 15 gesetzt)

  for (int i=0; i<8 ; i++) {
    delayMicroseconds(pulsTs);                        //wait time to set data (Ts)
    if (daten >> i & 0x01) {
      cmb_bus_signal_release(CBM_DATA);               //Bits auf der Datenleitug sind negiert (nicht dokumentiert) also eine 1 ist "release" und nicht "active"!! (optimierung=eigentlich kann man die Zeile weglassen, da DATA immer released ist)
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
  warte=true;
  i=0;
  cmb_bus_signal_release(CBM_DATA);
  do{
    kontakt = digitalRead(CBM_DATA);
    if ( kontakt ) {
      i++;                                      //so lange DATA high ist warten 100x
      if (i > 999) {                             //no ACK from Device (DAT=1) after 100 loops
        Serial.println("Error Stepp 4: no ACK from Device (DAT=1) after 1000 loops");             // no ACK from Device for the last Byte
        resetFunc();                            //Arduino neustarten
      }
    }
    else {
      warte=false;                                    //wird etwa nach XX loops high
    }
  }while(warte);
}



//Seikosha SP-180VC control codes
void cmb_prncmd_cr()      //Zeilenumbruch und Wagenruecklauf = CR
{
  cmb_bus_send_byte(13, false);   //Zeilenumbruch
}
void cmb_prncmd_italic(bool schalter)        //kursiv    (true/false)
{
  cmb_bus_send_byte(27, false);
  if (schalter) {
    cmb_bus_send_byte(52, false);     //an
  }
  else {
    cmb_bus_send_byte(53, false);     //aus
  }
}
void cmb_prncmd_underline(bool schalter)    //unterstrichen   (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(45, false);
  if (schalter) {
    cmb_bus_send_byte(1,  false);     //an
  }
  else {
    cmb_bus_send_byte(0,  false);     //aus
  }
}
void cmb_prncmd_bold(bool schalter)         //fett   (true/false)
{
  cmb_bus_send_byte(27, false);
  if (schalter) {
    cmb_bus_send_byte(69, false);     //an
  }
  else {
    cmb_bus_send_byte(70, false);     //aus
  }
}
void cmb_prncmd_negative(bool schalter)     //Negativdruck    (true/false)
{
  if (schalter) {
    cmb_bus_send_byte(18, false);     //an
  }
  else {
    cmb_bus_send_byte(146,false);     //aus
  }
}
void cmb_prncmd_big(bool schalter)          //doppelte Breite  (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(87, false);
  if (schalter) {
    cmb_bus_send_byte(1,  false);     //an
  }
  else {
    cmb_bus_send_byte(0,  false);     //aus  
  }
}
void cmb_prncmd_smallspacing(bool schalter)   //geringer Zeichenabstand  (true/false)
{
  cmb_bus_send_byte(27, false);
  if (schalter) {
    cmb_bus_send_byte(77, false);   //Zeichenabstand enger
  }
  else {
    cmb_bus_send_byte(80, false);   //Zeichenabstand normal
  }
}
void cmb_prncmd_superscript(byte schalter)       //hoch- oder tiefgestellt/superscript  (0=aus,1=hoch,2=tief)
{
  cmb_bus_send_byte(27, false);
  if (schalter == 0) {
    cmb_bus_send_byte(84, false);   //superscript aus
  }
  else if (schalter == 1) {
    cmb_bus_send_byte(83, false);   //superscript oben
    cmb_bus_send_byte(0,  false);
  }
  else if (schalter == 2) {  
    cmb_bus_send_byte(83, false);   //superscript unten
    cmb_bus_send_byte(1,  false);
  }
}
void cmb_prncmd_graphic(bool schalter)         //Grafikdruck  (true/false)
{
  if (schalter) {
    cmb_bus_send_byte(8, false);    //an
  }
  else { 
    cmb_bus_send_byte(15, false);   //aus
  }
}
void cmb_prncmd_doubstrike(bool schalter)         //doppelter Anschlag  (true/false)
{
  cmb_bus_send_byte(27, false);
  if (schalter) {
    cmb_bus_send_byte(71, false);   //an
  }
  else { 
    cmb_bus_send_byte(72, false);   //aus
  }
}
void cmb_prncmd_nlq(bool schalter)              //hohe Qualitaet/Near Letter Quality (NLQ)    (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(120,false);
  if (schalter) {
    cmb_bus_send_byte(1,  false);   //an
  }
  else {
    cmb_bus_send_byte(0,  false);   //aus
  }
}
void cmb_prncmd_unidirectional(bool schalter)      //einheitliche Druckrichtung (von links nach rechts)    (true/false)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(85, false);
  if (schalter) {
    cmb_bus_send_byte(1,  false);   //Druckrichtung links nach rechts
  }
  else {
    cmb_bus_send_byte(0,  false);   //Druckrichtung hin und her (schneller)  
  }
}
void cmb_prncmd_leftstartposin(word wert)      //linke Druckstartposition in 1/60 Zoll    (word)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(16, false);
  cmb_bus_send_byte(highByte(wert), false);   //high byte
  cmb_bus_send_byte(lowByte(wert),  false);   //low byte
}
void cmb_prncmd_leftmarin(byte wert)      //linke Rand in Anzahl Zeichen     (byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(108, false);
  cmb_bus_send_byte(wert, false);
}
void cmb_prncmd_rightmarin(byte wert)      //rechter Rand in Anzahl Zeichen    (byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(81, false);
  cmb_bus_send_byte(wert, false);
}
void cmb_prncmd_linefeed(byte wert)      //Zeilenabstand in 1/216 Zoll (23 ist etwa normal)     (byte)
{
  cmb_bus_send_byte(27, false);
  cmb_bus_send_byte(51, false);
  cmb_bus_send_byte(wert, false);
}


void cmb_bus_printtxt(const char* text)
{
  int laenge = strlen(text);
  for (int i=0; i<laenge; i++)
  {
    cmb_bus_send_byte(cbm_switch_case(text[i]),false);
  }
  //cmb_bus_send_byte(13, true);
}

int cbm_switch_case(char data)
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

void cbm_bus_command(int primcommand, int primaddress, int seccommand, int secaddress)
{

  //call attention on the serial bus to prepare to send a command

  //Step 0: Call Attention und warte das alle Devices ueber DATA low melden das sie bereit sind
  warte=true;
  kontakt=false;
  i=0;
  cmb_bus_signal_release(CBM_DATA);          // DATA muss hier eigentlich schon released sein, aber damit es nicht zu einem Programmfehler mit dem digitalRead kommt, wenn das nicht sichergestellt ist, setze ich es hier noch mal auf inaktiv/release
  
  cmb_bus_signal_active(CBM_ATN);
  cmb_bus_signal_active(CBM_CLK);
  do{
    kontakt = digitalRead(CBM_DATA);
    if ( kontakt ) {
      i++;                                      //so lange DATA nicht low-active ist warten 1000x
      if (i > 999) {
        Serial.println("Error Step 0: devices not in attention (DAT=1) after 1000 loops");
        resetFunc();                            //Arduino neustarten
      }
    }
    else {
      warte=false;                              //wird etwa nach 4 loops low
    }
  }while(warte);

  delayMicroseconds(60); // nicht definiert, aber eine kleine Pause



  cmb_bus_send_byte(primcommand + primaddress, false);

  if (seccommand + secaddress > 0) {
    delayMicroseconds(100);     //eine Pause zwischen den Bytes von 100miksek muss noch rein... wenn es mal gelungen ist ein Byte erfolgreicht zu senden
    cmb_bus_send_byte(seccommand + secaddress, false);
  }


  //ATN wieder los lassen... fertig
  delayMicroseconds(10);       //Tr >20
  cmb_bus_signal_release(CBM_ATN);
}




// Arduino setup function is run once when the sketch starts.
void setup()
{ 
  // Set pins to either input or output.

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
    cbm_bus_command(0x20, 4, 0x60, 7);     //primcommand 0x20=open, primaddr 4=printer, seccommand 0x60, secaddr (0=grafik 7=busines)

// ToDo:
//       Fehlerhaendling verbessern (aktuell immerhin Arduino reset --> wenn Problem auftaucht --> Meldung ausgeben --> cb_bus_init --> Sinnvoll rausspringen, vor open command 



/*
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
    cmb_bus_send_byte(13, true);   //Zeilenumbruch + Ende
*/

    cmb_prncmd_nlq(true);
    cmb_prncmd_graphic(true);

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
    cmb_bus_send_byte(13, true);
    

    delay(500);
    cbm_bus_command(0x20, 4, 0xE0, 7);    // close channel (device 4, secaddr 7)
    delay(100);
    cbm_bus_command(0x3F, 0, 0, 0);       // 3Fh = unlisten all devices  <-- ich weiss nicht ob das noetig ist, aber der C64 macht das
  }
  
  delay(100);


}
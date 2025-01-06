This code is to comunicate from a Arduino (Atmega or Attiny) to an old C64 dot matrix printer over the serial bus of the C64.

The idea: Instead of using read and write pins and attaching a 7506 (negator open collector) to the write pins... we simply use only one PIN which
we configure as follows:
- release = set PIN to input (and we will only read from it if it is released)
- active low = set PIN to output low 
So we reconfigure the PIN between input and output insted of writing 0 and 1 to the PIN. Thasts the magic of this code.

Then i have written some functions for the Seikosha SP-180VC control codes. They use ESC/P codes, so hopefully you can used it on other printers.
The printer supports text mode output and graphic outut. I have tow samples for this.

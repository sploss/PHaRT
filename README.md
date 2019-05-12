# PHaRT
Pocket Ham Radio Tool - small radio memory programmer using Chirp .img files, arduino based

Background:
I share my time between two homes, one in Ohio and one in Tennessee.  I am a Ham Radio operator (K8GN) and have a programmable mobile radio in my car.  I needed a convenient way to change the channel memories for each location.  Simply programming the radio for both locations is not ideal.  Scanning the memories would waste half the time on the wrong channels and there may be two channels with the same frequency but different PL tones.

Hardware Choices:
Most radios are programmed with an asyncronous TTL serial connection.  So my selection of hardware needed the following; a hardware USART, SD card to store the channel memories, an I2C port for display control, and at least one GPIO for user interface.  Not essential, but useful, is a computer serial connection to aid in file management.  Fortunately, I found the perfect platform in the Arduino MKR ZERO.

The display I chose is the 0.96" 128x64 I2C OLED display with the SSD1306 interface.

The user control consists of a single pushbutton.

Software:
Programming with the Arduino C++ has a bit of a learning curve.  I spent a large part of my time testing code to see how it worked.  Once you get used to it, it's a breeze.

I designed the current version of the software somewhat selfishly, it's only designed to work with one radio, the BTech UV2501+220.  It's a 25 watt radio for the ham 144 MHz, 220 MHz, and 440 MHz bands.  The radio driver is hard coded.  One future goal is the have the drivers for the different radios on the SD card so the user can select the appropriate driver for his radio.

/*********************************************************************
*********************************************************************/

#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#include <SD.h>

File root;

#define maxlen 16   //Max string length of each filename (FAT16 = 8.3 + string terminator)
#define maxnum 64   //Max number of filenames (it has to be something) 
char dirBuf[maxnum][maxlen];
char imgDirBuf[maxnum][maxlen];
char tempname[16];
char nIMG = 0;
char nDIR = 0;
File dataFile;
char errMSG[4][11];
int errNum;

//# of bytes per block when sending to radio
#define blockSize   0x10
byte blockBuf[blockSize];
char charBlockBuf[blockSize];
int nChars;

//# of bytes in the pre-block header when sending to radio
#define hdrSize     5
byte blockHdr[hdrSize] = {0x06, 'X', 0, 0, blockSize};

//Magic string for UV2501+220
byte magic[8] = {0x55,0x20,0x15,0x12,0x12,0x01,0x4d,0x02};

#define memSize 0x3100  //Size of writable channel memory
#define imgSize 0x4000  //Full size of the image files

//For progress bar: # of block transfers between status bar updates
//Based on a progress bar length of 128 pixels
#define blocksPerUpdate ((memSize/blockSize)>>7)

int memCount = 0;
int blockCount = 0;

int led_val = 0;

#define PUSHBUTTON 7
boolean btn_val_rdy = false;
int debounce_cntr = 0;
int debounced_val = 1;
int prev_debounced_val = 1;
int btn_down_cntr = 0;
int btn_up_cntr = 0;
int btn_val = !0;
int prev_btn_val = !0;

boolean short_press = false;
boolean long_press = false;

int i=0, idx=0;
byte serBuf[69];
char serInBuf[128];
char charBuf;
byte ack=0;

#define OLED_RESET 4
Adafruit_SSD1306 display(OLED_RESET);
#define SSD1306_ADR 0x3C



/**********************************************************/
/* Function Prototypes */
void getSerialIn ();
void debounceBtn ();
void getButtonPress ();
int programRadio ();
void dispERR ();
int openDatafile (char fileName[12]);
int doMagic ();
int sendData ();
void refreshDisplay ();
char getDirectory (File dir, char strarray[maxnum][maxlen]);
char saveIMG (char strarray[maxnum][maxlen]);
void sortIMG (char strarray[maxnum][maxlen], char asize);
void rotateIMG (char strarray[maxnum][maxlen], char asize);
void showIMG (char strarray[maxnum][maxlen],char asize);
void TC4_setup ();
void TC4_Handler ();



/*****************************************************************/
void setup ()   {
    // by default, we'll generate the high voltage from the 3.3v line internally! (neat!)
    display.begin (SSD1306_SWITCHCAPVCC, SSD1306_ADR);  // initialize with the I2C addr 0x3D (for the 128x64)
    
    Serial1.begin (9600);//RX-TX Pins on CPU?
    while (Serial1.available ()) Serial1.read ();//Clear serial buffer
    
    Serial.begin (115200);
    while (Serial.available ()) Serial.read ();//Clear serial buffer
    
    pinMode (PUSHBUTTON, INPUT);
    pinMode (LED_BUILTIN, OUTPUT);

    display.clearDisplay ();
    //display.dim (1);
    display.display ();
    
    if (!SD.begin (SDCARD_SS_PIN)) {
        display.setTextSize (2);
        display.setTextColor (BLACK,WHITE);
        display.setCursor (0,0);
        display.println ("   No     ");
        display.println (" SDcard!! ");
        display.display ();
        return;
    }

    refreshDisplay ();
    TC4_setup ();
}//setup


/*****************************************************************/
void loop () {
    
    /////////////////////////
    //Debounce the pushbutton
    if (btn_val_rdy) {
        btn_val_rdy = false;
        debounceBtn ();    
        getButtonPress ();
    }

    //////////////////////////////
    //After a short press...
    //Rotate through the filenames
    if (short_press) {
        short_press = false;
        rotateIMG (imgDirBuf,nIMG);
        showIMG (imgDirBuf,nIMG);
    }

    ///////////////////////////////////////////////
    //After a long press...
    //Program the radio with the selected .img file
    if (long_press) {
        long_press = false;
        REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts

        //Program the radio, else display error
        if (programRadio ()) dispERR ();

        //Show the file list again
        display.clearDisplay ();
        display.setCursor (0,0);
        display.setTextSize (2);
        display.display ();
        showIMG (imgDirBuf,nIMG);
        
        REG_TC4_INTENSET = TC_INTENSET_OVF;          // Enable TC4 interrupts
        btn_down_cntr = 0;
        debounced_val = 1;
        prev_debounced_val = 1;
    }//if (long_press)


    /////////////////////////////////////////
    //Check serial port
    if (Serial.available()) {
        getSerialIn ();    
    }
    
    
}//loop ()



//*****************************************************************
void getSerialIn (){

    //Add incoming characters to the buffer
    //until '\r' is sent
    charBuf = Serial.read ();
    if (charBuf != '\r' && charBuf != '\n'){
        strcat (serInBuf, &charBuf);
        return;
    }

    //Clear the serial buffer of any leftovers
    while (Serial.available()) Serial.read();
    
    //Parse the input into a command word, an 8.3 filename, 
    //and a decimal number of bytes to transfer
    //separated by spaces
    char cmd[32], param1[32], param2[32];
    strcpy (cmd, strtok (serInBuf, " "));
    strcpy (param1, strtok (NULL, " "));
    strcpy (param2, strtok (NULL, " "));
    
    strcpy (cmd, strupr (cmd));
    strcpy (param1, strupr (param1));

    //Show the help message
    if (strcmp (cmd, "?") == 0) {
        Serial.print ("Usage:");
        Serial.println ("\t\t\t_________________________________________________________________");
        Serial.println ("'?' \t\t\tThis message");
        Serial.println ("'DIR' \t\t\treturns a list of files in the root directory");
        Serial.println ("'DEL filename.ext' \tdeletes the named file from the root directory");
        Serial.println ("'GET filename.ext' \treturns the binary contents of the named file");
        Serial.println ("'PUT filename.ext' \tcreates or overwrites the named file with the data that follows");
    }

    //Return the file list for the root directory
    if (strcmp (cmd,"DIR") == 0) {
        root = SD.open ("/");
        nDIR = getDirectory (root,dirBuf);
        root.close ();

        for (i=0; i<nDIR; i++) {
            Serial.println (dirBuf[i]);
        }
    }
        
    //Delete the named file if it exists
    if (strcmp (cmd,"DEL") == 0 && strcmp (param1,"/") != 0) {
        if (SD.exists (param1)) {
            SD.remove (param1);
            refreshDisplay ();
        }
    }

    //Return the binary contents of the named file
    if (strcmp (cmd,"GET") == 0) {
        dataFile = SD.open (param1);
        if (!dataFile) {
            return;
        }
        delay(500);
        REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts
        memCount = 0;
        while (memCount < imgSize) {
            dataFile.read (blockBuf,blockSize);
            Serial.write (blockBuf,blockSize);
            memCount += blockSize;
            delay(3);
        }
        
        dataFile.close ();
        REG_TC4_INTENSET = TC_INTENSET_OVF;          // Enable TC4 interrupts
    }

    //Writes received bytes to the named file
    //If file opens, send back "OK" and wait for data
    //If opening fails, send back "FAIL"
    if (strcmp (cmd,"PUT") == 0) {
        strcpy (tempname,param1);
        dataFile = SD.open (tempname,FILE_WRITE);
        if (!dataFile) {
            Serial.print ("FAIL");
        } else {
            REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts
            //dataFile.seek (0);
            while (Serial.available()) Serial.read();
            Serial.print ("OK");
            //*charBlockBuf = 0;
            nChars = 0;
            memCount = 0;
            while (true) {
                nChars = Serial.readBytes (charBlockBuf,blockSize);
                dataFile.write (charBlockBuf,nChars);
                memCount += nChars;
                if (nChars == 0) break;
            }
            dataFile.close ();
            refreshDisplay ();
            REG_TC4_INTENSET = TC_INTENSET_OVF;          // Enable TC4 interrupts
        }
    }
    
    *serInBuf = 0;
    while (Serial.available ()) Serial.read (); //Clean out the serial buffer
    
}



//*****************************************************************
// Debounce the button press
// Called after a new btn_val is ready
// Sets the debounced button value
//
void debounceBtn() {
    
    if(btn_val == prev_btn_val){
        debounce_cntr++;        //Number of consecutive button values
    } else {
        debounce_cntr = 0;
    }//if
    prev_btn_val = btn_val;
    
    //Process debounced button values
    if(debounce_cntr > 3){//40mS duration
        debounce_cntr = 3;
        debounced_val = btn_val;
    }
}


//*****************************************************************
// Get the button press
//
void getButtonPress(){
    if(debounced_val != prev_debounced_val){//debounced_val changed
        if(debounced_val == 0){//Pushbutton was just pressed
            btn_down_cntr = 0;
        } else {//Pushbutton was just released
            btn_up_cntr = 0;
            if(btn_down_cntr > 0 && btn_down_cntr < 50){//Short button press < 0.5 seconds
                short_press = true;
            }
        }//if(debounced_val == 0)
        prev_debounced_val = debounced_val;
        
    } else {//debounced_val did not change
        //if(btn_val == 0){
        if(debounced_val == 0){
            if(btn_down_cntr != 65535) btn_down_cntr++;
            if(btn_down_cntr > 200) long_press = true;
        } else {
            if(btn_up_cntr != 65535) btn_up_cntr++;
        }
    }//if(debounced_val != prev_debounced_val)
}




//*****************************************************************
// Program the radio
//
int programRadio() {
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Program-");
    display.println("ming");
    display.println("radio now.");
    display.display();
    
    //Clear the message buffer
    for(i=0;i<4;i++) {
        strcpy(errMSG[i], "");
    }
    
    //Open selected image file
    //if not opened, error message, wait 3 secs and exit
    errNum = openDatafile( imgDirBuf[0] );
    if( errNum == 1){
        strcpy(errMSG[0], "No File");
        return 1;
    }
    
    //Send magic & verify radio
    //if not ack, error message, wait 3 secs and exit
    errNum = doMagic();
    if( errNum == 1 ) {
        strcpy(errMSG[0], "No Radio");
        dataFile.close();
        return 1;
    }
    if( errNum == 2 ) {
        strcpy(errMSG[0], "No ACK");
        strcpy(errMSG[1], "from radio");
        dataFile.close();
        return 1;
    }
    
    errNum = sendData();
    if(errNum == 1){
        strcpy(errMSG[0], "No ACK");
        strcpy(errMSG[1], "while");
        strcpy(errMSG[2], "sending");
        dataFile.close();
        return 1;
    }
    if(errNum == 2){
        strcpy(errMSG[0], "Bad ACK");
        strcpy(errMSG[1], "while");
        strcpy(errMSG[2], "sending");
        dataFile.close();
        return 1;
    }
    
    dataFile.close();
    return 0;
}



//*****************************************************************
// An error occurred. Display the error message for 2 seconds
//
void dispERR() {
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    for(i=0;i<4;i++) {
        display.println(errMSG[i]);
        //Serial.println(errMSG[i]);
    }
    display.display();
    delay(2000);
}



//*****************************************************************
// Open the selected file for reading
//
int openDatafile( char fileName[13] ) {
    
    strcpy(tempname,fileName);
    strcat(tempname,".IMG");
    dataFile = SD.open(tempname);
    if(!dataFile) {
        return 1;
    }

    return 0;
}



//*****************************************************************
// Send the magic string to the radio to put it into program mode
//
int doMagic(){

    while(Serial1.available()) Serial1.read();//Clear serial buffer

    Serial1.write(magic, 8);
    idx = 0;
    while(!Serial1.available()){ //Wait for ack
        delay(10);
        if(idx++ > 100){
            return 1; //ack took too long
        }
    }
    
    ack = Serial1.read();
    if(ack != 6){
        return 2;
    }
    
    delay(500);
    Serial1.readBytes(serBuf,49);
    while(Serial1.available()) Serial1.read();//Clear remaining bytes

    return 0;
}



//*****************************************************************
// A file has been selected, now send the contents to the radio
//
int sendData() {
    int x=0, y0=48, y1=63; //For the progress bar
    
    while(Serial1.available()) Serial1.read();//Clear serial buffer

    blockCount = 0;
    memCount = 0;
    while( memCount < memSize ) {
        blockHdr[2] = (byte)(memCount>>8);  //Big endian (MSB first)
        blockHdr[3] = (byte)(memCount&0xFF);
        dataFile.read(blockBuf,blockSize);  //Get block from file
        
        //Send header bytes
        for(idx=0;idx<5;idx++) {
            Serial1.write(blockHdr[idx]);
            delay(2);
        }//for
        
        //Send one block
        for(idx=0;idx<blockSize;idx++) {
            Serial1.write(blockBuf[idx]);
            delay(2);
        }//for

        //Wait for ack for at most 2 secs
        while(!Serial1.available()){ 
            delay(10);
            if(idx++ > 200){
                return 1; //Didn't get ack, return with error
            }//if
        }//while
        
        ack = Serial1.read();
        if(ack != 6 && ack != 5){
            return 2; //Didn't get the correct ack, return with error
        }//if
    
        memCount = memCount + blockSize;
        
        //Display a progress bar across the bottom
        if(blockCount++ % blocksPerUpdate == 0) {
            display.drawLine(x,y0,x,y1,WHITE);
            display.display();
            x++;
        }//if
        
    }//while
    
    return 0;
}


//*****************************************************************
// Refresh the display file listing
//
void refreshDisplay () {
    root = SD.open ("/");
    
    display.clearDisplay ();
    //display.dim (1);
    display.display ();
    
    getDirectory (root, imgDirBuf);
    root.close ();
    
    nIMG = saveIMG (imgDirBuf);
    if (nIMG == 0) {
        display.setTextSize (2);
        display.setTextColor (BLACK,WHITE);
        display.setCursor (0,0);
        display.println ("   No     ");
        display.println ("  .IMG    ");
        display.println ("  Files   ");
        display.display ();
        return;
    }
    sortIMG (imgDirBuf,nIMG);
    showIMG (imgDirBuf,nIMG);
}




//*****************************************************************
// Retrieve a file list from SD Card root directory
//
char getDirectory(File dir, char strarray[maxnum][maxlen]) {
    char nfiles = 0;
  
    while (true) {

        File entry =  dir.openNextFile();
        if (! entry) {
            // no more files
            break;
        }
        strcpy(strarray[nfiles++],entry.name());
        entry.close();
    }
    return nfiles;
}



//*********************************************************************
// Find .img files from directory, strip off .img and save the filename
//         
char saveIMG(char strarray[maxnum][maxlen]){
    char i = 0, j = 0, nIMG;
    char * pIMG;

    while(*strarray[i]){
        if(pIMG = strstr(strarray[i], ".IMG")){
            strarray[i][(pIMG-strarray[i])]=0;
            strcpy(strarray[j++], strarray[i]);
        }
        i++;
    }
    nIMG = j;
    for(i=j;i<maxnum;i++){
        *strarray[i]=0;
    }
    return nIMG;
}



//*****************************************************************
// In-place alphabetical sort of filenames
//
void sortIMG(char strarray[maxnum][maxlen], char asize){
    char t[maxlen];
    
    for(int i=1; i<asize; i++) {
        for(int o=0; o<(asize-i); o++) {
            if(strcmp(strarray[o], strarray[o+1]) > 0) {
                strcpy(t, strarray[o]);
                strcpy(strarray[o], strarray[o+1]);
                strcpy(strarray[o+1], t);
            }
        }
    }
}



//*****************************************************************
// Rotate filenames in array by one
//
void rotateIMG(char strarray[maxnum][maxlen], char asize){
    char t[maxlen];
    
    strcpy(t, strarray[0]);
    for(char i=1; i<asize; i++) {
        strcpy(strarray[i-1], strarray[i]);
    }
    strcpy(strarray[asize-1],t);
}



//*****************************************************************
// Display the top 3 files in the array
//
void showIMG(char strarray[maxnum][maxlen],char asize) {
    
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.setTextColor(BLACK,WHITE); //Reverse video for first filename
    display.println(strarray[0]);
    
    display.setTextColor(WHITE); //Normal video for the rest
    display.println(strarray[1]);
    display.println(strarray[2]);
    display.display();
}



//*****************************************************************
// Set timer TC4 to call the TC4_Handler every 10 milliseconds
// which becomes the sample period for the pushbutton.
//
void TC4_setup() {
  // Set up the generic clock (GCLK4) used to clock timers
  REG_GCLK_GENDIV = GCLK_GENDIV_DIV(1) |          // Divide the 48MHz clock source by divisor 1: 48MHz/1=48MHz
                    GCLK_GENDIV_ID(4);            // Select Generic Clock (GCLK) 4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  REG_GCLK_GENCTRL = GCLK_GENCTRL_IDC |           // Set the duty cycle to 50/50 HIGH/LOW
                     GCLK_GENCTRL_GENEN |         // Enable GCLK4
                     GCLK_GENCTRL_SRC_DFLL48M |   // Set the 48MHz clock source
                     GCLK_GENCTRL_ID(4);          // Select GCLK4
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization

  // Feed GCLK4 to TC4 and TC5
  REG_GCLK_CLKCTRL = GCLK_CLKCTRL_CLKEN |         // Enable GCLK4 to TC4 and TC5
                     GCLK_CLKCTRL_GEN_GCLK4 |     // Select GCLK4
                     GCLK_CLKCTRL_ID_TC4_TC5;     // Feed the GCLK4 to TC4 and TC5
  while (GCLK->STATUS.bit.SYNCBUSY);              // Wait for synchronization
 
  REG_TC4_COUNT16_CC0 = 0x1D5;                    // (10mS)Set the TC4 CC0 register as the TOP value in match frequency mode
//  REG_TC4_COUNT16_CC0 = 0x24A0;                   // (200mS)Set the TC4 CC0 register as the TOP value in match frequency mode
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization

  //NVIC_DisableIRQ(TC4_IRQn);
  //NVIC_ClearPendingIRQ(TC4_IRQn);
  NVIC_SetPriority(TC4_IRQn, 0);    // Set the Nested Vector Interrupt Controller (NVIC) priority for TC4 to 0 (highest)
  NVIC_EnableIRQ(TC4_IRQn);         // Connect TC4 to Nested Vector Interrupt Controller (NVIC)

  REG_TC4_INTFLAG |= TC_INTFLAG_OVF;              // Clear the interrupt flags
  REG_TC4_INTENSET = TC_INTENSET_OVF;             // Enable TC4 interrupts
  // REG_TC4_INTENCLR = TC_INTENCLR_OVF;          // Disable TC4 interrupts
 
  REG_TC4_CTRLA |= TC_CTRLA_PRESCALER_DIV1024 |   // Set prescaler to 1024, 48MHz/1024 = 46.875kHz
                   TC_CTRLA_WAVEGEN_MFRQ |        // Put the timer TC4 into match frequency (MFRQ) mode
                   TC_CTRLA_ENABLE;               // Enable TC4
  while (TC4->COUNT16.STATUS.bit.SYNCBUSY);       // Wait for synchronization
}


//*****************************************************************
// Interrupt Service Routine (ISR) for timer TC4
//
void TC4_Handler()                              
{     
  // Check for overflow (OVF) interrupt
  if (TC4->COUNT16.INTFLAG.bit.OVF && TC4->COUNT16.INTENSET.bit.OVF)             
  {
    // Read the state of the pushbutton 
    // and signal that we have a new value ready   
    btn_val_rdy = true;
    btn_val = digitalRead(PUSHBUTTON);
    
    REG_TC4_INTFLAG = TC_INTFLAG_OVF;         // Clear the OVF interrupt flag
  }
}

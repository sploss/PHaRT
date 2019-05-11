#!/usr/bin/env python
''' 
radprog_io.py
Communicate with the radio programming dongle.
Its commands include: ?, DIR, DEL filename.ext, GET filename.ext, PUT filename.ext
'''

import time
import serial

import sys
import glob
import serial




def portIsUsable(portName):
    try:
       ser = serial.Serial(port=portName)
       return True
    except:
       return False


if portIsUsable('/dev/ttyACM0')==False:
    print 'Plug in the programmer and try again.'
    exit()

# configure the serial connections (the parameters differs on the device you are connecting to)
ser = serial.Serial(
    port='/dev/ttyACM0',
    baudrate=115200,
    parity=serial.PARITY_NONE,
    stopbits=serial.STOPBITS_ONE,
    bytesize=serial.EIGHTBITS
)
ser.timeout = 0.1
ser.isOpen()

input=1
count=0

while 1:
    print '''        
    1) List Files on SD card
    2) Copy a file from SD to harddrive
    3) Copy a file from harddrive to SD
    4) Delete a file on SD
    Type "exit" to quit'''

    action = raw_input("    Select an action >> ")
    
    if action == 'exit':
        ser.close ()
        exit()
    
    if action == '1':
        ser.write('DIR\n')
        while 1:
            SD_dir = ser.readline()
            print(SD_dir[:-1])
            if SD_dir == "": break

    if action == '2':   #Copy a file from SD to harddrive
        sdFile = raw_input('Enter name of SD source file >>')
        hdFile = raw_input('Enter name of HD destination file >>')

        try:
            fd = open(str(hdFile),"wb")
            
            ser.write('GET ' + sdFile + '\n')
            out = ''
            ser.timeout = 1

            # let's wait one second before reading output (let's give device time to answer)
            time.sleep(1)
            
            print "Reading..."
            out = ser.read(0x4000)
            fd.write(out)
            fd.close()
        except:
            print "Can't open harddisk file: " + hdFile

    if action == '3':   #Copy a file from harddrive to SD
        hdFile = raw_input('Enter name of HD source file >>')
        sdFile = raw_input('Enter name of SD destination file >>')
        
        try:
            fd = open(str(hdFile),"rb")
            ser.timeout = 0.5
            ser.write('DEL ' + sdFile + '\n')
            time.sleep(1)
            ser.write('PUT ' + sdFile + '\n')
            #print 'PUT ' + sdFile + '\n'
            ans = ser.read(3)
            print ans
            if ans == 'OK':
                while 1:
                    buf = fd.read(16)
                    if buf == '':
                        fd.close()
                        break
                    ser.write(buf)
            else:
                fd.close()
                print "Failed to access SD file: " + str(input2)
        except:
            print "Can't open harddisk file: " + hdFile
        
    if action == '4':
        sdFile = raw_input('Enter the name of the SD file to be deleted >>')
        ser.write('DEL ' + sdFile + '\n')



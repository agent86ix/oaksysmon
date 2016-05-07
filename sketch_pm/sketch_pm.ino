#include <Wire.h>
#include <Keyboard.h>

#define I2C_ADDRESS_PM 8
#define I2C_ADDRESS_OAK 9
#define I2C_REQUEST_MAX 32
#define REPORT_BUFFER_SIZE 1500

byte dataLenWritten = 0;
byte cmdByte = 0;
int bufferWritten = 0;
byte reportBuffer[REPORT_BUFFER_SIZE];
byte serialState = 0;
int reportLen = 0;

/* This function is called when the Oak sends bytes via I2C
   These bytes represent keyboard commands to send to the PC */
void receiveEvent(int count) {
  while(Wire.available()) {
    byte data = Wire.read();
    if(data == 0) {
      /* if the byte is 0, reset everything */
      cmdByte = 0;
      dataLenWritten = 0;
      bufferWritten = 0;
      Keyboard.releaseAll();
      continue;
    }
    if(cmdByte == 0) {
      /* The first byte is the "command byte" in my I2C protocol.
         It is 4 bits of command and 4 bits of key modifiers. */
      cmdByte = data;
      dataLenWritten = 0;
    } else {
      /* The second byte contains only data, the keycode to send */
      byte command = cmdByte>>4;
      if(command == 1) {
        byte mod = cmdByte&0xf;
        if(mod&1) {
          Keyboard.press(KEY_LEFT_CTRL);
        }
        if(mod&2) {
          Keyboard.press(KEY_LEFT_SHIFT);
        }
        if(mod&4) {
          Keyboard.press(KEY_LEFT_ALT);
        }
        if(mod&8) {
          Keyboard.press(KEY_LEFT_GUI);
        }
        Keyboard.write(data);
        Keyboard.releaseAll();
      }
      cmdByte = 0;
    }
  }
}

/* This function is called when the Oak requests bytes over I2C */
void requestEvent() {
  byte dataTmp[33];
  int i;

  if(dataLenWritten == 0) {
    /* The first transaction is to get the size of the report.     
     *  This is a 2-byte transfer, so the max report size is
     *  ~65k bytes
     */
    int dataLenCount = 0;
    dataLenCount = reportLen;

    dataLenCount++;
    dataTmp[0] = (dataLenCount>>8)&0x0f;
    if(serialState) dataTmp[0] |= 0x80;
    dataTmp[1] = dataLenCount&0xff;
    Wire.write(dataTmp,2);
    dataLenWritten = 1;
    bufferWritten = 0;
  } else {
    /* We already wrote the size, write the report.
     *  The max I2C transfer supported by Arduino's 
     *  libraries is only 32 bytes, so we have to send
     *  the report in 32-byte chunks.
     */
    for(i=0; i<I2C_REQUEST_MAX; i++) {
      dataTmp[i] = reportBuffer[bufferWritten];
      if(reportBuffer[bufferWritten] == 0) {
        dataLenWritten = 0;
        i++;
        break;
      }
      bufferWritten++;      
    }
    dataTmp[i] = 0;
    Wire.write(dataTmp, i);
  }
    
}


void setup() {
  Serial.begin(115200);
  delay(500);
  Keyboard.begin();
  Serial.println("pro micro startup");
  
  Wire.begin(I2C_ADDRESS_PM);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
  reportBuffer[REPORT_BUFFER_SIZE-1] = 0;
  reportBuffer[0] = 0;
  Serial.println((const char*)reportBuffer);


}

int reportBufPos = 0;
void loop() {
  while(Serial.available()) 
    /* Read bytes of the report from the PC */
    byte byteRead = Serial.read();
    /* There are no newlines in the PC's report, but I might want 
     *  to test the program with the serial monitor.  So, treat a 
     *  newline as the end of the report.
     */
    if(byteRead == '\n') byteRead = 0;
    reportBuffer[reportBufPos] = byteRead;

    /* Is this the end of the report?  If so, calculate the size. */
    if(byteRead == 0) {
      reportBufPos = 0;
      reportLen = strlen((const char*)reportBuffer);
    }
    else if(reportBufPos<REPORT_BUFFER_SIZE-1) reportBufPos++;
  }
  /* If the serial port isn't ready, make note of that for our
   *  status.
   */
  if(Serial) serialState = 1;
  else serialState = 0;
}

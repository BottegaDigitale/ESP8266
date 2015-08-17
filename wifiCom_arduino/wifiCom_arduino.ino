
/*--------> wifiCom_arduino
  Comunicating with ESP8266 custom firmware
  Author: Gianni Massi
  Uses queue array to store all the values to be sent, so that if an attempt 
  to send is not successful then the element is not removed from the queue.
  
  This code can be used as a starting point for writing the code for a different
  application. The QueueArray class can be used multiple times for different data,
  more commands to respond to can be added and so on.
  
  [TO DO: -add more comments, 
          -add check if wifi is already connected to desired AP before sending new Ap settings,
          -add separate function for sampling and adding data to queue]
*/
#include <QueueArray.h>  //required for buffering data to be sent over wifi
#include <SoftwareSerial.h> //used for communication with ESP (saving Serial for debug)

String host = "bottegadelleartidigitali.com";
String url = "/add_data.php?serial=ESPino";
String getend = "GET:&temperature=";
String ssid = "CasaMassi";
String password = "massianzuino";

unsigned long elapsed;            //used to time events
unsigned int getTimeout = 20000;  //timeout time for receiving response to request
int samplePeriod = 5000;          //determines the frequency of sampling
String request;
unsigned long lastGet;            //when the last get request was made;
unsigned int aliveCount = 0; //counts upward for every request that has not received answer from ESP

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean getOK = true;
boolean wifiOK = false;
boolean wifiSent = false;
boolean gotResponse = true;


const int resetPin = 9;

unsigned int data1 = 0;

// create a queue of characters.
QueueArray <int> queue;
SoftwareSerial ESP(10, 11); // RX, TX

void setup() {
  pinMode(13, OUTPUT);
  pinMode(resetPin, OUTPUT);
  digitalWrite(resetPin, HIGH);
  Serial.begin(57600);
  ESP.begin(9600);
  Serial.println("Restarted");
  
  //communicate WIFI login details to ESP
  String ap = "SSID:"+ssid;
  ESP.println(ap);
  ap = "PASS:" + password;
  delay(200);
  ESP.println(ap);
  
  // reserve 200 bytes for the inputString:
  inputString.reserve(200);
}

void loop() {

  // when String is arrived
  if (stringComplete) {
    //parse response
    parseResponse();
    // clear the string:
    inputString = "";
    stringComplete = false;
  }

// every samplePeriod ms new data is sampled and added to the queue
  if (millis() - elapsed >= samplePeriod) {
    elapsed = millis();
    Serial.println();
    Serial.print(millis());
    Serial.print(" Add to queue. N items= ");
    Serial.print(queue.count());
    Serial.print(" WIFIOK: ");
    Serial.print(wifiOK);
    Serial.print(" GETOK: ");
    Serial.print(getOK);
    Serial.print(" gotresponse: ");
    Serial.println(gotResponse);

    data1 = ++data1 % 30;
    if (queue.count() >= 50) {
      Serial.println("popping one");
      queue.pop();
    }
    queue.push(data1);
  }
  
  //send pending get requests (if there is data in the queue array)
  getRequest();

  //check if ESP is stuck
  if (wdtESP()) {
    Serial.println("ESP is stuck");
    //manually reset the ESP via the reset pin
    digitalWrite(resetPin, LOW);
    delay(300);
    digitalWrite(resetPin, HIGH);
    
    aliveCount=aliveCount/2;
  }

  Serial.print(".");
  delay(100);
  ESPEvent();
}

/* -----------> getRequest()
  send next message in queue
*/
void getRequest() {
  //if response has not been received withing the time set in getTimeout, then it moves on
  if (!gotResponse && long(millis()) - long(lastGet) >= getTimeout) {
    Serial.print("Response timeout" );
    Serial.println(millis() - lastGet);
    gotResponse = true;
    //remove item from queue
    if (!queue.isEmpty ()) {
      Serial.println("Removing item from queue (cause: response timeout)");
      queue.pop();
    }
  }

  // if the response from the previous request has been received (or timed out) and wifi is connected, then send a new request
  if (gotResponse && wifiOK) {

    //if queue is not empty send request at the front of the queue
    if (!queue.isEmpty ()) {

      getOK = false;
      gotResponse = false;

      //send request at the front of the queue
      Serial.print("Sending ");
      Serial.println(queue.peek());
      request = getend + queue.peek() + "\n";
      ESP.print(request);
      lastGet = millis();
    }
  }

  aliveCount ++;
}

/* -----------> parseResponse()
  deal with messages from ESP8266
  */
void parseResponse() {
  //check for GET request success message
  if ( inputString.indexOf("GET:OK") != -1) {
    Serial.println("GET request successful.");
    getOK = true;
    gotResponse = true;
    aliveCount = 0;
    //remove first item from front of the queue
    if (!queue.isEmpty ()) {
      queue.pop();
    }
  }

  //check for Get request unsuccess message
  else if ( inputString.indexOf("GET:BAD") != -1 || inputString.indexOf("GET:FAIL") != -1) {
    Serial.println("GET request unsuccessful");
    getOK = false;
    gotResponse = true;
    aliveCount = 0;
  }

  //check for wifi  connection successful message
  if ( inputString.indexOf("WIFI:OK") != -1) {
    wifiOK = true;
    aliveCount = 0;
    if (!wifiSent) {
      Serial.println("Wifi connected");
      wifiSent = 1;
    }
  }

  //check for connection to wifi successful message
  else if ( inputString.indexOf("WIFI:BAD") != -1) {
    wifiOK = false;
    aliveCount = 0;
    Serial.println("Wifi disconnected");
    wifiSent = 0;
  }

  //check for GET commands received over wifi
  if (inputString.indexOf("GOT") != -1) {
    int GOTindex = inputString.indexOf("GOT");
    parseGot(inputString.substring(GOTindex));
    aliveCount = 0;
  }


  // check response for ESP resetting messages
  if (checkReset()) {
    wifiOK = false;
    Serial.println("ESP restarted");
    aliveCount = 0;
  }
}

/*-----------> checkReset()
  Function that checks the string received for characters that are received
  on a reset of the ESP8266 module. [More words coould be added]
  */
boolean checkReset() {
  boolean ckreset = false;
  if ((inputString.indexOf("wdt") != -1) || (inputString.indexOf("reset") != -1) || (inputString.indexOf("checksum") != -1) || (inputString.indexOf("tail") != -1) || (inputString.indexOf("RESTART") != -1)) {
    ckreset = true;
    aliveCount = 0;
  }
  return ckreset;
}

/*-----------> wdtESP()
  Function that check if the ESP is stuck by checking the number of unanswered requests
  Return true if it is still running, false if it hasn't been answering
  */
boolean wdtESP() {
  boolean aliveFlag;
  if (aliveCount >= 500) aliveFlag = true;
  else aliveFlag = false;
  return aliveFlag;
}


/* ----------> SerialEvent()
  Function that reads from the Serial buffer when new data arrives
  and adds to the string called inputString
  */
void ESPEvent() {
  while (ESP.available()) {
    // get the new byte:
    char inChar = (char)ESP.read();
    // add it to the inputString:
    inputString += inChar;
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      stringComplete = true;
    }
    
  }
}
/* ------------> parseGot()
  Function that deals with messages received by the ESP over network.
  Insert more code here to deal with different types of messages
*/

void parseGot(String cmd) {

  if (cmd.indexOf("led1") != -1) {
    cmd = cmd.substring(9, -2);
    int ledState = cmd.toInt();
    Serial.print("setting led 1 to: ");
    Serial.println(ledState);
    digitalWrite(13, ledState);
  }

}

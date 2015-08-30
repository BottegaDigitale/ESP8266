/* wifiCom_esp8266
  Author: Gianni Massi
  
  Firmware for ESP8266 module for communicating with Arduino via serial
  Allows to: 
            -set new ssid and password settings
            -set new host and url settings
            -send get request (with "GET:myrequest\n") which will be formatted with host and url
            -receive requests and pass data to arduino
  Informs connected arduino of:
            -status of wifi connection ("WIFI:OK\n" periodically when connected, "WIFI:BAD\n" once if not connected)
            -success of get request ("GET:OK\n" if success, "GET:BAD\n" if bad request, "GET:FAIL\n" if fails)
  [TO DO: - add more comments
          - add command that allows to check AP settings
          - add separate timing for checking wifi connection (less frequently) and 
            listening over serial and wifi]
  */
  
  
#include <ESP8266WiFi.h>
#include <EEPROM.h>

/*   ssid and password are read from EEprom memory and store in these two variables
    they can be changed by sending the messages "SSID:myssid\n"  and "PASS:mypass\n"
    to the ESP via serial
  */
char* ssid     = "xxxxxxxxxx";
char* password = "xxxxxxxxxx";


/*  initial host and url are stored in these variables and can be changed
    by sending "HOST:myhost\n" and "URL:myurl\n"
*/
const char* host = "bottegadelleartidigitali.com";
String url = "/add_data.php?serial=ESPino";

String getend;  //holds the last part of the get request received via a "GET:myrequest\n" message
String settings; //used for formatting and retrieving wifi settings 

String inputString = "";         // a string to hold incoming data
boolean stringComplete = false;  // whether the string is complete
boolean sflag = false;           //whether the get request succeded
boolean BADsent = false;        //whether wifi disconnected message has been sent

String htmlResponse;          //received response to request

// Create an instance of the server
// specify the port to listen on as an argument
WiFiServer server(80);


void setup() {
  pinMode(0, OUTPUT);
  
  Serial.begin(9600);
  
  EEPROM.begin(512);

  Serial.println("RESTART");
  
  //uncommment to display debugging form ESP
  //Serial.setDebugOutput(true);
  delay(10);
  WiFi.disconnect();

  //retrieve Wifi settings from memory
  getAP();
  //connect to wifi with retrieved settings
  WiFi.begin(ssid, password);
  // Start the server
  server.begin();

  // reserve 200 bytes for the inputString:
  inputString.reserve(200);

  WiFi.mode(WIFI_AP_STA);

  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI:OK\n");
    //Send IP response
    Serial.print("IP:");
    Serial.print(WiFi.localIP());
    Serial.print("\n");
  } else {
    Serial.print("WIFI:BAD\n");
    BADsent = 1;
  }
}


void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    Serial.print("WIFI:OK\n");

    //sends IP if just reconnected to wifi
    if (BADsent) {
      //Send IP response
      Serial.print("IP:");
      Serial.print(WiFi.localIP());
      Serial.print("\n");
    }

    WifiListen();
    BADsent = 0;
  } else {
    if (!BADsent) {
      Serial.print("WIFI:BAD\n");
      BADsent = 1;
    }
  }

  //Listen for commands on Serial
  SerialListen();
  
  delay(200);
}

/* --------> WifiListen()
 Function that listens for get request received over Wifi
*/
void WifiListen() {
  // Check if a client has connected
  WiFiClient client = server.available();
  if (!client) {
    return;
  }

  //Wait until the client sends some data
  int timeout = millis();
  while (!client.available() && millis() - timeout < 10000) {
    //Serial.print(".");
    delay(200);
  }
  String line;
  while (client.available()) {
    line = client.readStringUntil('\r');
    htmlResponse = "HTTP/1.1 400 BAD\r\n";

    if (line.indexOf("GET") != -1 && line.indexOf("favicon") == -1) {
      sendGot(line.substring(5));
      htmlResponse = "HTTP/1.1 200 OK\r\n";
    }
    client.flush();
  }
  //format response string
  String s = htmlResponse + "Content-Type: text/html\r\n\r\n" + line + "\r\n----.. end of what I got\r\n"; //<!DOCTYPE HTML>\r\n<html>\r\nGPIO is now ";

  // Send the response to the client
  client.print(s);

}


/* -----> SerialListen()
    Function that listens for commands on Serial
*/
void SerialListen() {
  while (Serial.available()) {

    // get the new byte:
    char inChar = (char)Serial.read();
    // if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
    if (inChar == '\n') {
      stringComplete = true;
    } else {
      // add it to the inputString:
      inputString += inChar;
    }
  }
  // When String is received decide what to do based on starting letters
  if (stringComplete) {

    //execute statements based on value of the string
    parseCmd();

    // clear the string:
    inputString = "";
    stringComplete = false;
  }

}


/* ------->      parseCmd()
   Function that executes statements depending on the value
   of the string received through serial
   */
void parseCmd() {

  //set host variable if starts with host
  if (inputString.substring(0, 4) == "HOST") {

    //inputString.substring(5).toCharArray(host, 25);
    Serial.print("HOST:");
    Serial.println(host);
  }

  //set url variable of starts with url
  else if (inputString.substring(0, 3) == "URL")
  {
    url = inputString.substring(4);
    Serial.print("URL:");
    Serial.println(url);
  }

  //set getend variable and send get request if get is received
  else if (inputString.substring(0, 3) == "GET")
  {
    //assign value to getend
    getend = inputString.substring(4);
    // send get request
    sendGet();
  }

  //set ssid and reconnect
  else if (inputString.substring(0, 4) == "SSID")
  {
    //assign value to ssid
    String _ssid = inputString.substring(5);
    boolean changed = false;
    if (_ssid != ssid) {
      Serial.println("New ssid");
      changed = true;
    }
    ssid = new char[_ssid.length() + 1];
    _ssid.toCharArray(ssid, _ssid.length() + 1);
    BADsent = 0;
    if (changed) {
      saveSSID();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }

  //set password and reconnect
  else if (inputString.substring(0, 4) == "PASS")
  {
    //assign value to pass
    String _password = inputString.substring(5);
    boolean changed = false;
    if (_password != password) {
      Serial.println("new password");
      changed = true;
    }
    password = new char[_password.length() + 1];
    _password.toCharArray(password, _password.length() + 1);
    BADsent = 0;
    if (changed) {
      savePASS();
      WiFi.disconnect();
      WiFi.begin(ssid, password);
    }
  }
}

/*--------->   sendGet()
  Function that send the get request
  */
void sendGet() {
  // Use WiFiClient class to create TCP connections
  WiFiClient client;

  const int httpPort = 80;

  if (!client.connect(host, httpPort)) {
    Serial.println("GET:FAIL");
    return;
  }

  // We now create a URI for the request
  String completeUrl = url + getend;

  // This will send the request to the server
  client.print(String("GET ") + completeUrl + " HTTP/1.1\r\n" +
               "Host: " + host + "\r\n" +
               "Connection: close\r\n\r\n");
  int timeout = millis();
  
  //wait for response with timeout of 10 s
  while (!client.available() && millis() - timeout < 10000) {
  }
  
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    
    //uncomment to display response received
    //Serial.print(line);

    if (line.indexOf("HTTP/1.1 200 OK") != -1) {
      sflag = 1;
    } else if (line.indexOf("BAD") != -1) {
      sflag = 0;
    }
  }
  if (sflag) {
    Serial.print("GET:OK\n");
  } else {
    Serial.print("GET:BAD\n");
  }
  client.flush();
  client.stop();
}

/*-------> parseRequest()
    Function that parses the string received over wifi
*/
void sendGot(String line) {
  int spaceIndex = line.indexOf(" ");
  Serial.print("GOT:");
  Serial.print(line.substring(0, spaceIndex));
  Serial.print("\n");

}

/*--------->getAP
  Function that retrieves the AP settings from EEprom memory
  and assigns them to ssid and password varibales
*/
void getAP() {
  settings = EEprom_readWord(0);
  Serial.print("I read these settings: ");
  Serial.println(settings);
  Serial.println("--------------");

  int separatorIndex = settings.indexOf(":");


  String _temp = settings.substring(0, separatorIndex);
  delay(100);
  ssid = new char[_temp.length() + 1];
  _temp.toCharArray(ssid, _temp.length() + 1);
  delay(100);
  Serial.print("This is is the ssid:");
  Serial.print(ssid);

  _temp = settings.substring(separatorIndex + 1);
  delay(100);
  password = new char[_temp.length() + 1];
  _temp.toCharArray(password, _temp.length() + 1);
  delay(100);
  Serial.print("This is the password:");
  Serial.print(password);
}

/*--------->saveAP
  Function that saves the ssid and password variables inside EEprom memory
*/
void saveAP() {

  settings = String(ssid) + ":" + String(password);
  Serial.print("Saving new settings: ");
  Serial.println(settings);
  EEprom_writeWord(0, settings);
}

/*--------->saveSSID
Function that saves the ssid variable inside EEprom memory
*/
void saveSSID() {
  String toWrite = String(ssid) + ":" + String(password);
  EEprom_writeWord(0, toWrite);
}

/*--------->savePASS
Function that saves the password variable inside EEprom memory
*/
void savePASS() {
  String toWrite = String(ssid) + ":" + String(password);
  EEprom_writeWord(0, toWrite);
  getAP();
}

/* --------> EEprom_writeWord
   Function that writes a word to specified address in EEProm memory
*/
void EEprom_writeWord(int address, String myWord) {
  myWord += "&";
  byte c;
  for (int i = 0; i < myWord.length(); i++) {
    c = byte(myWord[i]);
    EEPROM.write(address++, c);
    delay(1);
  }
  EEPROM.commit();
}

/* --------> EEprom_readWord
   Function that reads a word from the specified address in EEProm memory
*/
String EEprom_readWord(int address) {
  String foundString;
  byte c = '1';
  while (c != '&') {
    c = EEPROM.read(address++);

    if (c != '&') {
      foundString += char(c);
    }
  }
  return foundString;
}



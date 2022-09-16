/* 
 * This file is part of the XXX distribution (https://github.com/PandaSB/D1mini_Teleinfo.git).
 * Copyright (c) 2022 BARTHELEMY Stéphane.
 * 
 * This program is free software: you can redistribute it and/or modify  
 * it under the terms of the GNU General Public License as published by  
 * the Free Software Foundation, version 2.
 *
 * This program is distributed in the hope that it will be useful, but 
 * WITHOUT ANY WARRANTY; without even the implied warranty of 
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU 
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License 
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <FS.h>                   //this needs to be first, or it all crashes and burns...
#include <LittleFS.h>  // This file system is used.

#include <ESP8266WiFi.h>          //https://github.com/esp8266/Arduino
#include <DNSServer.h>
#include <ESP8266WebServer.h>
#include <WiFiManager.h>          //https://github.com/tzapu/WiFiManager
#include <ArduinoJson.h>          //https://github.com/bblanchon/ArduinoJson
#include <Ticker.h>
#include <SoftwareSerial.h>
#include <ESP8266mDNS.h>
#include <PubSubClient.h>



#define BUTTON_GPIO D3
#define LED_GPIO D4
#define TELEINFO_RX_GPIO D7
#define TELEINFO_TX_GPIO D8

#define SIZE_LINECAPTURE 80 
#define MSG_BUFFER_SIZE 100

static const char TEXT_PLAIN[] PROGMEM = "text/plain";
static const char TEXT_HTML[] PROGMEM = "text/html";



typedef enum {
  eBase ,
  eHc,
  eEjp,
  eTempo
} tTARIF ; 

typedef  struct {
  char ADCO[16] ;
  char OPTARIF[16] ;
  char ISOUSC[16]; 
  tTARIF detecttarif;
  union {
    struct {
      char BASE[16];
    } base ; 
    struct {
      char HCHC[16];
      char HCHP[16];
    } hc ;
    struct {
      char EJPHN[16];
      char EJPHPM[16];
      char PEJP[16];

    } ejp ; 
    struct {
      char BBRHCJB[16];
      char BBRHPJB[16];
      char BBRHCJW[16];
      char BBRHPJW[16];
      char BBRHCJR[16];
      char BBRHPJR[16];
    } tempo ; 
  } ;
  char PTEC[16];
  char DEMAIN[16];
  boolean detecttri ;
  union {
    struct {
      char IINST[16]; 
      char IMAX[16];
    }mono ;
    struct {
      char IINST1[16]; 
      char IINST2[16]; 
      char IINST3[16]; 
      char IMAX1[16];
      char IMAX2[16];
      char IMAX3[16];
      char PMAX[16];
      char PPOT[16];
    } tri;
  };
  char ADPS[16];
  char PAPP[16];
  char HHPHC[16];
  char MOTDETAT[16];
} tTeleinfoData ; 
tTeleinfoData CurrentTeleinfo = {0} ; 


char mqtt_server[40] = "192.168.0.233";
char mqtt_port[6] = "1883";
char domoticz_pmeter_id[10] = "1";
char domoticz_current_id[10] = "0";
char host[16] = "teleinfo";


ESP8266WebServer server(80);
WiFiClient espClient;
PubSubClient client(espClient);

WiFiManager wifiManager;



boolean StartCapture = false ;
bool shouldSaveConfig = false;

char LineCapture[SIZE_LINECAPTURE] = "" ;

Ticker flipper;
SoftwareSerial TeleInfoSerial ;


boolean ButtonPressed = false ; 
unsigned long ButtonStartPress = 0 ; 

void FlipLed()
{
  int state = digitalRead(LED_GPIO);  // get the current state of GPIO1 pin
  digitalWrite(LED_GPIO, !state);     // set pin to the opposite state
}

void saveConfigCallback () {
  Serial.println("Sauvegarde Configuration");
  shouldSaveConfig = true;
}


void mqtt_callback(char* topic, byte* payload, unsigned int length) {
}

void mqtt_reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      // Once connected, publish an announcement...
      client.publish("outTopic", "hello world");
      // ... and resubscribe
      client.subscribe("inTopic");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void SaveData ( char * etiquette , char * data , char * checksum)
{
  if      (strstr (etiquette , "ADCO")    != NULL ) {strcpy (CurrentTeleinfo.ADCO,data ) ;}
  else if (strstr (etiquette , "OPTARIF") != NULL ) {strcpy (CurrentTeleinfo.OPTARIF,data ) ;}
  else if (strstr (etiquette , "ISOUSC")  != NULL ) {strcpy (CurrentTeleinfo.ISOUSC,data ) ;}
  else if (strstr (etiquette , "BASE")    != NULL ) {strcpy (CurrentTeleinfo.base.BASE,data ) ;CurrentTeleinfo.detecttarif = eBase ; }
  else if (strstr (etiquette , "HCHC")    != NULL ) {strcpy (CurrentTeleinfo.hc.HCHC,data ) ;CurrentTeleinfo.detecttarif = eHc; }
  else if (strstr (etiquette , "HCHP")    != NULL ) {strcpy (CurrentTeleinfo.hc.HCHP,data ) ;CurrentTeleinfo.detecttarif =eHc;}
  else if (strstr (etiquette , "EJPHN")   != NULL ) {strcpy (CurrentTeleinfo.ejp.EJPHN,data ) ;CurrentTeleinfo.detecttarif = eEjp;}
  else if (strstr (etiquette , "EJPHPM")  != NULL ) {strcpy (CurrentTeleinfo.ejp.EJPHPM,data ) ;CurrentTeleinfo.detecttarif = eEjp;}
  else if (strstr (etiquette , "BBRHCJB") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHCJB,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "BBRHPJB") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHPJB,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "BBRHCJW") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHCJW,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "BBRHPJW") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHPJW,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "BBRHCJR") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHCJR,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "BBRHPJR") != NULL ) {strcpy (CurrentTeleinfo.tempo.BBRHPJR,data ) ;CurrentTeleinfo.detecttarif = eTempo;}
  else if (strstr (etiquette , "PEJP")    != NULL ) {strcpy (CurrentTeleinfo.ejp.PEJP,data ) ;CurrentTeleinfo.detecttarif = eEjp;}
  else if (strstr (etiquette , "PTEC")    != NULL ) {strcpy (CurrentTeleinfo.PTEC,data ) ;}
  else if (strstr (etiquette , "DEMAIN")  != NULL ) {strcpy (CurrentTeleinfo.DEMAIN,data ) ;}
  else if (strstr (etiquette , "IINST1")  != NULL ) {strcpy (CurrentTeleinfo.tri.IINST1,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IINST2")  != NULL ) {strcpy (CurrentTeleinfo.tri.IINST2,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IINST3")  != NULL ) {strcpy (CurrentTeleinfo.tri.IINST3,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IINST")   != NULL ) {strcpy (CurrentTeleinfo.mono.IINST,data ) ; CurrentTeleinfo.detecttri = false;}
  else if (strstr (etiquette , "ADPS")    != NULL ) {strcpy (CurrentTeleinfo.ADPS,data ) ;}
  else if (strstr (etiquette , "IMAX1")   != NULL ) {strcpy (CurrentTeleinfo.tri.IMAX1,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IMAX2")   != NULL ) {strcpy (CurrentTeleinfo.tri.IMAX2,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IMAX3")   != NULL ) {strcpy (CurrentTeleinfo.tri.IMAX3,data ) ; CurrentTeleinfo.detecttri = true;}
  else if (strstr (etiquette , "IMAX")    != NULL ) {strcpy (CurrentTeleinfo.mono.IMAX,data ) ; CurrentTeleinfo.detecttri = false;}
  else if (strstr (etiquette , "PMAX")    != NULL ) {strcpy (CurrentTeleinfo.tri.PMAX,data ) ;}
  else if (strstr (etiquette , "PAPP")    != NULL ) {strcpy (CurrentTeleinfo.PAPP,data ) ;}
  else if (strstr (etiquette , "HHPHC")   != NULL ) {strcpy (CurrentTeleinfo.HHPHC,data ) ;}
  else if (strstr (etiquette , "MOTDETAT")!= NULL ) {strcpy (CurrentTeleinfo.MOTDETAT,data ) ;}
  else if (strstr (etiquette , "PPOT")    != NULL ) {strcpy (CurrentTeleinfo.tri.PPOT,data ) ;}
  else Serial.printf ("Data inconnu :%s \r\n", etiquette) ; 
}

void ParseLine ( char * line )
{
  //for (int i=0 ; i < strlen (line) ; i++)
  //{
  //  if (line[i] > 0x20 ) { Serial.printf ("%c", line[i]); } else {Serial.printf ("[%02x]%c",line[i],line[i]); } 
  //}
  char * part[10] ; 
	char delim[] = "\r\n ";

	char *ptr = strtok(line, delim);
  int index = 0 ; 
	while(ptr != NULL)
	{
    part[index++] = ptr ; 
 		ptr = strtok(NULL, delim);
	}

  //for (int i = 0 ; i < index ; i++ )
  //{
  //Serial.printf ("part[%d]:\"%s\" ", i , part[i]) ; 
  //}
  // Serial.printf(" parts : %d \r\n", index ) ; 

  if ( index == 3 ) SaveData (part[0],part[1],part[2]) ;
  else if  ( index == 4 ) SaveData (part[0],part[2],part[3]) ;
}

void handleRoot() {
  String message;
  message.reserve(200);
  message = "<!doctype html>";
  message += "<html lang=\"en\">";
  message += "<head>";
  message += "<meta charset=\"utf-8\">";
  message += "<title>ESP8266 - Teleinfo</title>" ;
  message += "<meta http-equiv=\"refresh\" content=\"10\" />" ;
  

  message += "</head>";
  message += "<body>";
  message += "<center><h1>ESP 8266 - TELEINFO </h1></center>";
  message += "<div><center>" ;
  message += "Id : " + String(CurrentTeleinfo.ADCO) ; 
  message += "</center></div>";
  message += "<div>"; 
  message += "Tarif             : " + String(CurrentTeleinfo.OPTARIF) + "<br>";
  message += "Courant souscrit  : " + String(CurrentTeleinfo.ISOUSC) + "A <br>"; 
  message += "Code Erreur       : " + String(CurrentTeleinfo.MOTDETAT) +" <br>";
  message += "</div>";
  message += "<div>Compteurs : <br>" ;
  if (CurrentTeleinfo.detecttarif == eBase)
  {
    message += "Compteur BASE      : " + String(CurrentTeleinfo.base.BASE) + " Wh<br>";
  } else if (CurrentTeleinfo.detecttarif == eHc)
  {
    message += "Compteur HP        : " + String(CurrentTeleinfo.hc.HCHP) + " Wh<br>";
    message += "Compteur HC        : " + String(CurrentTeleinfo.hc.HCHC) + " Wh<br>"; 
  } else if (CurrentTeleinfo.detecttarif == eEjp)
  {
    message += "Compteur EJPHN     : " + String(CurrentTeleinfo.ejp.EJPHN) + " Wh<br>";
    message += "Compteur EJPHPM    : " + String(CurrentTeleinfo.ejp.EJPHPM) + " Wh<br>"; 
    message += "Debut Preavis EJP  : " + String(CurrentTeleinfo.ejp.PEJP) + " min<br>"; 
    
  }else if (CurrentTeleinfo.detecttarif == eTempo)
  {
    message += "Compteur BBRHCJB   : " + String(CurrentTeleinfo.tempo.BBRHCJB) + " Wh<br>";
    message += "Compteur BBRHCJR   : " + String(CurrentTeleinfo.tempo.BBRHCJR) + " Wh<br>"; 
    message += "Compteur BBRHCJW   : " + String(CurrentTeleinfo.tempo.BBRHCJW) + " Wh<br>"; 
    message += "Compteur BBRHPJB   : " + String(CurrentTeleinfo.tempo.BBRHPJB) + " Wh<br>";
    message += "Compteur BBRHPJR   : " + String(CurrentTeleinfo.tempo.BBRHPJR) + " Wh<br>"; 
    message += "Compteur BBRHPJW   : " + String(CurrentTeleinfo.tempo.BBRHPJW) + " Wh<br>"; 
  }
  message += "Periode tarifaire  : " + String(CurrentTeleinfo.PTEC) + " <br>"; 
  message += "Horaire HP/HC      : " + String(CurrentTeleinfo.HHPHC) + "<br>";

  message += "Puissance Aparante : " + String(CurrentTeleinfo.PAPP) + " VA<br>"; 
if (CurrentTeleinfo.detecttri)
{
  message += "Courant instanné 1 : " + String(CurrentTeleinfo.tri.IINST1) + " A <br>"; 
  message += "Courant instanné 2 : " + String(CurrentTeleinfo.tri.IINST2) + " A <br>"; 
  message += "Courant instanné 3 : " + String(CurrentTeleinfo.tri.IINST3) + " A <br>"; 
  message += "Courant Max 1      : " + String(CurrentTeleinfo.tri.IMAX1) + " A<br>"; 
  message += "Courant Max 2      : " + String(CurrentTeleinfo.tri.IMAX2) + " A<br>"; 
  message += "Courant Max 3      : " + String(CurrentTeleinfo.tri.IMAX3) + " A<br>"; 
  message += "Puissance Max      : " + String(CurrentTeleinfo.tri.PMAX) + " W<br>"; 
  message += "Presence potentiel : " + String(CurrentTeleinfo.tri.PPOT) + " W<br>"; 
} else {
  message += "Courant instanné   : " + String(CurrentTeleinfo.mono.IINST) + " A <br>"; 
  message += "Courant Max        : " + String(CurrentTeleinfo.mono.IMAX) + " A<br>"; 
}
  message += "</div>";
  message += "</body>";
  message += "</html>"; 

  message += '\n';
  server.send(404, FPSTR(TEXT_HTML), message);
}

void handleNotFound() {
  String uri = ESP8266WebServer::urlDecode(server.uri());  // required to read paths with blanks
  String message;
  message.reserve(100);
  message = F("Error: File not found\n\nURI: ");
  message += uri;
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += F("\nArguments: ");
  message += server.args();
  message += '\n';
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" NAME:");
    message += server.argName(i);
    message += F("\n VALUE:");
    message += server.arg(i);
    message += '\n';
  }
  message += "path=";
  message += server.arg("path");
  message += '\n';
  server.send(404, FPSTR(TEXT_PLAIN), message);

}


void setup() {
  pinMode(BUTTON_GPIO,INPUT);
  pinMode(LED_GPIO,OUTPUT) ; 
  digitalWrite(LED_GPIO, LOW) ; 
  flipper.attach(0.3, FlipLed);

  Serial.begin(115200);
  TeleInfoSerial.begin(1200, SWSERIAL_7E1, TELEINFO_RX_GPIO, TELEINFO_TX_GPIO, false, 128, 11);

  Serial.println();

  //clean FS, for testing
  //LittleFS.format();
  //Serial.println("Montage File System FS...");


  if (LittleFS.begin()) {
    Serial.println("mounted file system");
    if (LittleFS.exists("/config.json")) {
      //file exists, reading and loading
      Serial.println("reading config file");
      File configFile = LittleFS.open("/config.json", "r");
      if (configFile) {
        Serial.println("opened config file");
        size_t size = configFile.size();
        // Allocate a buffer to store contents of the file.
        std::unique_ptr<char[]> buf(new char[size]);

        configFile.readBytes(buf.get(), size);

        DynamicJsonDocument json(2048);
        auto deserializeError = deserializeJson(json, buf.get());
        serializeJson(json, Serial);
        if ( ! deserializeError ) {
          Serial.println("\nparsed json");
          strcpy(mqtt_server, json["mqtt_server"]);
          strcpy(mqtt_port, json["mqtt_port"]);
          strcpy(domoticz_pmeter_id, json["pmeter_id"]);
          strcpy(domoticz_current_id, json["current_id"]);

        } else {
          Serial.println("failed to load json config");
        }
        configFile.close();
      }
    }
  } else {
    Serial.println("failed to mount FS");
  }

  WiFiManagerParameter custom_mqtt_server("server", "mqtt server", mqtt_server, 40);
  WiFiManagerParameter custom_mqtt_port("port", "mqtt port", mqtt_port, 6);
  WiFiManagerParameter custom_domoticz_pmeter_id("domoticz_pmeter_id", "pmeter id", domoticz_pmeter_id, 10,"pmeter id");
  WiFiManagerParameter custom_domoticz_current_id("domoticz_current_id", "current id", domoticz_current_id, 10,"current id");
  
  //set config save notify callback
  wifiManager.setSaveConfigCallback(saveConfigCallback);

  wifiManager.addParameter(&custom_mqtt_server);
  wifiManager.addParameter(&custom_mqtt_port);
  wifiManager.addParameter(&custom_domoticz_pmeter_id);
  wifiManager.addParameter(&custom_domoticz_current_id);

  //reset settings - for testing



  //wifiManager.setTimeout(120);
if (!wifiManager.autoConnect("Teleinfo")) {
    Serial.println("failed to connect and hit timeout");
    delay(3000);
    //reset and try again, or maybe put it to deep sleep
    ESP.reset();
    delay(5000);
  }

  //if you get here you have connected to the WiFi
  Serial.println("connected...yeey :)");

  //read updated parameters
  strcpy(mqtt_server, custom_mqtt_server.getValue());
  strcpy(mqtt_port, custom_mqtt_port.getValue());
  strcpy(domoticz_current_id, custom_domoticz_current_id.getValue());
  strcpy(domoticz_pmeter_id, custom_domoticz_pmeter_id.getValue());


  //save the custom parameters to FS
  if (shouldSaveConfig) {
    Serial.println("saving config");

    DynamicJsonDocument json(2048);

    json["mqtt_server"] = mqtt_server;
    json["mqtt_port"] = mqtt_port;
    json["pmeter_id"] = domoticz_pmeter_id;
    json["current_id"] = domoticz_current_id;

    json["ip"] = WiFi.localIP().toString();
    json["gateway"] = WiFi.gatewayIP().toString();
    json["subnet"] = WiFi.subnetMask().toString();

    File configFile = LittleFS.open("/config.json", "w");
    if (!configFile) {
      Serial.println("failed to open config file for writing");
    }

    serializeJson(json, Serial);
    serializeJson(json, configFile);

    configFile.close();
    //end save
  }
  flipper.detach() ;
  digitalWrite(LED_GPIO, LOW) ; 

  Serial.println("local ip");
  Serial.println(WiFi.localIP());
  Serial.println(WiFi.gatewayIP());

  if (MDNS.begin(host)) {
    MDNS.addService("http", "tcp", 80);
  }
  server.on("/",handleRoot) ; 
  server.onNotFound(handleNotFound);
  server.begin();

  client.setServer(mqtt_server, 1883);
  client.setCallback(mqtt_callback);
}


void loop() {
  int ButtonValue ;
  if ( TeleInfoSerial.available())
  {
    char Data ; 
    char msg[MSG_BUFFER_SIZE] ;
    TeleInfoSerial.read(&Data,1) ;
    switch (Data) {
      case 0x02 : //Start Frame
        Serial.println("");
        Serial.println("*** Start Frame ***");
        strcpy ( LineCapture, "") ; 
        StartCapture = true; 
        break ;
      case 0x03 : //End Frame
        StartCapture = false ; 
        Serial.println("");
        Serial.println("*** End Frame ***");
        if (CurrentTeleinfo.detecttarif == eBase)
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": 0,\"svalue\": \"%s;%s;%s;%s;%s;%s\"}", 
          domoticz_pmeter_id , CurrentTeleinfo.base.BASE, "0","0","0",CurrentTeleinfo.PAPP,"0");
        }
        else if (CurrentTeleinfo.detecttarif == eHc)
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": 0,\"svalue\": \"%s;%s;%s;%s;%s;%s\"}", 
          domoticz_pmeter_id , CurrentTeleinfo.hc.HCHP, CurrentTeleinfo.hc.HCHC,"0","0",CurrentTeleinfo.PAPP,"0");
        }
        else if (CurrentTeleinfo.detecttarif == eEjp)
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": 0,\"svalue\": \"%s;%s;%s;%s;%s;%s\"}", 
          domoticz_pmeter_id , CurrentTeleinfo.ejp.EJPHN, CurrentTeleinfo.ejp.EJPHPM,"0","0",CurrentTeleinfo.PAPP,"0");
        }
        else if (CurrentTeleinfo.detecttarif == eTempo)
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": 0,\"svalue\": \"%s;%s;%s;%s;%s;%s\"}", 
          domoticz_pmeter_id , String(atoi(CurrentTeleinfo.tempo.BBRHCJB)+atoi(CurrentTeleinfo.tempo.BBRHCJR)+atoi(CurrentTeleinfo.tempo.BBRHCJW)).c_str(), \
          String(atoi(CurrentTeleinfo.tempo.BBRHPJB)+atoi(CurrentTeleinfo.tempo.BBRHPJR)+atoi(CurrentTeleinfo.tempo.BBRHPJW)).c_str(),"0","0",CurrentTeleinfo.PAPP,"0");
        }
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish("domoticz/in", msg); 
        if (CurrentTeleinfo.detecttri)
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": %s,\"svalue\": \"%s;%s;%s\"}", 
          domoticz_current_id , "0", CurrentTeleinfo.tri.IINST1,CurrentTeleinfo.tri.IINST2,CurrentTeleinfo.tri.IINST3);          
        } else 
        {
          snprintf (msg, MSG_BUFFER_SIZE, "{\"idx\":%s,\"nvalue\": %s,\"svalue\": \"%s\"}", 
          domoticz_current_id , CurrentTeleinfo.mono.IINST, CurrentTeleinfo.mono.IINST);          
        }
        Serial.print("Publish message: ");
        Serial.println(msg);
        client.publish("domoticz/in", msg); 
        break;
      default :
        //if (Data >= 0x20 ) { Serial.printf ("%c", Data); } else {Serial.printf ("[%02x]",Data);if( Data == 0x0d) Serial.println ("");  } 
        if (StartCapture )
        {
          int size ; 
          if ((size = strlen(LineCapture)) < SIZE_LINECAPTURE) {
            LineCapture[size] = Data ; 
            LineCapture[size+1] = 0 ;
            if (Data == 0x0d) 
            {
              ParseLine (LineCapture) ;
              strcpy ( LineCapture ,"") ; 
            }  
          }
        }
    }
  }

  if (!client.connected()) {
    mqtt_reconnect();
  }
  client.loop();
  server.handleClient();
  MDNS.update();

  ButtonValue = digitalRead(BUTTON_GPIO) ; 
  if ((ButtonValue == 0) && (ButtonPressed == false ))
  {
    ButtonPressed = true ; 
    ButtonStartPress = millis() ;
    Serial.println ("Button pressed") ; 
  }
  if (( ButtonValue != 0) && (ButtonPressed == true))
  {
    unsigned long CurrentButtonTime = millis () - ButtonStartPress; 
    Serial.printf ("Button release %ld ms ",CurrentButtonTime) ; 
    if (CurrentButtonTime > 4000)
    {
      wifiManager.resetSettings();
      ESP.eraseConfig();
      boolean formatted = LittleFS.format() ;
      if(formatted){
        Serial.println("\n\nSuccess formatting");
      }else{
        Serial.println("\n\nError formatting");
      }
      delay(3000);
      ESP.reset(); 
    }
    ButtonPressed = false ;
  }
}
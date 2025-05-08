#include <WiFi.h>
#include <WebServer.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Keypad.h>
#include <Preferences.h>
#include <HX711.h>
#include <SPI.h>
#include <MFRC522.h>

#define LOCK_GPIO 13
#define TEMP_GPIO 10
#define HEATER_GPIO 14
#define LOAD_D 11
#define LOAD_SCK 12
#define SDA_RFID 17
#define RST_RFID 15
#define MOSI_RFID 21
#define MISO_RFID 16
#define SCK_RFID 18

const char* ssid = "Wifiname";
const char* password = "Password";
WebServer server(80);
Preferences preferences;
OneWire oneWire(TEMP_GPIO);
DallasTemperature sensors(&oneWire);
HX711 scale;
MFRC522 rfid(SDA_RFID,RST_RFID); // init sensors
bool Lock_toggle = false;
float temperature =0.0;
unsigned long curr_temprature =0;
unsigned long lock_timemilis = 0;
bool food_there = false;
bool isUnlocking = false;
bool manualOverride = false;
String first_master = "2356"; // init master code
String tempCode = "";
bool heaterOn = false;
bool heaterState = false;
bool too_hot = false;
unsigned long heater_button =0;
bool right_code = false;
unsigned long twofa_timer = 0;
const unsigned long twofa_lockout =30000; // 30 seconds
bool RFID_right =false;
bool tempcode_flag =false;
unsigned long lock_tempcode =0;
byte correct_UID[] = {0xE3,0x7F,0x6D,0xFA};
char keys[4][3] ={{'1', '2', '3'},{'4', '5', '6'},{'7', '8', '9'},{'*', '0', '#'}};
byte rowPins[4] = {3,8,7,5};
byte colPins[3] = {4,2,6};
Keypad keypad = Keypad(makeKeymap(keys),rowPins,colPins,4,3);
String enteredCode = "";
void get_temprature(){
   sensors.requestTemperatures();
  temperature = sensors.getTempCByIndex(0);
  if (temperature >= 32.1){
    digitalWrite(HEATER_GPIO,LOW);
    heaterState = false;
    heaterOn = false;
    too_hot = true;
  }
  else if (temperature < 31.0 && too_hot){
    too_hot = false;
  }
}
void RFID_Poll(){
  if(!rfid.PICC_IsNewCardPresent()||!rfid.PICC_ReadCardSerial()){
    return;
  } // no rfid
  bool match = true;
  if (rfid.uid.size != 4){match =false;} // not the right one
  else{
    for(byte i = 0; i < 4; i++){
      if(rfid.uid.uidByte[i] != correct_UID[i]){
        match = false;
        break;
      }
    }
  }

  if(match){
    Serial.println("RFID CORRECT");
    RFID_right = true;
    twofa_timer = millis();
  }
  else{
    for (byte i = 0; i <4; i++){
      Serial.println(rfid.uid.uidByte[i]);
    }
  }
  rfid.PICC_HaltA();
}
void keypad_checker(){
  char key = keypad.getKey();
  if (key){
    Serial.println("Key:" + String(key));
    if (key == '#'){
      if (enteredCode.length() >= 4 && enteredCode ==tempCode){
        Lock_toggle = true;
        digitalWrite(LOCK_GPIO,HIGH);
        tempcode_flag =true;
        lock_tempcode = millis();
        tempCode = String(random(1000, 9999));
      }
      else if (enteredCode ==first_master){
        right_code = true; // 2FA now
        twofa_timer = millis();
      }
      else{
        Lock_toggle = false;
        digitalWrite(LOCK_GPIO, LOW);
      }
      enteredCode = "";
    }
    else if (key == '*'){
      enteredCode = "";
    }
    else{
      enteredCode += key;
    }
  }
}
String website_html(){
  String website ="<!DOCTYPE html><html><head>";
  website += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  website += "<script>setTimeout(()=>{location.reload();},5000);</script>";
  website += "<style>";
  website += "body {font-family: Arial, sans-serif; background-color: #121212; color: #ffffff; text-align: center; padding: 20px;}";
  website += "h1, h2 {color: #ffcc00;}";
  website += ".container {max-width: 500px; margin: auto; padding:20px; background: #1e1e1e; border-radius: 10px; box-shadow: 0 0 10px rgba(255, 255, 255, 0.2); }";
  website += ".btn {padding: 10px 20px; font-size: 18px; color: #ffffff; background: #ff6600; border: none; border-radius: 5px; cursor: pointer; margin: 10px; }";
  website += ".btn:hover {background: #ff4500;}";
  website += ".input-box {padding: 10px; width: 80%; font-size: 16px; border-radius: 5px; border: 1px solid #ccc;}";
  website += "</style></head><body>";
  website += "<div class='container'>";
  website += "<h1>Secure Food Delivery Box</h1>";
  website += "<p>Lock Status: <b>" +String(Lock_toggle ? "<span style='color:#00ff00;'>Unlocked</span>" :"<span style='color:#ff0000;'>Locked</span>") + "</b></p>";
  website += "<form action='/toggle' method='get'>";
  website += "<button class='btn' type='submit'>" +String(Lock_toggle ?"Lock" : "Unlock") + "</button>";
  website += "</form>";
  website += "<h2>Temperature Sensor</h2>";
  website += "<p>Current Temperature: <b>" +String(temperature) +"°C</b></p>";
  website += "<h2>scale Sensor</h2>";
  website += "<p>Food Present: <b>" +String(food_there ?"<span style='color:#00ff00;'>Yes</span>" : "<span style='color:#ff0000;'>No</span>") + "</b></p>";
  website += "<h2>Temporary Code</h2>";
  website += (tempCode.length() >0) ? "<p>" + tempCode + " (one-time use)</p>" :"<p>None (No new food yet)</p>";
  website += "<h2>Change Master first_master</h2>";
  website += "<form action='/setpass' method='post'>";
  website += "<input class='input-box' type='password' name='newpass' minlength='4' maxlength='4'required>";
  website += "<button class='btn'type='submit'>Set first_master</button>";
  website += "</form>";
  website += "<h2>Heater Control</h2>";
  if (too_hot){
    website +="<p style='color:red;'>Heater disabled: box too hot</p>";
    website +="<button class='btn' disabled>Heater Locked</button>";
  }
  else{
    website +="<form action='/toggleheater' method='get'>";
    website +="<button class='btn' type='submit'>" + String(heaterOn ? "Turn Off Heater" : "Turn On Heater") + "</button>";
    website +="</form>";
  }
  website +="</div></body></html>";
  return website;
}
void setup(){
  Serial.begin(115200);
  pinMode(LOCK_GPIO, OUTPUT);
  digitalWrite(LOCK_GPIO, LOW);
  pinMode(HEATER_GPIO, OUTPUT); // init everything
  digitalWrite(HEATER_GPIO, LOW);
  preferences.begin("lockdata", false);
  first_master = preferences.getString("first_master", "2356");
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  while(WiFi.status() != WL_CONNECTED){
    delay(500);
    Serial.print(".");
  }
  Serial.println("IP is at: " + WiFi.localIP().toString());
  sensors.begin();
  scale.begin(LOAD_D, LOAD_SCK);
  delay(100);
  scale.set_scale(-471.497);
  scale.tare();
  SPI.begin(SCK_RFID, MISO_RFID, MOSI_RFID, SDA_RFID);
  rfid.PCD_Init();
  keypad.setDebounceTime(20);
  get_temprature();
  tempCode = String(random(1000, 9999));
  server.on("/", [](){server.send(200,"text/html",website_html());});
  server.on("/toggle", [](){
    Lock_toggle = !Lock_toggle;
    digitalWrite(LOCK_GPIO, Lock_toggle ?HIGH :LOW);
    server.sendHeader("Location", "/", true);
    server.send(303);
  });
  server.on("/setpass", HTTP_POST, [](){
    if (server.hasArg("newpass")){
      first_master = server.arg("newpass");
      preferences.putString("first_master",first_master);
    }
    server.sendHeader("Location","/",true);
    server.send(303);
  });
  server.on("/toggleheater", [](){
    if(!too_hot){
      heaterOn = !heaterOn;
      if(!heaterOn) {
        digitalWrite(HEATER_GPIO,LOW);
        heaterState = false;
      }
    }
    server.sendHeader("Location","/",true);
    server.send(303);
  });
  server.begin();
}
void loop(){
  static unsigned long RFID_checked =0;
  static unsigned long Loadcell_check =0;
  static unsigned long temprature_check =0;
  unsigned long curr_time = millis();
  server.handleClient();
  keypad_checker(); 
  if(curr_time -RFID_checked >= 200){
    RFID_Poll();
    RFID_checked =curr_time;
  }
  if(curr_time -Loadcell_check >= 10000){
  float weight = scale.get_units(10);
  Serial.println(weight);
  food_there = (weight < -40);
  scale.power_down();
  delay(3);
  scale.power_up();
    Loadcell_check = curr_time;
  }
  if(curr_time - temprature_check >= 30000){
    get_temprature();
    temprature_check = curr_time;
  }
  keypad_checker();
  if(RFID_right && right_code){
    Lock_toggle = true;
    digitalWrite(LOCK_GPIO, HIGH);
    isUnlocking = true;
    manualOverride = false;
    lock_timemilis = millis();
    right_code = false;
    RFID_right = false;
  }
  if((right_code || RFID_right) && millis() - twofa_timer > twofa_lockout){
    right_code = false;
    RFID_right = false;
  }
  if(tempcode_flag && millis() -lock_tempcode >= 30000){
    Lock_toggle = false;
    float time =millis();
    digitalWrite(LOCK_GPIO, LOW);
    tempcode_flag = false;
  }
  if(heaterOn && !too_hot){
    if(millis() -heater_button >= 5000){
      heaterState =!heaterState;
      digitalWrite(HEATER_GPIO, heaterState ? HIGH : LOW);
      heater_button = millis();
    }
  }
}
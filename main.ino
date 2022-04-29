/*
 * Function:
 * 1. Circuit connects to power supply after the floating switch inside the tank.
 * 2. Only turns on when floating switch enables.
 * 3. It will cut off power to the pump if no water is pumping to prevent pump failure.
 * 4. You can toggle it on and off from Telegram, if it has power.
 * 5. You will need to calibrate to your sensors needs with the calibrationFactor constant.
 * 6. I used an ESP01 relay module. The circuitry es pretty simple. 
 * 7. If you want it let me know and I can send you pictures or help you with anything you need.
 * 8. Have fun!
 * 
 * !! Set PINS correctly
 * 
 * https://www.forward.com.au/pfod/ESP8266/GPIOpins/ESP8266_01_pin_magic.html
 * 
*/

#include <ESP8266WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>

// Wifi network station credentials
#define WIFI_SSID "<YOUR_WIFI_NAME>"
#define WIFI_PASSWORD "<YOUR_WIFI_PASS>"
// Telegram BOT Token (Get from Botfather)
#define BOT_TOKEN "<YOUR_BOT_TOKEN>"
#define CHAT_ID "<YOUR_TELEGRAM_CHAT_ID>"

// Telegram
const unsigned long BOT_MTBS = 1000; // mean time between scan messages
const unsigned long BOT_UPDATE_TANK = 120000; // mean time between telegram updates
unsigned long bot_lasttime; // last time messages' scan has been done
X509List cert(TELEGRAM_CERTIFICATE_ROOT);
WiFiClientSecure secured_client;
UniversalTelegramBot bot(BOT_TOKEN, secured_client);

//Pins
char sensorPin       = 3;  // RX PIN
char relay           = 0;  //

//Flow Sensor
float calibrationFactor = 5;

volatile byte pulseCount;  
float flowRate;
unsigned long flowMilliLitres;
unsigned long totalMilliLitres;

unsigned long oldTime;
unsigned long upTime;

bool WORKING = true;

void setup()
{
   //Relay
  pinMode(relay,OUTPUT);
  digitalWrite(relay, LOW);
  
  //Flow
  pinMode(sensorPin, INPUT);
  digitalWrite(sensorPin, HIGH);
    
  // attempt to connect to Wifi network:
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  secured_client.setTrustAnchors(&cert); // Add root certificate for api.telegram.org
  
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }

  configTime(0, 0, "pool.ntp.org"); // get UTC time via NTP
  time_t now = time(nullptr);
  while (now < 24 * 3600)
  {
    delay(100);
    now = time(nullptr);
  }
  
  bot.sendMessage(CHAT_ID, "WATER PUMP IS ON", "");

  pulseCount        = 0;
  flowRate          = 0.0;
  flowMilliLitres   = 0UL;
  totalMilliLitres  = 0UL;
  oldTime           = 0;
  upTime            = 0;

  // The Hall-effect sensor is connected to pin 2 which uses interrupt 0.
  // Configured to trigger on a FALLING state change (transition from HIGH
  // state to LOW state)
  attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING);

  delay(5000);

}

void loop() {
    
  if (WORKING) {
      
      if((millis() - oldTime) > BOT_MTBS) {    // Only process counters once per second
      // Disable the interrupt while calculating flow rate and sending the value to
      // the host

      //noInterrupts();
      detachInterrupt(digitalPinToInterrupt(sensorPin));
          
      // Because this loop may not complete in exactly 1 second intervals we calculate
      // the number of milliseconds that have passed since the last execution and use
      // that to scale the output. We also apply the calibrationFactor to scale the output
      // based on the number of pulses per second per units of measure (litres/minute in
      // this case) coming from the sensor.
      flowRate = ((1000.0 / (millis() - oldTime)) * pulseCount) / calibrationFactor;
      
      // Note the time this processing pass was executed. Note that because we've
      // disabled interrupts the millis() function won't actually be incrementing right
      // at this point, but it will still return the value it was set to just before
      // interrupts went away.
      oldTime = millis();
      
      // Divide the flow rate in litres/minute by 60 to determine how many litres have
      // passed through the sensor in this 1 second interval, then multiply by 1000 to
      // convert to millilitres.
      flowMilliLitres = (flowRate / 60) * 1000;

      //Check if its charging
      if(flowMilliLitres == 0) {

        if(totalMilliLitres > 0){
          bot.sendMessage(CHAT_ID, "ERROR: PUMP TURNED OFF MIDFILLING", "");
          digitalWrite(relay, !digitalRead(relay));
          delay(5000);
          bot.sendMessage(CHAT_ID, "ERROR: PUMP TURNED OFF MIDFILLING", "");
        } else {
          bot.sendMessage(CHAT_ID, "ERROR: PUMP IS NOT PUMPING WATER", "");
          digitalWrite(relay, !digitalRead(relay));
          delay(5000);
          bot.sendMessage(CHAT_ID, "ERROR: PUMP IS NOT PUMPING WATER", "");
        };
                
        WORKING = false;   
             
      } else {
      
        // Add the millilitres passed in this second to the cumulative total
        totalMilliLitres += flowMilliLitres;
              
        // Reset the pulse counter so we can start incrementing again
        pulseCount = 0;
        
        // Enable the interrupt again now that we've finished sending output

        //interrupts();
        attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING);

          if((millis() - upTime) > BOT_UPDATE_TANK) {
    
            String MSG;
            MSG = "FILLING UP: " + String(totalMilliLitres/1000) + " Litres... \n";
            bot.sendMessage(CHAT_ID, MSG, "");
            upTime = millis();
        
          };
     
       };
     }
   } else {
    if (millis() - bot_lasttime > BOT_MTBS)
    {
      int numNewMessages = bot.getUpdates(bot.last_message_received + 1);
  
      while (numNewMessages)
      {
        handleNewMessages(numNewMessages);
        numNewMessages = bot.getUpdates(bot.last_message_received + 1);
      }
  
      bot_lasttime = millis();
    }
    } 
 }

ICACHE_RAM_ATTR void pulseCounter()
{
  // Increment the pulse counter
  pulseCount++;
}

void handleNewMessages(int numNewMessages) {

  for (int i = 0; i < numNewMessages; i++) {
    String chat_id = bot.messages[i].chat_id;
    String text = bot.messages[i].text;

    String from_name = bot.messages[i].from_name;
    if (from_name == "")
      from_name = "Guest";

    if (text == "/toggleRelay") {
      digitalWrite(relay, !digitalRead(relay));
      
      if(digitalRead(relay) == HIGH){
        
        WORKING = false;   
        bot.sendMessage(chat_id, "PUMP MANUALLY TURNED OFF... ", "");

      } else {
        
        pulseCount        = 0;
        flowRate          = 0.0;
        flowMilliLitres   = 0;
        totalMilliLitres  = 0;
        oldTime           = 0;
        upTime            = 0;

        //interrupts();
        attachInterrupt(digitalPinToInterrupt(sensorPin), pulseCounter, FALLING); 
        
        bot.sendMessage(chat_id, "PUMP MANUALLY TURNED ON... ", "");
        
        delay(5000);
        WORKING = true;  
        
      };
    };

    if (text == "/help") {
      String welcome = "Hey, " + from_name + ", I'm the water pump.\n";
      welcome += "Pretty simple:\n\n";
      welcome += "/help : this is this menu\n";
      welcome += "/toggleRelay : this toggles the relay";
      bot.sendMessage(chat_id, welcome, "Markdown");
    }; 
  }
}

// modified from batteryMonitor_v2 to drive second revision of surface mount board
//     and hopefully compatible with through hole shield also
// turns out that analog in pins changed because of thermistor and zener
#include <Metro.h>
#include <Streaming.h>
#include <LiquidCrystal.h>
#include <avr/eeprom.h>
#include "batteryMonitor.h"

/*
  Simple  Battery Monitor
 
 The circuit:
 Volts scaled input from 12vdc battery to max 5v for digitize
 Output to 16x2 LCD display
 Metro Library for timing and Streaming for Serial output
 
 created Aug 2010 for Battery Monitor
 by Jerry Jeffress
 
 Modified March 2014 George Hunt
 
 */
/*--------------------------------------------------------------------------------------
 Variables
 --------------------------------------------------------------------------------------*/
//LCD stuff
const int backLitePin = 4;
// initialize the LCD library with the numbers of the interface pins
LiquidCrystal lcd(12, 11, 10, 9, 8, 7);
boolean lcdON;

//Analog read pins
const int zenerInPin = 0;
const int thermistorInPin = 1;
const int voltsInPin = 2;
const int ampsInPin = 3;
const int ampsOutPin = 4;

// User interface buttons
const int button1in = 5;
const int inactiveState = 1;
const int activeState = 0;
const int longpressmillis = 1000; // milliseconds

const float batterySize = 96.0;  //Battery capacity in AmpHrs
const float chargeEff = 0.85;   //Approx Amount of power into a lead acid battery that is stored

Metro oneSecond = Metro(1000);  // Instantiate a one second timer
Metro hundredMS = Metro(100);  // Instantiate a 100ms pd
Metro desulfatePeriod = Metro(2);  // Instantiate a desulfate pd
Metro lcdOnPd = Metro(30000);  // Instantiate a long period for backlight
boolean desulfateOn = true;
const int desulfatePin = 6;

int blinkit;
boolean backlightOn = false;
int bStatus;  //Battery status, 0-6 states
float absorbCtr, chargeCtr, disChargeCtr, eqCtr, voltCount, ampCount, voltRatio, ampRatio;
float absorbTimeOut = 60*60*3.0;//3 hours in seconds
float eqTimeOut = 60*60*3.0;  //3 Hours in seconds
float tenHours = 60*60*10.0;
float sec2Hr = 1.0/(3600.0);  //Convert watt-sec to Watt-Hrs
float volts, amps, amps_reverse, power, watts;
float  bCharge;  //power variable (watt-hours) total effective charge to and from the battery
float bLow;  //low battery value

char inputString[20];         // a array of char to hold incoming data
boolean stringComplete = false;  // whether the string is complete
int inputIndex = 0;// accumulation index
long serialValue; // we'll use this as substitute for float biased by 1000

button_t button1;
const int button1pin = 5;
//-----------------------------------------------------------------------------------------------
void setup(){
  boolean init_settings = false;
  Serial.begin(9600); // Start the Serial communication for debug mainly
  delay(5000);
  if (Serial){
  Serial <<"Free RAM: " << freeRam() << " of original 2000 bytes" << endl;  
  }
  pinMode( A0, INPUT );         //ensure A0 is an input
  pinMode( A1, INPUT );
  pinMode( A2, INPUT );
  pinMode( A3, INPUT );
  pinMode( A4, INPUT );
  digitalWrite( A0, LOW );      //ensure pullup is off on A0
  digitalWrite( A1, LOW );      //ensure pullup is off on A0
  digitalWrite( A2, LOW );      //ensure pullup is off on A0
  digitalWrite( A3, LOW );      //ensure pullup is off on A0
  digitalWrite( A4, LOW );      //ensure pullup is off on A0
  pinMode(desulfatePin, OUTPUT);

  //lcd backlight control
  pinMode( backLitePin, OUTPUT );     //D3 is an output
  digitalWrite(backLitePin, LOW);  //LCD backlite  20.6 ma on battery pack
  button1.pin = button1pin;

  //retrieve the persistent data from eeprom
  eeprom_read_block((void*)&settings, (void*)0, sizeof(settings));
  if (settings.flag != 0x5a || init_settings){
    if (!init_settings)settings.timesWritten = 0;
    settings.flag = 0x5a;
    settings.ampsOutZeroCount = 0;
    settings.ampsInZeroCount = 0;
    settings.ampsOutScaleFactor = 0.417;
    settings.ampsInScaleFactor = 0.417; 
    settings.ampRatio = 0.968;
    settings.voltsScaleFactor = 0.0163;
    if (Serial) {Serial.println("initializing eeprom values");}
  } else {
    if (Serial){
    Serial.println("settings pulled from eeprom");
    Serial << "timsWritten:" << settings.timesWritten << " ampsOutScaleFactor:" <<
           settings.ampsOutScaleFactor*10 << " ampInScaleFactor:" << settings.ampsInScaleFactor*10 << endl;
    Serial <<  " ampRatio:" << settings.ampRatio*10 << " voltsScaleFactor:" <<
              settings.voltsScaleFactor*10 <<  " OutZero:" << settings.ampsOutZeroCount <<
              " InZero:" << settings.ampsInZeroCount << endl;
    }
  }
  
  blinkit = 0;
  bCharge = batterySize * 0.95; //init charge 95%  full battery 
  absorbCtr = 0;
  eqCtr = 0;
  chargeCtr = 60*9;
  disChargeCtr = 0;
  bLow = 0.5;  //Low battery warning when hattery 50% charged
  bStatus = 7;//This a an error value must be updated in Calculate bStatus
  
  // set up the LCD's number of rows and columns: 
  lcd.begin(16, 2);
  
  if (Serial){Serial.println("done with setup");}

}
//-----------------------------------------------------------------------------------------------
void loop(){
  if (oneSecond.check() == 1) { // check if the metro has passed it's interval .
    //sample volt and amps from the battery
    sampleVoltsAmps();
    
    // Output serial time volts amps as to display
    if (false){
      Serial << volts << " Volts " << amps << " Amps watts:" << volts*amps << 
        " inctr:" << analogRead(ampsInPin) << " outCtr:" << analogRead(ampsOutPin) << 
        endl;
    }

    //Update LCD display each second
    calcPower();
    calcbStatus();
    displayStatus();

    if (stringComplete) {
      if (Serial){Serial.println(inputString);}
      process_string();
      
      // clear the string:
      inputString[0] = 0;
      stringComplete = false;
    }
  } // end of onsecond processing
  if (lcdON and lcdOnPd.check() == 1){ // time to turn it off
    digitalWrite(backLitePin, LOW);  //LCD backlite  20.6 ma on battery pack
    lcdON = false;
  }
  if (button1.short_press){  
    digitalWrite(backLitePin, HIGH);  //LCD backlite  20.6 ma on battery pack
    lcdON = true;
    button1.short_press = false;
    Serial.println("short press");
  }
  if (hundredMS.check() == 1){
    serialEvent(); //check for datacom characters
    buttonEvents(); //and button presses
  }
  if (desulfatePeriod.check() == 1 and desulfateOn) { // check if the metro has passed it's interval
     doDesulfate();
  }

} // end of loop

//-----------------------------------------------------------------------------------------------
void sampleVoltsAmps() {
  float voltSum = 0.0;
  float ampSum = 0.0, scaler;
  int chargepin, loopctr, zero;
  // figure out which way the current is flowing
  if (analogRead(ampsInPin) > analogRead(ampsOutPin)) {
    chargepin = ampsInPin;
    scaler = settings.ampsInScaleFactor;
    zero = settings.ampsInZeroCount;
  } else {
    chargepin = ampsOutPin;
    scaler = -settings.ampsOutScaleFactor;
    zero = -settings.ampsOutZeroCount;
  }
  // loop to get rid of gitter in reading
  loopctr = 10;
  for (int i = 0; i < loopctr; i++){
    voltSum = voltSum + analogRead(voltsInPin);
    ampSum = ampSum + analogRead(chargepin) + zero;
    delay(1);
  }//end smooth loop
  //Get 10 sample average
  volts = voltSum / loopctr * settings.voltsScaleFactor;  //Factor to scale to volts
  //loat zeroRef = volts / 2.0 + settings.zeroAmpsOffset;
  //amps = ampSum /10000.0;  //Factor to scale to volts

  amps = (ampSum / loopctr) * scaler;  //Factor to scale to Amps
}

//-----------------------------------------------------------------------------------------------
//Calc power
void calcPower(){
  power =  volts * amps;  //Units are watt-seconds
  if (power <= 0){
    bCharge = bCharge +(power * sec2Hr);
  }
  else {
    bCharge = bCharge + (power * sec2Hr * chargeEff);
  }
}

//-----------------------------------------------------------------------------------------------
//Calculate  
void calcbStatus(){
  bStatus = 0; //This charge/discharge
  if(amps >= 0)
    chargeCtr = chargeCtr +1;
  if(amps < 0){
    disChargeCtr = disChargeCtr + 1;
  }

  //if(volts < 28.3)
  //absorbCtr = absorbCtr -1; //debug only commint
  if(absorbCtr < 0)
    absorbCtr = 0; 
  if(volts > 28.3){
    bStatus = 1;
    absorbCtr = absorbCtr + 1;
  }
  if(volts > 27.3 && absorbCtr > absorbTimeOut)
    bStatus = 2;
}

//-----------------------------------------------------------------------------------------------
void process_string(){
 char c;
 char *p;
   p = inputString;
   c=*p;
   Serial << "verb:" << c << endl;
   while (*p != ' ' && *p != '/n') p++; // advance past verb
   Serial.println(*p);   serialValue = parse_decimal(p);
   if (c == 'r'){
     report();
   } else if (c == 'v'){ // specify the correct voltage
     float v;
     v = serialValue;
     int cnt = analogRead(voltsInPin);
     if (v > 0) {   
       settings.voltsScaleFactor = v / (1000.0 * cnt);
       Serial <<"volt Scalefactor:" <<settings.voltsScaleFactor << " v:" << v << " cnt:" << cnt << endl;
     }
   } else if (c == 'c') {
     float chg = serialValue;
     int cnt = analogRead(ampsInPin);
     if (cnt > 0) {   
       settings.ampsInScaleFactor = chg / (1000.0 * cnt);
       Serial.print("charge:");
       Serial.println(settings.ampsInScaleFactor,4);
     }
   } else if (c == 'd') {    
     float d = serialValue;
     int cnt = analogRead(ampsOutPin);
     if (cnt > 0) {   
       settings.ampsOutScaleFactor = d / (1000.0 * cnt);
       Serial.print("discharge:");
       Serial.println(settings.ampsOutScaleFactor,4);
     }
     Serial.print("discharge");
   } else if (c == 'z') {
       Serial.print("zero");
       int cnt = analogRead(ampsOutPin);
       settings.ampsOutZeroCount = cnt;
       cnt = analogRead(ampsInPin);
       settings.ampsInZeroCount = cnt;
   } else if (c == 'w') {
     Serial.println("writing eeprom");
     settings.timesWritten++;
     eeprom_write_block((const void*)&settings, (void*)0, sizeof(settings));
   } else if (c == 'x') {
       Serial.println("reporting settings");
       
       int cnt = analogRead(voltsInPin);
       Serial.print("Voltage Scale Factor: ");
       Serial.print(settings.voltsScaleFactor,4);
       Serial.print(" Voltage Count: ");
       Serial.print(cnt);
       
       cnt = analogRead(zenerInPin);
       Serial.print(" Zener Count: ");
       Serial.println(cnt);
       
       cnt = analogRead(ampsOutPin);
       Serial.print("discharge factor: ");
       Serial.print(settings.ampsOutScaleFactor,4);
       Serial.print("discharge count: ");
       Serial.println(cnt);
       
       cnt = analogRead(ampsInPin);
       Serial.print("charge factor: ");
       Serial.print(settings.ampsInScaleFactor,4);
       Serial.print("charge count: ");
       Serial.println(cnt);
 }

}

//-----------------------------------------------------------------------------------------------
long parse_decimal(const char *str) // decimals are of form ###.###
{
  int fraction = 0;
  while (*str == ' ') str++;
  boolean isneg = *str == '-';
  if (isneg) ++str;
  if (*str == '/n') return 0L;
  unsigned long ret = 1000UL * get_atol(str);
  Serial << "ret:" << ret;
  while (isDigit(*str)) ++str;  // advance over integer part
  if (*str == '.')
  {
    unsigned long mult = 100;
    for (int i=0;i<4;i++){
      if (isDigit(*++str)){
        fraction += mult * (*str - '0');
        mult /= 10;
      }
        
    }
  }
  Serial << "return value:" << ret + fraction << endl;
  return ret + fraction;
}

//-----------------------------------------------------------------------------------------------
long get_atol(const char *str)
{
  long ret = 0;
  Serial.println("inget_atoal");
  while (isDigit(*str))
    ret = 10 * ret + *str++ - '0';
  return ret;
}

boolean isDigit(char c){
  if (c >= '0' && c <= '9') return true;
  return false;
}

//----------------------------------------------------------------------------------------------- 
void serialEvent() {
  while (Serial.available()) {
     char inChar = (char)Serial.read(); 
     // add it to the inputString:
     inputString[inputIndex++] = inChar;
     if (inChar == '\n'){
    //if the incoming character is a newline, set a flag
    // so the main loop can do something about it:
        stringComplete = true;
        inputIndex = 0;
     }
  }
} 

//-----------------------------------------------------------------------------------------------
void processButton(struct button_t *button){
  // button_t is a package of variables related to the current button
  if (digitalRead(button->pin) == activeState){
    if (not button->active){ //wait for another time pd for bounce to quiet
      button->active = true;
      button->activeMSstart = 0;
      button->long_press_timeout = millis() + longpressmillis;
      return;
    }
    if (button->activeMSstart == 0){ //bounce is over, record the beginning keypress time
      button->activeMSstart = millis();
      lcdOnPd.reset();
    } 
    if ( millis() > button->long_press_timeout){
        button->long_press = true;
        button->long_press_timeout = millis();
    }  
  }
  if (digitalRead(button->pin) != activeState and button->active){ // just went inactive
    // was this a long or a short press?
    if ( millis() - button->activeMSstart < longpressmillis){
      button->short_press = true;
    }
    button->long_press = false;
    button->active = false;
  }   
}

//-----------------------------------------------------------------------------------------------
void buttonEvents(){
  // called every 100 mS
// allow for multible buttons
 processButton(&button1);
}  

//-----------------------------------------------------------------------------------------------
//update LCD Display
void displayStatus(){ 
  
  watts = volts * amps;
  // Print volts to LCD.
  lcd.setCursor(0,0);
  lcd.print("BAT ");
  lcd.print(volts, 2); //display volts to one decimal place
  lcd.print("v ");
  // Print % full to display
  if(bCharge < batterySize){
    lcd.print(100 * bCharge/batterySize,1);
    lcd.print("%");
  }
  else{
    lcd.print("Full ");
    bCharge = batterySize;
  }
  //Print Battery Status to Display
  lcd.setCursor(0, 1);
  switch(bStatus){
  case 0:
    /*if(amps >= 0)
      lcd.print("Charge   ");
    else
      lcd.print("Discharge "); */
    //if(amps >= 0) lcd.print("+"); else lcd.print("+");
    if (watts < 10.0) lcd.print(" ");
    //if (watts < 100.0) lcd.print(" ");
    lcd.print(watts,1);
    lcd.print("W ");
    break;
  case 1:
    lcd.print("Absorb   ");
    break;
  case 2:
    lcd.print("Float    ");
    break;
  default:
    lcd.print("Error    ");
  }
  if(bCharge < bCharge * bLow){//Battery Low blink
    if(blinkit==0)
      lcd.print("Low      ");
    else
      lcd.print("      Low");
  }
  if(volts > 29.2 && eqCtr < eqTimeOut){
    lcd.setCursor(0, 1);
    lcd.print("Equalize ");
    eqCtr =eqCtr + 1;
  }
  //Print Amps to display right justified
  if(amps<0){  //All negative
    if(amps<-9.9){
      lcd.print(amps,1);
    }
    else{
      lcd.print(" ");
      lcd.print(amps,2);
    }
  }
  else { //all positive or zero
    if(amps>9.9){
      lcd.print("+");
      lcd.print(amps,1);
    }
    else{
      lcd.print(" +");
      lcd.print(amps,2);
    }
  }

  lcd.print("A");
}
/*
//Counter page
 void displayCtr(){
 lcd.clear();
 lcd.print("Chr ");
 lcd.print(chargeCtr/60,0);
 lcd.print("  Dis ");
 lcd.print(disChargeCtr/60,0
 );
 lcd.setCursor(0,1);
 lcd.print("Abs ");
 lcd.print(absorbCtr/60,0);
 lcd.print("   eq ");
 lcd.print(eqCtr/60,0);
 }
 */

//-----------------------------------------------------------------------------------------------
void displayCtr(){
  lcd.clear();
  lcd.print("VCtr");
  lcd.print(voltCount,0);
  lcd.print("  Amp ");
  lcd.print(ampCount);
  lcd.setCursor(0,1);
  lcd.print("Abs ");
  lcd.print(absorbCtr/60,0);
  lcd.print("   eq ");
  lcd.print(eqCtr/60,0);
}

//-----------------------------------------------------------------------------------------------
int freeRam() {
  extern int __heap_start, *__brkval; 
  int v; 
  return (int) &v - (__brkval == 0 ? (int) &__heap_start : (int) __brkval); 
}
void      doDesulfate(){
  digitalWrite(desulfatePin,HIGH);
  delay(1);
  digitalWrite(desulfatePin,LOW);
}
//-----------------------------------------------------------------------------------------------
void report() {
        Serial << volts << " Volts " << amps << " Amps watts:" << volts*amps << 
        " inctr:" << analogRead(ampsInPin) << " outCtr:" << analogRead(ampsOutPin) << 
        " bulk:" << bCharge << endl;
}







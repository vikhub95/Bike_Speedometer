//******************** Libraries ********************************************************************************************************************************************************************************************************************
#include <EEPROM.h>  
#include <Wire.h>                                                 //Wire library used for I2C Communication: Arduino Pro Mini pins
                                                                  //A4 = SDA and A5 = SCL
#include <Adafruit_SSD1306.h>                                     //Adafruit driver for OLED 1306
//#include <Adafruit_GFX.h>                                        //Adafruit graphic display library used for OLED Display

//************* Initialize Variables ****************************************************************************************************************************************************************************************************************
#define som_version "5.0"

#define reed            2                                                //First Interrupt pin D2
#define screen_button   3                                              //Second Interrupt pin D3
#define units_button    4
#define OLED_RESET      5  

Adafruit_SSD1306 oled(OLED_RESET);                                //Initialize OLED Display

// Constants
float radius_km = 0.0003429;                                         //Diameter: 27 in = 0.3429 m = 0.0003429 km
float circumference_km_km;                                              //Must initialize C here so that interrupt can work properly
bool imperial = false;
float K = 0.621371;                                                           // Conversion factor from km to miles

// Volatile Variables
float distance       = 0.00;
float speed          = 0.00;

// EEPROM Variables
float longest_ride;      // in minutes
float furthest_ride;     // in 100 meter increments
float fastest_speed;

static unsigned long last_interrupt_time;
unsigned long interrupt_time;
unsigned long time_difference;
long return_time;

//********************************************************************************************************************************************************************
// Screens
volatile bool screen = 0;      // Defaults to MainScreen

//******************** Code Begins ******************************************************************************************************************************************************************************************************************
void setup(){
  
  //**************** Calculations ***************************
  circumference_km = 2 * 3.14159 * radius_km;                           // Only unchanging calculation

  //*************** Initializations *************************
  Serial.begin(9600);                                             // Initialize use of Serial Monitor with baud rate of 9600. Make sure this matches with the one on the Serial Monitor
  
  pinMode(reed,          INPUT_PULLUP);                           // Enable pull up
  pinMode(screen_button, INPUT_PULLUP);                           // Enable pull up
  
  attachInterrupt(0, magnet_pulse,  RISING);                       // Execute magnet_pulse function on a rising edge (0 = Pin 2; 1 = Pin 3; These are the only pins that can utilize this interrupt functionality
  attachInterrupt(1, Switch_Screen, FALLING);                      // Execute Switch_Screen function on a falling edge

  //*********** SSD1306 Display Initializations ***************
  oled.begin(SSD1306_SWITCHCAPVCC, 0x3C);                         // Initialize with the I2C addr 0x3C (for the 128x64)
  oled.clearDisplay();                                            // Clears Adafruit's startup screen
  oled.display();                                                 // Displays object "oled"

  //************* Startup Screen *******************************
  Splash_Screen();

  longest_ride  = EEPROM.read(0);
  furthest_ride = EEPROM.read(1);
  fastest_speed = EEPROM.read(2);
}

//******************** Loop Begins ******************************************************************************************************************************************************************************************************************
void loop(){
  //********************* Calculations *******************************
  // No pulse runout
  time_difference = millis() - return_time;                       //Delta t (t2 - t1)
  if(time_difference > 2000){                                     //If no pulse within 2000ms (2 sec), speed = 0
    speed = 0.00;
  }

  // EEPROM Overwrite condition
  speed = round(speed*10.0)/10.0;                           //Formatting to round speed to nearest tenth
  if(speed > fastest_speed){
    EEPROM.write(2, speed);
  }
  EEPROM.write(0, total_distance);
  if(distance > top_distance){
    EEPROM.write(1, distance);
  }

  // Metric or Imperial Units check
  if( digitalRead(units_button) == LOW ){
    imperial = true;
  }
  else{
    imperial = false;
  }
    
  //----------- Draw Screens ----------------------------------------
  switch(screen){
    case 1:
      StatsScreen();
      break;
    default:
      MainScreen();
      break;
  }
  
}

//*************** Main Screen ***************************************************************************************************************************************************************************************************************
// Primary screen with live updated speed and distance
void MainScreen(){
  // Static Text
  oled.setTextColor(WHITE);                                   //Don't need background color because this remains static, so we don't care if it accumulates.
  if( imperial == true ){
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    oled.println("miles: ");

    oled.setTextSize(1);
    oled.setCursor(105, 40);
    oled.println("mph: ");
  }
  else{
    oled.setTextSize(2);
    oled.setCursor(0, 0);
    oled.println("km: ");

    oled.setTextSize(1);
    oled.setCursor(105, 40);
    oled.println("kmh: ");
  }

  //********** Now display what was built ************
  oled.display();

  // Dynamic Text
  while(screen == 0){
    oled.setTextColor(WHITE, BLACK);                                //Note: setTextColor(FG, BG). Set the background color or the old text will still display. Only need this once.
  
    //************ Print distance ***************
    oled.setTextSize(2);
    oled.setCursor(64, 0);
    oled.println(distance, 2);
  
    //************ Print speed ***************
    oled.setTextSize(3);
  
    if(speed < 10.0 ){
      oled.setCursor(33, 40);
      oled.println(speed, 1);                                    //"1" gives how many digits after decimal to display
      oled.setCursor(15, 40);                                       //Covers 10s place from previous double digit number. Otherwise "1" will remain in front of single digit number
      oled.println(" ");
    }
    else if(speed >= 10.0 ){
      oled.setCursor(15, 40);
      oled.println(speed, 1);                                    //"1" gives how many digits after decimal to display
    }
  
    //********** Now display what was built ************
    oled.display();                                                 //Build oled with above parameters
  }
}

//*************** Stats Screen ***************************************************************************************************************************************************************************************************************
// Secondary screen with stats from the EEPROM displayed
void StatsScreen(){
  oled.clearDisplay();                                          //Remove contents of the first screen
    
  oled.setTextColor(WHITE, BLACK); 
  oled.setTextSize(1);
  oled.setCursor(0, 0);
  oled.println("Statistics EEPROM");
  oled.setCursor(0, 16);
  oled.print("Top Recorded Distance: ");
  oled.println(top_distance, 2);
  oled.setCursor(0, 26);
  oled.print("Top Recorded Speed: ");
  oled.println(fastest_speed, 2);
  oled.setCursor(0, 36);
  
  oled.display();                                               //Build oled with above parameters  
}

//*************** Interrupt Functions ***************************************************************************************************************************************************************************************************************
//ISR: Reed Switch
void magnet_pulse(){
  
  //***************** Calculations & Debouncing ************************
  static unsigned long last_interrupt_time = 0;
  unsigned long interrupt_time = millis();

  if(interrupt_time - last_interrupt_time > 150){             // Sequential pulses must be at least 150ms apart
    distance += circumference_km;                                 // Accumulate distance
    speed = (3600000*circumference_km)/(interrupt_time - last_interrupt_time);   // 3600000ms in 1hr
  }
  
  last_interrupt_time = interrupt_time;
  return_time = last_interrupt_time;
}

// ISR: Switch_Screen
void Switch_Screen(){

  //***************** Calculations & Debouncing ************************
  static unsigned long last_pressed_time = 0;
  unsigned long pressed_time = millis();

  if(pressed_time - last_pressed_time > 350){
    screen = !screen;    
  }

  last_pressed_time = pressed_time;
}

//*************** Startup Screen ***************************************************************************************************************************************************************************************************************
// Loading screen with version number
void Splash_Screen(){
  oled.setTextColor(WHITE);
  oled.setTextSize(1);
  oled.setCursor(0,0);
  oled.println("Bike Speedometer");
  oled.print("Version ");
  oled.println(som_version);
  oled.display();
  delay(2000);
  oled.clearDisplay();
}

/***************************************************
 *  Socotel S63 vintage phone revival with an arduino and a MP3 shield
 *  
 *  @file socotel.ino
 *  @brief play a random MP3 song of the year your dialed on the S63
 *
 *  @copyright  [Thomas CHAPPE](https://github.com/ThomasChappe), 2019
 *  @copyright  GNU General Public License v3.0
 *
 *  @author    [Thomas CHAPPE](https://github.com/ThomasChappe)
 *  @version   2.0.0
 *  @date      2019-12-29
 *  
 *  All the documentation for this code is available here : 
 *  https://github.com/ThomasChappe/S63_Arduino/
 *  
 *  Arduino shield and/or wiring instructions available here : 
 *  https://github.com/ThomasChappe/S63_Arduino/#des-fils-des-connexions-un-peu-de-code-et-la-magie-prend-forme
 *  
 *  Extra hardware used :   
 *  - DFPlayer - A Mini MP3 Player For Arduino
 *    https://www.dfrobot.com/wiki/index.php/DFPlayer_Mini_SKU:DFR0299
 *    https://www.dfrobot.com/product-1121.html
 *    also available under the name "MP3-TF-16P"
 *
 *  - optional Solenoid (x2) to have the bells ringing
 *    https://www.aliexpress.com/item/32801392552.html
 *    and to have them working and managed by the Arduino, 2 NPN transistors (TIP102) used as switches.
 * 
 *  software library required for MP3 player :
 *  https://github.com/DFRobot/DFRobotDFPlayerMini/
*/


#include "SoftwareSerial.h"
#include "DFRobotDFPlayerMini.h"

/*****
****** BEGIN OF CUSTOMIZABLE SECTION : Here, ou can modify values for timers, pins, etc... 
******/

// MP3 player will use a serial communication, let's initialize it with Arduino pins.
SoftwareSerial mySoftwareSerial(10, 11); // RX, TX
// sometime ACK messages sent on software serial seems to create bugs when trying to access to number of files in folders... strange but observed.
// if you get "no file found" too often, then try to set the flag to false.
const bool gc_useMP3Ack = true;
// sometime you should try to disable reset flag, if your SD card isn't recognized at boot... strange but observed.
// if the MP3 does not start properly at boot, then try to set the flag to false.
const bool gc_resetMP3OnBoot = true;
// Set volume value (0~30). /!\ 5 is clearly enough if you don't want to damage your ears !
const unsigned short gc_MP3Volume = 5;


// define pins used by arduino
const unsigned int gc_hangUpPin = 5;
// WARNING ! 
//This pin MUST be connected to an interruption capable pin (see your Arduino board documentation to know which pin is available)
const unsigned int gc_dialPin = 2; // WARNING ! Interruption pin : rotaring pulses (numbers) are caught here
const unsigned int gc_rotaringPin = 4; // pin used to know if we are currently rotaring.

// dial constants
const unsigned int gc_pickUpTimeout = 10000; // time (ms) before occupied tone will ring (ie : too long before dialing)
const unsigned int gc_dialingTimeout = 10000; // time (ms) before timeout after digit composed => launch call

// SD card content definitions
const unsigned int gc_minYearToCall = 1940; // older year available in SD card
const unsigned int gc_maxYearToCall = 2017; // earlier year available in SD card
const unsigned int gc_00YearFolderReplacementNumber = 39; // ugly trick ! folder number replacement, used for year 00 (like 2000, since folder "00" is not supported by chip  :-/)

// version 2.0 : added the ability to have the bells ringing by calling number : (default number : 666)
const unsigned int gc_bell1Pin = 7; // the output pin for the first bell (solenoid with knock on the bell)
const unsigned int gc_bell2Pin = 8; // the output pin for the second bell (solenoid with knock on the bell)
const unsigned short gc_max_bell_ring = 8; // max number of ringing in the full sequence : bells will ring X time before stop. (8 times for legacy in France)
const unsigned int gc_bell_ring_duration = 1500; // the bells rings for this duration (in ms) in the ringing sequence (1500ms for legacy in France)
const unsigned int gc_bell_ring_delay = 3500; // the bells pause for this duration (in ms) in the ringing sequence (3500ms for legacy in France)
const unsigned int gc_bell_ring_period = 17; // the bells ring one after the other, at current period (1/frequency), in ms. (50Hz => 20ms for legacy in France, but rings better at 17, IMHO)
const String gc_self_call_number = "0666"; // the number to dial, to have the phone ring
const unsigned int gc_delay_before_call = 2000; // delay after having self dialed and hanged up, before the phone rings (in ms).

/*****
****** END OF CUSTOMIZABLE SECTION, DO NOT CHANGE ANYTHING BELOW THIS LINE 
******/



// we will use special tones to bring phone back to life and simulate calls
// this MP3 are stored in a special folder on SD Card. This folder is SD:/MP3/000X.mp3 (where X is the constant below)
const unsigned short GC_TONE_WAITING    = 1;
const unsigned short GC_TONE_OCCUPIED   = 2;
const unsigned short GC_TONE_SEARCHING  = 3;
const unsigned short GC_TONE_RINGING    = 4;
const unsigned short GC_TONE_ERROR      = 5;
const unsigned short GC_TONE_CALL       = 6; // special sound when asked to be called back (have the bells ringing).

// we will use a status to manage code execution : check g_phoneStatus and time elapsed to decide which code is active 
//  0 = hanged up
//  1 = tone
//  2 = dialing (no tone)
//  3 = finished composing number
//  4 = too long waiting => occupied tone => end
//  5 = playing song / mission complete !
//  6 = no song found or invalid number dialed
//  7 = incoming call ! The phone will ring !
const unsigned short GC_PHONE_HANGED_UP     = 0;
const unsigned short GC_PHONE_TONE          = 1;
const unsigned short GC_PHONE_DIALING       = 2;
const unsigned short GC_PHONE_NUMBER_OK     = 3;
const unsigned short GC_PHONE_TIMEOUT       = 4;
const unsigned short GC_PHONE_CALL_OK       = 5;
const unsigned short GC_PHONE_NO_SOUND_SD   = 6;
const unsigned short GC_PHONE_INCOMING_CALL = 7;
unsigned short g_phoneStatus = GC_PHONE_HANGED_UP;


// declare the MP3 player
DFRobotDFPlayerMini g_MP3Player;


// do we need to have the phone ring now ?
bool g_incomingCall = false;  

// phone and dial constants
const unsigned int gc_pulseInterval = 66; // ms lasting of dialing pulses (see rotary specs)

// debounce, rotary pulse detection and digit composed (use interruptions)
volatile unsigned long g_dialTime = 0;
volatile unsigned long g_lastDialTime = 0;
volatile short g_currentDigitComposed = -1;

// loop status and functional management
unsigned long g_timer; 
bool g_previouslyRotaring = false;

// timeout will change after 4th digit composed => accelerate switch to calling mode
unsigned int g_currentDialingTimeout = gc_dialingTimeout;
// the number dialed so far (string to manage number beginning by "0")
String g_numberDialed = ""; 



/****************************************************************
 * when a digit is composed, add it to number given in parameter
*/
void addDigitToComposedNumber(String & p_composedNumber)
{
  if (g_currentDigitComposed != -1)
  {
    // append digit to string, since we begin with -1, add +1, and %10 to get the "0"
    p_composedNumber += (g_currentDigitComposed + 1) % 10;
  }
  g_currentDigitComposed = -1; // set digit back to -1
}


/****************************************************************
* check if rotary is currently active (digit is being composed)
*/
bool isRotaring()
{
  // if pin is HIGH, since we are in pullup mode => not rotaring
  return digitalRead(gc_rotaringPin) == LOW;
}


/****************************************************************
* pulse callback : used by an interruption when pulse is detected from rotary
* checks if new pulse is coming, then add it to current digit composed
*/
void rotaryPulseCallback()
{
  g_dialTime = millis();

  // check we got a new pulse, not the same 
  if (g_dialTime - g_lastDialTime >= gc_pulseInterval)
  {
    g_currentDigitComposed++; // ok, got pulse, add it
    g_lastDialTime = g_dialTime;
  }
}


/****************************************************************
* checks if phone is hanged up
*/
bool isPhoneHangedUp()
{
  // if pin is HIGH, since we are un pullup mode => phone hanged up
  int sensorVal = digitalRead(gc_hangUpPin);

  // show hanged status with onboard led (just for debug / fun)
  if (sensorVal == HIGH) {
    digitalWrite(LED_BUILTIN, LOW);   
  } else {
    digitalWrite(LED_BUILTIN, HIGH);
  }
  
  return sensorVal == HIGH;
}


/****************************************************************
* compute folder to use on SD Card, from number called.
* Translates 4 digit years to 2 digits folders
* and avoir "0" folder, not supported by MP3 chip
* param : the number dialed on the phone (if more than 4 digits, will take the last 4 digits to get year)
* return 0 if invalid date/number is given
*/
unsigned short getFolderFromNumberDialed(String p_numberDialed)
{    
  // get year only from input (ie : get last 4 digits)
  if (p_numberDialed.length() > 4)
  {
    p_numberDialed = p_numberDialed.substring(p_numberDialed.length() - 4);
  }
    
  unsigned short l_yearToCall = p_numberDialed.toInt();
  
  // check that year is valid
  if (l_yearToCall <= gc_maxYearToCall && l_yearToCall >= gc_minYearToCall)
  {    
    // MP3 player only reads "01-99" folders => remove extra years numbers
    l_yearToCall -= 1900;
    
    // manage 1900 vs 2000 years
    if (l_yearToCall >= 100)
    {
      l_yearToCall -= 100;
    }
  
    // ugly trick : MP3 player cannot read into "00" folder =>> let's read special folder number for year 2000 ... :-/
    if (l_yearToCall == 0)
    {
      l_yearToCall = gc_00YearFolderReplacementNumber;
    }

    return l_yearToCall;
  }

  return 0;
}


/****************************************************************
* get number of songs in folder, on SD Card.
* then get a random song from this folder.
* param : the folder to use
* return 0 if unable to get a song from given folder
*/
unsigned short getRandomSongFromFolder(unsigned short p_folderToCall)
{
  // read file counts in folder SD:/<year>
  short l_nbFilesInFolder = g_MP3Player.readFileCountsInFolder(p_folderToCall);

  // sometime, the sd card seems to be lazy. Strange. But fixed by a second try. 
  if (l_nbFilesInFolder <= 0)
  {
    Serial.println("Got no file from the folder. Let's try another time, just in case of a lazy sd card"); 
    delay(100);
    l_nbFilesInFolder = g_MP3Player.readFileCountsInFolder(p_folderToCall);
  }
  
  if (l_nbFilesInFolder > 0)
  {
    Serial.println (String (l_nbFilesInFolder) + " files found in folder");
    long l_songNumber = random(1, l_nbFilesInFolder + 1);
    Serial.println ("Random selection of music : done. We will play song number : " + String(l_songNumber) );
    return l_songNumber;
  }

  // empty folder or error trying to get files
  return 0;
}


/****************************************************************
* called when everything is allright, so let's play song !
* but before, it will emulate the old song of a french phone long distance call, then ring.
* params : the folder to use and the file number to play
*/
void callAndPlaySong (unsigned short p_folderToCall, unsigned short p_fileToPlay)
{
  // let's emulate long call (on old french phone lines)
  g_MP3Player.playMp3Folder(GC_TONE_SEARCHING);  //play specific mp3 in SD:/MP3/0003.mp3; => searching line
  delay(3000);
  g_MP3Player.playMp3Folder(GC_TONE_RINGING);  //play specific mp3 in SD:/MP3/0004.mp3; => ringing !
  delay(6000);

  Serial.println ("Call succeed : now play song !");
  Serial.println("Play random song number : " + String(p_fileToPlay)); 

  //play mp3 in SD:/<year>/<random>.mp3; Folder Name(1~99); File Name(1~255)
  g_MP3Player.playFolder(p_folderToCall, p_fileToPlay);  
}


/*
 * ring the phone bells. Plays the full and genuine sequence of ringing :
 *   - ring for gc_bell_ring_duration, knocking the bells at gc_bell_ring_period
 *   - wait for gc_bell_ring_delay before ringing again
 *   - repeat it gc_max_bell_ring times.
 * 
 * if phone is picked up => ring stops immediately.
 * 
 * put solenoid pins low before exit, in order to avoid the solenoid to get hot and drain power.
*/
void ringBells ()
{
  long l_toneBeginTime = millis();
  
  // ring the specified number of time, and stop ringing if phone is picked up
  for (unsigned short l_ringCount = 0 ; l_ringCount < gc_max_bell_ring  && ( isPhoneHangedUp());)
  {    
    long l_currentTime = millis();
    if (l_currentTime - l_toneBeginTime < gc_bell_ring_duration)
    {
      // let's ring for the right duration
      delay(gc_bell_ring_period);
      digitalWrite (gc_bell1Pin, LOW);
      digitalWrite (gc_bell2Pin, HIGH);
      delay(gc_bell_ring_period);
      digitalWrite (gc_bell2Pin, LOW);
      digitalWrite (gc_bell1Pin, HIGH); // finish sequence with one bell high too let one bell "ring" (better sound !)
    }
    else if (l_currentTime - l_toneBeginTime > gc_bell_ring_delay)
    {
      // let's wait between rings for the right duration
       l_toneBeginTime = l_currentTime;
       l_ringCount ++;
    }
  }

  // avoid to send useless power to solenoid when not ringing and have them get too hot
  digitalWrite (gc_bell2Pin, LOW);
  digitalWrite (gc_bell1Pin, LOW);
}


/*
 * a simple function to check if we are calling ourselve : do we want the phone to ring ?
 * 
 * return true if we just dialed the special "self call" number.
*/
inline bool isSelfCalling (String p_numberDialed)
{
  return p_numberDialed == gc_self_call_number;
}





/****************************************************************
* the standard Arduino setup function
*/
void setup()
{ 
  //start serial connection for MP3 player AND for debug traces
  mySoftwareSerial.begin(9600);
  Serial.begin(115200);

  //configure pin as an input and enable the internal pull-up resistor => phone up or down ?
  pinMode(gc_hangUpPin, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT); /* DEBUG only : internal led is on when phone is working  */

  // configure rotary dial pin to get pulses (and attach interruption callback)
  pinMode(gc_dialPin, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(gc_dialPin), rotaryPulseCallback, RISING);

  //configure pin as an input and enable the internal pull-up resistor => rotaring or not ?
  pinMode(gc_rotaringPin, INPUT_PULLUP);

  // configure pins for the bells and ringing
  pinMode(gc_bell1Pin, OUTPUT); 
  pinMode(gc_bell2Pin, OUTPUT); 
  digitalWrite (gc_bell1Pin, LOW);
  digitalWrite (gc_bell2Pin, LOW);

  Serial.println();
  Serial.println(F("Welcome to Socotel S63 Arduino revival !"));
  Serial.println();
  Serial.println(F("Initializing MP3 player, please wait... (about 3 seconds)"));

  delay(2000); // MP3 player seem to need time to boot.

  // Use softwareSerial to communicate with mp3,
  if ( ! g_MP3Player.begin(mySoftwareSerial, gc_useMP3Ack, gc_resetMP3OnBoot) ) 
  { 
    Serial.println(F("Unable to begin:"));
    Serial.println(F("1.Please recheck the connection!"));
    Serial.println(F("2.Please insert the SD card!"));
    Serial.println(F("3.If it still fail : try to change MP3 flags in configuration"));    
    while (true);
  }
  Serial.println(F("MP3 player ready"));

  delay(1000); // and more time to listen to its serial port


  //----Setup MP3 player ----
  //Set serial communication time out 800ms (bellow seems to create errors when getting SD files number)
  g_MP3Player.setTimeOut(800); 
  g_MP3Player.volume(gc_MP3Volume);
  g_MP3Player.EQ(DFPLAYER_EQ_NORMAL);  // Set EQ
  g_MP3Player.outputDevice(DFPLAYER_DEVICE_SD);   // Set device we use : SD card 

  // generate random number : from Arduino documentation :
  // analog input pin 0 is unconnected, random analog
  // noise will cause the call to randomSeed() to generate
  // different seed numbers each time the sketch runs.
  // randomSeed() will then shuffle the random function.
  
  // personnal side note : reading the pin only once is not enough and produce too predictive results... 
  // so, add some (ugly) randomness (that works to randomize the song order)
  unsigned long l_seed = analogRead(A0);
  l_seed *= analogRead(A0);
  l_seed *= analogRead(A0);
  l_seed /= analogRead(A0);
  l_seed *= analogRead(A0);
  l_seed *= analogRead(A0);
  l_seed /= analogRead(A0);
  l_seed += analogRead(A0);
  Serial.println ("Songs are randomized with random seed : " + String (l_seed));
  randomSeed(l_seed);

  // timer is used to compute delays and timeout
  g_timer = millis();

  Serial.println();
  Serial.println("Socotel Ready ! Let's go !");
  Serial.println();
}





/****************************************************************
* the standard Arduino main loop function
*/
void loop()
{ 
  if (isPhoneHangedUp())
  {
    g_MP3Player.pause(); // stop sounds

    // phone is hanged up. Did we program an incoming call ?
    if (g_phoneStatus == GC_PHONE_INCOMING_CALL)
    {
      // wait a little, not to have the phone calling instantly after hanged up
      delay (gc_delay_before_call); 
      ringBells();
    }

    // reset previous variables since we hanged up. 
    g_phoneStatus = GC_PHONE_HANGED_UP;    
    g_numberDialed = "";
    g_currentDigitComposed = -1;
    g_currentDialingTimeout = gc_dialingTimeout;    
    g_timer = millis();
  }
  else
  {
    // phone is picked up, are we rotaring a digit ?
    if (isRotaring() && g_phoneStatus < GC_PHONE_NUMBER_OK)
    {
      g_timer = millis(); // no timeout while rotaring
      if ( (!g_previouslyRotaring) )
      {
        addDigitToComposedNumber(g_numberDialed); // if not first digit => we need to add it to number
        Serial.println ("Start rotaring, adding previous digit. Number so far : " + String(g_numberDialed) );
        g_previouslyRotaring = true;
        g_phoneStatus = GC_PHONE_DIALING;
        g_MP3Player.pause(); // stop playing tone when dialing starts

        // if we already have 3 digits and we start dialing the 4th, reduce timeout, to start call quickly
        if (g_numberDialed.length() == 3)
        {
          g_currentDialingTimeout = 500;
        }
      }
    }
    else
    {
      g_previouslyRotaring = false;

      if ( (millis() - g_timer > g_currentDialingTimeout) && (g_currentDigitComposed != -1) && g_phoneStatus < GC_PHONE_NUMBER_OK )
      {
        // digit has been composed, and timeout reached => add digit to number, then call !
        addDigitToComposedNumber(g_numberDialed);
        Serial.println ("Time out for rotaring. Now calling : " + String(g_numberDialed));
        g_phoneStatus = GC_PHONE_NUMBER_OK;
      }

      // time out not reached, phone just picked up => tone
      if (millis() - g_timer < gc_pickUpTimeout && g_phoneStatus == GC_PHONE_HANGED_UP)
      {
        g_phoneStatus = GC_PHONE_TONE;
        Serial.println ("Phone picked up : let's tone");
        g_MP3Player.playMp3Folder(GC_TONE_WAITING); //play specific mp3 in SD:/MP3/0001.mp3; File Name(0~65535) => A3 Tone
      }

      // timeout and no digit => occupied tone
      if (millis() - g_timer > gc_pickUpTimeout && (g_phoneStatus == GC_PHONE_TONE || g_phoneStatus == GC_PHONE_DIALING))
      {
        g_phoneStatus = GC_PHONE_TIMEOUT;
        Serial.println ("Time out and no digit dialed : occupied tone");
        g_MP3Player.playMp3Folder(GC_TONE_OCCUPIED); //play specific mp3 in SD:/MP3/0002.mp3; => occupied tone
      }

      // we got a number to call ! => go for it !
      if (g_phoneStatus == GC_PHONE_NUMBER_OK)
      {
        Serial.println ("Start calling : check number");

        unsigned short l_folderToCall = getFolderFromNumberDialed(g_numberDialed);

        // if invalid date
        if (! l_folderToCall)
        {
          // check special numbers : did we ask to be recalled ? (and btw, "who you gonna call ?" :-) )
          if (isSelfCalling(g_numberDialed))
          {
            Serial.println("Phone asked to be recalled. Let's program an incoming call ! Please hang up !"); 
            g_phoneStatus = GC_PHONE_INCOMING_CALL;
            //notify user that call is scheduled : play specific mp3 in SD:/MP3/0006.mp3; => special call back tone !
            g_MP3Player.playMp3Folder(GC_TONE_CALL);  
          }
          else
          {
            Serial.println("Invalid year called, play error tone"); 
            g_phoneStatus = GC_PHONE_NO_SOUND_SD;
            g_MP3Player.playMp3Folder(GC_TONE_ERROR);  //play specific mp3 in SD:/MP3/0005.mp3; => error tone !          
          }
        }
        else
        {
          // play random song from folder, if exists.
          Serial.println("Ok, valid number. Calling year : "  + g_numberDialed + " => folder : " + l_folderToCall);
          unsigned short l_fileToPlay = getRandomSongFromFolder(l_folderToCall);
 
          if (l_fileToPlay <= 0)
          {
            Serial.println("No file found in folder, play error tone"); 
            //if no file in folder =>> play error tone
            g_phoneStatus = GC_PHONE_NO_SOUND_SD;
            g_MP3Player.playMp3Folder(GC_TONE_ERROR);  //play specific mp3 in SD:/MP3/0005.mp3; => error tone !          
          }
          else
          {
            Serial.println ("Songs found in folder ! Play search then ringing tone");
            g_phoneStatus = GC_PHONE_CALL_OK;            
            callAndPlaySong (l_folderToCall, l_fileToPlay);
          }
        }
      }
    }
  }
}


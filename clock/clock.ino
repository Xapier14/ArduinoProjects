#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <DS3231.h>
#include <String.h>

struct Info
{
public:
  double CpuUsage = 0.0;
  double RamUsage = 0.0;
  double GpuUsage = 0.0;
  short Processes = 0;
  byte Volume = 0;
  String NowPlaying = "na";
  byte PlayerStatus = 0;
};

struct Alarm
{
public:
  bool Enabled = false;
  bool Rang = false;
  String Name = "-n/a-";
  byte Hour = 0;
  byte Minute = 0;
  byte DayOfWeek = 0;
};

struct InputState
{
public:
  bool lUp;
  bool lDown;
  bool lRight;
  bool lLeft;
  bool rUp;
  bool rDown;
  bool rLeft;
  bool rRight;
  bool select;
  bool cancel;

  bool any()
  {
    return lUp || lDown || lLeft || lRight ||
           rUp || rDown || rLeft || rRight ||
           select || cancel;
  }
};

String centerString(String, byte = 16);

// classes
class AlarmManager
{
private:
  const byte _MAXALARMS = 30;
  Alarm* _alarms;
  const byte _MAXCHECK = 15; // minutes

public:
  AlarmManager()
  {
    _alarms = new Alarm[30];
  }

  void setAlarm(byte index, String name, byte hour, byte minute, byte dayOfWeek = 0)
  {
    _alarms[index].Name = centerString(name);
    _alarms[index].Hour = hour;
    _alarms[index].Minute = minute;
    _alarms[index].Enabled = true;
    _alarms[index].Rang = false;
    _alarms[index].DayOfWeek = dayOfWeek;
  }

  void armAll()
  {
    for (byte i = 0; i < _MAXALARMS; ++i)
    {
      if (_alarms[i].Enabled)
      {
        _alarms[i].Rang = false;
      }
    }
  }

  void init()
  {
    for (byte i = 0; i < _MAXALARMS; ++i)
    {
      _alarms[i].Name = "na";
      _alarms[i].Hour = 255;
      _alarms[i].Minute = 255;
      _alarms[i].Enabled = false;
      _alarms[i].Rang = false;
      _alarms[i].DayOfWeek = 255;
    }
  }

  Alarm* getAlarm(byte index)
  {
    return &(_alarms[index]);
  }

  bool isEnabled(byte index)
  {
    if (index >= _MAXALARMS || index < 0)
      return false;
    return _alarms[index].Enabled;
  }

  byte checkAlarm(byte currentHour, byte currentMinute, byte currentDayOfWeek)
  {
    for (byte i = 0; i < _MAXALARMS; ++i)
    {
      if (_alarms[i].Enabled)
      {
        if (currentHour == _alarms[i].Hour &&
            currentMinute >= _alarms[i].Minute &&
            currentMinute <= _alarms[i].Minute + _MAXCHECK &&
            !_alarms[i].Rang &&
            (_alarms[i].DayOfWeek == 0 || _alarms[i].DayOfWeek == currentDayOfWeek))
        {
          return i;
        }
      }
    }
    return 255;
  }
};
class InputManager
{
private:
  const double deadzone = 0.3;
  double rawLX = 0, rawLY = 0, rawRX = 0, rawRY = 0;
  bool selectDown = false;
  bool cancelDown = false;

  // rebounce stuff
  bool wasLD = false,
       wasLU = false,
       wasLR = false,
       wasLL = false,
       wasRD = false,
       wasRU = false,
       wasRR = false,
       wasRL = false,
       wasSelect = false,
       wasCancel = false;

public:
  int pinLX, pinLY, pinRX, pinRY, pinSelect, pinCancel;

  InputManager(int lX, int lY, int rX, int rY, int select, int cancel)
  {
    pinLX = lX;
    pinLY = lY;
    pinRX = rX;
    pinRY = rY;
    pinSelect = select;
    pinCancel = cancel;
  }

  void begin()
  {
    pinMode(pinSelect, INPUT_PULLUP); //pull select high
    pinMode(pinCancel, INPUT_PULLUP); //pull select high
  }

  void poll()
  {
    rawLX = (analogRead(pinLX) / 1023.0) - 0.5;
    rawLY = (analogRead(pinLY) / 1023.0) - 0.5;
    rawRX = (analogRead(pinRX) / 1023.0) - 0.5;
    rawRY = (analogRead(pinRY) / 1023.0) - 0.5;

    selectDown = digitalRead(pinSelect) == LOW;
    cancelDown = digitalRead(pinCancel) == LOW;

    //Serial.println("L: (" + String(rawLX) + ", " + String(rawLY)+ "); R: (" + String(rawRX) + ", " + String(rawRY) + ")");
  }

  InputState getState()
  {
    InputState state;
    bool lu = rawLY < -deadzone;
    bool ld = rawLY > deadzone;
    bool lr = rawLX < -deadzone;
    bool ll = rawLX > deadzone;

    bool ru = rawRY < -deadzone;
    bool rd = rawRY > deadzone;
    bool rr = rawRX < -deadzone;
    bool rl = rawRX > deadzone;

    state.lUp = lu && !wasLU;
    state.lDown = ld && !wasLD;
    state.lRight = lr && !wasLR;
    state.lLeft = ll && !wasLL;

    state.rUp = ru && !wasRU;
    state.rDown = rd && !wasRD;
    state.rLeft = rl && !wasRL;
    state.rRight = rr && !wasRR;

    state.select = selectDown && !wasSelect;
    state.cancel = cancelDown && !wasCancel;

    wasLU = lu;
    wasLD = ld;
    wasLL = ll;
    wasLR = lr;
    wasRU = ru;
    wasRD = rd;
    wasRL = rl;
    wasRR = rr;

    wasSelect = selectDown;
    wasCancel = cancelDown;

    return state;
  }
};

// modules
DS3231 clock;
RTClib rtc;
LiquidCrystal_I2C lcd(0x27, 16, 2);
InputManager input(A3, A2, A1, A0, 2, 3);
AlarmManager alarms;
//PCClient client(9600);

// cached datetime
byte _day = 0;
byte _month = 0;
byte _year = 0;
byte _hour = 0;
byte _minute = 0;
byte _second = 0;
byte _dow = 0;

// state vars
bool _clockChanged = false, _hourChanged = false, _dayChanged = false;
bool _buzzed = false;
int _backlightCycles = 0;

// alarm sound
double alarmSound[] = {600, 400, 800, 200};
byte alarmTone = 0;
byte alarmLength = 4;

// pins
int buzzerPin = 12;

/*
   MenuStates
    0 = Display DateTime
    1 = Monitor/Logging
    2 = Menu
    3 = Time Setup
    4 = Date Setup
    5 = Alarms Setup
    6 = Alarm Running
*/
byte menuState = 0;
byte alarmTrack = 0;

// timing
byte maxCycles = 100; // 100*10ms = 1000ms per single cycle
byte printOn = 0;     // which cycle to refresh clock
byte currentCycle = 0;
byte cycleDelay = 10;     //ms
int backlightTime = 1000; // 1000*10ms = 10,0000ms
bool backlightIsOn = true;

// date funcs
void SetDate(byte day, byte month, byte year, byte dayWeek)
{
  clock.setDate(day);
  clock.setMonth(month);
  clock.setYear(year);
  clock.setDoW(dayWeek);
}

void SetTime(byte hour, byte minute, byte second)
{
  clock.setHour(hour);
  clock.setMinute(minute);
  clock.setSecond(second);
}

void Set24Hour(bool enable)
{
  clock.setClockMode(!enable);
}

void updateClock()
{
  DateTime dateTime = rtc.now();

  byte day = dateTime.day();
  byte month = dateTime.month();
  byte year = clock.getYear();
  byte dow = clock.getDoW();

  byte hour = dateTime.hour();
  byte minute = dateTime.minute();
  byte second = dateTime.second();

  if (day != _day)
  {
    _clockChanged = true;
    _dayChanged = true;
    _day = day;
  }
  if (month != _month)
  {
    _clockChanged = true;
    _month = month;
  }
  if (year != _year)
  {
    _clockChanged = true;
    _year = year;
  }
  if (dow != _dow)
  {
    _clockChanged = true;
    _dow = dow;
  }

  if (hour != _hour)
  {
    _clockChanged = true;
    _hourChanged = true;
    _hour = hour;
  }
  if (minute != _minute)
  {
    _clockChanged = true;
    _minute = minute;
  }
  if (second != _second)
  {
    _second = second;
  }
}

String getWeekday(byte dow)
{
  if (dow == 1)
    return "Sun";
  if (dow == 2)
    return "Mon";
  if (dow == 3)
    return "Tue";
  if (dow == 4)
    return "Wed";
  if (dow == 5)
    return "Thu";
  if (dow == 6)
    return "Fri";
  if (dow == 7)
    return "Sat";
  return "Any";
}

String getMonth(byte month)
{
  if (month == 1)
    return "Jan";
  if (month == 2)
    return "Feb";
  if (month == 3)
    return "Mar";
  if (month == 4)
    return "Apr";
  if (month == 5)
    return "May";
  if (month == 6)
    return "Jun";
  if (month == 7)
    return "Jul";
  if (month == 8)
    return "Aug";
  if (month == 9)
    return "Sep";
  if (month == 10)
    return "Oct";
  if (month == 11)
    return "Nov";
  if (month == 12)
    return "Dec";
  return "nul";
}

// formatting & printing
String formatDoubleDigit(byte val)
{
  String v = String(val);
  if (val < 10)
    return "0" + v;
  return v;
}

String formatDate()
{
  return "  " + getMonth(_month) + " " + formatDoubleDigit(_day) + ", 20" + String(_year);
}

String amPm(byte hour)
{
  if (hour >= 0 && hour < 12)
    return "AM";
  return "PM";
}

String formatTime()
{
  return " " + formatDoubleDigit(_hour) + ":" + formatDoubleDigit(_minute) + " " + amPm(_hour) + " - " + getWeekday(_dow);
}

String centerString(String str, byte length = 16)
{
  byte startIndex = 0;
  if (str.length() < length)
  {
    startIndex = (length/2) - (str.length()/2);
  }
  
  String ret = "";

  // init string
  for (int i = 0; i < length; ++i)
  {
    if (i < startIndex)
    {
      ret += " ";
    } else if (i >= startIndex + str.length())
    {
      ret += " ";
    } else {
      ret += str[i-startIndex];
    }
  }

  return ret;
}

void setup()
{
  Wire.begin();
  pinMode(buzzerPin, OUTPUT);
  Serial.begin(9600);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  input.begin();
  alarms.init();
  // monday
  alarms.setAlarm(0, "Wake Up", 8, 30, 2);
  alarms.setAlarm(1, "Join Class", 9, 00, 2);
  // tuesday
  alarms.setAlarm(2, "Wake Up", 7, 30, 3);
  alarms.setAlarm(3, "Join Class", 8, 00, 3);
  // wednesday
  alarms.setAlarm(4, "Wake Up", 6, 30, 4);
  alarms.setAlarm(5, "Join Class", 7, 00, 4);
  // thursday
  alarms.setAlarm(6, "Wake Up", 7, 30, 5);
  alarms.setAlarm(7, "Join Class", 8, 00, 5);
  // friday
  alarms.setAlarm(8, "Wake Up", 9, 30, 6);
  alarms.setAlarm(9, "Join Class", 10, 00, 6);

  // everyday
  alarms.setAlarm(10, "ECheck", 20, 00);
  alarms.setAlarm(11, "TEST", 14, 00);

  //lcd.setCursor(0, 0);
  //lcd.print("Loaded alarms.");
  //delay(1500);
  //lcd.clear();
  _backlightCycles = backlightTime;
}

void printTime()
{
  _clockChanged = false;
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(formatTime());
  lcd.setCursor(0, 1);
  lcd.print(formatDate());
  if (_hourChanged)
  {
    _hourChanged = false;
    tone(buzzerPin, 400);
    _buzzed = true;
  }
}

void clockMenuState()
{
  InputState state = input.getState();
  Serial.println("CLOCK");

  if (state.cancel)
  {
    // switch to monitoring/logging state
    menuState = 1;
    alarmTrack = 0;
    noTone(buzzerPin);
    _backlightCycles = backlightTime;
    lcd.backlight();
    backlightIsOn = true;
    return;
  }

  if (state.lUp)
  {
    tone(buzzerPin, 600, 200);
  }
  if (state.lDown)
  {
    tone(buzzerPin, 800, 200);
  }
  if (state.lLeft)
  {
    tone(buzzerPin, 200, 200);
  }
  if (state.lRight)
  {
    tone(buzzerPin, 400, 200);
  }

  if (state.any() || _hourChanged)
  {
    _backlightCycles = backlightTime;
    //Serial.println("Activated.");
  }

  // check if we are supposed to refresh the screen, and that the clock has changed
  if (currentCycle == printOn && _clockChanged)
    printTime();

  if (currentCycle == 6)
  {
    if (_buzzed)
    {
      noTone(buzzerPin);
      _buzzed = false;
    }
  }

  if (_backlightCycles > 1)
  {
    if (!backlightIsOn)
    {
      lcd.backlight();
      backlightIsOn = true;
    }
    _backlightCycles--;
  }
  else
  {
    if (backlightIsOn)
    {
      lcd.noBacklight();
      backlightIsOn = false;
    }
  }
}

void alarmState(byte alarmIndex)
{
  Alarm* alarm = alarms.getAlarm(alarmIndex);
  if (alarmIndex == 255)
  {
    return;
  }
  if (!backlightIsOn)
  {
    lcd.backlight();
    backlightIsOn = true;
  }
  InputState state = input.getState();
  /*
  if (currentCycle % 20 == 0)
  {
    tone(buzzerPin, 600);
    delay(50);
    noTone(buzzerPin);
  }
  */
  if (currentCycle % 20 == 0)
  {
    tone(buzzerPin, alarmSound[alarmTone], 150);
    alarmTone++;
    if (alarmTone >= alarmLength) {
      alarmTone = 0;
    }
  }
  if (_clockChanged)
  {
    _clockChanged = false;
    lcd.clear();
    lcd.setCursor(0, 0);
    Serial.println("PRINTING TIME");
    lcd.print(formatTime());
    lcd.setCursor(0, 1);
    Serial.println("PRINTING NAME");
    lcd.print(alarm->Name);
  }
  if (state.select || _hour != alarm->Hour)
  {
    _clockChanged = true;
    _hourChanged = false;
    menuState = 0;
    currentCycle = maxCycles - 1;
    alarm->Rang = true;
    _backlightCycles = backlightTime;
    //Serial.println("Alarm '" + alarm->Name + "' rang.");
  }
}

void settingsMenuState()
{

}
void timeMenuState()
{

}
void dateMenuState()
{

}
void alarmMenuState()
{

}

void loggingState()
{
  InputState state = input.getState();

  if (state.cancel)
  {
    menuState = 0;
    _clockChanged = true;
    _hourChanged = false;
    return;
  }

  if (currentCycle == 0)
  {
    Alarm* alarm = alarms.getAlarm(alarmTrack);

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("#" + String(alarmTrack+1) + " " + getWeekday(alarm->DayOfWeek) + " " + formatDoubleDigit(alarm->Hour) + ":" + formatDoubleDigit(alarm->Minute) + " " + amPm(alarm->Hour));
    lcd.setCursor(0, 1);
    lcd.print(centerString(alarm->Name));

    if (alarms.isEnabled(alarmTrack + 1))
    {
      alarmTrack++;
    } else {
      alarmTrack = 0;
    }
  }
}

void loop()
{
  updateClock();
  input.poll();
  byte alarmIndex = alarms.checkAlarm(_hour, _minute, _dow);

  if (alarmIndex != 255)
  {
    menuState = 6;
  }

  switch (menuState)
  {
    case 0: // clock display
      clockMenuState();
      break;
    case 1:
      loggingState();
      break;
    case 2: // settings menu state
      settingsMenuState();
      break;
    case 3: // time setting
      timeMenuState();
      break;
    case 4: // date setting
      dateMenuState();
      break;
    case 5: // alarm setting
      alarmMenuState();
      break;
    case 6: // alarm ringing
      if (alarmIndex == 255)
      {
        menuState = 0;
        break;
      }
      alarmState(alarmIndex);
      break;
  }

  if (currentCycle < maxCycles)
    currentCycle++;
  else
    currentCycle = 0;

  if (_dayChanged)
  {
    alarms.armAll();
    _dayChanged = false;
  }

  delay(cycleDelay);
}

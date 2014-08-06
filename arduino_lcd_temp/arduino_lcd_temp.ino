#include <LiquidCrystal.h>

#include <OneWire.h>
#include <MenuBackend.h>
#include <stdio.h>
#include <string.h>
#include <EEPROM.h>

#define DEBUG 0
#define TEST 1

#define TEMPERATUR_SENORS 4
#define DELAY 1000
#define ERRORVALUE -1000;
#define btnRIGHT  0
#define btnUP     1
#define btnDOWN   2
#define btnLEFT   3
#define btnSELECT 4
#define btnNONE   5
#define MENU_MODE 0
#define TEMP_MODE 1
#define DEFAULT_PRINT_TIME 150

int temperaturPins[TEMPERATUR_SENORS] = {24, 25, 26, 27};
OneWire oneWires[TEMPERATUR_SENORS] = {OneWire (temperaturPins[0]), OneWire (temperaturPins[1]), OneWire (temperaturPins[2]), OneWire (temperaturPins[3])};
LiquidCrystal lcd(8, 9, 4, 5, 6, 7);

byte allAddress [TEMPERATUR_SENORS][8];

unsigned long last = 0;
float lastResult[TEMPERATUR_SENORS] = {0, 0, 0, 0};
int mode = TEMP_MODE;

// KEY VARIABLES
int lcd_key = btnNONE;
int lcd_key_prev = btnNONE;
unsigned long lcd_key_prev_pressed = 0;
unsigned long millisPressedKey = 0;
int buttonPressed = 0;
unsigned long debounceTime = 0;

//MENU
MenuBackend menu = MenuBackend(menuUseEvent, menuChangeEvent);
MenuItem settings = MenuItem("Settings"); //1
MenuItem serialLogging = MenuItem("Serial logging");//11
MenuItem refreshRate = MenuItem("Refresh rate");//12
MenuItem refreshRateDefault = MenuItem("150ms");//121
MenuItem refreshRate500 = MenuItem("500ms");//122
MenuItem refreshRate1s = MenuItem("1s");//123
MenuItem refreshRate10s = MenuItem("10s");//124
MenuItem serialLoggingTrue = MenuItem("On");//111
MenuItem serialLoggingFalse = MenuItem("Off");//112
MenuItem exitItem = MenuItem("Exit");//2
int activeItem = 1; //very hack. But make it easy to program. Just a stupid menu.
int up = 1;

//PROPERTIES
int EEPROM_ADDR = 0;
int doTempSerialLogging = 1;

void menuSetup() { //TODO: use this more.
  menu.getRoot().add(settings);
  //Serial logging
  exitItem.addBefore(settings);
  settings.addAfter(exitItem);
  settings.addRight(serialLogging);
  serialLogging.addRight(serialLoggingTrue);
  serialLoggingTrue.addBefore(serialLoggingFalse);
  serialLoggingTrue.addAfter(serialLoggingFalse);
  serialLoggingFalse.addAfter(serialLoggingTrue);
  serialLoggingFalse.addLeft(serialLogging);
  serialLoggingTrue.addLeft(serialLogging);
  serialLogging.addLeft(settings);

  refreshRate.addBefore(serialLogging);
  refreshRate.addAfter(serialLogging);
  refreshRate.addRight(refreshRateDefault);
  refreshRate.addLeft(settings);
  menu.moveDown(); //Just get out of root.
}

void menuUseEvent(MenuUseEvent used) {
#if defined(DEBUG)
  Serial.print("Menu use ");
  Serial.println(used.item.getName());
#endif

  if (used.item == exitItem) {
    lcd.clear();
    for (int i = 0; i < TEMPERATUR_SENORS; i++) {
      lastResult[i] = 0;
    }
    mode = TEMP_MODE;
  } else if (used.item == serialLoggingFalse) {
    doTempSerialLogging = 0;
    EEPROM.write(EEPROM_ADDR, doTempSerialLogging);
  } else if (used.item == serialLoggingTrue) {
    doTempSerialLogging = 1;
    EEPROM.write(EEPROM_ADDR, doTempSerialLogging);
  }
}

/*
        XXX: Hack to make it easy.
*/
void menuChangeEvent(MenuChangeEvent changed) {
#if defined(DEBUG)
  Serial.print("Menu change from: ");
  Serial.print(changed.from.getName());
  Serial.print(" to: ");
  Serial.println(changed.to.getName());
#endif

  if (changed.to == settings) {
    activeItem = 1;
    up = 1;
  } else if (changed.to == exitItem) {
    activeItem = 2;
    up = 0;
  } else if (changed.to == serialLogging) {
    activeItem = 11;
    up = 1;
  } else if (changed.to == serialLoggingTrue) {
    activeItem = 111;
    up = 1;
  } else if (changed.to == serialLoggingFalse) {
    activeItem = 112;
    up = 0;
  } else if (changed.to == refreshRate) {
    activeItem = 12;
    up = 0;
  } else if (changed.to == refreshRateDefault) {
    activeItem = 121;
    up = 0;
  } else if (changed.to == refreshRate500) {
    activeItem = 122;
    up = 1;
  } else if (changed.to == refreshRate1s) {
    activeItem = 123;
    up = 0;
  } else if (changed.to == refreshRate10s) {
    activeItem = 124; //TODO: struct
    up = 1;
  }
}

// read the buttons
int read_LCD_buttons() {
  int adc_key_in = 0;
  adc_key_in = analogRead(0);
  // my buttons when read are centered at these valies: 0, 97, 254, 437, 639
  // we add approx 50 to those values and check to see if we are close
  if (adc_key_in > 1000) return btnNONE; // We make this the 1st option for speed reasons since it will be the most likely result
  // For V1.1 us this threshold
  if (adc_key_in < 50)   return btnRIGHT;
  if (adc_key_in < 150)  return btnUP;
  if (adc_key_in < 350)  return btnDOWN;
  if (adc_key_in < 450)  return btnLEFT;
  if (adc_key_in < 850)  return btnSELECT;
  return btnNONE;  // when all others fail, return this...
}

void setup(void) {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.print("Starting..");
  menuSetup();
  int value = -1;
  value = EEPROM.read(EEPROM_ADDR);
  if (value < 0 || value > 1) {
    EEPROM.write(EEPROM_ADDR, doTempSerialLogging);
  }
  lcd.clear();
}


float printTemp(OneWire ds, String& name) {
  byte i;
  byte present = 0;
  byte type_s;
  byte data[12];
  byte addr[8];
  float celsius, fahrenheit;
  if ( !ds.search(addr)) {
    ds.reset_search();
    //delay(250);
    //Serial.println("ERROR");
    return ERRORVALUE;
  }

  if (OneWire::crc8(addr, 7) != addr[7]) {
    return ERRORVALUE;
  }

  // the first ROM byte indicates which chip
  switch (addr[0]) {
    case 0x10:
#if defined(DEBUG_TEMP)
      Serial.println("  Chip = DS18S20");  // or old DS1820
#endif
      type_s = 1;
      break;
    case 0x28:
#if defined(DEBUG_TEMP)
      Serial.println("  Chip = DS18B20");
#endif
      type_s = 0;
      break;
    case 0x22:
#if defined(DEBUG_TEMP)
      Serial.println("  Chip = DS1822");
#endif
      type_s = 0;
      break;
    default:
#if defined(DEBUG_TEMP)
      Serial.println("Device is not a DS18x20 family device.");
#endif
      return ERRORVALUE;
  }

  ds.reset();
  ds.select(addr);
  ds.write(0x44, 1);        // start conversion, with parasite power on at the end

  // we might do a ds.depower() here, but the reset will take care of it.

  present = ds.reset();
  ds.select(addr);
  ds.write(0xBE);         // Read Scratchpad

  for ( i = 0; i < 9; i++) {           // we need 9 bytes
    data[i] = ds.read();
  }

  // convert the data to actual temperature

  unsigned int raw = (data[1] << 8) | data[0];
  if (type_s) {
    raw = raw << 3; // 9 bit resolution default
    if (data[7] == 0x10) {
      // count remain gives full 12 bit resolution
      raw = (raw & 0xFFF0) + 12 - data[6];
    }
  } else {
    byte cfg = (data[4] & 0x60);
    if (cfg == 0x00) raw = raw << 3;  // 9 bit resolution, 93.75 ms
    else if (cfg == 0x20) raw = raw << 2; // 10 bit res, 187.5 ms
    else if (cfg == 0x40) raw = raw << 1; // 11 bit res, 375 ms
    // default is 12 bit resolution, 750 ms conversion time
  }
  celsius = (float)raw / 16.0;
  fahrenheit = celsius * 1.8 + 32.0;
  return celsius;
}

void printSafeLCD(const char* innSrc) {
  printSafeLCD(innSrc, ' ');
}

void printSafeLCD(const char* src, const char marker) {
  char value[17];
  strlcpy(value, src, sizeof(value));
  if (strlen(src) < sizeof(value)) {
    for (int i = strlen(src) ; i < sizeof(value) ; i++) {
      value[i] = ' ';
    }
  }
  if (marker != ' ') {
    char menuVal[18] = {marker, '\0', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*', '*'};
    strlcat(menuVal, value, sizeof (menuVal) - strlen(menuVal) - 1);
    lcd.print(menuVal);
  } else {
    lcd.print(value);
  }

}

void printMenuMarker(int down) {
  //Menu marker
  if (!down) {
    lcd.setCursor(0, 0);
    lcd.print(" ");
    lcd.setCursor(0, 1);
    lcd.print(">");
  } else {
    lcd.setCursor(0, 0);
    lcd.print(">");
    lcd.setCursor(0, 1);
    lcd.print(" ");
  }
}

void printMenu(void) {
  if (mode == TEMP_MODE) return;
  printMenuMarker(up);
  if (activeItem <= 10) {
    lcd.setCursor(1, 0);
    printSafeLCD(settings.getName(), '+');
    lcd.setCursor(1, 1);
    printSafeLCD(exitItem.getName());
  } else if (activeItem <= 100) {
    lcd.setCursor(1, 0);
    printSafeLCD(serialLogging.getName(), '+');
    lcd.setCursor(1, 1);
    printSafeLCD(refreshRate.getName(), '+');
  } else {
    if (activeItem < 120) {
      if (doTempSerialLogging) {
        lcd.setCursor(1, 0);
        printSafeLCD(serialLoggingTrue.getName(), '*');
        lcd.setCursor(1, 1);
        printSafeLCD(serialLoggingFalse.getName());
      } else {
        lcd.setCursor(1, 0);
        printSafeLCD(serialLoggingTrue.getName());
        lcd.setCursor(1, 1);
        printSafeLCD(serialLoggingFalse.getName(), '*');
      }
    } else {
      lcd.setCursor(1, 0);
      printSafeLCD(refreshRateDefault.getName(), '*');
      lcd.setCursor(1, 1);
      printSafeLCD(refreshRate500.getName());
    }
  }
}

void printTemp(void) {
  unsigned long now = millis();
  if ((last + DEFAULT_PRINT_TIME) < now) {
    for (int i = 0; i < TEMPERATUR_SENORS; i++) {
      String tempName = String("Sensor");
      tempName = tempName + (i + 1) + ":";
      float value = printTemp(oneWires[i], tempName);
      if (lastResult[i] != value ) {
        lastResult[i] = value;
        if (value == -1000) {
          continue;
        }
        doTempSerialLogging = EEPROM.read(EEPROM_ADDR);
        if (doTempSerialLogging) {
          Serial.print(tempName);
          Serial.print(value);
          Serial.println(" c");
        }
        if (mode == TEMP_MODE) {
          int row = i % 2;
          int col = 0;
          if (i > 1) {
            col = 8;
          }
          lcd.setCursor(col, row);
          String printTxt = String("");
          printTxt = printTxt + (i + 1) + ":";
          char temp[6];
          if (value < 100) { // One digit precicion if temp > 100
            dtostrf(value, 1, 2, temp);
          } else {
            dtostrf(value, 1, 1, temp);
          }
          printTxt = printTxt + temp;
          lcd.print(printTxt);
        }
      }
    }
    last = millis();
  }
}

/**
* Check if button is pressed. A click is defined to between 20-200 mills.
*/
int getButtonPressed() {
  lcd_key = read_LCD_buttons();  // read the buttons
  unsigned long now = millis();
  if (lcd_key == btnNONE) {
    return btnNONE;
  } else if (millisPressedKey == 0 && lcd_key != btnNONE) {
    millisPressedKey = now;
    return btnNONE;
  } else if (((millisPressedKey + 10) < now) && (debounceTime < now)) {
    int lcd_key_confirm = read_LCD_buttons();
    debounceTime = now + 300;
    millisPressedKey = 0;
    if (lcd_key == lcd_key_confirm) {
      //Serial.println(lcd_key);
      buttonPressed = 0;
      return lcd_key;
    } else {
      millisPressedKey = now;
      return btnNONE;
    }
  } else {
    return btnNONE;
  }
}

void doButtonAction(int value) {
  switch (value) {
    case btnRIGHT: {
        if (mode == TEMP_MODE) {
          Serial.println("MARKER");
        } else {
          menu.moveRight();
        }
        break;
      }
    case btnLEFT: {
        if (mode == MENU_MODE) {
          menu.moveLeft();
        }
        break;
      }
    case btnUP:
      {
        if (mode == MENU_MODE) {
          menu.moveUp();
          Serial.println('a');
        }
        break;
      }
    case btnDOWN:
      {
        if (mode == MENU_MODE) {
          menu.moveDown();
        }
        break;
      }
    case btnSELECT:
      {
        if (mode == MENU_MODE) {
          menu.use();
        } else {
          lcd.clear();
          mode = MENU_MODE;
        }
        break;
      }
    case btnNONE:
      {
        break;
      }
  }
  lcd_key = btnNONE;
}

void loop(void) {
  int btn = btnNONE;

  btn = getButtonPressed();
  doButtonAction(btn);
  printTemp();
  printMenu();
  delay(2);
}

// CTRL + T to format code
// include serial prints throughout code as a way to test on the fly

// use # confirm/lock/unlock, may be subject to change since key has a lot of features
// use * delete
char master_code[5] = { '#', '2', '8', '0', '4' };    // hard-coded value
int adc_vals[10] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 };  // array to hold adc vals to be averaged

const int GREENLED = 13;
const int REDLED = 12;

// const int interruptPin = 2;  // uses digital pin 2 for INT0 interrupt

bool BLOCKED = false;
bool servo_moving = false;

bool movement_window_active = false;
unsigned long block_start_time;
unsigned long block_duration = 900;

volatile bool interrupt_woke = false;
volatile bool raw_interrupt = false;
bool sleeping = false;
unsigned long idle_start_time;
unsigned long idle_countdown_duration = 12500;
bool timer_flag = true;

enum Lock_State {
  // POWER_ON, for now unneeded7, but may see future use
  SLEEP_MODE,      // enters sleep
  LOCKED,          // locker is locked, degrees 0
  UNLOCKED,        // locker is unlocked, degrees -90/90
  INPUT_KEY,       // inut key to user curr passkey array
  CORRECT_KEY,     // after passkey check, if match
  ERROR,           // error is also used as the incorrect passkey, after passkey check, if mismatch
  PASSKEY_CHANGE,  // after passkey check, if master key,
  PASSKEY_CHECK,   // checks between two passkeys
};

struct Servo {
  int position = 0;       // starts at the middle
  bool attached = false;  // starts initally not attached
  int currentPin = -1;    // since not attached pin is set to something not in digital pin range
};

struct user_details {
  char curr_passkey[5]{};       // the current passkey being inputting using the keys
  char saved_passkey[5]{};      // the saved passkey, saved onto the chip during sleep and will be compared to for opening
  Lock_State locker_status;     // initial status is LOCKED, change if future code shows this
  Lock_State prev_state;        // the previous state the locker was in before the change
  Lock_State prev_sleep_state;  // The previous state we were in before entering sleep

  int u_salt = 0;      // salt saved into the user_details for comparison
  int u_checksum = 0;  // checksum saved into the user_details for comparison, the xor of the key and salt

  int hash_value = 0;  // set initally to zero but its the salt and the key saved this is what its compared to
};



// Keypad pins
const int colPins[3] = { 3, 4, 5 };  // columns connected to digital pins
const int adc_Pin = A0;              // rows through resistor ladder


// Row thresholds
const int Row1_adc = 922;
const int Row2_adc = 715;
const int Row3_adc = 512;
const int Row4_adc = 320;

char keyPad[4][3] = {
  { '1', '2', '3' },
  { '4', '5', '6' },
  { '7', '8', '9' },
  { '*', '0', '#' }
};

bool spam = true;
bool block_spam = true;

Servo theServo;          // instance of servo struct
user_details curr_user;  // initalizes the user object

void servoAttach(Servo &servo, int pin);     // attaches the servo to its pin for functionality
void servoDetach(Servo &servo);              // removes the servo from its pin and stops its functionality
void servoWrite(Servo &servo, int degrees);  // moves the servo based on the param degrees, using PWM signals
void servoRead(Servo &servo);                // returns the position of the servo based on the previous servoWrite call

Lock_State locker_state_machine(Lock_State &lock_status, Servo &servo, user_details &user);
char *inputPasskey(char &key_press);  // an attempt to making it return a character array, as in the completed password
bool check_passkey_helper(user_details &user);

char getKey();

void overcurrent_detection();  // for overcurrent detection

/*
  For sleep implementation
*/

void sleep_mode();
void before_sleep_mode();
void after_sleep_mode();

void wdt_reset();
void restart_wdt();
ISR(WDT_vect);
ISR(INT0_vect);

void master_key_LEDs();

int hash_algorithm(user_details &user, uint8_t *data, size_t length);  // hashing algorithm with salting param
uint8_t *appendSalttoKey(user_details &user);

int *char_to_int(user_details &user);

// put your setup code here, to run once:
// be sure to clear serial monitor
void setup() {
  //Serial.begin(19200); 
   Serial.begin(9600);  // sets up initial comms with arduino, think of it as baud in MobaXTerm

  EICRA |= (1 << ISC01);
  EICRA &= ~(1 << ISC00);  // falling edge?

  EIMSK |= (1 << INT0);  // enables int 0 interrupt

  pinMode(2, INPUT_PULLUP);

  // below are Timer1 interrupt commands
  TCCR1A = 0;
  TCCR1B = 0;
  TCCR1B |= B00000010;  // only have CS11 as 1 for a prescaler of 8
  ICR1 = 39999;         // starts at 0 and counting
  OCR1B = 3000;         // after doing the math for this at 0 degrees at 1.5 ms its 3000

  TCCR1A |= (1 << WGM11);
  TCCR1B |= (1 << WGM13) | (1 << WGM12);
  TCCR1A |= (1 << COM1B1);  // COM1BX so that it changes for pin 10 not 9, made this mistake before
  TCCR1A &= ~(1 << COM1B0);

  // attachInterrupt(digitalPinToInterrupt(interruptPin), interrupt_woke_func, RISING);

  servoAttach(theServo, 10);

  int adc_a1_pin = analogRead(A1);
  Serial.print("A1 Volts: ");
  Serial.println(adc_a1_pin);

  //column pins sets as outputs and start LOW
  for (int i = 0; i < 3; i++) {
    pinMode(colPins[i], OUTPUT);
    digitalWrite(colPins[i], LOW);
  }

  idle_start_time = millis();
  block_start_time = millis();

  /* LEDS */
  pinMode(GREENLED, OUTPUT);
  pinMode(REDLED, OUTPUT);

  digitalWrite(GREENLED, LOW);
  digitalWrite(REDLED, LOW);

  // sets the current state to locked (after sleep implemented this will be SLEEP_MODE), look at enums for numbers
  Serial.print("Servo is connected, and the locker status is SLEEP MODE");
  // Serial.println(curr_user.locker_status);  // tells the initial locker status as a debugging tool

  downloadKeyEEPROM(curr_user);
  Serial.println(", POWERED ON AND EEPROM SAVED");

  /* VALUES SAVED TO EEPROM WILL TRANSFER TO BE SAVED IN USER VARIABLES, SALT AND HASH_VALUE */
  Serial.print("SALT: ");
  Serial.println(curr_user.u_salt);

  Serial.print("HASH VALUE: ");
  Serial.println(curr_user.hash_value);

  /* IF THERE ARE VALUES SAVED IN EEPROM FOR THE SAVED OR CURRENT PASSKEY THEY WILL PRINT HERE*/
  //Serial.println("PASSKEY SAVED: ");
  //Serial.println(curr_user.saved_passkey);

  //Serial.println("PASSKEY CURRENT: ");
  //Serial.println(curr_user.curr_passkey);

  curr_user.locker_status = SLEEP_MODE;
  curr_user.prev_sleep_state = LOCKED;
}

void loop() {
  /* Segment relates to SLEEP implemetation */

  if (interrupt_woke) {
    Serial.println("DEBUG PRINT: INT0 interrupt triggered!");  // this will flash initially when the code is ran
    Serial.println(digitalRead(2));

    interrupt_woke = false;
    timer_flag = true;
  }

  if ((millis() - idle_start_time) >= idle_countdown_duration) {
    if (timer_flag) {
      // Serial.println("DEBUG PRINT: MILLIS HAS REACHED ITS DESIGNATED TIME!");
      timer_flag = false;
    }

    Serial.println("Transitioning to Sleep Mode!");
    curr_user.locker_status = SLEEP_MODE;
  }

  ///////////////////////////////////////////////////////////////////

  /* Segment relates to overcurrent detection and servo blockage */

  overcurrent_detection();
  if (BLOCKED && curr_user.locker_status == UNLOCKED) {
    curr_user.locker_status = LOCKED;
  }

  ///////////////////////////////////////////////////////////////////

  locker_state_machine(curr_user.locker_status, theServo, curr_user);
}

/* SLEEP IMPLEMENTATION BLOCK */

void before_sleep_mode() {
  servoDetach(theServo);
  ADCSRA &= ~(1 << ADEN);
}

void sleep_mode() {
  // the bits should be 010
  SMCR &= ~(1 << SM0);
  SMCR |= (1 << SM1);
  SMCR &= ~(1 << SM2);

  // enter sleep mode through SE
  SMCR |= (1 << SE);  // sets sleep enable

  __asm__ __volatile__("sleep");
  SMCR &= ~(1 << SE);
}

void after_sleep_mode() {
  ADCSRA |= (1 << ADEN);
  servoAttach(theServo, 10);
}

ISR(INT0_vect) {
  raw_interrupt = true;
  if (digitalRead(2) == 0) {
    idle_start_time = millis();
    interrupt_woke = true;
  }
}


/* SLEEP IMPLEMENTATION BLOCK END */

/*
  Using millis over delay since millis doesn't block code
*/
int average_ADC() {
  int sum = 0;
  const int data_points = 10;
  const unsigned long space = 3;
  for (int i = 0; i < data_points; i++) {
    unsigned long start = millis();  // declares
    sum += analogRead(A1);

    while (millis() - start < space) {
      // this portion is here to wait 3 millis
    }
  }

  return sum / data_points;
}

void overcurrent_detection() {
  int avg = average_ADC();

  const int resistance = 1;
  double voltage = avg * (5 / 1023.0);
  double current = voltage / resistance;

  // Serial.print("Avg current: ");
  // Serial.println(current);

  if ((millis() - block_start_time) >= block_duration) {
    if (current < 4.90) {
      BLOCKED = true;
      if (BLOCKED) {
        Serial.println("DEBUG PRINT: BLOCKED AFTER TIMER IS DONE");
      }
    } else {
      BLOCKED = false;
      if (!BLOCKED) {
        Serial.println("DEBUG PRINT: SERVO IS NOT BLOCKED");
      }
    }

    block_start_time = millis();
  }
}

/*
  From datasheet
  add = address
  data = data fr
*/
void writeEEPROM(uint16_t add, uint8_t data) {
  while (EECR & (1 << EEPE))
    ;  // waits for the last write to be done

  EEAR = add;   // sets up the address
  EEDR = data;  // sets up data ATmega328p

  EECR |= (1 << EEMPE); /* start EEPROM write */
  EECR |= (1 << EEPE);  // start write
}

unsigned char EEPROM_read(unsigned int add) {
  while (EECR & (1 << EEPE))
    ;

  EEAR = add;
  EECR |= (1 << EERE);

  return EEDR;
}

void saveEEPROM(user_details &user) {
  // EEPROM can only store one byte in each address
  // this is why whne flashed hash wouldn't be greater than 256
  uint16_t hash_value = user.hash_value;
  uint16_t salt = user.u_salt;

  writeEEPROM(0, (hash_value >> 8) & 0xFF);  // upper byte, 15-8, shift the hash down by 2 bytes, masking
  writeEEPROM(1, hash_value & 0xFF);         // lower byte, 7-0, get the lesser half of the hash

  writeEEPROM(2, (salt >> 8) & 0xFF);  // upper byte, 15-8
  writeEEPROM(3, salt & 0xFF);         // lower byte, 7-0
}

void downloadKeyEEPROM(user_details &user) {
  uint8_t high = EEPROM_read(0);
  uint8_t low = EEPROM_read(1);

  user.hash_value = ((uint16_t)high << 8) | low;

  uint8_t high_salt = EEPROM_read(2);
  uint8_t low_salt = EEPROM_read(3);

  user.u_salt = ((uint16_t)high_salt << 8) | low_salt;
}

int hash_algorithm(user_details &user, uint8_t *data, size_t length) {
  int checksum = user.u_salt;
  /* 
      Thought process for allowing values like 2222 to be the key.
      This portion adds rotation to the code
      previously the code would allow any code through when the key 
      was somehting like 1111 or 2222. that is due to XOR canceling out duplicates 
      and when all are duplicates everything is canceled.
  */

  for (size_t i = 0; i < length; i++) {
    checksum ^= data[i];
  }

  return checksum;
}

// call delete, dynamic array
int *char_to_int(user_details &user) {
  int *int_array = new int[5];
  for (auto i = 0; i < 5; i++) {
    int digit = user.curr_passkey[i] - '0';
    int_array[i] = digit;
  }

  return int_array;
}

char getKey() {
  for (int i = 0; i < 3; i++) {
    digitalWrite(colPins[i], HIGH);  // activate this column
    delayMicroseconds(50);

    int adc_Val = analogRead(adc_Pin);
    //Serial.print("Col ");
    //Serial.print(i);
    //Serial.print("  ADC: ");
    //Serial.println(adc_Val);

    // expected row values
    int row1 = 1017;
    int row2 = 1005;
    int row3 = 700;
    int row4 = 682;

    // differences from each expected row
    int diff1 = abs(adc_Val - row1);
    int diff2 = abs(adc_Val - row2);
    int diff3 = abs(adc_Val - row3);
    int diff4 = abs(adc_Val - row4);

    int row = -1;
    int minDiff = diff1;
    row = 0;

    if (diff2 < minDiff) {
      minDiff = diff2;
      row = 1;
    }
    if (diff3 < minDiff) {
      minDiff = diff3;
      row = 2;
    }
    if (diff4 < minDiff) {
      minDiff = diff4;
      row = 3;
    }

    // add tolerance so noise doesn’t trigger
    if (minDiff < 20) {
      digitalWrite(colPins[i], LOW);  // turn off column before return
      return keyPad[row][i];
    }

    digitalWrite(colPins[i], LOW);  // turn off column before testing next
  }
  return '\0';  // no key pressed
}

void servoAttach(Servo &servo, int pin) {
  if (servo.attached) {
    Serial.println("Already attached to pin.");
    return;
  }

  pinMode(pin, OUTPUT);  // pin used for servo
  servo.currentPin = pin;
  servo.attached = true;
  Serial.print("Attached to pin ");  // println makes sense here if we're trying to test detach to attach, back and forth, easy to see clearly
  Serial.println(servo.currentPin);
}

void servoDetach(Servo &servo) {
  pinMode(servo.currentPin, INPUT);
  servo.attached = false;

  delay(2000);  // delays it by 2 seconds
  Serial.println("Detached from the pin");
  // Implement something to stop future timer1 interrupts
}

void servoWrite(Servo &servo, int degrees) {
  if (!servo.attached) {
    Serial.print("Not attached to a pin");
    return;
  }

  servo_moving = true;
  switch (degrees) {
    case 0:  // 1.5 ms
      OCR1B = 3000;
      break;
    case 90:  // 2 ms
      OCR1B = 5000;
      break;
    case -90:  // 1 ms
      OCR1B = 1000;
      break;
    default:
      Serial.print("Pick 0, 90, or -90 degrees only please.");
      Serial.println("");
      return;  // return so that it doesn't send the serial print (degrees has been set to). Doing return instead of break
  }

  servo.position = degrees;
  // Serial.print("Degrees has been set to ");
  Serial.print(servo.position);
  Serial.println("");
}

void servoRead(Servo &servo) {  // test this later after write() has been implemented
  if (!servo.attached) {
    Serial.println("Servo is not attached.");
  }

  Serial.print("Degrees: ");
  Serial.print(servo.position);
  Serial.println("");
}

// works the same as two of these, the for loop just makes it smaller
void master_key_LEDs() {
  for (int i = 0; i < 2; i++) {
    digitalWrite(GREENLED, HIGH);
    delay(100);
    digitalWrite(GREENLED, LOW);
    delay(100);
  }
}

void ADCtoPasskey(user_details &user) {
  if (user.locker_status != INPUT_KEY && user.locker_status != PASSKEY_CHANGE) {
    return;
  }

  int key_index = 0;  // what number we are on in the password
  char single_key;    // one individual key

  for (int i = 0; i < 5; i++) {
    user.curr_passkey[i] = '\0';
  }
  while (user.locker_status == INPUT_KEY || user.locker_status == PASSKEY_CHANGE) {
    /*
        use get key then put each value into a array one by one
        if * delete the previous value
        if # change status to check passkey
      */

    single_key = getKey();  // gets the single key press
    if (single_key != '\0') {
      if (single_key == '*')  // deletes the key
      {
        if (key_index > 0)  // checks if key_index isn't 0
        {
          key_index--;
          user.curr_passkey[key_index] = '\0';
          Serial.println("Previous key deleted.");

          Serial.print("Current Passkey: ");
          for (int k = 0; k < 5; k++) {
            if (user.curr_passkey[k] == '\0') {
              Serial.print('_');
            } else {
              Serial.print(user.curr_passkey[k]);
            }
          }
          Serial.println();
        }
      } else if (single_key == '#' && key_index != 0) {  // check that index isn't equal to 0 for master key
        if (user.locker_status == PASSKEY_CHANGE) {

          srand(millis());       // generate seed using millis()
          user.u_salt = rand();  // set salt equal to 16-bit rand

          char *key = user.curr_passkey;
          user.hash_value = hash_algorithm(user, key, 5);  // perform the hash_algorithm to get the hash_value

          /* Clear the passkeys after getting the hash_value, 
              this is to prove that no vlaues are saved, and 
              security is done through hashing with salt */

          for (int i = 0; i < 5; i++) {
            user.saved_passkey[i] = '\0';  // clear the saved passkey
          }

          for (int i = 0; i < 5; i++) {
            user.curr_passkey[i] = '\0';  // clear the current passkey
          }

          Serial.println("SALT: ");
          Serial.println(user.u_salt);  // Prints Salt

          Serial.println("Hash_value: ");
          Serial.println(user.hash_value);  // Prints Hash

          Serial.println("Passkey CHANGE Submitted.");
          saveEEPROM(user);

          /* EEPROM SAVED AND GREENLED TURNED ON */
          digitalWrite(GREENLED, HIGH);
          digitalWrite(REDLED, LOW);

          delay(500);
          digitalWrite(GREENLED, LOW);

          delay(1000);

          spam = true;
          user.locker_status = LOCKED;
        } else {
          Serial.println("Passkey Submitted.");
          user.locker_status = PASSKEY_CHECK;
        }

        break;
      } else if (key_index < 5 && user.curr_passkey[4] == '\0') {
        user.curr_passkey[key_index] = single_key;
        Serial.print(single_key);
        Serial.println(" key pressed.");
        key_index++;

        Serial.print("Current Passkey: ");
        for (int k = 0; k < 5; k++) {
          if (user.curr_passkey[k] == '\0') {
            Serial.print('_');
          } else {
            Serial.print(user.curr_passkey[k]);
          }
        }
        Serial.println();
      }

      delay(500);
    }
  }
}

/*  
    this function makes the locker_state_machine function shorter as a helper 
    with this it checks if the user passkey arrays (curr and saved) are equal to each other 
*/
bool check_passkey_helper(user_details &user) {
  // compares the user passkey to its salt
  char *key = user.curr_passkey;
  int curr_hashValue = hash_algorithm(user, key, 5);
  Serial.println(curr_hashValue);


  if (user.hash_value == curr_hashValue) {
    return true;
  }

  return false;
}

bool check_masterkey_helper(user_details &user) {
  for (int i = 0; i < 5; i++) {
    if (user.curr_passkey[i] != master_code[i]) {
      return false;
    }
  }

  return true;
}

/*
  Before making more of the state machine, to make testing easier get the inputs from the key pad onto the Arduino.
  So coding isn't as tedious here's the Lock_state enum
  enum Lock_State {
  POWER_ON,
  SLEEP_MODE,
  LOCKED,
  UNLOCKED,
  INPUT_KEY,
  CORRECT_KEY,
  ERROR,  // error is also used as the incorrect passkey
  PASSKEY_CHANGE,
  PASSKEY_CHECK,
*/
Lock_State locker_state_machine(Lock_State &lock_status, Servo &servo, user_details &user) {
  switch (lock_status) {
    case SLEEP_MODE:
      if (!sleeping) {
        before_sleep_mode();
        sleeping = true;
        interrupt_woke = false;
        Serial.println("Currently in sleep mode, after press key..");
      }

      sleep_mode();

      if (interrupt_woke) {
        sleeping = false;
        interrupt_woke = false;

        after_sleep_mode();
        Serial.println("Woke up!");

        user.locker_status = user.prev_sleep_state;
      }

      break;
    case LOCKED:
      {
        user.prev_sleep_state = LOCKED;

        /*
        if (BLOCKED) {
          lock_status = UNLOCKED;
        } else {
          servoWrite(servo, 0);
        }
        */

        servoWrite(servo, 0);

        if (spam) {
          Serial.println("Locker is LOCKED, now waiting for the 'user' input.");
          spam = false;
        }
        char key = getKey();
        if (key != '\0') {
          user.prev_state = lock_status;
          lock_status = INPUT_KEY;
        }

        break;
      }
    case INPUT_KEY:
      {
        user.prev_sleep_state = INPUT_KEY;

        /*
          Utilize the input passkey function.
        */
        Serial.println("Enter passkey...");
        ADCtoPasskey(user);
        break;
      }
    case PASSKEY_CHECK:
      {
        bool correct = check_passkey_helper(user);
        bool master_key = check_masterkey_helper(user);

        for (int i = 0; i < 5; i++) {
          user.curr_passkey[i] = '\0';
        }

        if (master_key) {
          master_key_LEDs();

          lock_status = PASSKEY_CHANGE;
          delay(500);
        } else if (correct)  // returns true if same
        {
          Serial.println("PASSKEY IS CORRECT (MATCH FOUND!)");

          digitalWrite(GREENLED, HIGH);
          digitalWrite(REDLED, LOW);

          delay(500);
          digitalWrite(GREENLED, LOW);

          user.prev_state = lock_status;
          lock_status = UNLOCKED;
        } else {

          auto val = hash_algorithm(user, user.curr_passkey, 5);
          Serial.println(val);
          Serial.println("PASSKEY IS INCORRECT (NO MATCH!)");

          digitalWrite(GREENLED, LOW);
          digitalWrite(REDLED, HIGH);

          delay(500);
          digitalWrite(REDLED, LOW);

          user.prev_state = lock_status;
          lock_status = ERROR;
        }
        break;
      }
    case PASSKEY_CHANGE:
      {
        user.prev_sleep_state = PASSKEY_CHANGE;

        Serial.println("PLEASE CREATE A NEW PASSKEY!");
        ADCtoPasskey(user);

        break;
      }
    case UNLOCKED:
      {
        user.prev_sleep_state = UNLOCKED;

        Serial.println("Locker UNLOCKED..");
        if (spam) {
          Serial.println("Locker UNLOCKED..");
          spam = false;
        }

        servoWrite(servo, 90);

        char key = getKey();
        if (key == '#') {
          Serial.println("Locker is LOCKING.");
          delay(400);
          servoWrite(servo, 0);

          spam = true;
          user.prev_state = lock_status;
          lock_status = LOCKED;
        }

        break;
      }

    case ERROR:
      {
        Serial.println("ERROR! Incorrect passkey. Transitioning to Locked, please try again.");
        spam = true;
        user.prev_state = lock_status;
        lock_status = LOCKED;

        break;
      }

    default:
      user.prev_state = lock_status;
      lock_status = LOCKED;
      break;
  }

  return lock_status;
}
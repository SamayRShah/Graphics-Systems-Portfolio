const int potPin = A0;
const int redPin = 3;
const int greenPin = 6;
const int bluePin = 5;

int potData = -1;
char buffer[20];
byte index = 0;

void setup() {
  // initialize serial communication
  Serial.begin(9600);

  // assign led pins
  pinMode(redPin, OUTPUT);
  pinMode(greenPin, OUTPUT);
  pinMode(bluePin, OUTPUT);
}

// data processing wll be done by Unity
// just bus raw values to serial port as soon as possible,
// and use raw values from serial port
void loop() {
  // read data
  potData = analogRead(potPin);

  // write data to serial port
  Serial.println(potData);

  // If there is data in the serial port, read it
  while (Serial.available() > 0) {
    // reads all available bytes
    char c = Serial.read();

    // process rgb value on end of line
    // --Doesn't update led until full RGB line is sent
    if (c == '\n') {
      buffer[index] = '\0';
      parseRGB(buffer);
      index = 0;
    }
    // Store character if buffer not full
    else if (index < sizeof(buffer) - 1) {
      buffer[index++] = c;
    }
  }
}

void parseRGB(char* data) {
  int r = 0, g = 0, b = 0;

  // splits incoming string into rgb values
  char* token = strtok(data, ",");
  if (token) r = atoi(token);
  token = strtok(NULL, ",");
  if (token) g = atoi(token);
  token = strtok(NULL, ",");
  if (token) b = atoi(token);

  // sets values as given from data stream
  // Only processing is keeping it in range from 0-255
  analogWrite(redPin, constrain(r, 0, 255));
  analogWrite(greenPin, constrain(g, 0, 255));
  analogWrite(bluePin, constrain(b, 0, 255));
}

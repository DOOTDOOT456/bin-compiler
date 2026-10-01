/*
  BlinkD1Mini — basic onboard-LED blink for the WeMos D1 Mini.

  The D1 Mini's onboard LED is on GPIO2 (labeled D4) and is active LOW:
  it turns ON when the pin is driven LOW.
*/

#define LED_BUILTIN_PIN 2

void setup() {
  pinMode(LED_BUILTIN_PIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN_PIN, LOW);  // LED on (active low)
  delay(1000);
  digitalWrite(LED_BUILTIN_PIN, HIGH); // LED off
  delay(1000);
}

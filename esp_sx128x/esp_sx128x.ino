/*
 * ESP32 + SX128x test application
 */

//-----------------------------------------------------------------------------
#include "config.h"
#include "global.h"
#include "cli.h"
#include "opt.h"
#include "print.h"
#include "sx128x_hw_arduino.h"
#include "sx128x_irq.h"
#include "sx128x.h"
#include "eeprom.h"
#include "tfs.h"
#include "wifi.h"
#include "mqtt.h"
//-----------------------------------------------------------------------------
#ifdef ARDUINO_USBCDC
#  include "usbcdc.h"
#endif
//-----------------------------------------------------------------------------
bool Mqtt_reconnect = false;
//-----------------------------------------------------------------------------
void ticker_callback() {

  if ((Ticks % TICKER_HZ) == 0 && Ticks >= TICKER_HZ) {
    Seconds++;
  
    // check MQTT connection every MQTT_CHECK_PERIOD seconds
    if (Seconds % MQTT_CHECK_PERIOD == 0) Mqtt_reconnect = true;
  }

  if (Mqtt_reconnect) {
    Mqtt_reconnect = false;
    if (wifi_connected() && !mqtt_connected()) {
      mrl_clear(&Mrl);
      mqtt_reconnect(Opt.mqtt_id, Opt.mqtt_user, Opt.mqtt_key);
      mrl_refresh(&Mrl);
    }
  }
  
  // TODO: reset WDT every N seconds
  //...

  Ticks++;
}
//-----------------------------------------------------------------------------
// MQTT callback
void mqtt_callback(char *topic, byte *payload, unsigned int length)
{
  mrl_clear(&Mrl);
  // FIXME: example code
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  for (int i = 0; i < length; i ++) {
    Serial.print((char) payload[i]);
  }
  Serial.println();
  mrl_refresh(&Mrl);
}
//-----------------------------------------------------------------------------
void setup() {
  // setup blink LED
  Led.begin(LED_PIN, LED_INVERT, LED_BLINK_ON, LED_BLINK_OFF);

  Led.on();

  // setup UART
#ifdef ARDUINO_USBCDC
#  if ARDUINO_USB_CDC_ON_BOOT
  // Serial0 -> UART, Serial -> USB-CDC
#    if defined(UART_RXPIN) && defined(UART_TXPIN)
  Serial0.begin(UART_BAUDRATE, SERIAL_8N1, UART_RXPIN, UART_TXPIN);
#    else
  Serial0.begin(UART_BAUDRATE);
#    endif
#  else
  // Serial -> UART, Serial1 -> USB-CDC
#    if defined(UART_RXPIN) && defined(UART_TXPIN)
  Serial.begin(UART_BAUDRATE, SERIAL_8N1, UART_RXPIN, UART_TXPIN);
#    else
  Serial.begin(UART_BAUDRATE);
#    endif
#  endif
#else
  // Serial -> UART, No USB-CDC
#    if defined(UART_RXPIN) && defined(UART_TXPIN)
  Serial.begin(UART_BAUDRATE, SERIAL_8N1, UART_RXPIN, UART_TXPIN);
#    else
  Serial.begin(UART_BAUDRATE);
#    endif
#endif

#ifdef ARDUINO_USBCDC
  // setup USB-CDC
  usbcdc_begin();
  delay(1000); // FIXME: magic
#endif
  
  // print hello message
  print_str("\r\n\r\n" HOST_NAME " started\r\n");
  printf("printf()\r\n");
  print_flush();

  // set EEPROM
  EEPROM_BEGIN(TFS_PAGE_SIZE * TFS_PAGE_NUM);

  // init TFS structure (initial set EEPROM space)
  tfs_init(&Tfs, TFS_PAGE_NUM, 0, TFS_PAGE_SIZE);

  // restore Opt from EEPROM
  opt_default(&Opt); // set to default all options
  opt_read_from_flash(&Opt, &Tfs);

  // init SPI and SPI pins
  print_str("SX128X_SPI_CLOCK=");  
  print_dint(SX128X_SPI_CLOCK / 100000);  
  print_str("MHz\r\n");  
  sx128x_hw_begin();
  
#if defined(SX128X_RXEN_PIN) && defined(SX128X_TXEN_PIN)
  // reset RXEN/TXEN by default
  setRXEN(0);
  setTXEN(0);
#endif

  // hardware reset SX128x
  delay(100); // FIXME: magic
  sx128x_hw_reset(10, 10, NULL);
  
  // init sx128x_t object (Radio) by default pars
  print_str("init SX128x\r\n");
  int8_t retv = sx128x_init(&Radio,
                            sx128x_hw_busy_wait, sx128x_hw_exchange,
                            &Opt.radio, NULL);
  print_ival("sx128x_init() return ", retv);

  // setup onboard button
#ifdef BUTTON_PIN
  //pinMode(BUTTON_PIN, INPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP); // FIXME
  Button = digitalRead(BUTTON_PIN);
#endif

  // connect to Wi-Fi Access Point
  if (Opt.wifi_ssid[0] != '\0')
  {
    wifi_connect(Opt.wifi_ssid, Opt.wifi_passwd, true); // auto_reconnect=true
  
    // connect to MQTT broker
    if (wifi_connected() && Opt.mqtt_host[0] != '\0')
      mqtt_connect(Opt.mqtt_host, Opt.mqtt_port, Opt.mqtt_id,
                   Opt.mqtt_user, Opt.mqtt_key);
  }
  Mqtt.setCallback(mqtt_callback);

  // setup ticker
  Ticker.begin(ticker_callback, TICKER_MS, true, millis());
  Ticks = 0;

  // init FSM
  Fsm.begin(&Led,             // on board LED
            &Opt.fsm,         // FSM options
            Opt.data,         // RX/TX packet data
            &Opt.data_size,   // RX/TX packet data size (bytes)
            &Opt.radio.fixed, // 1-fixed packet size, 0-variable packet size 
            Opt.code,         // OOK code (like "100101")
            &Opt.code_size,   // OOK code size (chips) = strlen(code)
            &Radio,           // SX128x object
            setRXEN,          // set RXEN or NULL
            setTXEN);         // set TXEN or NULL
  
  Seconds = 0;
  print_uval("autostart=", Autostart = Opt.autostart);
  print_uval("verbose=", Opt.verbose);

  if (Autostart) {
    print_str("mode=");
    print_uint(Opt.fsm.mode);

    if (Opt.verbose)
    {
      print_str(" (");
      print_str(afsm_mode_string[Opt.fsm.mode]);
      print_chr(')');
    }
    print_eol();

    int8_t retv = sx128x_sleep(&Radio, SX128X_SLEEP_OFF_RETENTION);
    print_ival("sx128x_sleep() return ", retv);

  } else {
#if defined(SX128X_RXEN_PIN) && defined(SX128X_TXEN_PIN)
    // set RXEN and TXEN from FLASH
    setRXEN(Opt.rxen);
    setTXEN(Opt.txen);
    print_ival("set RXEN=", RXEN);
    print_ival("set TXEN=", TXEN);
#endif
  }

  // init CLI (MicroRL)
  cli_init();
  print_flush();

  Led.off();
}
//-----------------------------------------------------------------------------
void loop() {
  unsigned long ms = millis();
  unsigned long t = TIME_FUNC();
  Led.yield(ms);

  Ticker.yield(ms);
  Fsm.yield(t);

#ifndef USE_DIO1_INTERRUPT
  // periodic check IRQ (DIO1)
  sx128x_hw_check_dio1();
#endif // !USE_DIO1_INTERRUPT
 
  // check SX128x IRQ (DIO1) flag
  sx128x_irq();

  // check user CLI commands
  cli_loop();
  
#ifdef BUTTON_PIN
  // check onboard button
  uint8_t btn = digitalRead(BUTTON_PIN);
  if (btn == 0 && Button == 1) {
    print_str("\r\nButton BOOT pressed (ms=");
    print_uint(ms);
    print_str(")\r\n");
    mrl_refresh(&Mrl);
  }
  Button = btn;
#endif
  
  // check autostart
  if (Autostart && Seconds >= Opt.delay) {
    Autostart = 0;
    print_str("\r\nautostart: ms=");
    print_uint(ms);
    print_str(" seconds=");
    print_uint(Seconds);
    print_eol();
    Fsm.start();
    mrl_refresh(&Mrl);
  }

  // check Wi-Fi conection
  //if (Opt.wifi_ssid[0] != '\0')
  //  if (!wifi_connected())
  //    wifi_reconnect();

  // MQTT PubSubClient loop
  Mqtt.loop();
}
//-----------------------------------------------------------------------------
#if 0
void loop() {
  static int cnt = 0;

  Serial.printf("%i\r\n", cnt);
  cnt++;

  digitalWrite(LED_PIN, HIGH);
  delay(200);

  digitalWrite(LED_PIN, LOW);
  delay(800);
}
#endif
//-----------------------------------------------------------------------------
/*** end of "esp32_sx128x.ino" file ***/


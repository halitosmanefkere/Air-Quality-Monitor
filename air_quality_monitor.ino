#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "Adafruit_CCS811.h"
#include <HTTPClient.h>

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1

// RGB LED pins
const int redPin = 25;
const int greenPin = 26;
const int bluePin = 27;

Adafruit_CCS811 ccs;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// Captive portal and network
DNSServer dnsServer;
const byte DNS_PORT = 53;
const char *ssid = "AirQualityMonitor";
WebServer server(80);
IPAddress apIP(192, 168, 4, 32);  // Fixed IP for the ESP32

// Other configurations
float previous_eCO2 = 0;
float previous_TVOC = 0;
const int ledPin = 2; // Pin for additional LED, if needed

// Initializes the RGB LED pins
void setupRGB() {
    pinMode(redPin, OUTPUT);
    pinMode(greenPin, OUTPUT);
    pinMode(bluePin, OUTPUT);
}

// Function to turn off all RGB LEDs
void turnOffRGB() {
    digitalWrite(redPin, LOW);
    digitalWrite(greenPin, LOW);
    digitalWrite(bluePin, LOW);
}

// Function to set RGB LED color based on air quality
void setRGBColor(String airQuality) {
    turnOffRGB(); // Ensure all LEDs are off before setting new color
    if (airQuality == "Sehr Gut") {
        digitalWrite(greenPin, HIGH);  // Green for Very Good
    } else if (airQuality == "Gut" || airQuality == "Maessig") {
        digitalWrite(redPin, HIGH);     // Yellow (Red + Green) for Good and Moderate
        digitalWrite(greenPin, HIGH);
    } else if (airQuality == "Schlecht" || airQuality == "Sehr Schlecht") {
        digitalWrite(redPin, HIGH);     // Red for Bad and Very Bad
    }
}

// Evaluates the air quality based on eCO2 and TVOC levels
String evaluateAirQuality(float eCO2, float TVOC) {
    if (eCO2 < 600 && TVOC < 50)
        return "Sehr Gut";
    else if (eCO2 < 1000 && TVOC < 150)
        return "Gut";
    else if (eCO2 < 1500 && TVOC < 250)
        return "Maessig";
    else if (eCO2 < 2000 && TVOC < 350)
        return "Schlecht";
    return "Sehr Schlecht";
}

// Determines the trend of the air quality
String getAirQualityTrend(float current_eCO2, float current_TVOC) {
    if (current_eCO2 < previous_eCO2 && current_TVOC < previous_TVOC)
        return "+";
    else if (current_eCO2 > previous_eCO2 && current_TVOC > previous_TVOC)
        return "-";
    return "o";
}

// Reads ESP32's internal temperature
extern "C" uint8_t temprature_sens_read();
float readESP32Temperature() {
    return (temprature_sens_read() - 32) * 5.0 / 9.0;
}

// Handles root page with an enhanced UI and AJAX
void handleRoot() {
    String html = "<!DOCTYPE html><html lang='de'><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    html += "<title>Air Quality Monitor</title>";

    // Add CSS for better styling
    html += "<style>";
    html += "body { font-family: Arial, sans-serif; background-color: #f4f4f9; color: #333; margin: 0; padding: 20px; text-align: center; }";
    html += "h1 { color: #444; }";
    html += ".container { max-width: 600px; margin: 0 auto; padding: 20px; background-color: #fff; border-radius: 8px; box-shadow: 0px 0px 12px rgba(0,0,0,0.1); }";
    html += "p { font-size: 1.2rem; margin: 10px 0; }";
    html += ".good { color: green; } .moderate { color: orange; } .poor { color: red; }";
    html += "</style>";

    // Add JavaScript for AJAX to update values dynamically
    html += "<script>";
    html += "function fetchData() {";
    html += "  fetch('/data')";  // Fetch data from the /data endpoint
    html += "    .then(response => response.json())";
    html += "    .then(data => {";
    html += "      document.getElementById('eCO2').innerText = data.eCO2 + ' ppm';";
    html += "      document.getElementById('TVOC').innerText = data.TVOC + ' ppb';";
    html += "      document.getElementById('temperature').innerText = data.temperature + ' °C';";
    html += "      document.getElementById('airQuality').innerText = data.airQuality;";
    html += "      if (data.airQuality === 'Sehr Gut') {";
    html += "        document.getElementById('airQuality').className = 'good';";
    html += "      } else if (data.airQuality === 'Maessig') {";
    html += "        document.getElementById('airQuality').className = 'moderate';";
    html += "      } else {";
    html += "        document.getElementById('airQuality').className = 'poor';";
    html += "      }";
    html += "    });";
    html += "}";
    html += "setInterval(fetchData, 5000);";  // Fetch new data every 5 seconds
    html += "</script>";

    html += "</head><body onload='fetchData()'>";
    html += "<h1>Air Quality Monitor</h1><div class='container'>";
    html += "<p><strong>eCO2:</strong> <span id='eCO2'></span></p>";
    html += "<p><strong>TVOC:</strong> <span id='TVOC'></span></p>";
    html += "<p><strong>Temperature:</strong> <span id='temperature'></span></p>";
    html += "<p><strong>Luftqualität:</strong> <span id='airQuality'></span></p>";
    html += "</div>";
    html += "<footer style='background-color: #f4f4f9; text-align: center; padding: 20px; color: #555; font-family: Arial, sans-serif;'>"
        "<p style='margin: 0;'>Made with &hearts; by Halit Osman Efkere</p>"
        "<p style='font-size: 0.8rem;'>All rights reserved © 2024</p>"
        "</footer>";
    html += "</div></body></html>";

    server.send(200, "text/html", html);
}

// Handles data in JSON format for AJAX calls
void handleData() {
    if (ccs.available()) {
        float eCO2 = ccs.geteCO2();
        float TVOC = ccs.getTVOC();
        float temp = ccs.calculateTemperature();
        String airQuality = evaluateAirQuality(eCO2, TVOC);
        String trendIcon = getAirQualityTrend(eCO2, TVOC);

        String jsonData = "{";
        jsonData += "\"eCO2\": " + String(eCO2) + ",";
        jsonData += "\"TVOC\": " + String(TVOC) + ",";
        jsonData += "\"temperature\": " + String(temp) + ",";
        jsonData += "\"airQuality\": \"" + airQuality + "\",";
        jsonData += "\"trend\": \"" + trendIcon + "\"";
        jsonData += "}";

        server.send(200, "application/json", jsonData);
    } else {
        server.send(500, "application/json", "{\"error\": \"Sensor error\"}");
    }
}

// Sends data to a remote server
void sendDataToServer() {
    if (WiFi.status() == WL_CONNECTED) {
        HTTPClient http;
        http.begin("http://yourserver.com/api"); // Replace with your server endpoint
        http.addHeader("Content-Type", "application/json");

        float eCO2 = ccs.geteCO2();
        float TVOC = ccs.getTVOC();
        float temp = ccs.calculateTemperature();

        String jsonPayload = "{\"eCO2\": " + String(eCO2) + ", \"TVOC\": " + String(TVOC) + ", \"temperature\": " + String(temp) + "}";
        int httpResponseCode = http.POST(jsonPayload);

        if (httpResponseCode > 0) {
            String response = http.getString();
            Serial.println(httpResponseCode);   // Print response code
            Serial.println(response);           // Print server response
        } else {
            Serial.println("Error in sending POST");
        }
        http.end();
    }
}

void setup() {
    Serial.begin(115200);
    
    // Initialize RGB LED pins
    setupRGB();

    // Initialize CCS811 and OLED display
    if (!ccs.begin(0x5A)) {
        Serial.println("CCS811 not found!");
        while (1);
    }
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println(F("SSD1306 allocation failed"));
        while (true);
    }

    display.clearDisplay();
    display.display();

    while (!ccs.available());
    float temp = ccs.calculateTemperature();
    ccs.setTempOffset(temp - 25.0);

    // Set up ESP32 as Access Point
    WiFi.softAP(ssid);
    WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
    Serial.println("Access Point Started");

    dnsServer.start(DNS_PORT, "*", apIP);  // Captive portal functionality

    // Start the web server
    server.on("/", handleRoot);
    server.on("/data", handleData);  // For AJAX
    server.begin();
}

void loop() {
    dnsServer.processNextRequest();  // Handle DNS requests for captive portal
    server.handleClient();           // Handle incoming client requests

    if (ccs.available()) {
        if (!ccs.readData()) {  // Ensure valid data is read from the sensor
            float eCO2 = ccs.geteCO2();
            float TVOC = ccs.getTVOC();
            float temp = ccs.calculateTemperature();

            // Update OLED display
            display.clearDisplay();
            display.setCursor(0, 0);
            display.setTextSize(1); // Use normal size
            display.setTextColor(SSD1306_WHITE);

            display.print("eCO2: ");
            display.print(eCO2);
            display.println(" ppm");
            display.setCursor(0, 16); // Move down for the next line
            display.print("TVOC: ");
            display.print(TVOC);
            display.println(" ppb");
            display.setCursor(0, 30); // Move down for the next line
            display.print("Temp: ");
            display.print(temp);
            display.println(" C");
            display.setCursor(0, 45); // Move down for the next line

            String airQuality = evaluateAirQuality(eCO2, TVOC);
            display.print("Luft: ");
            display.println(airQuality);

            // Set RGB LED color based on air quality
            setRGBColor(airQuality);

            String trendIcon = getAirQualityTrend(eCO2, TVOC);
            display.setCursor(110, 45);  // Position for trend icon
            display.println(trendIcon);

            previous_eCO2 = eCO2;
            previous_TVOC = TVOC;

            display.display();
        } else {
            Serial.println("Error reading CCS811 sensor!");
        }
    }

    // Periodically send data to server every 1.2 seconds
    sendDataToServer();
    delay(1200);
}

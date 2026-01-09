#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// Gateway Access Point Configuration
const char *ap_ssid = "ESP32-AP";        // AP name for nodes to connect
const char *ap_password = "12345678";     // AP password (min 8 chars)

// WiFi Station Configuration (connects to your network)
const char *sta_ssid = "iPhone";          // Your WiFi network name
const char *sta_password = "55566678";    // Your WiFi password

// Controller Configuration
const char *controller_ip = "172.20.10.3"; // Raspberry Pi IP where controller.py runs
const int controller_port = 5000;

// Gateway HTTP Server
WiFiServer server(80);

void setup()
{
    Serial.begin(115200);
    delay(1000);

    Serial.println("\n========================================");
    Serial.println("    ESP32 Gateway Starting...");
    Serial.println("========================================\n");

    // Set WiFi to AP + STA mode
    WiFi.mode(WIFI_AP_STA);

    // Start Access Point for nodes
    Serial.println("[Gateway] Starting Access Point...");
    WiFi.softAP(ap_ssid, ap_password);
    IPAddress apIP = WiFi.softAPIP();
    Serial.print("[Gateway] AP SSID: ");
    Serial.println(ap_ssid);
    Serial.print("[Gateway] AP IP Address: ");
    Serial.println(apIP);
    Serial.print("[Gateway] AP MAC Address: ");
    Serial.println(WiFi.softAPmacAddress());

    // Connect to WiFi Station (to reach controller)
    Serial.println("\n[Gateway] Connecting to WiFi (STA mode)...");
    Serial.print("[Gateway] Network: ");
    Serial.println(sta_ssid);
    WiFi.begin(sta_ssid, sta_password);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 30)
    {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("\n[Gateway] ✅ Connected to WiFi!");
        Serial.print("[Gateway] STA IP Address: ");
        Serial.println(WiFi.localIP());
        Serial.print("[Gateway] STA MAC Address: ");
        Serial.println(WiFi.macAddress());
        Serial.print("[Gateway] Controller IP: ");
        Serial.print(controller_ip);
        Serial.print(":");
        Serial.println(controller_port);
    }
    else
    {
        Serial.println("\n[Gateway] ❌ Failed to connect to WiFi");
        Serial.println("[Gateway] AP is still active for local nodes");
    }

    // Start HTTP server for nodes
    server.begin();
    Serial.println("\n[Gateway] HTTP Server started on port 80");
    Serial.println("[Gateway] Ready to receive node connections");
    Serial.println("========================================\n");
}

void loop()
{
    WiFiClient client = server.available();
    if (client)
    {
        Serial.println("\n--- [Gateway] New Node Connection ---");
        Serial.print("[Gateway] Node IP: ");
        Serial.println(client.remoteIP());

        // Read HTTP request
        String request = "";
        unsigned long timeout = millis() + 5000; // 5 second timeout

        while (client.connected() && millis() < timeout)
        {
            if (client.available())
            {
                String line = client.readStringUntil('\r');
                request += line;
                if (line.length() == 1 && line[0] == '\n')
                {
                    break;
                }
            }
        }

        if (request.length() > 0)
        {
            Serial.print("[Gateway] Request: ");
            Serial.println(request.substring(0, min(200, (int)request.length())));

            // Parse request to determine endpoint
            String endpoint = "";
            String jsonPayload = "";

            if (request.indexOf("POST /get_token") >= 0)
            {
                endpoint = "/get_token";
                Serial.println("[Gateway] 📝 Node requesting TOKEN");
            }
            else if (request.indexOf("POST /data") >= 0)
            {
                endpoint = "/data";
                Serial.println("[Gateway] 📊 Node sending DATA");
            }
            else
            {
                Serial.println("[Gateway] ⚠️ Unknown request type");
            }

            // Extract JSON payload from request body
            int jsonStart = request.indexOf('{');
            int jsonEnd = request.lastIndexOf('}') + 1;
            if (jsonStart >= 0 && jsonEnd > jsonStart)
            {
                jsonPayload = request.substring(jsonStart, jsonEnd);
                Serial.print("[Gateway] Payload: ");
                Serial.println(jsonPayload);
            }

            // Forward to controller if WiFi is connected
            if (WiFi.status() == WL_CONNECTED && endpoint.length() > 0 && jsonPayload.length() > 0)
            {
                String controllerUrl = "http://" + String(controller_ip) + ":" + String(controller_port) + endpoint;
                Serial.print("[Gateway] Forwarding to: ");
                Serial.println(controllerUrl);

                HTTPClient http;
                http.begin(controllerUrl);
                http.addHeader("Content-Type", "application/json");
                http.setTimeout(5000);

                int httpCode = http.POST(jsonPayload);
                String response = http.getString();

                Serial.print("[Gateway] Controller response code: ");
                Serial.println(httpCode);
                Serial.print("[Gateway] Controller response: ");
                Serial.println(response);

                // Send response back to node
                client.println("HTTP/1.1 200 OK");
                client.println("Content-Type: application/json");
                client.println("Connection: close");
                client.println();
                client.println(response);

                http.end();
            }
            else
            {
                Serial.println("[Gateway] ❌ Cannot forward - WiFi not connected or invalid request");
                client.println("HTTP/1.1 503 Service Unavailable");
                client.println("Content-Type: application/json");
                client.println("Connection: close");
                client.println();
                client.println("{\"error\":\"Gateway not connected to controller\"}");
            }
        }
        else
        {
            Serial.println("[Gateway] ⚠️ Empty or timeout request");
            client.println("HTTP/1.1 400 Bad Request");
            client.println("Connection: close");
            client.println();
        }

        delay(10);
        client.stop();
        Serial.println("[Gateway] Connection closed");
    }

    delay(100); // Small delay to prevent watchdog issues
}

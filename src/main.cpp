#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_BME280.h>
#include <time.h>

// WiFi credentials
const char* ssid = "UPC6628674";
const char* password = "Ar6jxnrurxhe";

// NTP Configuration
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 3600;     // GMT+1 for Central Europe (adjust for your timezone)
const int daylightOffset_sec = 3600; // Daylight saving time offset

// Create BME280 object
Adafruit_BME280 bme;

// Create web server on port 80
WebServer server(80);

// Sensor readings
float temperature = 0.0;
float pressure = 0.0;
float humidity = 0.0;

// Data logging system
struct SensorData {
    unsigned long timestamp;
    float temperature;
    float pressure;
    float humidity;
};

const int MAX_DATA_POINTS = 720; // Store 720 readings (24 hours at 2-minute intervals)
SensorData dataBuffer[MAX_DATA_POINTS];
int dataIndex = 0;
int dataCount = 0;
unsigned long lastDataLog = 0;
unsigned long firstDataLog = 0; // Track first data point timestamp for runtime calculation
const unsigned long DATA_LOG_INTERVAL = 120000/18; // Log every 2 minutes

// Server-Sent Events
struct SSEClient {
    WiFiClient client;
    unsigned long lastPing;
    bool active;
};
const int MAX_SSE_CLIENTS = 5;
SSEClient sseClients[MAX_SSE_CLIENTS];
int sseClientCount = 0;

// HTML webpage with charts
const char* htmlPage = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>BME280 Weather Station</title>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
    <style>
        body { 
            font-family: Arial, sans-serif; 
            margin: 0; 
            padding: 10px; 
            background-color: #f0f0f0; 
        }
        .container { 
            max-width: 1200px; 
            margin: 0 auto; 
            padding: 0 10px;
        }
        .dashboard { 
            display: grid; 
            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr)); 
            gap: 15px; 
            margin-bottom: 20px; 
        }
        .card { 
            background-color: white; 
            padding: 15px; 
            border-radius: 10px; 
            box-shadow: 0 2px 10px rgba(0,0,0,0.1); 
        }
        h1 { 
            color: #333; 
            text-align: center; 
            margin-bottom: 20px; 
            font-size: 2em;
        }
        .current-time {
            text-align: center;
            color: #666;
            margin-bottom: 20px;
            font-size: 16px;
            background: white;
            padding: 10px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        
        /* Mobile responsiveness */
        @media (max-width: 768px) {
            body { padding: 5px; }
            .container { padding: 0 5px; }
            h1 { font-size: 1.8em; margin-bottom: 15px; }
            .dashboard { 
                grid-template-columns: 1fr; 
                gap: 10px; 
                margin-bottom: 15px; 
            }
            .card { padding: 12px; }
            .reading { padding: 12px; margin: 8px 0; }
            .value { font-size: 20px; }
            .chart-container { height: 250px; }
            .nav-buttons { margin: 15px 0; }
            .btn { 
                padding: 8px 15px; 
                margin: 2px; 
                font-size: 13px; 
                display: inline-block;
            }
        }
        
        @media (max-width: 480px) {
            h1 { font-size: 1.5em; }
            .stats { grid-template-columns: repeat(2, 1fr); gap: 10px; }
            .value { font-size: 18px; }
            .chart-container { height: 200px; }
            .btn { display: block; margin: 5px auto; width: 80%; }
        }
        .reading { 
            background-color: #f9f9f9; 
            padding: 15px; 
            margin: 10px 0; 
            border-radius: 8px; 
            border-left: 5px solid #007bff; 
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .reading.temperature { border-left-color: #dc3545; }
        .reading.pressure { border-left-color: #28a745; }
        .reading.altitude { border-left-color: #ffc107; }
        .reading.humidity { border-left-color: #17a2b8; }
        .value { 
            font-size: 24px; 
            font-weight: bold; 
            color: #007bff; 
        }
        .unit { 
            font-size: 14px; 
            color: #666; 
        }
        .label { 
            font-size: 16px; 
            color: #333; 
            margin-bottom: 5px; 
            font-weight: bold;
        }
        .chart-container { 
            position: relative; 
            height: 300px; 
            margin-top: 20px; 
        }
        .info { 
            text-align: center; 
            color: #666; 
            margin-top: 20px; 
            font-size: 13px; 
            line-height: 1.4;
            background: white;
            padding: 15px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        .info div {
            margin: 3px 0;
        }
        .nav-buttons {
            text-align: center;
            margin: 20px 0;
        }
        .btn {
            background: #007bff;
            color: white;
            border: none;
            padding: 10px 20px;
            margin: 0 5px;
            border-radius: 5px;
            cursor: pointer;
            font-size: 14px;
        }
        .btn:hover { background: #0056b3; }
        .btn.active { background: #28a745; }
        .stats {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin: 20px 0;
        }
        .stat-item {
            background: #f8f9fa;
            padding: 15px;
            border-radius: 8px;
            text-align: center;
        }
        .stat-value {
            font-size: 20px;
            font-weight: bold;
            color: #007bff;
        }
        .stat-label {
            font-size: 12px;
            color: #666;
            margin-top: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>BME280 Weather Station</h1>
        
        <div class="current-time" id="current-time">
            Loading current time...
        </div>
        
        <div class="dashboard">
            <div class="card">
                <div class="reading temperature">
                    <div class="label">Temperature</div>
                    <span class="value" id="temp">TEMP_VALUE</span>
                    <span class="unit">&deg;C</span>
                </div>
                
                <div class="reading pressure">
                    <div class="label">Pressure</div>
                    <span class="value" id="pressure">PRESSURE_VALUE</span>
                    <span class="unit">hPa</span>
                </div>
                
                <div class="reading altitude">
                    <div class="label">Altitude</div>
                    <span class="value" id="altitude">ALTITUDE_VALUE</span>
                    <span class="unit">m</span>
                </div>
                
                <div class="reading humidity">
                    <div class="label">Humidity</div>
                    <span class="value" id="humidity">HUMIDITY_VALUE</span>
                    <span class="unit">%</span>
                </div>
            </div>
            
            <div class="card">
                <h3>Statistics (DATA_POINTS points)</h3>
                <div class="stats">
                    <div class="stat-item">
                        <div class="stat-value" id="temp-avg">--</div>
                        <div class="stat-label">Avg Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="temp-min">--</div>
                        <div class="stat-label">Min Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="temp-max">--</div>
                        <div class="stat-label">Max Temp (&deg;C)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="humid-avg">--</div>
                        <div class="stat-label">Avg Humidity (%)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="runtime">--</div>
                        <div class="stat-label">Runtime</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="data-points">--</div>
                        <div class="stat-label">Data Points</div>
                    </div>
                </div>
            </div>
            
            <div class="card">
                <h3>Memory Usage</h3>
                <div class="stats">
                    <div class="stat-item">
                        <div class="stat-value" id="ram-total">--</div>
                        <div class="stat-label">Total RAM (KB)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="ram-used">--</div>
                        <div class="stat-label">Used RAM (%)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="ram-free">--</div>
                        <div class="stat-label">Free RAM (KB)</div>
                    </div>
                    <div class="stat-item">
                        <div class="stat-value" id="max-points">--</div>
                        <div class="stat-label">Est. Max Points</div>
                    </div>
                </div>
            </div>
        </div>
        
        <div class="nav-buttons">
            <button class="btn active" onclick="showChart('6h')">Last 6 Hours</button>
            <button class="btn" onclick="showChart('24h')">Last 24 Hours</button>
            <button class="btn" onclick="showChart('all')">All Data</button>
        </div>
        
        <div class="card">
            <h3>Temperature & Humidity History</h3>
            <div class="chart-container">
                <canvas id="tempHumidChart"></canvas>
            </div>
        </div>
        
        <div class="card">
            <h3>Pressure History</h3>
            <div class="chart-container">
                <canvas id="pressureChart"></canvas>
            </div>
        </div>
        
        <div class="info">
            <div>Data logs every 2 minutes • Updates every 30 seconds</div>
            <div>ESP32-S3 with BME280 Sensor</div>
            <div>I2C: SDA=GPIO21, SCL=GPIO20</div>
        </div>
    </div>

    <script>
        let tempHumidChart, pressureChart;
        let currentTimeframe = '6h';
        
        function initCharts() {
            // Temperature & Humidity Chart
            const ctx1 = document.getElementById('tempHumidChart').getContext('2d');
            tempHumidChart = new Chart(ctx1, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Temperature (°C)',
                        data: [],
                        borderColor: '#dc3545',
                        backgroundColor: 'rgba(220, 53, 69, 0.1)',
                        tension: 0.4,
                        yAxisID: 'y'
                    }, {
                        label: 'Humidity (%)',
                        data: [],
                        borderColor: '#17a2b8',
                        backgroundColor: 'rgba(23, 162, 184, 0.1)',
                        tension: 0.4,
                        yAxisID: 'y1'
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: { intersect: false },
                    scales: {
                        x: { display: true, title: { display: true, text: 'Time' }},
                        y: { type: 'linear', display: true, position: 'left', title: { display: true, text: 'Temperature (°C)' }},
                        y1: { type: 'linear', display: true, position: 'right', title: { display: true, text: 'Humidity (%)' }, grid: { drawOnChartArea: false }}
                    }
                }
            });
            
            // Pressure Chart
            const ctx2 = document.getElementById('pressureChart').getContext('2d');
            pressureChart = new Chart(ctx2, {
                type: 'line',
                data: {
                    labels: [],
                    datasets: [{
                        label: 'Pressure (hPa)',
                        data: [],
                        borderColor: '#28a745',
                        backgroundColor: 'rgba(40, 167, 69, 0.1)',
                        tension: 0.4
                    }]
                },
                options: {
                    responsive: true,
                    maintainAspectRatio: false,
                    interaction: { intersect: false },
                    scales: {
                        x: { display: true, title: { display: true, text: 'Time' }},
                        y: { type: 'linear', display: true, position: 'left', title: { display: true, text: 'Pressure (hPa)' }}
                    }
                }
            });
        }
        
        function showChart(timeframe) {
            currentTimeframe = timeframe;
            document.querySelectorAll('.btn').forEach(btn => btn.classList.remove('active'));
            event.target.classList.add('active');
            loadHistoricalData();
        }
        
        function loadHistoricalData() {
            console.log('Loading historical data for timeframe:', currentTimeframe);
            fetch('/history?range=' + currentTimeframe)
                .then(response => response.json())
                .then(data => {
                    console.log('Received data points:', data.length);
                    console.log('Sample data:', data.slice(0, 3));
                    updateCharts(data);
                    updateStats(data);
                })
                .catch(error => {
                    console.error('Error loading historical data:', error);
                });
        }
        
        function updateCharts(data) {
            console.log('Updating charts with', data.length, 'data points');
            if (data.length === 0) {
                console.log('No data to display');
                return;
            }
            
            const labels = data.map(d => {
                const date = new Date(d.timestamp * 1000);
                return date.toLocaleTimeString();
            });
            
            console.log('Sample labels:', labels.slice(0, 3));
            
            tempHumidChart.data.labels = labels;
            tempHumidChart.data.datasets[0].data = data.map(d => d.temperature);
            tempHumidChart.data.datasets[1].data = data.map(d => d.humidity);
            tempHumidChart.update();
            
            pressureChart.data.labels = labels;
            pressureChart.data.datasets[0].data = data.map(d => d.pressure);
            pressureChart.update();
        }
        
        function updateStats(data) {
            if (data.length === 0) return;
            
            const temps = data.map(d => d.temperature);
            const humids = data.map(d => d.humidity);
            
            document.getElementById('temp-avg').textContent = (temps.reduce((a,b) => a+b) / temps.length).toFixed(1);
            document.getElementById('temp-min').textContent = Math.min(...temps).toFixed(1);
            document.getElementById('temp-max').textContent = Math.max(...temps).toFixed(1);
            document.getElementById('humid-avg').textContent = (humids.reduce((a,b) => a+b) / humids.length).toFixed(1);
        }
        
        function updateCurrentReadings() {
            fetch('/data')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('temp').textContent = data.temperature.toFixed(1);
                    document.getElementById('pressure').textContent = data.pressure.toFixed(1);
                    document.getElementById('humidity').textContent = data.humidity.toFixed(1);
                    if (data.currentTime) {
                        document.getElementById('current-time').textContent = 'Current Time: ' + data.currentTime;
                    }
                    if (data.runtime) {
                        document.getElementById('runtime').textContent = data.runtime;
                    }
                    if (data.dataPoints !== undefined) {
                        document.getElementById('data-points').textContent = data.dataPoints;
                    }
                })
                .catch(error => {
                    console.error('Error updating readings:', error);
                });
        }
        
        function updateMemoryInfo() {
            fetch('/memory')
                .then(response => response.json())
                .then(data => {
                    document.getElementById('ram-total').textContent = (data.totalRAM / 1024).toFixed(0);
                    document.getElementById('ram-used').textContent = data.memoryUsagePercent.toFixed(1) + '%';
                    document.getElementById('ram-free').textContent = (data.freeRAM / 1024).toFixed(0);
                    document.getElementById('max-points').textContent = data.estimatedMaxPossible;
                    
                    console.log('Memory info updated:', data);
                })
                .catch(error => {
                    console.error('Error updating memory info:', error);
                });
        }
        
        // Server-Sent Events for real-time updates
        let eventSource;
        let reconnectInterval;
        
        function connectSSE() {
            if (eventSource) {
                eventSource.close();
            }
            
            eventSource = new EventSource('/events');
            
            eventSource.onopen = function(event) {
                console.log('SSE connection opened');
                if (reconnectInterval) {
                    clearInterval(reconnectInterval);
                    reconnectInterval = null;
                }
            };
            
            eventSource.onmessage = function(event) {
                console.log('Received SSE data:', event.data);
                try {
                    const data = JSON.parse(event.data);
                    if (data.type === 'newData') {
                        // Update current readings immediately
                        updateCurrentReadings();
                        // Refresh charts with new data
                        loadHistoricalData();
                        // Update memory info
                        updateMemoryInfo();
                        console.log('Real-time update triggered');
                    }
                } catch (e) {
                    console.error('Error parsing SSE data:', e);
                }
            };
            
            eventSource.onerror = function(event) {
                console.log('SSE connection error, attempting reconnection...');
                eventSource.close();
                if (!reconnectInterval) {
                    reconnectInterval = setInterval(connectSSE, 5000); // Reconnect every 5 seconds
                }
            };
        }
        
        // Initialize everything
        document.addEventListener('DOMContentLoaded', function() {
            initCharts();
            loadHistoricalData();
            updateCurrentReadings();
            updateMemoryInfo();
            
            // Connect to Server-Sent Events for real-time updates
            connectSSE();
            
            // Keep existing intervals as fallback
            // Update current readings every 30 seconds
            setInterval(updateCurrentReadings, 30000);
            // Update charts every 2 minutes
            setInterval(loadHistoricalData, 120000);
            // Update memory info every minute
            setInterval(updateMemoryInfo, 60000);
        });
        
        // Clean up on page unload
        window.addEventListener('beforeunload', function() {
            if (eventSource) {
                eventSource.close();
            }
        });
    </script>
</body>
</html>
)rawliteral";

// put function declarations here:
unsigned long getCurrentTimestamp();
String getMemoryInfo();
void readSensorData();
void logSensorData();
void handleRoot();
void handleData();
void handleHistory();
void handleMemory();
void handleNotFound();
void handleEvents();
void broadcastSSE(String message);
void cleanupSSEClients();

void setup() {
    // put your setup code here, to run once:
    Serial.begin(115200);
    delay(2000); // Give time for serial monitor to connect
    
    Serial.println("Starting ESP32-S3 Weather Station...");

    // Initialize I2C with ESP32-S3 available pins
    Wire.begin(21, 20); // SDA = GPIO21, SCL = GPIO20
    Serial.println("I2C initialized on SDA=21, SCL=20");

    // Scan for I2C devices
    Serial.println("Scanning for I2C devices...");
    byte error, address;
    int nDevices = 0;
    for(address = 1; address < 127; address++) {
        Wire.beginTransmission(address);
        error = Wire.endTransmission();
        if (error == 0) {
            Serial.print("I2C device found at address 0x");
            if (address < 16) Serial.print("0");
            Serial.println(address, HEX);
            nDevices++;
        }
    }
    if (nDevices == 0) {
        Serial.println("No I2C devices found!");
    } else {
        Serial.print("Found ");
        Serial.print(nDevices);
        Serial.println(" I2C device(s)");
    }

    // Initialize BME280 sensor
    Serial.println("Initializing BME280 sensor...");
    
    if (!bme.begin(0x76)) {
        Serial.println("BME280 not found at 0x76, trying 0x77...");
        if (!bme.begin(0x77)) {
            Serial.println("Could not find a valid BME280 sensor, check wiring!");
            Serial.println("Make sure:");
            Serial.println("- VCC -> 3.3V");
            Serial.println("- GND -> GND");
            Serial.println("- SDA -> GPIO 21");
            Serial.println("- SCL -> GPIO 20");
            while (1);
        } else {
            Serial.println("BME280 found at address 0x77!");
        }
    } else {
        Serial.println("BME280 found at address 0x76!");
    }

    // Configure BME280 settings
    bme.setSampling(Adafruit_BME280::MODE_NORMAL,
                    Adafruit_BME280::SAMPLING_X2,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::SAMPLING_X16,
                    Adafruit_BME280::FILTER_X16,
                    Adafruit_BME280::STANDBY_MS_500);

    Serial.println("BME280 sensor initialized successfully!");

    // Connect to WiFi
    WiFi.begin(ssid, password);
    Serial.print("Connecting to WiFi");
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    Serial.println("WiFi connected!");
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());

    // Initialize SSE client array
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        sseClients[i].active = false;
        sseClients[i].lastPing = 0;
    }
    sseClientCount = 0;
    Serial.println("SSE client array initialized");

    // Initialize NTP time
    Serial.println("Setting up NTP time...");
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
    
    // Wait for time to be set
    struct tm timeinfo;
    int attempts = 0;
    while (!getLocalTime(&timeinfo) && attempts < 10) {
        Serial.println("Failed to obtain time, retrying...");
        delay(1000);
        attempts++;
    }
    if (attempts < 10) {
        Serial.println("Time synchronized successfully!");
        Serial.println(&timeinfo, "Current time: %A, %B %d %Y %H:%M:%S");
    } else {
        Serial.println("Failed to sync time, using millis() fallback");
    }

    // Set up web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.on("/history", handleHistory);
    server.on("/memory", handleMemory);
    server.on("/events", handleEvents);
    server.onNotFound(handleNotFound);

    // Start server
    server.begin();
    Serial.println("HTTP server started");
    Serial.println("Access the weather station at: http://" + WiFi.localIP().toString());
    
    // Take initial sensor reading and log data immediately for testing
    Serial.println("Taking initial sensor reading...");
    readSensorData();
    logSensorData();
    
    // Set up for next data log
    lastDataLog = millis();
}

void loop() {
    // put your main code here, to run repeatedly:
    server.handleClient();

    // Read sensor data every 5 seconds
    static unsigned long lastReading = 0;
    if (millis() - lastReading > 5000) {
        readSensorData();
        lastReading = millis();
    }
    
    // Log data every 2 minutes
    if (millis() - lastDataLog > DATA_LOG_INTERVAL) {
        logSensorData();
        lastDataLog = millis();
        
        // Broadcast new data event to all connected SSE clients
        broadcastSSE("{\"type\":\"newData\",\"timestamp\":" + String(getCurrentTimestamp()) + "}");
    }
    
    // Clean up disconnected SSE clients every 30 seconds
    static unsigned long lastSSECleanup = 0;
    if (millis() - lastSSECleanup > 30000) {
        cleanupSSEClients();
        lastSSECleanup = millis();
    }
}

// put function definitions here:
unsigned long getCurrentTimestamp() {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo)) {
        // Convert to Unix timestamp
        return mktime(&timeinfo);
    } else {
        // Fallback to millis() if NTP time is not available
        return millis() / 1000;
    }
}

String getMemoryInfo() {
    // Get memory information
    size_t totalRAM = ESP.getHeapSize();
    size_t freeRAM = ESP.getFreeHeap();
    size_t usedRAM = totalRAM - freeRAM;
    
    // Calculate data buffer usage
    size_t bufferSize = sizeof(dataBuffer);
    size_t singleDataSize = sizeof(SensorData);
    
    // Calculate theoretical maximum data points that could fit in available RAM
    // (This is rough estimation - actual usable memory is less due to other program needs)
    size_t availableForData = freeRAM / 2; // Use only half of free RAM for safety
    size_t maxPossiblePoints = availableForData / singleDataSize;
    
    String info = "{";
    info += "\"totalRAM\":" + String(totalRAM) + ",";
    info += "\"usedRAM\":" + String(usedRAM) + ",";
    info += "\"freeRAM\":" + String(freeRAM) + ",";
    info += "\"bufferSize\":" + String(bufferSize) + ",";
    info += "\"singleDataSize\":" + String(singleDataSize) + ",";
    info += "\"currentDataPoints\":" + String(dataCount) + ",";
    info += "\"maxDataPoints\":" + String(MAX_DATA_POINTS) + ",";
    info += "\"estimatedMaxPossible\":" + String(maxPossiblePoints) + ",";
    info += "\"memoryUsagePercent\":" + String((float)usedRAM * 100.0 / totalRAM, 1);
    info += "}";
    
    return info;
}

void readSensorData() {
    temperature = bme.readTemperature();
    pressure = bme.readPressure() / 100.0; // Convert Pa to hPa
    humidity = bme.readHumidity();

    Serial.println("=== Sensor Readings ===");
    Serial.println("Temperature: " + String(temperature) + " °C");
    Serial.println("Pressure: " + String(pressure) + " hPa");
    Serial.println("Humidity: " + String(humidity) + " %");
    Serial.println();
}

void logSensorData() {
    unsigned long timestamp = getCurrentTimestamp();
    
    // Track the first data point for runtime calculation
    if (firstDataLog == 0) {
        firstDataLog = timestamp;
    }
    
    dataBuffer[dataIndex].timestamp = timestamp;
    dataBuffer[dataIndex].temperature = temperature;
    dataBuffer[dataIndex].pressure = pressure;
    dataBuffer[dataIndex].humidity = humidity;
    
    // Convert timestamp to readable format for debugging
    time_t rawtime = timestamp;
    struct tm * timeinfo = localtime(&rawtime);
    char timeStr[64];
    strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", timeinfo);
    
    Serial.println("=== Data Logged ===");
    Serial.println("Timestamp: " + String(timestamp) + " (" + String(timeStr) + ")");
    Serial.println("Index: " + String(dataIndex));
    Serial.println("Temperature: " + String(temperature, 2) + "°C");
    Serial.println("Pressure: " + String(pressure, 2) + "hPa");
    Serial.println("Humidity: " + String(humidity, 2) + "%");
    
    dataIndex = (dataIndex + 1) % MAX_DATA_POINTS;
    if (dataCount < MAX_DATA_POINTS) {
        dataCount++;
    }
    
    Serial.println("Total data points: " + String(dataCount));
    Serial.println("Next index: " + String(dataIndex));
    
    // Memory information
    size_t freeRAM = ESP.getFreeHeap();
    size_t totalRAM = ESP.getHeapSize();
    Serial.println("Free RAM: " + String(freeRAM) + " bytes (" + String(freeRAM/1024) + " KB)");
    Serial.println("RAM usage: " + String((float)(totalRAM - freeRAM) * 100.0 / totalRAM, 1) + "%");
    Serial.println("========================");
}

void handleRoot() {
    String html = htmlPage;
    html.replace("TEMP_VALUE", String(temperature, 1));
    html.replace("PRESSURE_VALUE", String(pressure, 1));
    html.replace("HUMIDITY_VALUE", String(humidity, 1));
    server.send(200, "text/html", html);
}

void handleData() {
    struct tm timeinfo;
    String currentTime = "Time not available";
    if (getLocalTime(&timeinfo)) {
        char timeStr[64];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        currentTime = String(timeStr);
    }
    
    // Calculate runtime
    unsigned long runtimeSeconds = 0;
    String runtimeString = "Not available";
    if (firstDataLog > 0) {
        runtimeSeconds = getCurrentTimestamp() - firstDataLog;
        unsigned long days = runtimeSeconds / 86400;
        unsigned long hours = (runtimeSeconds % 86400) / 3600;
        unsigned long minutes = (runtimeSeconds % 3600) / 60;
        
        if (days > 0) {
            runtimeString = String(days) + "d " + String(hours) + "h " + String(minutes) + "m";
        } else if (hours > 0) {
            runtimeString = String(hours) + "h " + String(minutes) + "m";
        } else {
            runtimeString = String(minutes) + "m " + String(runtimeSeconds % 60) + "s";
        }
    }
    
    String json = "{";
    json += "\"temperature\":" + String(temperature, 1) + ",";
    json += "\"pressure\":" + String(pressure, 1) + ",";
    json += "\"humidity\":" + String(humidity, 1) + ",";
    json += "\"timestamp\":" + String(getCurrentTimestamp()) + ",";
    json += "\"currentTime\":\"" + currentTime + "\",";
    json += "\"runtimeSeconds\":" + String(runtimeSeconds) + ",";
    json += "\"runtime\":\"" + runtimeString + "\",";
    json += "\"dataPoints\":" + String(dataCount);
    json += "}";
    server.send(200, "application/json", json);
}

void handleHistory() {
    String range = server.arg("range");
    int pointsToReturn = dataCount;
    
    Serial.println("=== History Request ===");
    Serial.println("Range: " + range);
    Serial.println("Total data count: " + String(dataCount));
    Serial.println("Data index: " + String(dataIndex));
    
    // Filter data based on requested timeframe
    if (range == "6h") {
        pointsToReturn = min(dataCount, 180); // 6 hours = 180 points (2-min intervals)
    } else if (range == "24h") {
        pointsToReturn = min(dataCount, 720); // 24 hours = 720 points
    }
    // "all" returns all available data
    
    Serial.println("Points to return: " + String(pointsToReturn));
    
    String json = "[";
    
    if (dataCount > 0) {
        int startIndex;
        if (dataCount < MAX_DATA_POINTS) {
            // Buffer not full yet, start from beginning
            startIndex = max(0, dataCount - pointsToReturn);
        } else {
            // Buffer is full, calculate circular buffer start
            startIndex = (dataIndex - pointsToReturn + MAX_DATA_POINTS) % MAX_DATA_POINTS;
        }
        
        Serial.println("Start index: " + String(startIndex));
        
        for (int i = 0; i < pointsToReturn; i++) {
            int index = (startIndex + i) % MAX_DATA_POINTS;
            if (i > 0) json += ",";
            json += "{";
            json += "\"timestamp\":" + String(dataBuffer[index].timestamp) + ",";
            json += "\"temperature\":" + String(dataBuffer[index].temperature, 2) + ",";
            json += "\"pressure\":" + String(dataBuffer[index].pressure, 2) + ",";
            json += "\"humidity\":" + String(dataBuffer[index].humidity, 2);
            json += "}";
        }
    }
    
    json += "]";
    Serial.println("JSON length: " + String(json.length()));
    Serial.println("========================");
    
    server.send(200, "application/json", json);
}

void handleMemory() {
    Serial.println("=== Memory Info Request ===");
    String memInfo = getMemoryInfo();
    Serial.println("Memory info: " + memInfo);
    Serial.println("===========================");
    server.send(200, "application/json", memInfo);
}

void handleNotFound() {
    server.send(404, "text/plain", "404: Page not found");
}

void handleEvents() {
    WiFiClient client = server.client();
    
    // Find an available slot for the new SSE client
    int clientIndex = -1;
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (!sseClients[i].active) {
            clientIndex = i;
            break;
        }
    }
    
    if (clientIndex == -1) {
        // No available slots, reject the connection
        server.send(503, "text/plain", "SSE: Too many clients");
        return;
    }
    
    // Send SSE headers manually using the client connection
    client.println("HTTP/1.1 200 OK");
    client.println("Content-Type: text/event-stream");
    client.println("Cache-Control: no-cache");
    client.println("Connection: keep-alive");
    client.println("Access-Control-Allow-Origin: *");
    client.println(); // Empty line to end headers
    
    // Store the client
    sseClients[clientIndex].client = client;
    sseClients[clientIndex].lastPing = millis();
    sseClients[clientIndex].active = true;
    sseClientCount++;
    
    Serial.println("New SSE client connected. Total clients: " + String(sseClientCount));
    
    // Send initial connection message
    String initialMessage = "data: {\"type\":\"connected\",\"message\":\"SSE connection established\"}\n\n";
    client.print(initialMessage);
    client.flush(); // Ensure data is sent immediately
}

void broadcastSSE(String message) {
    if (sseClientCount == 0) return;
    
    String sseMessage = "data: " + message + "\n\n";
    
    Serial.println("Broadcasting SSE message to " + String(sseClientCount) + " clients: " + message);
    
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sseClients[i].active) {
            if (sseClients[i].client.connected()) {
                size_t written = sseClients[i].client.print(sseMessage);
                sseClients[i].client.flush(); // Ensure immediate delivery
                if (written == 0) {
                    // Failed to write, mark as inactive
                    sseClients[i].active = false;
                    sseClients[i].client.stop();
                    sseClientCount--;
                    Serial.println("SSE client " + String(i) + " disconnected (write failed)");
                }
            } else {
                // Client disconnected
                sseClients[i].active = false;
                sseClients[i].client.stop();
                sseClientCount--;
                Serial.println("SSE client " + String(i) + " disconnected");
            }
        }
    }
}

void cleanupSSEClients() {
    for (int i = 0; i < MAX_SSE_CLIENTS; i++) {
        if (sseClients[i].active) {
            if (!sseClients[i].client.connected()) {
                sseClients[i].active = false;
                sseClients[i].client.stop();
                sseClientCount--;
                Serial.println("Cleaned up disconnected SSE client " + String(i));
            } else {
                // Send keepalive ping every 30 seconds
                if (millis() - sseClients[i].lastPing > 30000) {
                    String pingMessage = "data: {\"type\":\"ping\"}\n\n";
                    size_t written = sseClients[i].client.print(pingMessage);
                    sseClients[i].client.flush(); // Ensure immediate delivery
                    if (written > 0) {
                        sseClients[i].lastPing = millis();
                    } else {
                        // Failed to send ping, client is probably disconnected
                        sseClients[i].active = false;
                        sseClients[i].client.stop();
                        sseClientCount--;
                        Serial.println("SSE client " + String(i) + " removed (ping failed)");
                    }
                }
            }
        }
    }
    
    if (sseClientCount > 0) {
        Serial.println("SSE cleanup complete. Active clients: " + String(sseClientCount));
    }
}
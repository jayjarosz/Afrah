/**
 * ESP32 Dual Matrix Controller - Fixed Version
 * 
 * Fixed issues:
 * - Proper initialization sequence to prevent stuck LEDs
 * - Better SD card error handling
 * - Matrix reset functionality
 * - Improved timing and state management
 */

// Include required libraries
#include <Adafruit_GFX.h>          
#include <Adafruit_NeoMatrix.h>    
#include <Adafruit_NeoPixel.h>     
#include <SPI.h>                   
#include <SD.h>                    
#include <vector>                  

//================ PIN DEFINITIONS ================
#define SD_CS_PIN D12      // SD Card Chip Select pin (GPIO4)
#define TEXT_LED_PIN D9  // Data pin for text matrix (GPIO2)
#define GIF_LED_PIN D2   // Data pin for GIF matrix (GPIO25)

// if your esp board deos not have a mosi miso clk pin defined then you can use VSPI

// #define SD_MOSI_PIN 11   
// #define SD_MISO_PIN 13   
// #define SD_CLK_PIN 12    

//================ MATRIX CONFIGURATION ================
#define GIF_MATRIX_WIDTH 16    
#define GIF_MATRIX_HEIGHT 16   
#define TEXT_MATRIX_WIDTH 40   
#define TEXT_MATRIX_HEIGHT 8   

#define GIF_NUM_LEDS (GIF_MATRIX_WIDTH * GIF_MATRIX_HEIGHT)

//================ TIMING AND ANIMATION SETTINGS ================
#define DEFAULT_GIF_FRAME_DELAY 50  // Default delay if not specified in file
#define TEXT_SCROLL_DELAY 100       
#define PIXEL_PER_CHAR 4           
#define BRIGHTNESS 20              
#define STARTUP_DELAY 2000         // 2 second delay before starting

//================ INITIALIZE LED MATRICES ================
Adafruit_NeoPixel gifMatrix(GIF_NUM_LEDS, GIF_LED_PIN, NEO_GRB + NEO_KHZ800);

Adafruit_NeoMatrix textMatrix = Adafruit_NeoMatrix(
  TEXT_MATRIX_WIDTH, TEXT_MATRIX_HEIGHT, 
  TEXT_LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
  NEO_GRB + NEO_KHZ800
);

//================ GLOBAL VARIABLES ================
File gifFile;                  
int numFrames = 0;             
int currentFrameDelay = DEFAULT_GIF_FRAME_DELAY;  

unsigned long lastGifUpdate = 0;  
unsigned long lastTextUpdate = 0;  
unsigned long startupTime = 0;

std::vector<String> messages;  
int currentMessage = 0;        
int textX = 0;                 
int maxDisplacement = 0;       

std::vector<int> availablePairs; 
int currentPairIndex = 0;        
bool textScrollComplete = false; 
int gifLoopCount = 0;           
bool forceNextPair = false;     
int frameCounter = 0;           
bool systemReady = false;       // New flag to track system readiness

bool debug = false;            

const uint16_t textColor = textMatrix.Color(255, 0, 0);

void clearAllMatrices() {
  Serial.println("Clearing all matrices...");
  
  // Clear GIF matrix
  gifMatrix.clear();
  gifMatrix.show();
  
  // Clear text matrix
  textMatrix.fillScreen(0);
  textMatrix.show();
  
  delay(100); // Give time for clear operation
}

void initializeMatrices() {
  Serial.println("Initializing LED matrices...");
  
  // Initialize GIF matrix first
  gifMatrix.begin();
  gifMatrix.setBrightness(BRIGHTNESS);
  gifMatrix.clear();
  gifMatrix.show();
  delay(100);
  Serial.println("GIF matrix initialized");
  
  // Initialize text matrix
  textMatrix.begin();
  textMatrix.setTextWrap(false);
  textMatrix.setBrightness(BRIGHTNESS);
  textMatrix.fillScreen(0); // Use fillScreen(0) instead of clear()
  textMatrix.show();
  delay(100);
  Serial.println("Text matrix initialized");
  
  // Additional clear to ensure clean state
  clearAllMatrices();
}

bool initializeSDCard() {
  Serial.print("Initializing SD card...");
  
  // Configure SPI for Firebeetle ESP32 E board
  SPI.begin();
  delay(100);
  
  // Try multiple times to initialize SD card
  for (int attempt = 1; attempt <= 3; attempt++) {
    Serial.print(" (attempt ");
    Serial.print(attempt);
    Serial.print(")");
    
    if (SD.begin(SD_CS_PIN)) {
      Serial.println(" SUCCESS!");
      return true;
    }
    
    delay(500);
  }
  
  Serial.println(" FAILED after 3 attempts!");
  return false;
}

void setup() {
  Serial.begin(115200);
  delay(1000); // Give serial time to initialize
  
  Serial.println("\n\n*** Starting matrix display system... ***");
  Serial.println("Firmware version: Fixed LED Issue v1.0");
  
  // Record startup time
  startupTime = millis();
  
  // Step 1: Initialize matrices with proper sequence
  initializeMatrices();
  
  // Step 2: Initialize SD card with retry logic
  bool sdReady = initializeSDCard();
  
  if (!sdReady) {
    Serial.println("ERROR: SD Card failed to initialize!");
    Serial.println("System will continue but no content will load.");
    Serial.println("Check SD card connection and reboot.");
    
    // Flash both matrices to indicate error
    for (int i = 0; i < 5; i++) {
      gifMatrix.fill(gifMatrix.Color(255, 0, 0)); // Red flash
      textMatrix.fillScreen(textMatrix.Color(255, 0, 0));
      gifMatrix.show();
      textMatrix.show();
      delay(200);
      
      clearAllMatrices();
      delay(200);
    }
    
    return; // Don't proceed without SD card
  }
  
  // Step 3: Scan for content
  scanForPairs();
  
  if (debug) {
    listFiles();
  }
  
  // Step 4: Load initial content
  if (availablePairs.size() > 0) {
    Serial.print("Loading initial pair: ");
    Serial.println(availablePairs[currentPairIndex]);
    loadPair(availablePairs[currentPairIndex]);
  } else {
    Serial.println("ERROR: No GIF/text pairs found on SD card!");
    
    // Show error pattern
    textMatrix.setTextColor(textMatrix.Color(255, 0, 0));
    textMatrix.setCursor(0, 0);
    textMatrix.print("NO FILES");
    textMatrix.show();
  }
  
  // Step 5: Final startup delay and ready state
  Serial.println("Waiting for system to stabilize...");
  while (millis() - startupTime < STARTUP_DELAY) {
    delay(100);
  }
  
  systemReady = true;
  Serial.println("*** System ready! ***");
  Serial.println("Available commands: pair=X, list, pairs, debug, next, rescan, status, clear");
}

void scanForPairs() {
  Serial.println("Scanning for GIF/text pairs...");
  availablePairs.clear();
  
  for (int i = 1; i <= 999; i++) {  
    String gifFilename = "/gif" + String(i) + ".bin";
    String textFilename = "/text" + String(i) + ".txt";
    
    if (SD.exists(gifFilename) && SD.exists(textFilename)) {
      availablePairs.push_back(i);
      Serial.print("Found pair #");
      Serial.println(i);
    }
  }
  
  Serial.print("Total pairs found: ");
  Serial.println(availablePairs.size());
}

void listFiles() {
  Serial.println("Files on SD card:");
  File root = SD.open("/");
  if (root) {
    printDirectory(root, 0);
    root.close();
  } else {
    Serial.println("Failed to open root directory");
  }
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) break;
    
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void loadPair(int pairNumber) {
  Serial.print("\n*** Loading pair #");
  Serial.print(pairNumber);
  Serial.println(" ***");
  
  // Reset state
  textScrollComplete = false;  
  gifLoopCount = 0;            
  currentMessage = 0;          
  frameCounter = 0;            
  forceNextPair = false;       
  textX = textMatrix.width();  
  
  // Close existing file
  if (gifFile) {
    gifFile.close();
  }
  
  // Clear displays before loading new content
  clearAllMatrices();
  
  // Open the GIF file
  String gifFilename = "/gif" + String(pairNumber) + ".bin";
  gifFile = SD.open(gifFilename, "r");
  if (!gifFile) {
    Serial.println("Failed to open GIF file: " + gifFilename);
    return;
  } else {
    // Read the file header
    gifFile.read((uint8_t*)&numFrames, sizeof(numFrames));
    gifFile.read((uint8_t*)&currentFrameDelay, sizeof(currentFrameDelay));
    
    Serial.print("Number of frames: ");
    Serial.println(numFrames);
    Serial.print("Frame delay: ");
    Serial.print(currentFrameDelay);
    Serial.println(" ms");
    
    // Validate frame delay
    if (currentFrameDelay <= 0 || currentFrameDelay > 10000) {
      Serial.println("Invalid frame delay, using default");
      currentFrameDelay = DEFAULT_GIF_FRAME_DELAY;
    }
  }
  
  // Load text messages
  String textFilename = "/text" + String(pairNumber) + ".txt";
  loadMessagesFromSD(textFilename);
  
  if (messages.size() > 0) {
    Serial.print("First message: ");
    Serial.println(messages[0]);
    maxDisplacement = messages[0].length() * PIXEL_PER_CHAR + textMatrix.width();
  } else {
    Serial.println("WARNING: No messages loaded!");
  }
}

void loadMessagesFromSD(String filename) {
  messages.clear();
  
  File messageFile = SD.open(filename);
  if (!messageFile) {
    Serial.println("Failed to open text file: " + filename);
    messages.push_back("File Not Found");  
    return;
  }
  
  Serial.print("Loading text from file (");
  Serial.print(messageFile.size());
  Serial.println(" bytes)...");
  
  String fileContent = "";
  while (messageFile.available()) {
    fileContent += (char)messageFile.read();
  }
  messageFile.close();
  
  int startIndex = 0;
  bool skipFirstLine = false;
  
  if (fileContent.startsWith("#")) {
    skipFirstLine = true;
  }
  
  while (startIndex < fileContent.length()) {
    int endIndex = fileContent.indexOf('\n', startIndex);
    if (endIndex == -1) {
      endIndex = fileContent.length(); 
    }
    
    String line = fileContent.substring(startIndex, endIndex);
    line.trim(); 
    
    if (!line.startsWith("#") && line.length() > 0) {
      if (!skipFirstLine) {
        messages.push_back(line);
      }
      skipFirstLine = false;
    }
    
    startIndex = endIndex + 1;
  }
  
  Serial.print("Loaded ");
  Serial.print(messages.size());
  Serial.println(" messages from SD card");
}

int getGifIndex(int row, int col) {
    int subMatrixSize = 8;
    
    int subMatrixCol = col / subMatrixSize;  
    int subMatrixRow = row / subMatrixSize;  
    
    int localCol = col % subMatrixSize;
    int localRow = row % subMatrixSize;

    int matrixIndex;
    if (subMatrixRow == 0) {
        matrixIndex = subMatrixCol;  
    } else {
        matrixIndex = 2 + subMatrixCol;  
    }

    int baseIndex = matrixIndex * (subMatrixSize * subMatrixSize);
    
    return baseIndex + (localRow * subMatrixSize) + localCol;
}

void displayGifFrame() {
    if (!gifFile || !systemReady) return;  
    
    if (!gifFile.available()) {
        // Reset to beginning, skip header
        gifFile.seek(sizeof(numFrames) + sizeof(currentFrameDelay));  
        gifLoopCount++;  
        frameCounter = 0;
        
        if (debug) {
          Serial.print("GIF loop count: ");
          Serial.print(gifLoopCount);
          Serial.print(", text complete: ");
          Serial.println(textScrollComplete ? "Yes" : "No");
        }
        
        if (textScrollComplete || forceNextPair) {
            advanceToNextPair();
        }
        return;
    }
    
    frameCounter++;
    
    for (int row = 0; row < GIF_MATRIX_HEIGHT; row++) {
        for (int col = 0; col < GIF_MATRIX_WIDTH; col++) {
            int index = getGifIndex(row, col);
            
            uint8_t r, g, b;
            
            if (gifFile.available() >= 3) {
                gifFile.read(&r, 1);  
                gifFile.read(&g, 1);  
                gifFile.read(&b, 1);  
            } else {
                r = 0; g = 0; b = 0;
            }
            
            gifMatrix.setPixelColor(index, gifMatrix.Color(r, g, b));
        }
    }
    
    gifMatrix.show();
}

void scrollText() {
    if (messages.size() <= 0 || textScrollComplete || !systemReady) {
        return;
    }

    textMatrix.fillScreen(0); // Clear background
    textMatrix.setTextColor(textColor);  
    textMatrix.setCursor(textX, 0);
    textMatrix.print(messages[currentMessage]);
    textMatrix.show();
    
    textX--;
    
    if (textX < -maxDisplacement) {
        textX = textMatrix.width();
        currentMessage++;
        
        Serial.print("Finished message ");
        Serial.print(currentMessage);
        Serial.print(" of ");
        Serial.println(messages.size());
        
        if (currentMessage >= messages.size()) {
            textScrollComplete = true;
            Serial.println("*** Text scroll completed for this pair ***");
            
            textMatrix.fillScreen(0);
            textMatrix.show();
            
            if (gifLoopCount > 0) {
                forceNextPair = true;
            }
        } else {
            maxDisplacement = messages[currentMessage].length() * PIXEL_PER_CHAR + textMatrix.width();
            Serial.print("Next message: ");
            Serial.println(messages[currentMessage]);
        }
    }
}

void advanceToNextPair() {
    Serial.println("\n*** Advancing to next pair ***");
    
    if (availablePairs.size() == 0) {
        Serial.println("ERROR: No pairs available!");
        return;
    }
    
    currentPairIndex = (currentPairIndex + 1) % availablePairs.size();
    int nextPair = availablePairs[currentPairIndex];
    
    Serial.print("Loading pair #");
    Serial.print(nextPair);
    Serial.print(" (");
    Serial.print(currentPairIndex + 1);
    Serial.print(" of ");
    Serial.print(availablePairs.size());
    Serial.println(")");
    
    loadPair(nextPair);
}

void loop() {
    if (!systemReady) {
        delay(100);
        return;
    }
    
    unsigned long currentMillis = millis();
    
    // Handle serial commands
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        command.trim();
        
        if (command.startsWith("pair=")) {
            int pair = command.substring(5).toInt();
            
            bool found = false;
            for (int i = 0; i < availablePairs.size(); i++) {
                if (availablePairs[i] == pair) {
                    currentPairIndex = i;
                    found = true;
                    break;
                }
            }
            
            if (found) {
                loadPair(pair);
            } else {
                Serial.print("Pair #");
                Serial.print(pair);
                Serial.println(" not found!");
            }
        } else if (command == "clear") {
            clearAllMatrices();
            Serial.println("All matrices cleared");
        } else if (command == "list") {
            listFiles();
        } else if (command == "pairs") {
            Serial.println("Available pairs:");
            for (int i = 0; i < availablePairs.size(); i++) {
                Serial.print("  ");
                Serial.println(availablePairs[i]);
            }
        } else if (command == "debug") {
            debug = !debug;
            Serial.print("Debug mode ");
            Serial.println(debug ? "ON" : "OFF");
        } else if (command == "next") {
            forceNextPair = true;
        } else if (command == "rescan") {
            scanForPairs();
        } else if (command == "status") {
            Serial.println("\n*** Status Report ***");
            Serial.print("System ready: ");
            Serial.println(systemReady ? "Yes" : "No");
            if (availablePairs.size() > 0) {
                Serial.print("Current pair: ");
                Serial.print(availablePairs[currentPairIndex]);
                Serial.print(" (");
                Serial.print(currentPairIndex + 1);
                Serial.print(" of ");
                Serial.print(availablePairs.size());
                Serial.println(")");
            }
            Serial.print("Current frame delay: ");
            Serial.print(currentFrameDelay);
            Serial.println(" ms");
            Serial.print("Current message: ");
            Serial.print(currentMessage + 1);
            Serial.print(" of ");
            Serial.println(messages.size());
            Serial.print("Text scroll complete: ");
            Serial.println(textScrollComplete ? "Yes" : "No");
            Serial.print("GIF loop count: ");
            Serial.println(gifLoopCount);
            Serial.print("Frame counter: ");
            Serial.print(frameCounter);
            Serial.print(" of ");
            Serial.println(numFrames);
        }
    }
    
    // Update GIF animation
    if (currentMillis - lastGifUpdate >= currentFrameDelay) {
        displayGifFrame();
        lastGifUpdate = currentMillis;
    }
    
    // Update text scrolling
    if (currentMillis - lastTextUpdate >= TEXT_SCROLL_DELAY) {
        if (!textScrollComplete || currentMessage < messages.size()) {
            scrollText();
        }
        lastTextUpdate = currentMillis;
    }
    
    delay(5);
}

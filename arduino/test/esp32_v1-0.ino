/**
 * ESP32 Dual Matrix Controller - Gallery Installation Version
 * 
 * This program drives two LED matrices:
 * 1. A 16x16 matrix for displaying GIF animations
 * 2. An 8x40 matrix for scrolling text
 * 
 * Features:
 * - Reads GIF animations and text from SD card
 * - Supports multiple GIF/text pairs (gif1.bin/text1.txt, gif2.bin/text2.txt, etc.)
 * - Automatically cycles through all pairs
 * - Each text message scrolls exactly once before moving to next pair
 * - Clean startup with no test patterns (suitable for gallery installation)
 */

// Include required libraries
#include <Adafruit_GFX.h>          // Graphics library for drawing text
#include <Adafruit_NeoMatrix.h>    // Library for controlling NeoPixel matrices
#include <Adafruit_NeoPixel.h>     // Library for controlling NeoPixel LEDs
#include <SPI.h>                   // Required for SD card communication
#include <SD.h>                    // SD card library

//================ PIN DEFINITIONS ================
#define SD_CS_PIN D12      // SD Card Chip Select pin (GPIO5)
#define TEXT_LED_PIN D9  // Data pin for text matrix (GPIO25)
#define GIF_LED_PIN D2   // Data pin for GIF matrix (GPIO27)

//================ MATRIX CONFIGURATION ================
#define GIF_MATRIX_WIDTH 16    // Width of the GIF matrix in pixels
#define GIF_MATRIX_HEIGHT 16   // Height of the GIF matrix in pixels
#define TEXT_MATRIX_WIDTH 40   // Width of the text matrix in pixels
#define TEXT_MATRIX_HEIGHT 8   // Height of the text matrix in pixels

// Calculate total number of LEDs in GIF matrix
#define GIF_NUM_LEDS (GIF_MATRIX_WIDTH * GIF_MATRIX_HEIGHT)

//================ TIMING AND ANIMATION SETTINGS ================
#define GIF_FRAME_DELAY 50     // Milliseconds between GIF frames (higher = slower animation)
#define TEXT_SCROLL_DELAY 50   // Milliseconds between text scroll steps (higher = slower scrolling)
#define MAX_MESSAGE_LENGTH 100 // Maximum length of a text message in characters
#define MAX_MESSAGES 20        // Maximum number of messages per text file
#define PIXEL_PER_CHAR 4       // Width of each character in pixels (including spacing)
#define BRIGHTNESS 20          // LED brightness (0-255, lower values use less power)
#define MAX_PAIRS 3            // Maximum number of GIF/text pairs to cycle through

//================ INITIALIZE LED MATRICES ================
// Create NeoPixel object for the GIF matrix
Adafruit_NeoPixel gifMatrix(GIF_NUM_LEDS, GIF_LED_PIN, NEO_GRB + NEO_KHZ800);

// Create NeoMatrix object for the text matrix with appropriate configuration
Adafruit_NeoMatrix textMatrix = Adafruit_NeoMatrix(
  TEXT_MATRIX_WIDTH, TEXT_MATRIX_HEIGHT, 
  TEXT_LED_PIN,
  NEO_MATRIX_TOP + NEO_MATRIX_RIGHT + NEO_MATRIX_COLUMNS + NEO_MATRIX_PROGRESSIVE,
  NEO_GRB + NEO_KHZ800
);

//================ GLOBAL VARIABLES ================
// File handling
File gifFile;                  // Handle for the currently open GIF file
int numFrames = 0;             // Number of frames in the current GIF

// Timing variables
unsigned long lastGifUpdate = 0;  // Last time the GIF was updated
unsigned long lastTextUpdate = 0;  // Last time the text was scrolled

// Text variables
char* messages[MAX_MESSAGES];  // Array to store text messages
int messageCount = 0;          // Number of messages loaded
int currentMessage = 0;        // Index of current message being displayed
int textX = 0;                 // Current X position of text (for scrolling)
int maxDisplacement = 0;       // How far text needs to scroll to disappear

// State management
int currentPair = 1;           // Current GIF/text pair being displayed (starts at 1)
bool textScrollComplete = false;  // Whether text scrolling is complete for current pair
int gifLoopCount = 0;          // How many times the current GIF has looped
bool forceNextPair = false;    // Flag to force loading the next pair
int frameCounter = 0;          // Counter for frames in current GIF

// Debug flags
bool debug = false;            // Disable debug messages by default for gallery installation

// Set text color (red is often most visible)
const uint16_t textColor = textMatrix.Color(255, 0, 0); // Bright red

/**
 * Setup function - runs once at startup
 */
void setup() {
  // Initialize serial communication
  Serial.begin(115200);
  Serial.println("\n\n*** Starting matrix display system... ***");
  
  // Initialize GIF matrix
  gifMatrix.begin();
  gifMatrix.setBrightness(BRIGHTNESS);
  gifMatrix.clear();           // Clear any previous data
  gifMatrix.show();            // Update the display
  
  // Initialize text matrix
  textMatrix.begin();
  textMatrix.setTextWrap(false);  // Don't wrap text (important for scrolling)
  textMatrix.setBrightness(BRIGHTNESS);
  textMatrix.setTextColor(textColor);
  textMatrix.clear();
  textMatrix.show();
  
  // Initialize SD card
  Serial.print("Initializing SD card...");
  if (!SD.begin(SD_CS_PIN)) {
    Serial.println("SD Card initialization failed!");
  } else {
    Serial.println("SD card initialized successfully!");
  }

  // List all files on SD card to help with troubleshooting
  listFiles();
  
  // Load the first GIF/text pair
  loadPair(currentPair);
  
  // Note: No test pattern display for gallery installation
  
  Serial.println("Setup complete!");
}

/**
 * List all files on the SD card (for debugging)
 */
void listFiles() {
  Serial.println("Files on SD card:");
  File root = SD.open("/");
  printDirectory(root, 0);
  root.close();
}

/**
 * Helper function to recursively print directory contents
 */
void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      // No more files
      break;
    }
    // Print indentation
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    // Print file/directory name
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      // If it's a directory, print / and recurse into it
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      // If it's a file, print the size
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

/**
 * Load a specific GIF/text pair
 * 
 * @param pairNumber The pair number to load (1 = gif1.bin/text1.txt)
 */
void loadPair(int pairNumber) {
  Serial.print("\n*** Loading pair #");
  Serial.print(pairNumber);
  Serial.println(" ***");
  
  // Reset all status flags and counters
  textScrollComplete = false;  // Text hasn't scrolled yet
  gifLoopCount = 0;            // GIF hasn't looped yet
  currentMessage = 0;          // Start with first message
  frameCounter = 0;            // Start with first frame
  forceNextPair = false;       // Don't force next pair yet
  
  // Reset text position for new pair
  textX = textMatrix.width();  // Start text at the right edge
  
  // Close existing GIF file if open
  if (gifFile) {
    gifFile.close();
  }
  
  // Open the GIF file
  String gifFilename = "/gif" + String(pairNumber) + ".bin";
  gifFile = SD.open(gifFilename, "r");
  if (!gifFile) {
    Serial.println("Failed to open GIF file: " + gifFilename);
    return;
  } else {
    // Read number of frames (first 4 bytes of the file)
    gifFile.read((uint8_t*)&numFrames, sizeof(numFrames));
    Serial.print("Number of frames: ");
    Serial.println(numFrames);
  }
  
  // Load the corresponding text file
  String textFilename = "/text" + String(pairNumber) + ".txt";
  loadMessagesFromSD(textFilename);
  
  // Setup text scrolling if messages were loaded
  if (messageCount > 0) {
    Serial.print("First message: ");
    Serial.println(messages[0]);
    maxDisplacement = strlen(messages[0]) * PIXEL_PER_CHAR + textMatrix.width();
  } else {
    Serial.println("WARNING: No messages loaded!");
  }
  
  // Clear both displays to start fresh
  gifMatrix.clear();
  gifMatrix.show();
  textMatrix.clear();
  textMatrix.show();
}

/**
 * Load text messages from a file on the SD card
 * 
 * @param filename Path to the text file (e.g., "/text1.txt")
 */
void loadMessagesFromSD(String filename) {
  // Clean up previous messages to avoid memory leaks
  for (int i = 0; i < messageCount; i++) {
    if (messages[i] != NULL) {
      free(messages[i]);
      messages[i] = NULL;
    }
  }
  messageCount = 0;
  
  // Open text file
  File messageFile = SD.open(filename);
  if (!messageFile) {
    Serial.println("Failed to open text file: " + filename);
    messages[0] = strdup("File Not Found");  // Use a default message
    messageCount = 1;
    return;
  }
  
  // Prepare to read file
  char buffer[MAX_MESSAGE_LENGTH];
  int bufferIndex = 0;
  bool skipLine = false;          // Flag to skip configuration lines
  bool isFirstLine = true;        // Track if we're on the first line
  
  // Read the file character by character
  while (messageFile.available() && messageCount < MAX_MESSAGES) {
    char c = messageFile.read();
    
    // Skip lines starting with '#' (config lines like #SPEED:40)
    if (isFirstLine && c == '#') {
      skipLine = true;
    }
    
    // Handle end of line
    if (c == '\n' || c == '\r') {
      if (bufferIndex > 0 && !skipLine) {
        // We have a complete message
        buffer[bufferIndex] = '\0';  // Null-terminate the string
        messages[messageCount] = strdup(buffer);  // Make a copy of the buffer
        messageCount++;
      }
      bufferIndex = 0;               // Reset buffer
      skipLine = false;              // Reset skip flag
      isFirstLine = (c == '\n');     // Only reset isFirstLine on actual newlines
    } else if (!skipLine) {
      // Add character to buffer
      if (bufferIndex < MAX_MESSAGE_LENGTH - 1) {
        buffer[bufferIndex] = c;
        bufferIndex++;
      }
    }
  }
  
  // Handle last line if it's not terminated with a newline
  if (bufferIndex > 0 && !skipLine) {
    buffer[bufferIndex] = '\0';
    messages[messageCount] = strdup(buffer);
    messageCount++;
  }
  
  messageFile.close();
  
  // Print a summary of loaded messages
  Serial.print("Loaded ");
  Serial.print(messageCount);
  Serial.println(" messages from SD card:");
  for (int i = 0; i < messageCount; i++) {
    Serial.print("  ");
    Serial.print(i);
    Serial.print(": [");
    Serial.print(messages[i]);
    Serial.println("]");
  }
}

/**
 * Convert 2D coordinates to LED index for the GIF matrix
 * 
 * This function handles the specific arrangement of four 8x8 panels
 * that make up the 16x16 GIF matrix.
 * 
 * @param row Y coordinate (0-15)
 * @param col X coordinate (0-15)
 * @return The LED index in the strip
 */
int getGifIndex(int row, int col) {
    // Each panel is 8x8
    int subMatrixSize = 8;
    
    // Determine which panel we're addressing
    int subMatrixCol = col / subMatrixSize;  // 0 for left panels, 1 for right panels
    int subMatrixRow = row / subMatrixSize;  // 0 for top panels, 1 for bottom panels
    
    // Calculate position within the 8x8 panel
    int localCol = col % subMatrixSize;
    int localRow = row % subMatrixSize;

    // Determine panel number (0-3)
    int matrixIndex;
    if (subMatrixRow == 0) {
        matrixIndex = subMatrixCol;  // Top row: 0 (top-left), 1 (top-right)
    } else {
        matrixIndex = 2 + subMatrixCol;  // Bottom row: 2 (bottom-left), 3 (bottom-right)
    }

    // Calculate base index for this panel
    int baseIndex = matrixIndex * (subMatrixSize * subMatrixSize);
    
    // Calculate final LED index within the panel
    return baseIndex + (localRow * subMatrixSize) + localCol;
}

/**
 * Display a single frame of the GIF animation
 */
void displayGifFrame() {
    if (!gifFile) return;  // Safety check
    
    // Check if we've reached the end of the file
    if (!gifFile.available()) {
        // Reached end of file, seek back to start of frames
        gifFile.seek(sizeof(numFrames));  // Skip the frame count at the beginning
        gifLoopCount++;  // Increment loop counter
        
        // Reset frame counter
        frameCounter = 0;
        
        if (debug) {
          Serial.print("GIF loop count: ");
          Serial.print(gifLoopCount);
          Serial.print(", text complete: ");
          Serial.println(textScrollComplete ? "Yes" : "No");
        }
        
        // Check if text scrolling is complete or we're forced to move on
        if (textScrollComplete || forceNextPair) {
            // Move to next pair after GIF completes at least one loop
            advanceToNextPair();
        }
        return;
    }
    
    // Increment frame counter
    frameCounter++;
    
    // Read and display the current frame
    for (int row = 0; row < GIF_MATRIX_HEIGHT; row++) {
        for (int col = 0; col < GIF_MATRIX_WIDTH; col++) {
            // Get the LED index for this pixel
            int index = getGifIndex(row, col);
            
            // RGB values for the pixel
            uint8_t r, g, b;
            
            // Read RGB values from file (3 bytes per pixel)
            if (gifFile.available() >= 3) {
                gifFile.read(&r, 1);  // Red
                gifFile.read(&g, 1);  // Green
                gifFile.read(&b, 1);  // Blue
            } else {
                // Not enough data, set pixel to black
                r = 0; g = 0; b = 0;
            }
            
            // Set the pixel color
            gifMatrix.setPixelColor(index, gifMatrix.Color(r, g, b));
        }
    }
    
    // Update the display
    gifMatrix.show();
}

/**
 * Scroll and display the current text message
 */
void scrollText() {
    // Skip if no messages or text is already complete
    if (messageCount <= 0 || textScrollComplete) {
        if (debug && messageCount <= 0) {
            Serial.println("No messages to display!");
        }
        return;
    }

    // Debug output every 10 pixels of movement
    if (debug && currentMessage < messageCount) {
        static int lastTextX = -1;
        if (lastTextX != textX && textX % 10 == 0) {
            Serial.print("Text pos: ");
            Serial.print(textX);
            Serial.print(", Message: ");
            Serial.println(messages[currentMessage]);
        }
        lastTextX = textX;
    }

    // Clear the display
    textMatrix.fillScreen(0);
    
    // Set cursor position for scrolling text
    textMatrix.setCursor(textX, 0);
    
    // Print the current message
    textMatrix.print(messages[currentMessage]);
    
    // Update the display
    textMatrix.show();
    
    // Move text one pixel to the left
    textX--;
    
    // Check if we've scrolled completely off screen
    if (textX < -maxDisplacement) {
        // Reset position for next message
        textX = textMatrix.width();
        
        // Text has completed a full scroll cycle for current message
        currentMessage++;
        
        Serial.print("Finished message ");
        Serial.print(currentMessage);
        Serial.print(" of ");
        Serial.println(messageCount);
        
        // Check if all messages have been displayed
        if (currentMessage >= messageCount) {
            textScrollComplete = true;
            Serial.println("*** Text scroll completed for this pair ***");
            
            // Clear text display
            textMatrix.fillScreen(0);
            textMatrix.show();
            
            // If GIF has looped at least once, advance to next pair
            if (gifLoopCount > 0) {
                forceNextPair = true;
            }
        } else {
            // Set up for next message
            maxDisplacement = strlen(messages[currentMessage]) * PIXEL_PER_CHAR + textMatrix.width();
            Serial.print("Next message: ");
            Serial.println(messages[currentMessage]);
        }
    }
}

/**
 * Move to the next GIF/text pair
 */
void advanceToNextPair() {
    Serial.println("\n*** Advancing to next pair ***");
    
    // Move to next pair (loop back to 1 after reaching MAX_PAIRS)
    currentPair = (currentPair % MAX_PAIRS) + 1;
    Serial.print("Loading pair #");
    Serial.println(currentPair);
    
    // Load next pair
    loadPair(currentPair);
}

/**
 * Main loop - runs continuously after setup()
 */
void loop() {
    // Get current time for timing operations
    unsigned long currentMillis = millis();
    
    // Check for serial commands
    if (Serial.available() > 0) {
        String command = Serial.readStringUntil('\n');
        
        // Process different commands
        if (command.startsWith("pair=")) {
            // Set specific pair (e.g., "pair=2")
            int pair = command.substring(5).toInt();
            if (pair >= 1 && pair <= MAX_PAIRS) {
                currentPair = pair;
                loadPair(currentPair);
            }
        } else if (command == "list") {
            // List SD card files
            listFiles();
        } else if (command == "debug") {
            // Toggle debug mode
            debug = !debug;
            Serial.print("Debug mode ");
            Serial.println(debug ? "ON" : "OFF");
        } else if (command == "next") {
            // Force move to next pair
            forceNextPair = true;
        } else if (command == "status") {
            // Print current status
            Serial.println("\n*** Status Report ***");
            Serial.print("Current pair: ");
            Serial.println(currentPair);
            Serial.print("Current message: ");
            Serial.print(currentMessage);
            Serial.print(" of ");
            Serial.println(messageCount);
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
    
    // Update GIF display at specified interval
    if (currentMillis - lastGifUpdate >= GIF_FRAME_DELAY) {
        displayGifFrame();
        lastGifUpdate = currentMillis;
    }
    
    // Small delay to prevent NeoPixel conflicts
    delay(5);
    
    // Update text display at specified interval
    if (currentMillis - lastTextUpdate >= TEXT_SCROLL_DELAY) {
        // Continue scrolling until all messages are displayed
        if (!textScrollComplete || currentMessage < messageCount) {
            scrollText();
        }
        lastTextUpdate = currentMillis;
    }
}

/**
 * Clean up resources when program exits
 * (This is rarely called on ESP32, but good practice)
 */
void cleanup() {
    // Close files
    if (gifFile) gifFile.close();
    
    // Free allocated memory
    for (int i = 0; i < messageCount; i++) {
        if (messages[i] != NULL) {
            free(messages[i]);
        }
    }
}

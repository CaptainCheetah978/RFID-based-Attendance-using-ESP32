#include <SPI.h>
#include <MFRC522.h>

#define SS_PIN 21
#define RST_PIN 22

MFRC522 mfrc522(SS_PIN, RST_PIN);   // Create MFRC522 instance
MFRC522::MIFARE_Key key;

String inputName = "";
String inputID   = "";
bool readyToWrite = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  // Default key for most cards
  for (byte i = 0; i < 6; i++) key.keyByte[i] = 0xFF;

  Serial.println("=== RFID Card Writer ===");
  Serial.println("Enter name and ID in format:");
  Serial.println("NAME:ID");
  Serial.println("Example -> Dr. Smith:EMP001");
  Serial.println("--------------------------------");
}

void loop() {
  // Check if Serial Monitor has input
  if (Serial.available()) {
    String line = Serial.readStringUntil('\n');
    line.trim();

    int sep = line.indexOf(':');
    if (sep > 0 && sep < line.length()-1) {   // ensure both NAME and ID exist
    inputName = line.substring(0, sep);
    inputID   = line.substring(sep + 1);
    if (inputName.length() > 0 && inputID.length() > 0) {
        readyToWrite = true;
        Serial.println("✅ Data captured:");
        Serial.println("  Name: " + inputName);
        Serial.println("  ID  : " + inputID);
        Serial.println("Now place a card on the reader...");
    } else {
        Serial.println("⚠️ Invalid format! Use NAME:ID");
    }
}

  // Wait for card if data is ready
  if (readyToWrite) {
    if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
      return;

    // Write to card
    writeBlock(4, inputName); // block 4 = Name
    writeBlock(8, inputID);   // block 8 = ID

    Serial.println("✅ Data written successfully!");
    readyToWrite = false;
    Serial.println("Enter new NAME:ID for another card...");
    Serial.println("--------------------------------");

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    delay(2000);
  }
}

void writeBlock(int blockAddr, String data) {
  byte buffer[16];
  data.getBytes(buffer, 16); // Convert to byte array

  // Authenticate block
  MFRC522::StatusCode status = mfrc522.PCD_Authenticate(
    MFRC522::PICC_CMD_MF_AUTH_KEY_A,
    blockAddr,
    &key,
    &(mfrc522.uid)
  );
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Auth failed for block "); Serial.println(blockAddr);
    return;
  }

  // Write data
  status = mfrc522.MIFARE_Write(blockAddr, buffer, 16);
  if (status != MFRC522::STATUS_OK) {
    Serial.print("Write failed for block "); Serial.println(blockAddr);
  } else {
    Serial.print("✔ Block "); Serial.print(blockAddr); Serial.println(" written.");
  }
}

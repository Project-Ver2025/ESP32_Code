# ESP32 Sense
This repository contains the code for the ESP32 device. The repository contains:
- Archive (Older versions of the C code used on the ESP devices)
- (MVP) DFRobot_AI_Cam_Board (The C code version which was deployed on the AI cam: https://www.dfrobot.com/product-2899.html?srsltid=AfmBOoqT_r_T8-earYZpYft3Byd_v7P71BHVbjNUCMe3K5Vs7LF2y2Gu)
- ESP32_S3 Sense (The current C code deployed on the ESP32 S3 Xiao Sense board: https://core-electronics.com.au/seeed-studio-xiao-esp32s3-54467.html)

## Electronics
Microprocessor: [Seeed Studio XIAO ESP32 S3 SENSE](https://core-electronics.com.au/seeed-studio-xiao-esp32s3-54467.html)
Camera: [OV5640](https://core-electronics.com.au/ov5640-camera-xiao-esp32s3-sense.html)
Battery: [1000mAh LiPo Battery](https://ecocell.com.au/product/lipo-1000-603450/)

Button Microcontroller: [ESP32S3](https://www.amazon.com.au/ESP32S3-2-4GHz-Wi-Fi-Dual-core-Supported-Efficiency-Interface/dp/B0BYSB66S5)
Battery: [700mAh LiPo Battery](https://ecocell.com.au/product/lipo-700-603040/)
Buttons: [Mini Push Buttons](https://core-electronics.com.au/mini-push-button-switch-5-pcs.html)

## Circuitry
<img width="465" height="549" alt="image" src="https://github.com/user-attachments/assets/659d60f8-7e1c-40dd-9018-296f000b80e7" />
<img width="390" height="480" alt="image" src="https://github.com/user-attachments/assets/0f0b1670-2991-4890-a345-4c98b92a9bec" />

## Software 
The software on the device uses two external companies hosted LLMs, Groq and Gemini. Groq is used in the:
-	Conversion of the speech to text (Whisper Turbo Model)
-	Classification of text to a task number (Llama 3.3 70B)
-	Image description task (Llama-4-maverick-17b / Llama-4-scout-17b)
-	Text reading task (Llama-4-maverick-17b / Llama-4-scout-17b)
-	Object searching task (Llama-4-maverick-17b / Llama-4-scout-17b)
-	Help task (Llama-4-maverick-17b / Llama-4-scout-17b)

Gemini is used in the:
-	Question answering task (which has access to google when needed)

The device uses the Android TTS library to output the text to the phone speaker or the connected headphones through the [Project Ver app](https://github.com/Project-Ver2025/Project_Ver). 

<img width="1541" height="843" alt="image" src="https://github.com/user-attachments/assets/d2d69e1e-8a1f-417e-903d-439c758f4df9" />


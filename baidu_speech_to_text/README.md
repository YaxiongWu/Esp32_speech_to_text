

## Usage

Prepare the audio board:

- Connect speakers or headphones to the board. 

Configure the example:

- Select compatible audio board in `menuconfig` > `Audio HAL`
- Get the Google Cloud API Key: https://cloud.baidu.com/ 
- Enter BAIDU_ACCESS_KEY and BAIDU_SECRET_KEY , Wi-Fi `ssid` and `password` in `menuconfig` > `Example Configuration`.


Load and run the example:

 - Wait for Wi-Fi network connection.
 - Press [Rec] button, and wait for **Red** LED blinking or ` Start speaking now` yellow line in terminal.
 - Speak something in Chinese. 
 - After finish, release the [Rec] button. Wait a second the text for the speech will print in terminal.
 - To stop the pipeline press [Mode] button on the audio board.

// Strings for fullscreen errors
const char errorOpenSPIFFSString[]      = "Critical Error\nOpening SPIFFS";
const char errorOpenConfigWriteString[] = "Critical Error\nOpening config\nfor writing";
const char errorOpenConfigReadString[]  = "Error\nOpening config\ntry Setup";
const char errorWifiConnectString[]     = "Error\nConnecting to\nWifi";
const char errorNTPString[]             = "Error\nGetting NTP\nEnter it\nmanually";

// Strings for Startup Menu
const char menuTitleString[]      = "Startup Menu";
const char menuNormalModeString[] = "Normal Op.";
const char menuSetupWifiString[]  = "Setup Wifi";
const char menuOTAUpdateString[]  = "OTA Update";

// Strings for Setup Wifi
const char setupWifiSSIDAndPassString[]    = "SSID and pass.";
const char setupWifiIPString[]             = "IP";
const char serverNotAllArgsPresentString[] = "Not all the arguments are present";
const char serverReceivedArgsString[]      = "OK";
const char gotCredentialsString[]          = "Got credetials";

// Strings for OTA Update
const char updateWaitingString[]        = "Waiting for\nupdate";
const char updateStartedString[]        = "Update Started";
const char updateProgressFormatString[] = "Progress: %u%%\n";
const char updateEndedString[]          = "Update Ended";
const char updateErrorAuthString[]      = "Auth Error";
const char updateErrorBeginString[]     = "Begin Error";
const char updateErrorConnectString[]   = "Connect Error";
const char updateErrorReceiveString[]   = "Receive Error";
const char updateErrorEndString[]       = "End Error";

// Strings for Manual Time
const char manualTimeTitleString[]  = "Manual Time";
const char manualTimeFormatString[] = "%.2d";         // Used for hour, minute, date and month

// Strings for Temporary Schedule
const char temporaryScheduleTitleString[]            = "Temp. schedule";
const char temporaryScheduleTempFormatString[]       = "Temp:%.1fC\n";
const char temporaryScheduleDuration1FormatString[]  = "Duration:%dm\n";
const char temporaryScheduleDuration2FormatString[]  = "Duration:%.1fh\n";
const char temporaryScheduleDurationInfiniteString[] = "Duration:inf.\n";
const char temporaryScheduleOkString[]               = "OK";
const char temporaryScheduleCancelString[]           = "CANCEL";
const char temporaryScheduleDeleteString[]           = "DEL";

// Strings for Normal Operation
const char waitingForWifiString[]              = "Trying to\nconnect to\nWifi";
const char waitingForNTPString[]               = "Trying to get\nNTP time";
const char waitingForFirebaseString[]          = "Trying to\nconnect to\nFirebase";
const char gotTimeString[]                     = "Got Time:";
const char gotTimeHourFormatString[]           = "%02d:%02d\n";                      // Hour:Minute
const char gotTimeDateFormatString[]           = "%02d.%02d.%4d\n";                  // Day.Month.Year
const char displayHumidityNotAvailableString[] = "N\\A";
const char displayHumidityFormatString[]       = "%.2d%%";                           // Humidity%
const char displayDateLine1FormatString[]      = "%s. %d\n";                         // Short weekday. Day
const char displayDateLine2FormatString[]      = "%.2d.%d";                          // Month.Year
const char displayTempDegreeSymbol             = char(247);                          // °
const char displayTempLetter                   = 'C';                                // Celsius
const char displayClockFormatString[]          = "%.2d:%.2d";                        // Hour:Minute
const char displayErrorWifiString[]            = "!W";
const char displayErrorNTPString[]             = "!N";
const char displayErrorFirebaseString[]        = "!F";
const char displayErrorTemperatureString[]     = "!T";
const char displayErrorHumidityString[]        = "!H";
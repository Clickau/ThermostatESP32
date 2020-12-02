// Strings for fullscreen errors
const char errorSettingsNotFound[]  = "Settings not\nfound\nEntering Setup";
const char errorSetupServer[]       = "Error starting server";
const char errorWifiConnectString[] = "Error\nConnecting to\nWifi";
const char errorNTPString[]         = "Error\nGetting NTP\nEnter it\nmanually";

// Strings for Startup Menu
const char menuTitleString[]      = "Startup Menu";
const char menuNormalModeString[] = "Normal Op.";
const char menuSetupString[]      = "Setup";

// Strings for Setup
const char setupSSIDPassString[]         = "SSID and pass.";
const char setupIPString[]               = "IP";
const char setupReceivedSettingsString[] = "Received\nsettings";
const char setupRestartingString[]       = "Restarting";

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
const char gotTimeHourFormatString[]           = "%02d:%02d\n";                                      // Hour:Minute
const char gotTimeDateFormatString[]           = "%02d.%02d.%4d\n";                                  // Day.Month.Year
const char displayHumidityNotAvailableString[] = "N/A";
const char displayHumidityFormatString[]       = "%.2d%%";                                           // Humidity%
const char displayDateLine1FormatString[]      = "%s. %d\n";                                         // Short weekday. Day
const char displayDateLine2FormatString[]      = "%.2d.%d";                                          // Month.Year
const char *displayShortWeekdayStrings[]       = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
const char displayTempLetter                   = 'C';                                                // Celsius
const char displayClockFormatString[]          = "%.2d:%.2d";                                        // Hour:Minute
const char displayErrorWifiString[]            = "!W";
const char displayErrorNTPString[]             = "!N";
const char displayErrorFirebaseString[]        = "!F";
const char displayErrorSensorString[]          = "!S";
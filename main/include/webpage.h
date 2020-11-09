const char setupWifiPage[] = R"=====(
<!DOCTYPE html>
<html>
    <head>
        <title>Wifi Setup Form</title>
    </head>
    <body>
        <form action="/post" method="post">
            <label for="ssid_input">SSID:</label><br>
            <input type="text" name="ssid" id="ssid_input"><br>
            <label for="password_input">Password:</label><br>
            <input type="password" name="password" id="password_input"><br>
            <input type="submit" value="Submit">
        </form>
    </body>
</html>
)=====";
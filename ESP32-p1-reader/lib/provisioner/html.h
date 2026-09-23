#pragma once

const char CONFIG_PAGE[] PROGMEM = R"(
    <!DOCTYPE HTML>
    <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
        </head>
        <body>
            <form action="/save">
                SSID: <input name="ssid"><br>
                PASS: <input name="pass"><br>
                <input type="submit" value="Save">
            </form>
        </body>
    </html>
)";

const char SAVED_PAGE[] PROGMEM = R"(
    <!DOCTYPE HTML>
    <html>
        <head>
            <meta name="viewport" content="width=device-width, initial-scale=1.0">
            <style>
                div {
                    color: green;
                }
            </style>
        </head>
        <body>
            <div>your credentials have been saved</div>
        </body>
    </html>
)";
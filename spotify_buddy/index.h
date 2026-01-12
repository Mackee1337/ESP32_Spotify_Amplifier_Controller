const char mainPage[] PROGMEM = R"=====(
<HTML>
  <HEAD><TITLE>ESP32 Spotify Login</TITLE></HEAD>
  <BODY>
    <CENTER>
      <H2>Spotify Buddy — Logga in</H2>
      <a id="link" href="%s">Log in to Spotify</a>
    </CENTER>
  </BODY>
</HTML>
)=====";


const char errorPage[] PROGMEM = R"=====(
<HTML>
    <HEAD>
        <TITLE>Spotify Error</TITLE>
    </HEAD>
    <BODY>
        <CENTER>
            <B>Ett fel uppstod...</B><br><br>
            <a href="%s">Försök igen</a>
        </CENTER>
    </BODY>
</HTML>
)=====";

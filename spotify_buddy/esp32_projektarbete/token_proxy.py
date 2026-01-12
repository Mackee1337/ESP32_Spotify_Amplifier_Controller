# token_proxy.py
from flask import Flask, jsonify
import requests
import time

# Fyll i dina värden här:
CLIENT_ID = ""       # din Client ID
# kopiera från Spotify Dashboard
CLIENT_SECRET = ""
REFRESH_TOKEN = ""

app = Flask(__name__)
_cached = {"access_token": None, "expires_at": 0}


def refresh_access_token():
    resp = requests.post("https://accounts.spotify.com/api/token", data={
        "grant_type": "refresh_token",
        "refresh_token": REFRESH_TOKEN
    }, auth=(CLIENT_ID, CLIENT_SECRET))
    r = resp.json()
    if "access_token" not in r:
        print("Error refreshing token:", r)
        raise SystemExit("Failed to refresh token")
    _cached["access_token"] = r.get("access_token")
    _cached["expires_at"] = time.time() + r.get("expires_in", 3600) - 10
    return _cached["access_token"]


@app.route("/token")
def token():
    if not _cached["access_token"] or time.time() > _cached["expires_at"]:
        refresh_access_token()
    return jsonify({"access_token": _cached["access_token"]})


if __name__ == "__main__":
    # Kör på alla interfaces så ESP32 kan nå via din PC:s lokala IP
    app.run(host="0.0.0.0", port=5000)

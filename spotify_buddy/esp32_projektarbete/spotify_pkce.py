
import base64
import hashlib
import os
import webbrowser
import http.server
import urllib.parse
import requests
import threading
import time
import json
import sys

CLIENT_ID = ""  # ditt client id
REDIRECT_URI = ""
SCOPES = "user-read-playback-state user-modify-playback-state user-read-currently-playing"


def make_pkce_pair():
    verifier = base64.urlsafe_b64encode(
        os.urandom(40)).rstrip(b'=').decode('utf-8')
    challenge = base64.urlsafe_b64encode(hashlib.sha256(
        verifier.encode()).digest()).rstrip(b'=').decode('utf-8')
    return verifier, challenge


verifier, challenge = make_pkce_pair()

auth_url = (
    "https://accounts.spotify.com/authorize?"
    f"response_type=code&client_id={CLIENT_ID}"
    f"&redirect_uri={urllib.parse.quote(REDIRECT_URI, safe='')}"
    f"&scope={urllib.parse.quote(SCOPES, safe='')}"
    f"&code_challenge_method=S256&code_challenge={challenge}"
)

code_storage = {}


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, format, *args):
        return  # tysta logs

    def do_GET(self):
        qs = urllib.parse.urlparse(self.path).query
        params = urllib.parse.parse_qs(qs)
        if 'code' in params:
            code_storage['code'] = params['code'][0]
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b"Authorized. You can close this window.")
        else:
            self.send_response(400)
            self.end_headers()
            self.wfile.write(b"No code found.")


# start local server
server = http.server.HTTPServer(('127.0.0.1', 1410), Handler)
threading.Thread(target=server.serve_forever, daemon=True).start()

print("\n=== AUTH URL (open this in your browser if it doesn't open automatically) ===\n")
print(auth_url)
print("\nAttempting to open your default browser...")

opened = False
try:
    opened = webbrowser.open(auth_url, new=1)
except Exception as e:
    print("webbrowser.open failed:", e)

if not opened:
    try:
        opened = webbrowser.open_new_tab(auth_url)
    except Exception:
        opened = False

# Windows-specific fallback
if not opened and sys.platform.startswith("win"):
    try:
        os.startfile(auth_url)
        opened = True
    except Exception as e:
        print("os.startfile failed:", e)

if opened:
    print("Browser opened (or the system attempted to). If you don't see it, paste the AUTH URL into your browser manually.")
else:
    print("Could not open browser automatically. Please COPY the AUTH URL above and PASTE it into your browser manually.")

print("\nAfter allowing access in the browser, this script will wait up to 300 seconds for the callback...")

t0 = time.time()
while 'code' not in code_storage and time.time()-t0 < 300:
    time.sleep(0.5)
server.shutdown()

if 'code' not in code_storage:
    print("\nNo code received within 300s. If you pasted the URL, make sure you clicked Allow and that the browser tried to open:")
    print(" - Check that the browser address became: http://127.0.0.1:1410/callback?code=...")
    sys.exit(1)

code = code_storage['code']
print("\nGot code, exchanging for tokens...")

token_resp = requests.post("https://accounts.spotify.com/api/token", data={
    "grant_type": "authorization_code",
    "code": code,
    "redirect_uri": REDIRECT_URI,
    "client_id": CLIENT_ID,
    "code_verifier": verifier
})
j = token_resp.json()
print("\nToken response:\n", json.dumps(j, indent=2))
print("\nCOPY the value of \"refresh_token\" and save it for token_proxy.py")

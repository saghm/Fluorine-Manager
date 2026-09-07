#!/usr/bin/env python3
"""Experimental Steam client cloud capability probe (requires websocket-client).

Steam must be running with -cef-enable-debugging. The default operation only
inspects capabilities. --sync-down and --sync-up request the client's pre-launch
and post-game sync respectively. They can overwrite local or remote saves: back
up first and use only when the game is closed and save-folder mapping is correct.
--retry invokes the UI retry operation, which may do nothing when already synced.
An accepted command never implies sync completion.
No cookies, tokens, or cloud download URLs are printed.
"""

import argparse
import json
import urllib.parse
import urllib.request
import sys

import websocket


def evaluate(expression):
    with urllib.request.urlopen("http://127.0.0.1:8080/json/list", timeout=5) as response:
        targets = json.load(response)
    target = next((t for t in targets if t.get("title") == "SharedJSContext"), None)
    if target is None:
        raise RuntimeError("Steam's shared UI context is unavailable")
    url = target["webSocketDebuggerUrl"]
    parsed = urllib.parse.urlparse(url)
    if parsed.scheme != "ws" or parsed.hostname not in ("127.0.0.1", "localhost", "::1"):
        raise ValueError("Steam returned a non-local debugging endpoint")
    connection = websocket.create_connection(url, timeout=10, suppress_origin=True)
    try:
        connection.send(json.dumps({"id": 1, "method": "Runtime.evaluate", "params": {
            "expression": expression, "returnByValue": True, "awaitPromise": True,
        }}))
        while True:
            reply = json.loads(connection.recv())
            if reply.get("id") == 1:
                if "error" in reply or "exceptionDetails" in reply.get("result", {}):
                    raise RuntimeError("Steam debugging evaluation failed")
                return reply["result"]["result"].get("value")
    finally:
        connection.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("appid", type=int)
    actions = parser.add_mutually_exclusive_group()
    actions.add_argument("--retry", action="store_true", help="Request a real cloud sync")
    actions.add_argument("--status", action="store_true", help="Read fresh Steam cloud status")
    actions.add_argument("--sync-down", action="store_true",
                         help="Run pre-launch sync (may overwrite local saves)")
    actions.add_argument("--sync-up", action="store_true",
                         help="Run post-game sync (may overwrite cloud saves)")
    args = parser.parse_args()
    if not 0 < args.appid <= 0xFFFFFFFF:
        parser.error("appid must be a positive 32-bit Steam app ID")
    if args.sync_down or args.sync_up:
        command = "cloud_sync_down" if args.sync_down else "cloud_sync_up"
        print(json.dumps(evaluate(
            f'(() => {{ SteamClient.Console.ExecCommand("{command} {args.appid}"); '
            'return {requested: true, completionVerified: false}; })()'
        ), indent=2))
    elif args.retry:
        print(json.dumps(evaluate(
            f"(() => {{ SteamClient.Cloud.RetryAppSync({args.appid}); "
            "return {requested: true, completionVerified: false}; })()"
        ), indent=2))
    elif args.status:
        print(json.dumps(evaluate("""new Promise((resolve, reject) => {
            let registration;
            const timeout = setTimeout(() => {
                registration?.unregister(); reject(new Error('Steam status timeout'));
            }, 5000);
            registration = SteamClient.Apps.RegisterForAppDetails(APPID, d => {
                clearTimeout(timeout);
                setTimeout(() => registration?.unregister(), 0);
                resolve({appid: d.unAppID, available: d.bCloudAvailable,
                    accountEnabled: d.bCloudEnabledForAccount,
                    appEnabled: d.bCloudEnabledForApp, status: d.eCloudStatus,
                    progress: d.nCloudProgressPercent, completionVerified: false});
            });
        })""".replace("APPID", str(args.appid))), indent=2))
    else:
        print(json.dumps(evaluate("""(() => ({
            cloudMethods: Object.keys(SteamClient.Cloud || {}),
            appMethods: Object.keys(SteamClient.Apps || {}).filter(k => /Detail|Overview|Cloud|Running/.test(k)),
            stores: Object.keys(window).filter(k => /app.*store|cloud|login/i.test(k))
        }))()"""), indent=2))


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, websocket.WebSocketException):
        # Network exceptions can include endpoints or response bodies. Keep the
        # diagnostic output free of credentials and authenticated page content.
        print("Steam cloud probe failed. Check that Steam is running with "
              "-cef-enable-debugging and its library UI is available.", file=sys.stderr)
        sys.exit(1)

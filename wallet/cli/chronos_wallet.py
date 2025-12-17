#!/usr/bin/env python3
import json, os, sys, time
import requests
import blake3

HRP = "cqc"

def bech32m_encode(hrp: str, data: bytes) -> str:
    # importeer je bech32m uit je eerdere PoC of implementeer hier
    raise NotImplementedError

def addr_from_pub(pub: bytes) -> str:
    h = blake3.blake3(pub).digest()
    return bech32m_encode(HRP, h[:20])

def rpc(method, params=None):
    body = {"jsonrpc":"2.0","id":1,"method":method,"params":params or {}}
    r = requests.post("http://127.0.0.1:8646", json=body, timeout=5)
    r.raise_for_status()
    return r.json()["result"]

def cmd_balance(addr):
    print("Balance:", rpc("get_balance", {"address": addr}))

def cmd_send(frm, to, amount_nanos, memo=""):
    tx = {"from": frm, "to": to, "amount": amount_nanos, "memo": memo}
    res = rpc("send_tx", tx)
    print("TxID:", res["tx_id"])

if __name__ == "__main__":
    # demo
    if len(sys.argv) < 2:
        print("usage: chronos_wallet.py balance <addr> | send <from> <to> <nanos>")
        sys.exit(1)
    cmd = sys.argv[1]
    if cmd == "balance" and len(sys.argv) == 3: 
        cmd_balance(sys.argv[2])
    elif cmd == "send" and len(sys.argv) == 6:
        cmd_send(sys.argv[2], sys.argv[3], int(sys.argv[4]), sys.argv[5])
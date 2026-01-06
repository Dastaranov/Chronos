#!/usr/bin/env python3
import requests
import json
import argparse
import time
from collections import deque

def get_peers(rpc_url):
    payload = {
        "jsonrpc": "2.0",
        "method": "get_peers",
        "params": {},
        "id": 1
    }
    try:
        response = requests.post(rpc_url, json=payload, timeout=2)
        if response.status_code == 200:
            result = response.json()
            if "result" in result:
                return result["result"]
    except Exception as e:
        print(f"Error connecting to {rpc_url}: {e}")
    return []

def get_status(rpc_url):
    payload = {
        "jsonrpc": "2.0",
        "method": "get_status",
        "params": {},
        "id": 1
    }
    try:
        response = requests.post(rpc_url, json=payload, timeout=2)
        if response.status_code == 200:
            result = response.json()
            if "result" in result:
                return result["result"]
    except Exception as e:
        pass
    return None

def crawl_network(seed_url):
    visited = set()
    queue = deque([seed_url])
    nodes = {}
    edges = []

    print(f"Starting crawl from {seed_url}...")

    while queue:
        url = queue.popleft()
        if url in visited:
            continue
        visited.add(url)

        print(f"Scanning {url}...")
        
        # Get node status to identify itself
        status = get_status(url)
        if not status:
            continue
            
        node_id = status.get("node_id", "unknown")
        nodes[node_id] = {
            "id": node_id,
            "url": url,
            "height": status.get("current_block_height", 0),
            "peers_count": status.get("connected_peers_count", 0)
        }

        # Get peers
        peers = get_peers(url)
        for peer in peers:
            peer_id = peer.get("node_id")
            peer_addr = peer.get("address")
            
            # Add edge
            edges.append({
                "source": node_id,
                "target": peer_id,
                "score": peer.get("score", 0)
            })

            # Try to guess RPC URL from peer address (assuming default port 8080 for now or same as seed)
            # In a real scenario, we might need a way to discover RPC ports.
            # For this simulation, we assume peers might be running locally on different ports
            # or we just map the topology without crawling deeper if we can't guess the RPC port.
            
            # If peer_addr is like "127.0.0.1:9000" (P2P), we don't know RPC port.
            # So we just record the node existence.
            if peer_id not in nodes:
                nodes[peer_id] = {
                    "id": peer_id,
                    "address": peer_addr,
                    "height": peer.get("current_block_height", 0),
                    "inferred": True
                }

    return {"nodes": list(nodes.values()), "edges": edges}

def generate_html(graph_data, output_file="network_graph.html"):
    html_template = """
<!DOCTYPE html>
<html>
<head>
    <title>Chronos Network Graph</title>
    <script type="text/javascript" src="https://unpkg.com/vis-network/standalone/umd/vis-network.min.js"></script>
    <style type="text/css">
        #mynetwork {
            width: 100%;
            height: 800px;
            border: 1px solid lightgray;
        }
    </style>
</head>
<body>
    <h2>Chronos Network Topology</h2>
    <div id="mynetwork"></div>
    <script type="text/javascript">
        var nodes = new vis.DataSet(%NODES%);
        var edges = new vis.DataSet(%EDGES%);

        var container = document.getElementById('mynetwork');
        var data = {
            nodes: nodes,
            edges: edges
        };
        var options = {
            nodes: {
                shape: 'dot',
                size: 16,
                font: {
                    size: 12,
                    color: '#000000'
                },
                borderWidth: 2
            },
            edges: {
                width: 1,
                color: { inherit: 'from' },
                smooth: {
                    type: 'continuous'
                }
            },
            physics: {
                stabilization: false,
                barnesHut: {
                    gravitationalConstant: -8000,
                    springConstant: 0.04,
                    springLength: 95
                }
            }
        };
        var network = new vis.Network(container, data, options);
    </script>
</body>
</html>
"""
    
    vis_nodes = []
    for node in graph_data["nodes"]:
        color = "#97C2FC" # Default blue
        if node.get("inferred"):
            color = "#E0E0E0" # Grey for inferred
        
        label = node["id"][:8] + "..."
        title = f"ID: {node['id']}<br>Height: {node.get('height')}<br>Address: {node.get('address', 'N/A')}"
        
        vis_nodes.append({
            "id": node["id"],
            "label": label,
            "title": title,
            "color": color
        })

    vis_edges = []
    for edge in graph_data["edges"]:
        vis_edges.append({
            "from": edge["source"],
            "to": edge["target"],
            "title": f"Score: {edge['score']}"
        })

    html_content = html_template.replace("%NODES%", json.dumps(vis_nodes)).replace("%EDGES%", json.dumps(vis_edges))
    
    with open(output_file, "w") as f:
        f.write(html_content)
    print(f"Graph saved to {output_file}")

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Chronos Network Visualizer")
    parser.add_argument("--seed", default="http://127.0.0.1:8080", help="Seed node RPC URL")
    parser.add_argument("--output", default="network_graph.html", help="Output HTML file")
    args = parser.parse_args()

    graph_data = crawl_network(args.seed)
    generate_html(graph_data, args.output)

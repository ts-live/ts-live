import json
import os
import requests
import yaml

def get_mirakc_services(mirakc_url):
    try:
        response = requests.get(f"{mirakc_url}/api/services")
        response.raise_for_status()
        return response.json()
    except requests.RequestException as e:
        print(f"Error fetching services: {e}")
        return []

def gen_mirakc_config(mirakc_url, config_path):
    services = get_mirakc_services(mirakc_url)
    if not services:
        print("No services found or unable to fetch services.")
        return
    
    curl_url = "/api/channels/{{{channel_type}}}/{{{channel}}}/stream?decode=1"

    config = {
        "server": {
            "addrs": [
                {"http": "0.0.0.0:40772"},
            ]
        },
        "epg": {
            "cache-dir": "/var/lib/mirakc/epg",
        },
        "channels": [
            {
                "name": service["name"],
                "type": service["channel"]["type"],
                "channel": service["channel"]["channel"],
                "disabled": False,
            } for service in services
        ],
        "tuners": [
            {
                "name": "upstream",
                "types": ["GR", "BS"],
                "command": f"curl -s {mirakc_url}{curl_url}",
                "disabled": False,
            }
        ]
    }

    with open(config_path, 'w') as config_file:
        yaml.safe_dump(config, config_file, indent=2, allow_unicode=True, default_flow_style=False)
    print(f"Configuration saved to {config_path}")

if __name__ == "__main__":
    mirakc_url = os.getenv("MIRAKC_URL", "http://localhost:40772")
    config_path = os.getenv("MIRAKC_CONFIG_PATH", "/etc/mirakc/config.json")
    
    gen_mirakc_config(mirakc_url, config_path)
    print(f"Mirakc configuration generated at {config_path}")

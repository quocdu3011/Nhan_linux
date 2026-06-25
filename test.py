import requests
import json

API_KEY = "sk-QpurNz5d4bisu665Z6MlvxAEuKeKUYG9HKcZD5GXSruGIMOy"

url = "https://luongchidung.online/v1"

headers = {
    "x-api-key": API_KEY,
    "anthropic-version": "2023-06-01",
    "content-type": "application/json"
}

payload = {
    "model": "claude-opus-4-7",
    "max_tokens": 1024,
    "messages": [
        {
            "role": "user",
            "content": "Xin chào, hãy giới thiệu về bản thân."
        }
    ]
}

response = requests.post(
    url,
    headers=headers,
    json=payload
)
print("Status:", response.status_code)
print("Headers:", response.headers)
print("Text:", repr(response.text))
print("Status:", response.status_code)
print(json.dumps(response.json(), indent=4, ensure_ascii=False))

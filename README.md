## Tests
To set the pump **duty cycle**, use the following `curl` commands, replacing `${ESP_IP}` with the actual IP address dynamically.

### **1. Set Duty Cycle (Persistent)**
To set a **permanent** duty cycle (e.g., **50%**):
```sh
curl -X POST http://${ESP_IP}/pump -H "Content-Type: application/json" -d '{"duty": 50.0}'
```

### **2. Set Duty Cycle for a Limited Time**
To set a **temporary** duty cycle (e.g., **60% for 5000 ms**):
```sh
curl -X POST http://${ESP_IP}/pump -H "Content-Type: application/json" -d '{"duty": 60.0, "period": 5000}'
```

#### **Notes**
- The `"duty"` value represents the percentage of the pump’s cycle (e.g., `50.0` for **50%**).
- If `"period"` is **not** provided, the duty cycle is set **permanently**.
- If `"period"` **is** provided (in milliseconds), the pump will return to **0%** after the duration expires.


### 1. **Set Invert**
To enable (`true`) or disable (`false`) the invert setting:
```sh
curl -X POST http://${ESP_IP}/signal -H "Content-Type: application/json" -d '{"invert": true}'
```
or to disable it:
```sh
curl -X POST http://${ESP_IP}/signal -H "Content-Type: application/json" -d '{"invert": false}'
```

### 2. **Set Frequency**
To set the frequency to a specific value (e.g., **5000 Hz**):
```sh
curl -X POST http://${ESP_IP}/signal -H "Content-Type: application/json" -d '{"frequency": 5000}'
```

### 3. **Set Both Invert and Frequency**
If you need to update **both** settings in one request:
```sh
curl -X POST http://${ESP_IP}/signal -H "Content-Type: application/json" -d '{"invert": true, "frequency": 5000}'
```

#### Notes:
- Replace `${ESP_IP}` with the actual IP address of your ESP-based web server.
- Ensure that the **frequency** value is greater than `0`, as negative or zero values will be rejected.
- The `"invert"` parameter expects a boolean (`true` or `false`).

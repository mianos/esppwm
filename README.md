# esppwm

Condenser pump controller for the still: an ESP32-C3 driving a MOSFET from a
PWM output on GPIO 2.

## Build and update

```sh
./build.sh
curl -X POST --data-binary @build/condensor.bin http://condenser/firmware   # OTA
curl http://condenser/firmware                                              # running version
```

An update boots pending verification and rolls back automatically unless it
reaches Wi-Fi within 120 s. `/healthz` reports duty, version and partition.

## API conventions

`POST /pump`, `POST /signal` and `GET /pump` all reply with the pump state,
`{"duty", "frequency", "invert"}`. Errors return `{"error", "statusCode"}`
with the same HTTP status (400 bad input, 404/405 unknown path or method,
413 body too large). Every POST is logged with the caller's address.

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
- If `"period"` **is** provided (in milliseconds), the pump returns to its previous duty afterwards. A timed duty is not stored or reported as the standing duty.
- At power-on the pump resumes the last standing duty before Wi-Fi connects.


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
- The **frequency** must be 1 to 9765 Hz (the 13-bit PWM limit); anything else is rejected and the output keeps its current frequency.
- The `"invert"` parameter expects a boolean (`true` or `false`).

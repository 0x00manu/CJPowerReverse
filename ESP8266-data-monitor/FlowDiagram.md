# ESP8266 controller protocol flow

```mermaid
flowchart TD
    Boot[ESP8266 boots] --> AP[Start Wi-Fi AP and web server]
    AP --> Due{Handshake attempt due?}
    Due -- No --> Web[Serve dashboard and handshake status]
    Web --> Due
    Due -- Yes --> ProbeBaud[Set UART to 57600 8N1]
    ProbeBaud --> Probes[Send six captured read-only 7E probes]
    Probes --> RegisterBaud[Set UART to 115200 8N1]
    RegisterBaud --> Identity[Send 5A A5 identity read]
    Identity --> IdentityReply{Complete response within 5 seconds?}
    IdentityReply -- No --> Fail[Handshake failed]
    IdentityReply -- Yes --> Validate{Identity frame and CRC valid?}
    Validate -- No --> Fail
    Fail --> Retry[Keep polling disabled and wait 5 seconds]
    Retry --> Due
    Validate -- Yes --> Success[Store identity and remain at 115200]
    Success --> Read[Read next dashboard register with 5A A5]
    Read --> Reply{Response within 220 ms?}
    Reply -- No --> RetryRead{Already retried once?}
    RetryRead -- No --> Read
    RetryRead -- Yes --> Drop[Discard partial snapshot]
    Reply -- Yes --> Check{Expected address, size and CRC valid?}
    Check -- No --> RetryRead
    Check -- Yes --> More{All 25 registers read?}
    More -- No --> Read
    More -- Yes --> Publish[Publish complete snapshot to /api/live]
    Publish --> Cycle[Wait until about 350 ms after cycle start]
    Drop --> Cycle
    Cycle --> Read
```

```mermaid
sequenceDiagram
    participant ESP as ESP8266
    participant C as Controller
    participant Web as Browser

    ESP->>C: Six 7E probes at 57600 baud
    Note over ESP,C: No probe response is required
    ESP->>C: 5A A5 identity request at 115200 baud
    C-->>ESP: Identity plus CRC
    alt identity valid
        loop 25 dashboard registers at 115200 baud
            ESP->>C: 5A A5 read with encoded address and size
            C-->>ESP: Same address, value and CRC
        end
        ESP-->>Web: Publish complete dashboard snapshot
    else timeout or invalid identity
        Note over ESP: Polling remains disabled; retry after 5 seconds
    end
```

The CRC-valid `5A A5` identity response is the handshake gate. The preceding
`7E` probes reproduce the Windows application, but the working capture contains
no response to them. After identification the UART remains at 115200, and the
dashboard uses only individual read-only `5A A5` register requests.

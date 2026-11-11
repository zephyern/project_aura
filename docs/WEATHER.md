# Outdoor weather (optional)

Off by default. When it is off, the Aura makes no weather requests and nothing
on the device or the web page changes except the "Outdoor Weather" settings
group itself.

## Setting it up

1. Get a free API key from [OpenWeatherMap](https://openweathermap.org/api).
   The Aura needs its own key; the free plan allows 60 calls a minute and
   1,000,000 a month, and the Aura makes one call every five minutes.
2. Open the Aura's web page, **System → Outdoor Weather**.
3. Paste the key. It is stored on the device and never sent back to the page
   or written to the log; the page only says "API key saved". To change it,
   type a new one; leaving the box empty keeps the saved key.
4. Pick **City name** (for example `Millburn,NJ`) or **ZIP / postal code**,
   type the 2-letter country code (required; there is no default country) and
   press **Find place**. Save the key first: the lookup uses it.
5. Pick the right match from the list, tick **Outdoor temperature from
   OpenWeatherMap** and press **Save weather settings**.

The lookup uses OpenWeatherMap geocoding (`geo/1.0/direct` with `limit=5`, or
`geo/1.0/zip`). Only the chosen match's latitude, longitude and label are saved.

## How it runs

- A check runs right after the settings are saved or the device starts, then on
  every five-minute mark of the clock (`:00`, `:05`, ...). Until the clock is
  set from the network it runs every five minutes from the last check.
- Each check is one HTTPS request to `data/2.5/weather` with `units=metric`, on
  its own task so the screen never waits for the network. The certificate is
  checked against the firmware's certificate bundle.
- Readings are kept in °C.
- Every successful reading logs the measurement time OpenWeatherMap reports
  (`dt`). That shows how often the free plan really refreshes, so the poll
  interval can be checked against it.

## When a check fails

There is no grace period: a failed check shows its reason at once and the old
value is dropped.

| What happened | Shown as |
|---|---|
| Wi-Fi not connected, or no answer (DNS, TLS, timeout) | No internet |
| HTTP 401 | Bad API key |
| Any other HTTP error (including 429) | Weather service error |
| Answer is not JSON, is cut off, or lacks `main.temp` / `dt` | Weather service error |
| Turned on without a key or a place | Not set up |

## Web API

| Route | Body | Notes |
|---|---|---|
| `GET /api/weather/settings` | — | `enabled`, `key_saved`, `location_set`, `place_label`/`lat`/`lon` when set, `status`, `status_text`, `temp_c`/`dt` when the last check worked. Never the key. |
| `POST /api/weather/settings` | `{"enabled": bool, "api_key"?: str, "location"?: {"lat", "lon", "label"}}` | `enabled` is required. Unknown fields are refused by name. One save: if it fails, nothing changes. |
| `POST /api/weather/geocode` | `{"kind": "city" \| "zip", "query": str, "country": str}` | All fields required. Needs a saved key (409 otherwise). Returns up to five `matches`. |

Both POST routes answer 503 while a firmware update is running.

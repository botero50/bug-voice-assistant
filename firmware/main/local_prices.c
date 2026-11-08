#include "cJSON.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "local_skills_internal.h"
#include "voice_clips.h"
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

// The dollar, the euro, bitcoin and ethereum, fetched by the board itself from free services with no key:
// exchange rates from open.er-api.com and crypto prices from CoinGecko. The last prices are kept for a while, so
// a short loss of internet still gets an answer.

static const char *TAG = "PRICES";

#define FRESH_SECONDS 300
#define STALE_SECONDS (6 * 3600)

static struct {
    time_t rates_at, crypto_at;
    double cop_per_usd, cop_per_eur, btc_usd, eth_usd;
} cache;

static bool fetch_rates(void) {
    char *body = ls_http_get("https://open.er-api.com/v6/latest/USD", true);
    if (!body) return false;
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    cJSON *rates = root ? cJSON_GetObjectItem(root, "rates") : NULL;
    cJSON *cop = rates ? cJSON_GetObjectItem(rates, "COP") : NULL;
    cJSON *eur = rates ? cJSON_GetObjectItem(rates, "EUR") : NULL;
    bool ok = cJSON_IsNumber(cop) && cJSON_IsNumber(eur) && eur->valuedouble > 0;
    if (ok) {
        cache.cop_per_usd = cop->valuedouble;
        cache.cop_per_eur = cop->valuedouble / eur->valuedouble;
        cache.rates_at = time(NULL);
    }
    cJSON_Delete(root);
    return ok;
}

static bool fetch_crypto(void) {
    char *body = ls_http_get("https://api.coingecko.com/api/v3/simple/price?ids=bitcoin,ethereum&vs_currencies=usd", true);
    if (!body) return false;
    cJSON *root = cJSON_Parse(body);
    heap_caps_free(body);
    cJSON *btc = root ? cJSON_GetObjectItem(root, "bitcoin") : NULL;
    cJSON *eth = root ? cJSON_GetObjectItem(root, "ethereum") : NULL;
    cJSON *btc_usd = btc ? cJSON_GetObjectItem(btc, "usd") : NULL;
    cJSON *eth_usd = eth ? cJSON_GetObjectItem(eth, "usd") : NULL;
    bool ok = cJSON_IsNumber(btc_usd) && cJSON_IsNumber(eth_usd);
    if (ok) {
        cache.btc_usd = btc_usd->valuedouble;
        cache.eth_usd = eth_usd->valuedouble;
        cache.crypto_at = time(NULL);
    }
    cJSON_Delete(root);
    return ok;
}

static bool has(const char *text, const char *word) {
    char padded[32];
    snprintf(padded, sizeof(padded), " %s ", word);
    return strstr(text, padded) != NULL;
}

static void group(long long n, char *out, size_t size) {
    char plain[24];
    snprintf(plain, sizeof(plain), "%lld", n);
    size_t len = strlen(plain), o = 0;
    for (size_t i = 0; i < len && o + 2 < size; i++) {
        if (i > 0 && (len - i) % 3 == 0) out[o++] = '.';
        out[o++] = plain[i];
    }
    out[o] = '\0';
}

bool local_prices_try(const char *text, local_answer_t *a) {
    int what = 0;  // 1 dollar, 2 euro, 3 bitcoin, 4 ethereum
    if (has(text, "dolar") || has(text, "dolares")) what = 1;
    else if (has(text, "euro") || has(text, "euros")) what = 2;
    else if (has(text, "bitcoin") || has(text, "bitcoins")) what = 3;
    else if (has(text, "ethereum") || has(text, "etherium") || has(text, "ether")) what = 4;
    if (!what) return false;
    // Only a question about the price, not any sentence that mentions the word
    if (!(has(text, "precio") || has(text, "cuanto") || has(text, "esta") || has(text, "vale") || has(text, "cotizacion") ||
          has(text, "cuesta") || has(text, "como") || has(text, "cual"))) {
        return false;
    }
    // "cuánto es 5 dólares en pesos" is a conversion for the PC
    if (strstr(text, " en pesos ") || strstr(text, " a pesos ") || strstr(text, " en dolares ") || strstr(text, " en euros ")) return false;

    time_t now = time(NULL);
    bool crypto = what >= 3;
    time_t at = crypto ? cache.crypto_at : cache.rates_at;
    if (!at || now - at >= FRESH_SECONDS) {
        bool ok = crypto ? fetch_crypto() : fetch_rates();
        at = crypto ? cache.crypto_at : cache.rates_at;
        if (!ok && (!at || now - at >= STALE_SECONDS)) {
            ls_say(a, CLIP_SIN_CONEXION);
            ls_finish(a, "No tengo conexión en este momento.", NULL);
            return true;
        }
        if (!ok) ESP_LOGW(TAG, "No internet: using the price from a while ago");
    }

    static const char *const NAMES[] = {"", "El dólar", "El euro", "El bitcoin", "El ethereum"};
    double value = what == 1 ? cache.cop_per_usd : what == 2 ? cache.cop_per_eur : what == 3 ? cache.btc_usd : cache.eth_usd;
    long long whole = (long long)llround(value);
    ls_say(a, what == 1 ? CLIP_EL_DOLAR_ESTA : what == 2 ? CLIP_EL_EURO_ESTA : what == 3 ? CLIP_EL_BITCOIN_ESTA : CLIP_EL_ETHEREUM_ESTA);
    ls_say_big(a, whole);
    ls_say(a, crypto ? CLIP_DOLARES : CLIP_PESOS);
    char digits[24], reply[96];
    group(whole, digits, sizeof(digits));
    snprintf(reply, sizeof(reply), "%s está en %s %s.", NAMES[what], digits, crypto ? "dólares" : "pesos");
    ls_finish(a, reply, NULL);
    return true;
}

#include "multideck_return.h"
#include <string.h>
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"

static const esp_partition_t *factory(void)
{
    return esp_partition_find_first(ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_FACTORY, NULL);
}

bool multideck_present(void)
{
    const esp_partition_t *p = factory();
    esp_app_desc_t d;
    return p && esp_ota_get_partition_description(p, &d) == ESP_OK && strcmp(d.project_name, "multideck") == 0;
}

void multideck_return(void)
{
    if (!multideck_present()) return;
    if (esp_ota_set_boot_partition(factory()) != ESP_OK) return;
    ESP_LOGI("multideck", "restarting into MultiDeck");
    esp_restart();
}

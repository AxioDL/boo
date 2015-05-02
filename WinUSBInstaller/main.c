#include <stdio.h>
#include <stdbool.h>
#include <libwdi/libwdi.h>

int main(void)
{
    printf("Hello World!\n");

    struct wdi_device_info *device, *list;
    struct wdi_options_create_list WDI_LIST_OPTS =
    {
        true, false, true
    };
    int err = wdi_create_list(&list, &WDI_LIST_OPTS);
    if (err == WDI_SUCCESS)
    {
        for (device = list; device != NULL; device = device->next)
        {
            if (device->vid == 0x57E && device->pid == 0x337 &&
                !strcmp(device->driver, "HidUsb"))
            {
                printf("GC adapter detected; installing driver\n");
                char tempDir[128];
                GetTempPathA(128, tempDir);
                err = wdi_prepare_driver(device, tempDir, "winusb_smash.inf", NULL);
                if (err == WDI_SUCCESS)
                {
                    err = wdi_install_driver(device, tempDir, "winusb_smash.inf", NULL);
                    printf("");
                }
                break;
            }
        }
        wdi_destroy_list(list);
    }

    return 0;
}


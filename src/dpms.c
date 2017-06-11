#include "../inc/dpms.h"
#include "../inc/polkit.h"
#include "../inc/udev.h"
#include "../inc/drm_utils.h"
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

#define DPMS_DISABLED -1

static void drm_get_dpms(int *state, const char *card, int *err);
static void drm_set_dpms(int level, const char *card, int *err);
 
/*
 * state will be one of:
 * DPMS Extension Power Levels
 * 0     DPMSModeOn          In use
 * 1     DPMSModeStandby     Blanked, low power
 * 2     DPMSModeSuspend     Blanked, lower power
 * 3     DPMSModeOff         Shut off, awaiting activity
 * 
 * Clightd returns -1 (DPMS_DISABLED) if dpms is disabled
 */
static void drm_get_dpms(int *state, const char *card, int *err) {
    *state = DPMS_DISABLED;
    drmModeConnectorPtr connector;
    
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        *err = errno;
        return;
    }
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        for (int i = 0; i < res->count_connectors; i++) {
            connector = get_active_connector(fd, res->connectors[i]);
            if (!connector) {
                continue;
            }
        
            drmModePropertyPtr p = drm_get_prop(fd, connector, "DPMS");
            if (p) {
                /* prop_id is 2, it means it is second prop */
                *state = (int)connector->prop_values[p->prop_id - 1];
                drmModeFreeProperty(p);
                drmModeFreeConnector(connector);
                break;
            }
        }
        drmModeFreeResources(res);
    } else {
        *err = errno;
    }
    close(fd);
}

static void drm_set_dpms(int level, const char *card, int *err) {
    drmModeConnectorPtr connector;
    drmModePropertyPtr prop;
    
    int fd = open(card, O_RDWR | O_CLOEXEC);
    if (fd < 0) {
        perror("open");
        *err = errno;
        return;
    }
    
    if (drmSetMaster(fd)) {
        perror("SetMaster");
        *err = errno;
        goto end;
    }
    
    drmModeRes *res = drmModeGetResources(fd);
    if (res) {
        for (int i = 0; i < res->count_connectors; i++) {
            connector = get_active_connector(fd, res->connectors[i]);
            if (!connector) {
                continue;
            }
            
            prop = drm_get_prop(fd, connector, "DPMS");
            if (!prop) {
                drmModeFreeConnector(connector);
                continue;
            }
            if (drmModeConnectorSetProperty(fd, connector->connector_id, prop->prop_id, level)) {
                perror("drmModeConnectorSetProperty");
            }
            drmModeFreeProperty(prop);
            drmModeFreeConnector(connector);
        }
        drmModeFreeResources(res);
    } else {
        *err = errno;
    }
    
    if (drmDropMaster(fd)) {
        perror("DropMaster");
        *err = errno;
    }

end:
    if (fd > 0) {
        close(fd);
    }
}

int method_getdpms(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *card = NULL;
    struct udev_device *dev = NULL;
        
    /* Read the parameters */
    int r = sd_bus_message_read(m, "s", &card);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    get_udev_device(card, "drm", &ret_error, &dev);
    if (sd_bus_error_is_set(ret_error)) {
        return -sd_bus_error_get_errno(ret_error);
    }
    
    int dpms_state, error = 0;
    drm_get_dpms(&dpms_state, udev_device_get_devnode(dev), &error);
    udev_device_unref(dev);
    
    if (error) {
        sd_bus_error_set_errno(ret_error, error);
        return -error;
    }
    
    if (dpms_state == DPMS_DISABLED) {
        printf("Dpms is currently disabled.\n");
    } else {
        printf("Current dpms state: %d\n", dpms_state);
    }
    return sd_bus_reply_method_return(m, "i", dpms_state);
}

int method_setdpms(sd_bus_message *m, void *userdata, sd_bus_error *ret_error) {
    const char *card = NULL;
    struct udev_device *dev = NULL;
    int level, error = 0;
    
    /* Require polkit auth */
    if (!check_authorization(m)) {
        sd_bus_error_set_errno(ret_error, EPERM);
        return -EPERM;
    }
    
    /* Read the parameters */
    int r = sd_bus_message_read(m, "si", &card, &level);
    if (r < 0) {
        fprintf(stderr, "Failed to parse parameters: %s\n", strerror(-r));
        return r;
    }
    
    /* 0 -> DPMSModeOn, 3 -> DPMSModeOff */
    if (level < 0 || level > 3) {
        sd_bus_error_set_const(ret_error, SD_BUS_ERROR_FAILED, "Wrong DPMS level value.");
        return -EINVAL;
    }
    
    get_udev_device(card, "drm", &ret_error, &dev);
    if (sd_bus_error_is_set(ret_error)) {
        return -sd_bus_error_get_errno(ret_error);
    }
    
    drm_set_dpms(level, udev_device_get_devnode(dev), &error);
    udev_device_unref(dev);
    
    if (error) {
        sd_bus_error_set_errno(ret_error, error);
        return -error;
    }
    
    printf("New dpms state: %d\n", level);
    return sd_bus_reply_method_return(m, "i", level);
}

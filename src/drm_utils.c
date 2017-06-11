#include "../inc/drm_utils.h"
#include "../inc/commons.h"

void take_control(void) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;
    
    printf("Gaining control...");
    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "TakeControl",
                           &error,
                           NULL,
                           "b",
                           1,
                           NULL);
    
    if (r < 0) {
        fprintf(stderr, "%s\n", strerror(-r));
    } else {
        printf(" OK\n");
    }
}

void release_control(void) {
    sd_bus_error error = SD_BUS_ERROR_NULL;
    int r;
    
    printf("Releasing control...");
    r = sd_bus_call_method(bus,
                           "org.freedesktop.login1",
                           "/org/freedesktop/login1/session/self",
                           "org.freedesktop.login1.Session",
                           "ReleaseControl",
                           &error,
                           NULL,
                           NULL,
                           NULL);
    
    if (r < 0) {
        fprintf(stderr, "%s\n", strerror(-r));
    } else {
        printf(" OK\n");
    }
}

drmModeConnectorPtr get_active_connector(int fd, int connector_id) {
    drmModeConnectorPtr connector = drmModeGetConnector(fd, connector_id);
    
    if (connector) {
        if (connector->connection == DRM_MODE_CONNECTED 
            && connector->count_modes > 0 && connector->encoder_id != 0) {
            return connector;
            }
            drmModeFreeConnector(connector);
    }
    return NULL;
}

drmModePropertyPtr drm_get_prop(int fd, drmModeConnectorPtr connector, const char *name) {
    drmModePropertyPtr props;
    
    for (int i = 0; i < connector->count_props; i++) {
        props = drmModeGetProperty(fd, connector->props[i]);
        if (!props) {
            continue;
        }
        if (!strcmp(props->name, name)) {
            return props;
        }
        drmModeFreeProperty(props);
    }
    return NULL;
}
